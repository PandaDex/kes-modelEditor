#pragma once

#include "anim_blend_tree.h"
#include "animation_clip.h"

#include <QElapsedTimer>
#include <QHash>
#include <QObject>
#include <QString>
#include <QTimer>
#include <memory>
#include <vector>

namespace kme {

class GraphModel;
class ModelDocument;

class PreviewState : public QObject {
    Q_OBJECT
public:
    enum class Mode { BindPose, Clip, Graph };

    PreviewState(ModelDocument* doc, GraphModel* graph, QObject* parent = nullptr);

    void setContentRoot(const QString& root);
    const QString& contentRoot() const { return m_contentRoot; }
    QString resolvePath(const QString& contentPath) const;

    Mode mode() const { return m_mode; }
    void setMode(Mode mode);

    bool setClip(const QString& path, QString* error = nullptr);
    const QString& clipPath() const { return m_clipPath; }
    const AnimationClipData* clip() const { return m_clip.get(); }

    bool isPlaying() const { return m_playing; }
    void setPlaying(bool playing);
    float time() const { return m_time; }
    void setTime(float seconds);
    float duration() const;
    float speed() const { return m_speed; }
    void setSpeed(float speed) { m_speed = speed; }
    bool loop() const { return m_loop; }
    void setLoop(bool loop) { m_loop = loop; }

    void rebuildGraph();
    const QString& graphError() const { return m_graphError; }
    AnimBlendTreeInstance& graphInstance() { return m_instance; }
    void restartGraph();

    const std::vector<AnimationTransform>& pose() const { return m_pose; }
    const std::vector<float>& nodeWorld() const { return m_nodeWorld; }
    const std::vector<float>& skin() const { return m_skin; }

signals:
    void poseChanged();
    void playbackChanged();
    void graphRebuilt();

private:
    void tick();
    void evaluate(float dt);
    std::shared_ptr<AnimationClipData> loadClip(const QString& contentPath);

    ModelDocument* m_doc;
    GraphModel* m_graph;
    QString m_contentRoot;
    Mode m_mode = Mode::BindPose;

    QHash<QString, std::shared_ptr<AnimationClipData>> m_clipCache;
    QString m_clipPath;
    std::shared_ptr<AnimationClipData> m_clip;

    bool m_playing = false;
    float m_time = 0.0f;
    float m_speed = 1.0f;
    bool m_loop = true;
    QTimer m_timer;
    QElapsedTimer m_clock;

    AnimBlendTreeInstance m_instance;
    QString m_graphError;
    bool m_graphDirty = true;

    std::vector<AnimationTransform> m_pose;
    std::vector<float> m_nodeWorld;
    std::vector<float> m_skin;
};

} // namespace kme
