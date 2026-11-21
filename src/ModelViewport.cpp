#include "ModelViewport.h"

#include "KesStyle.h"
#include "ModelDocument.h"
#include "PreviewState.h"
#include "animation_pose.h"

#include <QFileInfo>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QQuaternion>
#include <QWheelEvent>
#include <algorithm>
#include <cfloat>
#include <cmath>

namespace kme {

namespace {

const char* kMeshVs = R"(#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
uniform mat4 uViewProj;
out vec3 vNormal;
void main() { vNormal = aNormal; gl_Position = uViewProj * vec4(aPos, 1.0); }
)";

const char* kMeshFs = R"(#version 330 core
in vec3 vNormal;
uniform vec3 uColor;
out vec4 oColor;
void main() {
    vec3 n = normalize(gl_FrontFacing ? vNormal : -vNormal);
    float key = max(dot(n, normalize(vec3(0.45, 0.8, 0.4))), 0.0);
    float rim = max(dot(n, normalize(vec3(-0.5, 0.25, -0.7))), 0.0);
    oColor = vec4(uColor * (0.22 + 0.68 * key + 0.25 * rim), 1.0);
}
)";

const char* kLineVs = R"(#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec4 aColor;
uniform mat4 uViewProj;
out vec4 vColor;
void main() { vColor = aColor; gl_Position = uViewProj * vec4(aPos, 1.0); }
)";

const char* kLineFs = R"(#version 330 core
in vec4 vColor;
out vec4 oColor;
void main() { oColor = vColor; }
)";

// row vectors (v * M), same as the engine
QVector3D transformPoint(const float* m, float x, float y, float z)
{
    return {x * m[0] + y * m[4] + z * m[8] + m[12], x * m[1] + y * m[5] + z * m[9] + m[13],
            x * m[2] + y * m[6] + z * m[10] + m[14]};
}

QVector3D nodePosition(const std::vector<float>& world, int node)
{
    if (node < 0 || size_t(node + 1) * 16 > world.size()) return {};
    const float* m = world.data() + size_t(node) * 16;
    return {m[12], m[13], m[14]};
}

const QColor kBoneColor(150, 170, 205);
const QColor kSocketColor(90, 165, 255);
const QColor kColliderColor(110, 210, 120);
const QVector3D kChildColor(0.80f, 0.66f, 0.48f);

} // namespace

ModelViewport::ModelViewport(ModelDocument* doc, PreviewState* preview, QWidget* parent)
    : QOpenGLWidget(parent), m_doc(doc), m_preview(preview)
{
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setMinimumSize(200, 150);
    connect(m_doc, &ModelDocument::modelReset, this, [this] {
        m_meshDirty = true;
        frameModel();
    });
    connect(m_doc, &ModelDocument::attachmentsChanged, this, qOverload<>(&ModelViewport::update));
    connect(m_doc, &ModelDocument::selectionChanged, this, qOverload<>(&ModelViewport::update));
    connect(m_preview, &PreviewState::poseChanged, this, qOverload<>(&ModelViewport::update));
}

ModelViewport::~ModelViewport()
{
    makeCurrent();
    m_meshVbo.destroy();
    m_meshIbo.destroy();
    m_lineVbo.destroy();
    m_childVbo.destroy();
    m_childIbo.destroy();
    m_meshVao.destroy();
    m_lineVao.destroy();
    m_childVao.destroy();
    doneCurrent();
}

void ModelViewport::setOverlays(const Overlays& o)
{
    m_overlays = o;
    update();
}

void ModelViewport::setGizmoMode(GizmoMode mode)
{
    if (mode == m_gizmoMode) return;
    m_gizmoMode = mode;
    m_hoverHandle = -1;
    update();
    emit gizmoModeChanged(mode);
}

QPointF ModelViewport::toScreen(const QVector3D& p) const
{
    const QVector4D clip = viewProj() * QVector4D(p, 1.0f);
    if (clip.w() <= 1e-6f) return {-1e6, -1e6};
    return {(clip.x() / clip.w() * 0.5f + 0.5f) * width(), (0.5f - clip.y() / clip.w() * 0.5f) * height()};
}

bool ModelViewport::gizmoFrame(GizmoFrame& out) const
{
    const Selection& sel = m_doc->selection();
    const KModelData& model = m_doc->model();
    const float* translation = nullptr;
    int bone = -1;
    if (sel.kind == Selection::Socket && sel.index >= 0 && sel.index < int(model.sockets.size())) {
        translation = model.sockets[size_t(sel.index)].translation;
        bone = model.sockets[size_t(sel.index)].boneIndex;
    } else if (sel.kind == Selection::Collider && sel.index >= 0 && sel.index < int(model.colliders.size())) {
        translation = model.colliders[size_t(sel.index)].center;
        bone = model.colliders[size_t(sel.index)].boneIndex;
    } else if (sel.kind == Selection::Child && sel.index >= 0 && sel.index < int(model.children.size())) {
        translation = model.children[size_t(sel.index)].translation;
        bone = model.children[size_t(sel.index)].boneIndex;
    } else {
        return false;
    }

    const float zero[3] = {0, 0, 0}, identity[4] = {0, 0, 0, 1}, one[3] = {1, 1, 1};
    float parent[16];
    BuildBoneAttachmentMatrix(model, m_preview->nodeWorld(), bone, zero, identity, one, parent);
    out.origin = transformPoint(parent, translation[0], translation[1], translation[2]);
    for (int k = 0; k < 3; ++k) {
        const QVector3D a(parent[k * 4], parent[k * 4 + 1], parent[k * 4 + 2]);
        out.axisScale[k] = std::max(a.length(), 1e-6f);
        out.axis[k] = a / out.axisScale[k];
    }
    out.size = (eye() - out.origin).length() * 0.16f;
    return true;
}

