#include "PreviewState.h"

#include "GraphModel.h"
#include "ModelDocument.h"
#include "animation_pose.h"

#include <QDir>
#include <QFileInfo>
#include <algorithm>
#include <cmath>

namespace kme {

PreviewState::PreviewState(ModelDocument* doc, GraphModel* graph, QObject* parent)
    : QObject(parent), m_doc(doc), m_graph(graph)
{
    m_timer.setInterval(16);
    m_timer.setTimerType(Qt::PreciseTimer);
    connect(&m_timer, &QTimer::timeout, this, &PreviewState::tick);

    connect(m_doc, &ModelDocument::modelReset, this, [this] {
        m_graphDirty = true;
        evaluate(0.0f);
    });
    connect(m_graph, &GraphModel::changed, this, [this] {
        m_graphDirty = true;
        if (m_mode == Mode::Graph && !m_playing) evaluate(0.0f);
    });
}

void PreviewState::setContentRoot(const QString& root)
{
    if (root == m_contentRoot) return;
    m_contentRoot = root;
    m_clipCache.clear();
    m_graphDirty = true;
}

QString PreviewState::resolvePath(const QString& contentPath) const
{
    if (contentPath.isEmpty() || QDir::isAbsolutePath(contentPath) || m_contentRoot.isEmpty()) return contentPath;
    return QDir(m_contentRoot).absoluteFilePath(contentPath);
}

std::shared_ptr<AnimationClipData> PreviewState::loadClip(const QString& contentPath)
{
    if (contentPath.isEmpty()) return nullptr;
    auto it = m_clipCache.constFind(contentPath);
    if (it != m_clipCache.constEnd()) return *it;

    auto clip = std::make_shared<AnimationClipData>();
    const QString file = resolvePath(contentPath);
    if (!LoadAnimationClipFile(file.toUtf8().constData(), *clip)) clip.reset();
    m_clipCache.insert(contentPath, clip);
    return clip;
}

void PreviewState::setMode(Mode mode)
{
    if (mode == m_mode) return;
    m_mode = mode;
    if (mode == Mode::Graph) restartGraph();
    m_time = 0.0f;
    evaluate(0.0f);
    emit playbackChanged();
}

bool PreviewState::setClip(const QString& path, QString* error)
{
    std::shared_ptr<AnimationClipData> clip = loadClip(path);
    if (!path.isEmpty() && !clip) {
        if (error) *error = QString("Could not load %1").arg(path);
        return false;
    }
    m_clipPath = path;
    m_clip = clip;
    m_time = 0.0f;
    evaluate(0.0f);
    emit playbackChanged();
    return true;
}

void PreviewState::setPlaying(bool playing)
{
    if (playing == m_playing) return;
    m_playing = playing;
    if (playing) {
        m_clock.start();
        m_timer.start();
    } else {
        m_timer.stop();
    }
    emit playbackChanged();
}

float PreviewState::duration() const
{
    return m_clip ? m_clip->duration : 0.0f;
}

void PreviewState::setTime(float seconds)
{
    if (m_mode == Mode::Graph) return;
    m_time = std::clamp(seconds, 0.0f, std::max(duration(), 0.0f));
    evaluate(0.0f);
    emit playbackChanged();
}

void PreviewState::rebuildGraph()
{
    m_graphDirty = false;
    const AnimBlendTreeData tree = m_graph->toBlendTree();
    std::string error;
    // don't reset values the user already set
    QHash<QString, float> values;
    for (const AnimBlendTreeParameter& p : m_instance.Parameters())
        values.insert(QString::fromStdString(p.name), m_instance.GetValue(p.name));
    if (!m_instance.Build(tree, &error)) {
        m_graphError = tree.nodes.empty() ? QStringLiteral("The graph is empty.") : QString::fromStdString(error);
    } else {
        m_graphError.clear();
        for (const AnimBlendTreeParameter& p : m_instance.Parameters()) {
            auto it = values.constFind(QString::fromStdString(p.name));
            if (it != values.constEnd()) m_instance.SetFloat(p.name, *it);
        }
    }
    emit graphRebuilt();
}

void PreviewState::restartGraph()
{
    m_graphDirty = true;
    m_clipCache.clear();
    rebuildGraph();
    m_instance.Reset();
    evaluate(0.0f);
}

void PreviewState::tick()
{
    const float dt = std::min(float(m_clock.restart()) / 1000.0f, 0.1f);
    m_childTime += dt * std::max(m_speed, 0.0f);
    if (m_mode == Mode::Clip && m_clip) {
        m_time += dt * m_speed;
        const float d = duration();
        if (d > 0.0f) {
            if (m_loop) {
                m_time = std::fmod(m_time, d);
                if (m_time < 0.0f) m_time += d;
            } else if (m_time >= d || m_time <= 0.0f) {
                m_time = std::clamp(m_time, 0.0f, d);
                setPlaying(false);
            }
        }
        emit playbackChanged();
    }
    evaluate(m_mode == Mode::Graph ? dt * m_speed : 0.0f);
}

void PreviewState::evaluate(float dt)
{
    const KModelData& model = m_doc->model();
    m_pose.clear();
    if (m_mode == Mode::Clip && m_clip) {
        // clip's Loop flag would wrap here, so clamp to the last frame
        const float d = duration();
        const float t = (!m_loop && m_time >= d) ? std::max(d - 1e-4f, 0.0f) : m_time;
        SampleAnimationClip(model, *m_clip, t, m_pose);
    } else if (m_mode == Mode::Graph) {
        if (m_graphDirty) rebuildGraph();
        if (m_instance.IsBuilt()) {
            const AnimBlendTreeInstance::ClipResolver clips = [this](const std::string& path) {
                return loadClip(QString::fromStdString(path)).get();
            };
            m_instance.Evaluate(model, dt, clips, m_pose);
        }
    }
    if (m_pose.size() != model.nodes.size()) BuildAnimationBindPose(model, m_pose);
    BuildAnimationWorldMatrices(model, m_pose, m_nodeWorld);
    BuildAnimationSkinMatrices(model, m_pose, m_skin);
    emit poseChanged();
}

} // namespace kme
