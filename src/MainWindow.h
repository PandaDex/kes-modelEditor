#pragma once

#include <QMainWindow>
#include <QPointF>

class QAction;
class QDockWidget;
class QLabel;
class QTabWidget;
class QUndoGroup;

namespace kme {

class AnimationPanel;
class AttachmentPanel;
class GraphModel;
class GraphParamsPanel;
class InspectorWidget;
class ModelDocument;
class ModelViewport;
class NodeGraphScene;
class NodeGraphView;
class NodeSearchPopup;
class PreviewState;
class SkeletonPanel;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    MainWindow();

    void openPath(const QString& path);

protected:
    void closeEvent(QCloseEvent*) override;

private:
    void buildUi();
    void buildActions();
    bool openModel(const QString& path);
    bool openGraph(const QString& path);
    void newGraph();
    bool saveModel();
    bool saveGraph();
    bool saveGraphAs();
    void saveAll();
    bool maybeSave();
    void browseClipForPreview();
    void browseClipForNode(int nodeId, const QString& param);
    void showAddNodePopup();
    void updateContentRoot(const QString& fromPath);
    void rescanClips();
    void updateTitle();
    void updateActiveUndo();
    bool graphTabActive() const;
    QString lastDir() const;
    void rememberDir(const QString& file);
    QString toContentPath(const QString& absolutePath) const;

    ModelDocument* m_doc;
    GraphModel* m_graph;
    PreviewState* m_preview;
    NodeGraphScene* m_scene = nullptr;
    NodeGraphView* m_view = nullptr;
    NodeSearchPopup* m_popup = nullptr;
    ModelViewport* m_modelView = nullptr;
    ModelViewport* m_graphPreview = nullptr;
    SkeletonPanel* m_skeleton = nullptr;
    AttachmentPanel* m_attachments = nullptr;
    InspectorWidget* m_inspector = nullptr;
    GraphParamsPanel* m_params = nullptr;
    AnimationPanel* m_animation = nullptr;
    QTabWidget* m_tabs = nullptr;
    QUndoGroup* m_undoGroup = nullptr;
    QLabel* m_statusContent = nullptr;
    QDockWidget* m_inspectorDock = nullptr;
    QDockWidget* m_propertiesDock = nullptr;

    QString m_graphPath;
    QStringList m_clips;
    QPointF m_popupPos;
    int m_pendingFromNode = -1;
    int m_pendingFromPort = -1;
    bool m_pendingFromIsOutput = false;
};

} // namespace kme