void ModelViewport::buildGizmoLines()
{
    GizmoFrame g;
    if (!gizmoFrame(g)) return;
    const QColor axisColors[3] = {QColor(235, 80, 80), QColor(110, 210, 110), QColor(90, 140, 255)};
    const QColor active(255, 220, 60);
    const int highlighted = m_dragHandle >= 0 ? m_dragHandle : m_hoverHandle;
    for (int k = 0; k < 3; ++k) {
        const QColor c = k == highlighted ? active : axisColors[k];
        const QVector3D u = g.axis[(k + 1) % 3], v = g.axis[(k + 2) % 3];
        if (m_gizmoMode == GizmoMode::Move) {
            const QVector3D tip = g.origin + g.axis[k] * g.size;
            addLine(g.origin, tip, c);
            const float head = g.size * 0.14f;
            for (const QVector3D& side : {u, -u, v, -v})
                addLine(tip, tip - g.axis[k] * head + side * head * 0.4f, c);
        } else {
            constexpr int kSegments = 48;
            QVector3D prev = g.origin + u * g.size;
            for (int i = 1; i <= kSegments; ++i) {
                const float a = 6.2831853f * float(i) / kSegments;
                const QVector3D p = g.origin + (u * std::cos(a) + v * std::sin(a)) * g.size;
                addLine(prev, p, c);
                prev = p;
            }
        }
    }
}

int ModelViewport::gizmoHandleAt(const QPoint& pos) const
{
    GizmoFrame g;
    if (!gizmoFrame(g)) return -1;
    const QPointF m(pos);
    const auto segmentDistance = [&](const QPointF& a, const QPointF& b) {
        const QPointF ab = b - a;
        const double len2 = QPointF::dotProduct(ab, ab);
        const double t = len2 > 1e-9 ? std::clamp(QPointF::dotProduct(m - a, ab) / len2, 0.0, 1.0) : 0.0;
        return QLineF(a + ab * t, m).length();
    };
    int best = -1;
    double bestDist = 9.0;
    for (int k = 0; k < 3; ++k) {
        double d = 1e9;
        if (m_gizmoMode == GizmoMode::Move) {
            d = segmentDistance(toScreen(g.origin), toScreen(g.origin + g.axis[k] * g.size));
        } else {
            const QVector3D u = g.axis[(k + 1) % 3], v = g.axis[(k + 2) % 3];
            constexpr int kSegments = 48;
            QPointF prev = toScreen(g.origin + u * g.size);
            for (int i = 1; i <= kSegments; ++i) {
                const float a = 6.2831853f * float(i) / kSegments;
                const QPointF p = toScreen(g.origin + (u * std::cos(a) + v * std::sin(a)) * g.size);
                d = std::min(d, segmentDistance(prev, p));
                prev = p;
            }
        }
        if (d < bestDist) {
            bestDist = d;
            best = k;
        }
    }
    return best;
}

void ModelViewport::dragGizmo(const QPoint& from, const QPoint& to)
{
    GizmoFrame g;
    if (m_dragHandle < 0 || !gizmoFrame(g)) return;
    const int k = m_dragHandle;
    const Selection sel = m_doc->selection();
    const QString key = QString("gizmo:%1").arg(m_dragSerial);

    if (m_gizmoMode == GizmoMode::Move) {
        // project mouse delta onto the axis on screen, then back to world units
        const QPointF s = toScreen(g.origin + g.axis[k] * g.size) - toScreen(g.origin);
        const double s2 = QPointF::dotProduct(s, s);
        if (s2 < 1.0) return;
        const float world = float(QPointF::dotProduct(QPointF(to - from), s) / s2) * g.size;
        const float local = world / g.axisScale[k];
        m_doc->edit("Move with gizmo", [&](KModelData& m) {
            float* t = sel.kind == Selection::Socket     ? m.sockets[size_t(sel.index)].translation
                       : sel.kind == Selection::Collider ? m.colliders[size_t(sel.index)].center
                                                         : m.children[size_t(sel.index)].translation;
            t[k] += local;
        }, key);
        return;
    }

    // screen-space angle around the gizmo centre, CCW positive
    const QPointF c = toScreen(g.origin);
    const double a0 = std::atan2(-(from.y() - c.y()), from.x() - c.x());
    const double a1 = std::atan2(-(to.y() - c.y()), to.x() - c.x());
    double delta = a1 - a0;
    constexpr double kPi = 3.14159265358979323846;
    if (delta > kPi) delta -= 2 * kPi;
    if (delta < -kPi) delta += 2 * kPi;
    // flip when looking at the axis from behind
    if (QVector3D::dotProduct(g.axis[k], eye() - g.origin) < 0.0f) delta = -delta;
    QVector3D axis;
    axis[k] = 1.0f;
    const QQuaternion r = QQuaternion::fromAxisAndAngle(axis, float(qRadiansToDegrees(delta)));
    m_doc->edit("Rotate with gizmo", [&](KModelData& m) {
        float* q = sel.kind == Selection::Socket     ? m.sockets[size_t(sel.index)].rotation
                   : sel.kind == Selection::Collider ? m.colliders[size_t(sel.index)].rotation
                                                     : m.children[size_t(sel.index)].rotation;
        const QQuaternion result = (r * QQuaternion(q[3], q[0], q[1], q[2])).normalized();
        q[0] = result.x();
        q[1] = result.y();
        q[2] = result.z();
        q[3] = result.scalar();
    }, key);
}

