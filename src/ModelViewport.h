#pragma once

#include "anim_blend_tree.h"
#include "animation_clip.h"
#include "kmodel.h"

#include <QDateTime>
#include <QHash>
#include <QMatrix4x4>
#include <QOpenGLBuffer>
#include <QOpenGLFunctions_3_3_Core>
#include <QOpenGLShaderProgram>
#include <QOpenGLVertexArrayObject>
#include <QOpenGLWidget>
#include <QVector3D>
#include <memory>
#include <vector>

namespace kme {

class ModelDocument;
class PreviewState;

class ModelViewport : public QOpenGLWidget, protected QOpenGLFunctions_3_3_Core {
    Q_OBJECT
public:
    enum class GizmoMode { Move, Rotate };

    struct Overlays {
        bool mesh = true;
        bool skeleton = true;
        bool sockets = true;
        bool colliders = true;
        bool children = true;
        bool grid = true;
    };

    ModelViewport(ModelDocument* doc, PreviewState* preview, QWidget* parent = nullptr);
    ~ModelViewport() override;

    Overlays& overlays() { return m_overlays; }
    void setOverlays(const Overlays& o);
    void frameModel();
    GizmoMode gizmoMode() const { return m_gizmoMode; }
    void setGizmoMode(GizmoMode mode);

signals:
    void gizmoModeChanged(GizmoMode mode);

protected:
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void paintGL() override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void wheelEvent(QWheelEvent*) override;
    void keyPressEvent(QKeyEvent*) override;

private:
    struct LineVertex {
        float x, y, z, r, g, b, a;
    };

    // unskinned vertices of all meshes, skin() outputs pos + normal
    struct SourceMesh {
        std::vector<float> pos, normal;
        std::vector<uint16_t> bones;
        std::vector<float> weights;
        std::vector<int> node;
        std::vector<uint32_t> indices;
        void fill(const KModelData& model);
        void skin(const KModelData& model, const std::vector<float>& skinMatrices, const std::vector<float>& nodeWorld,
                  std::vector<float>& out) const;
    };

    struct ChildAsset {
        bool ok = false;
        QDateTime modified;
        KModelData model;
        SourceMesh mesh;
        std::vector<float> bindWorld, bindSkin;
    };
    // keyed by child index path, e.g. "0/2" = child 2 of child 0
    struct ChildAnimation {
        std::string path;
        bool isTree = false;
        AnimBlendTreeInstance tree;
        std::shared_ptr<AnimationClipData> clip;
        float lastTime = -1.0f;
        std::vector<AnimationTransform> pose;
    };
    struct ChildDraw {
        size_t firstIndex;
        size_t indexCount;
        bool selected;
    };
    std::shared_ptr<const ChildAsset> childAsset(const std::string& contentPath);
    bool childPose(const QString& key, const KModelChild& child, const KModelData& model, std::vector<AnimationTransform>& outPose);
    void appendChild(const KModelChild& child, const QString& key, const float matrix[16], bool selected, int depth);
    void buildChildMeshes();
    bool childMatrix(int index, float out[16]) const;

    void rebuildStaticMesh();
    void skinMesh();
    void buildOverlayLines();
    void addLine(const QVector3D& a, const QVector3D& b, const QColor& c);
    void addTransformedCircle(const float m[16], int axisA, int axisB, float radius, float offsetAxis, int offsetIndex,
                              const QColor& c, float startAngle = 0.0f, float sweep = 6.2831853f);
    void addCollider(int index, const QColor& c);
    QMatrix4x4 viewProj() const;
    QVector3D eye() const;
    void pick(const QPoint& pos);

    struct GizmoFrame {
        QVector3D origin;
        QVector3D axis[3];      // parent-space axes in world, unit length
        float axisScale[3];     // world length of one parent-space unit along each axis
        float size;             // world length of the handles
    };
    bool gizmoFrame(GizmoFrame& out) const;
    void buildGizmoLines();
    int gizmoHandleAt(const QPoint& pos) const;
    void dragGizmo(const QPoint& from, const QPoint& to);
    QPointF toScreen(const QVector3D& p) const;

    ModelDocument* m_doc;
    PreviewState* m_preview;
    Overlays m_overlays;

    QOpenGLShaderProgram m_meshProgram;
    QOpenGLShaderProgram m_lineProgram;
    QOpenGLVertexArrayObject m_meshVao, m_lineVao, m_childVao;
    QOpenGLBuffer m_childVbo{QOpenGLBuffer::VertexBuffer};
    QOpenGLBuffer m_childIbo{QOpenGLBuffer::IndexBuffer};
    QHash<QString, std::shared_ptr<ChildAsset>> m_childCache;
    QHash<QString, ChildAnimation> m_childAnimations;
    std::vector<float> m_childSkinned, m_childWorld, m_childSkin;
    std::vector<float> m_childVerts; // pos + normal
    std::vector<uint32_t> m_childIndices;
    std::vector<ChildDraw> m_childDraws;
    QOpenGLBuffer m_meshVbo{QOpenGLBuffer::VertexBuffer};
    QOpenGLBuffer m_meshIbo{QOpenGLBuffer::IndexBuffer};
    QOpenGLBuffer m_lineVbo{QOpenGLBuffer::VertexBuffer};
    bool m_glReady = false;
    bool m_meshDirty = true;
    bool m_indicesPending = false;

    SourceMesh m_src;
    std::vector<float> m_skinned; // pos + normal
    std::vector<LineVertex> m_lines;
    size_t m_gridLineCount = 0;

    QVector3D m_target;
    float m_yaw = 35.0f;
    float m_pitch = 20.0f;
    float m_distance = 4.0f;
    float m_radius = 1.0f;
    GizmoMode m_gizmoMode = GizmoMode::Move;
    int m_hoverHandle = -1;
    int m_dragHandle = -1;
    int m_dragSerial = 0;
    QPoint m_lastMouse;
    QPoint m_pressMouse;
    Qt::MouseButton m_dragButton = Qt::NoButton;
};

} // namespace kme