void ModelViewport::initializeGL()
{
    initializeOpenGLFunctions();
    m_meshProgram.addShaderFromSourceCode(QOpenGLShader::Vertex, kMeshVs);
    m_meshProgram.addShaderFromSourceCode(QOpenGLShader::Fragment, kMeshFs);
    m_meshProgram.link();
    m_lineProgram.addShaderFromSourceCode(QOpenGLShader::Vertex, kLineVs);
    m_lineProgram.addShaderFromSourceCode(QOpenGLShader::Fragment, kLineFs);
    m_lineProgram.link();

    m_meshVao.create();
    m_meshVao.bind();
    m_meshVbo.create();
    m_meshVbo.setUsagePattern(QOpenGLBuffer::DynamicDraw);
    m_meshVbo.bind();
    m_meshIbo.create();
    m_meshIbo.bind();
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), nullptr);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), reinterpret_cast<void*>(3 * sizeof(float)));
    m_meshVao.release();

    m_lineVao.create();
    m_lineVao.bind();
    m_lineVbo.create();
    m_lineVbo.setUsagePattern(QOpenGLBuffer::DynamicDraw);
    m_lineVbo.bind();
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(LineVertex), nullptr);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(LineVertex), reinterpret_cast<void*>(3 * sizeof(float)));
    m_lineVao.release();

    m_childVao.create();
    m_childVao.bind();
    m_childVbo.create();
    m_childVbo.setUsagePattern(QOpenGLBuffer::DynamicDraw);
    m_childVbo.bind();
    m_childIbo.create();
    m_childIbo.setUsagePattern(QOpenGLBuffer::DynamicDraw);
    m_childIbo.bind();
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), nullptr);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), reinterpret_cast<void*>(3 * sizeof(float)));
    m_childVao.release();
    m_glReady = true;
    m_meshDirty = true;
}

void ModelViewport::resizeGL(int, int) {}

void ModelViewport::SourceMesh::fill(const KModelData& model)
{
    *this = {};
    for (size_t meshIndex = 0; meshIndex < model.meshes.size(); ++meshIndex) {
        int meshNode = -1;
        for (size_t n = 0; n < model.nodes.size(); ++n)
            if (model.nodes[n].meshIndex == int(meshIndex)) { meshNode = int(n); break; }
        const KModelMesh& mesh = model.meshes[meshIndex];
        const uint32_t base = uint32_t(node.size());
        for (const KModelVertex& v : mesh.vertices) {
            pos.insert(pos.end(), {v.px, v.py, v.pz});
            normal.insert(normal.end(), {v.nx, v.ny, v.nz});
            bones.insert(bones.end(), std::begin(v.boneIndices), std::end(v.boneIndices));
            weights.insert(weights.end(), std::begin(v.boneWeights), std::end(v.boneWeights));
            node.push_back(meshNode);
        }
        for (uint32_t i : mesh.indices) indices.push_back(base + i);
    }
}

void ModelViewport::SourceMesh::skin(const KModelData& model, const std::vector<float>& skinMatrices,
                                     const std::vector<float>& nodeWorld, std::vector<float>& outVerts) const
{
    const bool skinned = !model.bones.empty() && skinMatrices.size() >= model.bones.size() * 16;
    outVerts.resize(node.size() * 6);
    for (size_t v = 0; v < node.size(); ++v) {
        const float* p = &pos[v * 3];
        const float* n = &normal[v * 3];
        float out[6] = {};
        float total = 0.0f;
        if (skinned) {
            for (int k = 0; k < 4; ++k) {
                const float w = weights[v * 4 + k];
                const uint16_t b = bones[v * 4 + k];
                if (w <= 0.0f || b >= model.bones.size()) continue;
                const float* m = &skinMatrices[size_t(b) * 16];
                const QVector3D tp = transformPoint(m, p[0], p[1], p[2]);
                out[0] += w * tp.x();
                out[1] += w * tp.y();
                out[2] += w * tp.z();
                out[3] += w * (n[0] * m[0] + n[1] * m[4] + n[2] * m[8]);
                out[4] += w * (n[0] * m[1] + n[1] * m[5] + n[2] * m[9]);
                out[5] += w * (n[0] * m[2] + n[1] * m[6] + n[2] * m[10]);
                total += w;
            }
        }
        if (total <= 0.0f) {
            const int nodeIndex = node[v];
            if (nodeIndex >= 0 && size_t(nodeIndex + 1) * 16 <= nodeWorld.size()) {
                const float* m = &nodeWorld[size_t(nodeIndex) * 16];
                const QVector3D tp = transformPoint(m, p[0], p[1], p[2]);
                out[0] = tp.x(); out[1] = tp.y(); out[2] = tp.z();
                out[3] = n[0] * m[0] + n[1] * m[4] + n[2] * m[8];
                out[4] = n[0] * m[1] + n[1] * m[5] + n[2] * m[9];
                out[5] = n[0] * m[2] + n[1] * m[6] + n[2] * m[10];
            } else {
                std::copy(p, p + 3, out);
                std::copy(n, n + 3, out + 3);
            }
        } else if (total < 0.999f) {
            for (float& c : out) c /= total;
        }
        std::copy(out, out + 6, &outVerts[v * 6]);
    }
}

std::shared_ptr<const ModelViewport::ChildAsset> ModelViewport::childAsset(const std::string& contentPath)
{
    const QFileInfo info(m_preview->resolvePath(QString::fromStdString(contentPath)));
    const QString key = info.absoluteFilePath();
    const QDateTime modified = info.exists() ? info.lastModified() : QDateTime();
    auto it = m_childCache.find(key);
    if (it != m_childCache.end() && (*it)->modified == modified) return (*it)->ok ? *it : nullptr;

    auto asset = std::make_shared<ChildAsset>();
    asset->modified = modified;
    if (info.exists() && LoadKModelFile(key.toUtf8().constData(), asset->model) && !asset->model.meshes.empty()) {
        asset->ok = true;
        asset->mesh.fill(asset->model);
        std::vector<AnimationTransform> bind;
        BuildAnimationBindPose(asset->model, bind);
        BuildAnimationWorldMatrices(asset->model, bind, asset->bindWorld);
        BuildAnimationSkinMatrices(asset->model, bind, asset->bindSkin);
    }
    m_childCache.insert(key, asset);
    return asset->ok ? asset : nullptr;
}

bool ModelViewport::childMatrix(int index, float out[16]) const
{
    const KModelData& model = m_doc->model();
    if (index < 0 || index >= int(model.children.size())) return false;
    BuildKModelChildMatrix(model, m_preview->nodeWorld(), model.children[size_t(index)], out);
    return true;
}

// child animations use the preview clock, so they only move while playback is running
bool ModelViewport::childPose(const QString& key, const KModelChild& child, const KModelData& model,
                              std::vector<AnimationTransform>& outPose)
{
    outPose.clear();
    if (child.animationPath.empty()) {
        m_childAnimations.remove(key);
        return false;
    }
    ChildAnimation& anim = m_childAnimations[key];
    if (anim.path != child.animationPath) {
        anim = {};
        anim.path = child.animationPath;
        anim.isTree = IsAnimBlendTreePath(anim.path);
        if (anim.isTree) {
            AnimBlendTreeData treeData;
            const QString file = m_preview->resolvePath(QString::fromStdString(anim.path));
            if (LoadAnimBlendTreeFile(file.toUtf8().constData(), treeData)) anim.tree.Build(treeData);
        } else {
            anim.clip = m_preview->loadClip(QString::fromStdString(anim.path));
        }
    }

    const float time = m_preview->childTime();
    if (!anim.isTree) return anim.clip && SampleAnimationClip(model, *anim.clip, time, outPose);
    if (!anim.tree.IsBuilt()) return false;
    float dt = time - anim.lastTime;
    if (anim.lastTime < 0.0f || dt < 0.0f) {
        anim.tree.Reset();
        dt = 0.0f;
    }
    anim.lastTime = time;
    const AnimBlendTreeInstance::ClipResolver clips = [this](const std::string& path) {
        return m_preview->loadClip(QString::fromStdString(path)).get();
    };
    return anim.tree.Evaluate(model, dt, clips, outPose);
}

void ModelViewport::appendChild(const KModelChild& child, const QString& key, const float matrix[16], bool selected, int depth)
{
    const std::shared_ptr<const ChildAsset> asset = childAsset(child.modelPath);
    if (!asset) return;
    const KModelData& model = asset->model;

    std::vector<AnimationTransform> pose;
    std::vector<float> world = asset->bindWorld;
    if (childPose(key, child, model, pose)) {
        BuildAnimationWorldMatrices(model, pose, world);
        BuildAnimationSkinMatrices(model, pose, m_childSkin);
        asset->mesh.skin(model, m_childSkin, world, m_childSkinned);
    } else {
        asset->mesh.skin(model, asset->bindSkin, world, m_childSkinned);
    }

    const uint32_t base = uint32_t(m_childVerts.size() / 6);
    for (size_t v = 0; v + 5 < m_childSkinned.size(); v += 6) {
        const float* p = &m_childSkinned[v];
        const QVector3D tp = transformPoint(matrix, p[0], p[1], p[2]);
        const QVector3D n = QVector3D(p[3] * matrix[0] + p[4] * matrix[4] + p[5] * matrix[8],
                                      p[3] * matrix[1] + p[4] * matrix[5] + p[5] * matrix[9],
                                      p[3] * matrix[2] + p[4] * matrix[6] + p[5] * matrix[10]).normalized();
        m_childVerts.insert(m_childVerts.end(), {tp.x(), tp.y(), tp.z(), n.x(), n.y(), n.z()});
    }
    const size_t first = m_childIndices.size();
    for (uint32_t i : asset->mesh.indices) m_childIndices.push_back(base + i);
    m_childDraws.push_back({first, asset->mesh.indices.size(), selected});

    // same depth limit as the engine, also catches a model that includes itself
    if (depth >= 3) return;
    for (size_t i = 0; i < model.children.size(); ++i) {
        float local[16], placed[16];
        BuildKModelChildMatrix(model, world, model.children[i], local);
        MultiplyMatrices(local, matrix, placed);
        appendChild(model.children[i], QString("%1/%2").arg(key).arg(i), placed, selected, depth + 1);
    }
}

void ModelViewport::buildChildMeshes()
{
    m_childVerts.clear();
    m_childIndices.clear();
    m_childDraws.clear();
    if (!m_overlays.children) return;
    const Selection& sel = m_doc->selection();
    const KModelData& model = m_doc->model();
    for (size_t i = 0; i < model.children.size(); ++i) {
        float m[16];
        childMatrix(int(i), m);
        appendChild(model.children[i], QString::number(i), m, sel.kind == Selection::Child && sel.index == int(i), 0);
    }
}

void ModelViewport::rebuildStaticMesh()
{
    m_meshDirty = false;
    m_src.fill(m_doc->model());
    m_skinned.assign(m_src.node.size() * 6, 0.0f);
    m_indicesPending = true;
}

void ModelViewport::skinMesh()
{
    m_src.skin(m_doc->model(), m_preview->skin(), m_preview->nodeWorld(), m_skinned);
}

void ModelViewport::frameModel()
{
    if (m_meshDirty) rebuildStaticMesh();
    skinMesh();
    QVector3D lo(FLT_MAX, FLT_MAX, FLT_MAX), hi(-FLT_MAX, -FLT_MAX, -FLT_MAX);
    const auto grow = [&](const QVector3D& p) {
        lo = QVector3D(std::min(lo.x(), p.x()), std::min(lo.y(), p.y()), std::min(lo.z(), p.z()));
        hi = QVector3D(std::max(hi.x(), p.x()), std::max(hi.y(), p.y()), std::max(hi.z(), p.z()));
    };
    for (size_t v = 0; v * 6 < m_skinned.size(); ++v) grow({m_skinned[v * 6], m_skinned[v * 6 + 1], m_skinned[v * 6 + 2]});
    for (size_t n = 0; n * 16 < m_preview->nodeWorld().size(); ++n) grow(nodePosition(m_preview->nodeWorld(), int(n)));
    if (lo.x() > hi.x()) {
        lo = QVector3D(-1, 0, -1);
        hi = QVector3D(1, 2, 1);
    }
    m_target = (lo + hi) * 0.5f;
    m_radius = std::max(0.05f, (hi - lo).length() * 0.5f);
    m_distance = m_radius * 2.6f;
    update();
}

QVector3D ModelViewport::eye() const
{
    const float yaw = qDegreesToRadians(m_yaw);
    const float pitch = qDegreesToRadians(m_pitch);
    const QVector3D dir(std::cos(pitch) * std::sin(yaw), std::sin(pitch), std::cos(pitch) * std::cos(yaw));
    return m_target + dir * m_distance;
}

QMatrix4x4 ModelViewport::viewProj() const
{
    QMatrix4x4 proj, view;
    const float aspect = height() > 0 ? float(width()) / float(height()) : 1.0f;
    proj.perspective(45.0f, aspect, std::max(0.001f, m_distance * 0.01f), m_distance * 50.0f + m_radius * 10.0f);
    view.lookAt(eye(), m_target, QVector3D(0, 1, 0));
    return proj * view;
}

void ModelViewport::addLine(const QVector3D& a, const QVector3D& b, const QColor& c)
{
    const float r = float(c.redF()), g = float(c.greenF()), bl = float(c.blueF()), al = float(c.alphaF());
    m_lines.push_back({a.x(), a.y(), a.z(), r, g, bl, al});
    m_lines.push_back({b.x(), b.y(), b.z(), r, g, bl, al});
}

void ModelViewport::addTransformedCircle(const float m[16], int axisA, int axisB, float radius, float offsetAxis,
                                         int offsetIndex, const QColor& c, float startAngle, float sweep)
{
    constexpr int kSegments = 32;
    QVector3D prev;
    for (int i = 0; i <= kSegments; ++i) {
        const float a = startAngle + sweep * float(i) / float(kSegments);
        float local[3] = {0, 0, 0};
        local[axisA] = std::cos(a) * radius;
        local[axisB] = std::sin(a) * radius;
        if (offsetIndex >= 0) local[offsetIndex] += offsetAxis;
        const QVector3D p = transformPoint(m, local[0], local[1], local[2]);
        if (i > 0) addLine(prev, p, c);
        prev = p;
    }
}

void ModelViewport::addCollider(int index, const QColor& c)
{
    const KModelData& model = m_doc->model();
    const KModelCollider& col = model.colliders[size_t(index)];
    const float one[3] = {1, 1, 1};
    float m[16];
    BuildBoneAttachmentMatrix(model, m_preview->nodeWorld(), col.boneIndex, col.center, col.rotation, one, m);

    switch (col.shape) {
    case KModelColliderShape::Box: {
        const float* h = col.halfExtents;
        QVector3D corners[8];
        for (int i = 0; i < 8; ++i)
            corners[i] = transformPoint(m, (i & 1) ? h[0] : -h[0], (i & 2) ? h[1] : -h[1], (i & 4) ? h[2] : -h[2]);
        for (int i = 0; i < 8; ++i)
            for (int bit : {1, 2, 4})
                if (!(i & bit)) addLine(corners[i], corners[i | bit], c);
        break;
    }
    case KModelColliderShape::Sphere:
        addTransformedCircle(m, 0, 1, col.radius, 0, -1, c);
        addTransformedCircle(m, 0, 2, col.radius, 0, -1, c);
        addTransformedCircle(m, 1, 2, col.radius, 0, -1, c);
        break;
    case KModelColliderShape::Capsule: {
        const float r = col.radius, h = col.halfHeight;
        constexpr float pi = 3.14159265f;
        addTransformedCircle(m, 0, 2, r, h, 1, c);
        addTransformedCircle(m, 0, 2, r, -h, 1, c);
        addTransformedCircle(m, 0, 1, r, h, 1, c, 0.0f, pi);
        addTransformedCircle(m, 2, 1, r, h, 1, c, 0.0f, pi);
        addTransformedCircle(m, 0, 1, r, -h, 1, c, pi, pi);
        addTransformedCircle(m, 2, 1, r, -h, 1, c, pi, pi);
        for (const auto& [x, z] : {std::pair{r, 0.0f}, {-r, 0.0f}, {0.0f, r}, {0.0f, -r}})
            addLine(transformPoint(m, x, -h, z), transformPoint(m, x, h, z), c);
        break;
    }
    }
}

void ModelViewport::buildOverlayLines()
{
    m_lines.clear();
    const KModelData& model = m_doc->model();
    const Selection& sel = m_doc->selection();
    const QColor accent = kes::style::accent();

    if (m_overlays.grid) {
        const float step = std::pow(10.0f, std::floor(std::log10(std::max(m_radius, 0.01f) * 0.5f)));
        const int count = 20;
        const QColor minor(255, 255, 255, 22), major(255, 255, 255, 48);
        for (int i = -count; i <= count; ++i) {
            const float t = float(i) * step, e = float(count) * step;
            const QColor col = i == 0 ? QColor(255, 255, 255, 70) : (i % 5 == 0 ? major : minor);
            addLine({t, 0, -e}, {t, 0, e}, col);
            addLine({-e, 0, t}, {e, 0, t}, col);
        }
    }
    m_gridLineCount = m_lines.size();

    const std::vector<float>& world = m_preview->nodeWorld();
    if (m_overlays.skeleton) {
        const float cross = m_radius * 0.012f;
        for (size_t b = 0; b < model.bones.size(); ++b) {
            const KModelBone& bone = model.bones[b];
            const bool selected = sel.kind == Selection::Node && sel.index == bone.nodeIndex;
            const QColor col = selected ? accent : kBoneColor;
            const QVector3D p = nodePosition(world, bone.nodeIndex);
            if (bone.parentBoneIndex >= 0)
                addLine(nodePosition(world, model.bones[size_t(bone.parentBoneIndex)].nodeIndex), p, col);
            const float s = selected ? cross * 2.0f : cross;
            addLine(p - QVector3D(s, 0, 0), p + QVector3D(s, 0, 0), col);
            addLine(p - QVector3D(0, s, 0), p + QVector3D(0, s, 0), col);
            addLine(p - QVector3D(0, 0, s), p + QVector3D(0, 0, s), col);
        }
    }

    if (m_overlays.sockets) {
        const float len = m_radius * 0.08f;
        for (size_t i = 0; i < model.sockets.size(); ++i) {
            const KModelSocket& s = model.sockets[i];
            float m[16];
            BuildBoneAttachmentMatrix(model, world, s.boneIndex, s.translation, s.rotation, s.scale, m);
            const QVector3D o(m[12], m[13], m[14]);
            const bool selected = sel.kind == Selection::Socket && sel.index == int(i);
            const float l = selected ? len * 1.6f : len;
            for (int axis = 0; axis < 3; ++axis) {
                QVector3D dir(m[axis * 4], m[axis * 4 + 1], m[axis * 4 + 2]);
                dir = dir.normalized() * l;
                const QColor col = axis == 0 ? QColor(235, 80, 80) : axis == 1 ? QColor(110, 210, 110) : QColor(90, 140, 255);
                addLine(o, o + dir, col);
            }
            const float d = l * 0.25f;
            const QColor ring = selected ? accent : kSocketColor;
            addLine(o + QVector3D(d, 0, 0), o + QVector3D(0, d, 0), ring);
            addLine(o + QVector3D(0, d, 0), o + QVector3D(-d, 0, 0), ring);
            addLine(o + QVector3D(-d, 0, 0), o + QVector3D(0, -d, 0), ring);
            addLine(o + QVector3D(0, -d, 0), o + QVector3D(d, 0, 0), ring);
        }
    }

    if (m_overlays.colliders) {
        for (size_t i = 0; i < model.colliders.size(); ++i) {
            const bool selected = sel.kind == Selection::Collider && sel.index == int(i);
            addCollider(int(i), selected ? accent : kColliderColor);
        }
    }

    if (m_overlays.children) {
        // marker at the child's origin so it can be picked even if its model fails to load
        const float d = m_radius * 0.02f;
        for (size_t i = 0; i < model.children.size(); ++i) {
            float m[16];
            childMatrix(int(i), m);
            const QVector3D o(m[12], m[13], m[14]);
            const bool selected = sel.kind == Selection::Child && sel.index == int(i);
            const QColor col = selected ? accent : QColor(225, 170, 90);
            addLine(o - QVector3D(d, 0, 0), o + QVector3D(d, 0, 0), col);
            addLine(o - QVector3D(0, d, 0), o + QVector3D(0, d, 0), col);
            addLine(o - QVector3D(0, 0, d), o + QVector3D(0, 0, d), col);
        }
    }
    buildGizmoLines();
}

void ModelViewport::paintGL()
{
    glClearColor(0.133f, 0.133f, 0.149f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    if (!m_doc->isOpen()) {
        QPainter p(this);
        p.setPen(kes::style::muted());
        p.drawText(rect(), Qt::AlignCenter, "Open a .kmodel (File > Open Model)");
        return;
    }

    if (m_meshDirty) rebuildStaticMesh();
    skinMesh();
    buildChildMeshes();
    buildOverlayLines();
    const QMatrix4x4 vp = viewProj();

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    m_lineProgram.bind();
    m_lineProgram.setUniformValue("uViewProj", vp);
    m_lineVao.bind();
    m_lineVbo.bind();
    m_lineVbo.allocate(m_lines.data(), int(m_lines.size() * sizeof(LineVertex)));
    glDepthMask(GL_FALSE);
    glDrawArrays(GL_LINES, 0, GLsizei(m_gridLineCount));
    glDepthMask(GL_TRUE);
    m_lineVao.release();

    if (m_overlays.mesh && !m_src.indices.empty()) {
        m_meshProgram.bind();
        m_meshProgram.setUniformValue("uViewProj", vp);
        m_meshProgram.setUniformValue("uColor", QVector3D(0.72f, 0.72f, 0.76f));
        m_meshVao.bind();
        m_meshVbo.bind();
        m_meshVbo.allocate(m_skinned.data(), int(m_skinned.size() * sizeof(float)));
        m_meshIbo.bind();
        if (m_indicesPending) {
            m_meshIbo.allocate(m_src.indices.data(), int(m_src.indices.size() * sizeof(uint32_t)));
            m_indicesPending = false;
        }
        glDrawElements(GL_TRIANGLES, GLsizei(m_src.indices.size()), GL_UNSIGNED_INT, nullptr);
        m_meshVao.release();
    }

    if (!m_childDraws.empty()) {
        m_meshProgram.bind();
        m_meshProgram.setUniformValue("uViewProj", vp);
        m_childVao.bind();
        m_childVbo.bind();
        m_childVbo.allocate(m_childVerts.data(), int(m_childVerts.size() * sizeof(float)));
        m_childIbo.bind();
        m_childIbo.allocate(m_childIndices.data(), int(m_childIndices.size() * sizeof(uint32_t)));
        const QColor accent = kes::style::accent();
        const QVector3D selectedColor(float(accent.redF()), float(accent.greenF()), float(accent.blueF()));
        for (const ChildDraw& draw : m_childDraws) {
            m_meshProgram.setUniformValue("uColor", draw.selected ? (kChildColor + selectedColor) * 0.5f : kChildColor);
            glDrawElements(GL_TRIANGLES, GLsizei(draw.indexCount), GL_UNSIGNED_INT,
                           reinterpret_cast<void*>(draw.firstIndex * sizeof(uint32_t)));
        }
        m_childVao.release();
    }

    // no depth test so bones inside the mesh stay visible
    glDisable(GL_DEPTH_TEST);
    m_lineProgram.bind();
    m_lineVao.bind();
    m_lineVbo.bind();
    glDrawArrays(GL_LINES, GLsizei(m_gridLineCount), GLsizei(m_lines.size() - m_gridLineCount));
    m_lineVao.release();

    GizmoFrame g;
    if (gizmoFrame(g)) {
        QPainter p(this);
        p.setPen(kes::style::muted());
        p.drawText(rect().adjusted(8, 0, 0, -6), Qt::AlignLeft | Qt::AlignBottom,
                   m_gizmoMode == GizmoMode::Move ? "Move (W)   ·   E: rotate" : "Rotate (E)   ·   W: move");
    }
}

void ModelViewport::pick(const QPoint& pos)
{
    const KModelData& model = m_doc->model();
    const std::vector<float>& world = m_preview->nodeWorld();
    const QMatrix4x4 vp = viewProj();
    Selection best;
    float bestDist = 14.0f;
    const auto test = [&](const QVector3D& p, Selection::Kind kind, int index) {
        const QVector4D clip = vp * QVector4D(p, 1.0f);
        if (clip.w() <= 0.0f) return;
        const QPointF screen((clip.x() / clip.w() * 0.5f + 0.5f) * width(), (0.5f - clip.y() / clip.w() * 0.5f) * height());
        const float d = float(QLineF(screen, QPointF(pos)).length());
        if (d < bestDist) {
            bestDist = d;
            best = {kind, index};
        }
    };
    if (m_overlays.skeleton)
        for (const KModelBone& b : model.bones) test(nodePosition(world, b.nodeIndex), Selection::Node, b.nodeIndex);
    if (m_overlays.colliders) {
        for (size_t i = 0; i < model.colliders.size(); ++i) {
            const KModelCollider& c = model.colliders[i];
            const float one[3] = {1, 1, 1};
            float m[16];
            BuildBoneAttachmentMatrix(model, world, c.boneIndex, c.center, c.rotation, one, m);
            test({m[12], m[13], m[14]}, Selection::Collider, int(i));
        }
    }
    if (m_overlays.sockets) {
        for (size_t i = 0; i < model.sockets.size(); ++i) {
            const KModelSocket& s = model.sockets[i];
            float m[16];
            BuildBoneAttachmentMatrix(model, world, s.boneIndex, s.translation, s.rotation, s.scale, m);
            test({m[12], m[13], m[14]}, Selection::Socket, int(i));
        }
    }
    if (m_overlays.children) {
        for (size_t i = 0; i < model.children.size(); ++i) {
            float m[16];
            childMatrix(int(i), m);
            test({m[12], m[13], m[14]}, Selection::Child, int(i));
        }
    }
    m_doc->setSelection(best);
}

void ModelViewport::mousePressEvent(QMouseEvent* e)
{
    m_lastMouse = m_pressMouse = e->pos();
    m_dragButton = e->button();
    if (e->button() == Qt::LeftButton && !(e->modifiers() & Qt::ShiftModifier)) {
        m_dragHandle = gizmoHandleAt(e->pos());
        if (m_dragHandle >= 0) {
            ++m_dragSerial;
            update();
        }
    }
}

void ModelViewport::mouseMoveEvent(QMouseEvent* e)
{
    const QPoint d = e->pos() - m_lastMouse;
    const QPoint previous = m_lastMouse;
    m_lastMouse = e->pos();
    if (m_dragHandle >= 0) {
        dragGizmo(previous, e->pos());
        return;
    }
    if (m_dragButton == Qt::NoButton) {
        const int hover = gizmoHandleAt(e->pos());
        if (hover != m_hoverHandle) {
            m_hoverHandle = hover;
            setCursor(hover >= 0 ? Qt::SizeAllCursor : Qt::ArrowCursor);
            update();
        }
        return;
    }
    if (m_dragButton == Qt::LeftButton && !(e->modifiers() & Qt::ShiftModifier)) {
        m_yaw -= d.x() * 0.4f;
        m_pitch = std::clamp(m_pitch + d.y() * 0.4f, -89.0f, 89.0f);
    } else if (m_dragButton == Qt::MiddleButton || (m_dragButton == Qt::LeftButton && (e->modifiers() & Qt::ShiftModifier))) {
        const QVector3D forward = (m_target - eye()).normalized();
        const QVector3D right = QVector3D::crossProduct(forward, QVector3D(0, 1, 0)).normalized();
        const QVector3D up = QVector3D::crossProduct(right, forward);
        const float scale = m_distance * 0.0015f;
        m_target += (-right * float(d.x()) + up * float(d.y())) * scale;
    } else if (m_dragButton == Qt::RightButton) {
        m_distance = std::max(m_radius * 0.02f, m_distance * std::pow(1.01f, float(d.y())));
    } else {
        return;
    }
    update();
}

void ModelViewport::mouseReleaseEvent(QMouseEvent* e)
{
    const bool wasGizmo = m_dragHandle >= 0;
    m_dragHandle = -1;
    if (!wasGizmo && e->button() == Qt::LeftButton && (e->pos() - m_pressMouse).manhattanLength() < 4) pick(e->pos());
    m_dragButton = Qt::NoButton;
    if (wasGizmo) update();
}

void ModelViewport::wheelEvent(QWheelEvent* e)
{
    m_distance = std::max(m_radius * 0.02f, m_distance * std::pow(0.999f, float(e->angleDelta().y())));
    update();
}

void ModelViewport::keyPressEvent(QKeyEvent* e)
{
    if (e->key() == Qt::Key_F) {
        frameModel();
        return;
    }
    if (e->key() == Qt::Key_W) {
        setGizmoMode(GizmoMode::Move);
        return;
    }
    if (e->key() == Qt::Key_E) {
        setGizmoMode(GizmoMode::Rotate);
        return;
    }
    if (e->key() == Qt::Key_Space) {
        m_preview->setPlaying(!m_preview->isPlaying());
        return;
    }
    QOpenGLWidget::keyPressEvent(e);
}

} // namespace kme
