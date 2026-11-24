#include "MainWindow.h"

#include "AnimationPanel.h"
#include "AttachmentPanel.h"
#include "GraphModel.h"
#include "GraphParamsPanel.h"
#include "InspectorWidget.h"
#include "KesProject.h"
#include "KesStyle.h"
#include "ModelDocument.h"
#include "ModelViewport.h"
#include "NodeGraphScene.h"
#include "NodeGraphView.h"
#include "NodeSearchPopup.h"
#include "PreviewState.h"
#include "SkeletonPanel.h"
#include "anim_blend_tree.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QCloseEvent>
#include <QDir>
#include <QDirIterator>
#include <QDockWidget>
#include <QFileDialog>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QMenuBar>
#include <QMessageBox>
#include <QScrollArea>
#include <QSettings>
#include <QShortcut>
#include <QSplitter>
#include <QStatusBar>
#include <QTabWidget>
#include <QToolBar>
#include <QUndoGroup>

namespace kme
{

    namespace
    {

        QScrollArea* scrolled(QWidget* w)
        {
            auto* area = new QScrollArea;
            area->setWidget(w);
            area->setWidgetResizable(true);
            area->setFrameShape(QFrame::NoFrame);
            return area;
        }

    } // namespace

    MainWindow::MainWindow()
    {
        m_doc = new ModelDocument(this);
        m_graph = new GraphModel(this);
        // must exist before the viewports so the pose is updated before they redraw
        m_preview = new PreviewState(m_doc, m_graph, this);
        buildUi();
        buildActions();
        newGraph();
        updateTitle();
        resize(1500, 900);

        QSettings settings;
        restoreGeometry(settings.value("geometry").toByteArray());
        restoreState(settings.value("windowState").toByteArray());
    }

    void MainWindow::buildUi()
    {
        setDockOptions(AnimatedDocks | AllowTabbedDocks | AllowNestedDocks);

        m_modelView = new ModelViewport(m_doc, m_preview);
        m_graphPreview = new ModelViewport(m_doc, m_preview);
        m_scene = new NodeGraphScene(m_graph, this);
        m_view = new NodeGraphView(m_scene);
        m_popup = new NodeSearchPopup(this);

        auto* graphSplit = new QSplitter(Qt::Horizontal);
        graphSplit->addWidget(m_view);
        graphSplit->addWidget(m_graphPreview);
        graphSplit->setStretchFactor(0, 3);
        graphSplit->setStretchFactor(1, 2);

        m_tabs = new QTabWidget;
        m_tabs->setDocumentMode(true);
        m_tabs->addTab(m_modelView, "Model");
        m_tabs->addTab(graphSplit, "Anim Graph");
        setCentralWidget(m_tabs);

        const auto dock = [this](const QString& title, const QString& name, QWidget* w, Qt::DockWidgetArea area)
        {
            auto* d = new QDockWidget(title, this);
            d->setObjectName(name);
            d->setWidget(w);
            addDockWidget(area, d);
            return d;
        };

        m_skeleton = new SkeletonPanel(m_doc, m_preview);
        dock("Skeleton", "SkeletonDock", m_skeleton, Qt::LeftDockWidgetArea);

        m_attachments = new AttachmentPanel(m_doc, m_preview);
        m_propertiesDock = dock("Properties", "PropertiesDock", scrolled(m_attachments), Qt::RightDockWidgetArea);
        m_inspector = new InspectorWidget(m_graph);
        m_inspectorDock = dock("Node", "NodeDock", scrolled(m_inspector), Qt::RightDockWidgetArea);
        tabifyDockWidget(m_propertiesDock, m_inspectorDock);
        m_params = new GraphParamsPanel(m_preview);
        dock("Graph Parameters", "ParamsDock", scrolled(m_params), Qt::RightDockWidgetArea);
        m_propertiesDock->raise();

        m_animation = new AnimationPanel(m_preview);
        dock("Animation", "AnimationDock", m_animation, Qt::BottomDockWidgetArea);

        m_statusContent = new QLabel;
        statusBar()->addPermanentWidget(m_statusContent);

        m_undoGroup = new QUndoGroup(this);
        m_undoGroup->addStack(m_doc->undoStack());
        m_undoGroup->addStack(m_graph->undoStack());

        connect(m_tabs, &QTabWidget::currentChanged, this, [this](int)
            {
        updateActiveUndo();
        if (graphTabActive()) {
            m_inspectorDock->raise();
            m_preview->setMode(PreviewState::Mode::Graph);
            m_preview->setPlaying(true);
        } else {
            m_propertiesDock->raise();
        } });
        connect(m_doc, &ModelDocument::modifiedChanged, this, &MainWindow::updateTitle);
        connect(m_graph->undoStack(), &QUndoStack::cleanChanged, this, &MainWindow::updateTitle);
        connect(m_doc, &ModelDocument::selectionChanged, this, [this]
            {
        if (m_doc->selection().kind != Selection::None && !graphTabActive()) m_propertiesDock->raise(); });

        connect(m_scene, &NodeGraphScene::selectionIdsChanged, m_inspector, &InspectorWidget::setSelection);
        connect(m_scene, &NodeGraphScene::statusMessage, this, [this](const QString& t)
            { statusBar()->showMessage(t, 4000); });
        connect(m_view, &NodeGraphView::nodeDropped, this, [this](const QString& type, const QPointF& pos)
            { m_scene->addNodeAt(type, pos); });
        connect(m_view, &NodeGraphView::contextMenuRequestedAt, this, [this](const QPointF& scenePos, const QPoint& global)
            {
        m_popupPos = scenePos;
        m_pendingFromNode = -1;
        m_popup->popup(global); });
        connect(m_scene, &NodeGraphScene::nodePopupRequested, this,
            [this](const QPointF& scenePos, int fromNode, int fromPort, bool fromIsOutput)
            {
                m_popupPos = scenePos;
                m_pendingFromNode = fromNode;
                m_pendingFromPort = fromPort;
                m_pendingFromIsOutput = fromIsOutput;
                m_popup->popup(QCursor::pos());
            });
        connect(m_popup, &NodeSearchPopup::nodeChosen, this, [this](const QString& type)
            {
        if (m_pendingFromNode >= 0)
            m_scene->createNodeAndConnect(type, m_popupPos, m_pendingFromNode, m_pendingFromPort, m_pendingFromIsOutput);
        else
            m_scene->addNodeAt(type, m_popupPos);
        m_pendingFromNode = -1; });
        connect(m_inspector, &InspectorWidget::browseClipRequested, this, &MainWindow::browseClipForNode);
        connect(m_animation, &AnimationPanel::browseClipRequested, this, &MainWindow::browseClipForPreview);
        connect(m_preview, &PreviewState::poseChanged, this, [this]
            {
        if (m_preview->mode() == PreviewState::Mode::Graph && m_preview->graphError().isEmpty()) {
            AnimBlendTreeInstance& inst = m_preview->graphInstance();
            m_scene->setNodeWeights([&inst](int id) { return inst.GetNodeWeight(id); });
        } else {
            m_scene->setNodeWeights(nullptr);
        } });
        connect(m_doc, &ModelDocument::modelReset, this, [this]
            {
        QStringList bones;
        for (const KModelNode& n : m_doc->model().nodes) bones << QString::fromStdString(n.name);
        m_inspector->setChoices(m_clips, bones); });
    }

    void MainWindow::buildActions()
    {
        auto make = [this](const QString& text, const QKeySequence& ks, auto slot, const QString& icon = {})
        {
            auto* a = new QAction(text, this);
            if (!ks.isEmpty()) a->setShortcut(ks);
            if (!icon.isEmpty()) a->setIcon(kes::style::icon(icon));
            connect(a, &QAction::triggered, this, slot);
            return a;
        };

        QMenu* file = menuBar()->addMenu("&File");
        QAction* openModelAct = make("&Open Model…", QKeySequence::Open, [this]
            {
        if (!maybeSave()) return;
        const QString path = QFileDialog::getOpenFileName(this, "Open Model", lastDir(), "KES models (*.kmodel)");
        if (!path.isEmpty()) openModel(path); }, "open");
        QAction* openGraphAct = make("Open Anim &Graph…", QKeySequence("Ctrl+Shift+O"), [this]
            {
        if (!maybeSave()) return;
        const QString path = QFileDialog::getOpenFileName(this, "Open Animation Graph", lastDir(), "Animation graphs (*.ag)");
        if (!path.isEmpty()) openGraph(path); });
        QAction* newGraphAct = make("&New Anim Graph", QKeySequence("Ctrl+N"), [this]
            {
        if (m_graph->undoStack()->isClean() || QMessageBox::question(this, "New Graph", "Discard changes to the current graph?") == QMessageBox::Yes) {
            newGraph();
            m_tabs->setCurrentIndex(1);
        } }, "new");
        QAction* saveAct = make("&Save All", QKeySequence::Save, [this]
            { saveAll(); }, "save");
        QAction* saveModelAct = make("Save &Model", QKeySequence(), [this]
            { saveModel(); });
        QAction* saveGraphAct = make("Save &Graph", QKeySequence(), [this]
            { saveGraph(); });
        QAction* saveGraphAsAct = make("Save Graph &As…", QKeySequence::SaveAs, [this]
            { saveGraphAs(); });
        file->addAction(openModelAct);
        file->addAction(openGraphAct);
        file->addAction(newGraphAct);
        file->addSeparator();
        file->addAction(saveAct);
        file->addAction(saveModelAct);
        file->addAction(saveGraphAct);
        file->addAction(saveGraphAsAct);
        file->addSeparator();
        file->addAction(make("&Quit", QKeySequence::Quit, [this]
            { close(); }));

        QMenu* edit = menuBar()->addMenu("&Edit");
        QAction* undo = m_undoGroup->createUndoAction(this, "&Undo");
        undo->setShortcut(QKeySequence::Undo);
        undo->setIcon(kes::style::icon("undo"));
        QAction* redo = m_undoGroup->createRedoAction(this, "&Redo");
        redo->setShortcuts({ QKeySequence::Redo, QKeySequence("Ctrl+Y") });
        redo->setIcon(kes::style::icon("redo"));
        edit->addAction(undo);
        edit->addAction(redo);
        edit->addSeparator();
        QAction* del = make("&Delete", QKeySequence::Delete, [this]
            {
        if (graphTabActive()) m_scene->deleteSelection();
        else m_skeleton->deleteSelected(); }, "trash");
        edit->addAction(del);

        QMenu* model = menuBar()->addMenu("&Model");
        model->addAction(make("Add &Socket", QKeySequence("Ctrl+Shift+S"), [this]
            { m_skeleton->addSocket(); }, "plus"));
        model->addAction(make("Add &Collider", QKeySequence("Ctrl+Shift+C"), [this]
            { m_skeleton->addCollider(); }, "plus"));
        model->addAction(make("Attach &Model…", QKeySequence("Ctrl+Shift+M"), [this]
            { m_skeleton->addChildModel(); }, "plus"));
        model->addSeparator();
        model->addAction(make("&Frame Model", QKeySequence(), [this]
            {
        m_modelView->frameModel();
        m_graphPreview->frameModel(); }));

        auto* gizmoGroup = new QActionGroup(this);
        QAction* moveAct = make("&Move Gizmo", QKeySequence(), [this]
            {
        for (ModelViewport* v : {m_modelView, m_graphPreview}) v->setGizmoMode(ModelViewport::GizmoMode::Move); });
        QAction* rotateAct = make("&Rotate Gizmo", QKeySequence(), [this]
            {
        for (ModelViewport* v : {m_modelView, m_graphPreview}) v->setGizmoMode(ModelViewport::GizmoMode::Rotate); });
        for (QAction* a : { moveAct, rotateAct })
        {
            a->setCheckable(true);
            gizmoGroup->addAction(a);
        }
        moveAct->setChecked(true);
        moveAct->setToolTip("Move gizmo (W in the viewport)");
        rotateAct->setToolTip("Rotate gizmo (E in the viewport)");
        for (ModelViewport* v : { m_modelView, m_graphPreview })
        {
            connect(v, &ModelViewport::gizmoModeChanged, this, [this, moveAct, rotateAct](ModelViewport::GizmoMode mode)
                {
            (mode == ModelViewport::GizmoMode::Move ? moveAct : rotateAct)->setChecked(true);
            for (ModelViewport* other : {m_modelView, m_graphPreview}) other->setGizmoMode(mode); });
        }
        model->addSeparator();
        model->addAction(moveAct);
        model->addAction(rotateAct);

        QMenu* graph = menuBar()->addMenu("&Graph");
        graph->addAction(make("&Add Node…", QKeySequence("Shift+A"), [this]
            { showAddNodePopup(); }, "plus"));
        auto* tab = new QShortcut(QKeySequence(Qt::Key_Tab), m_view, [this]
            { showAddNodePopup(); });
        tab->setContext(Qt::WidgetShortcut);
        graph->addSeparator();
        graph->addAction(make("Cu&t", QKeySequence::Cut, [this]
            { m_scene->cutSelection(); }));
        graph->addAction(make("&Copy", QKeySequence::Copy, [this]
            { m_scene->copySelection(); }, "copy"));
        graph->addAction(make("&Paste", QKeySequence::Paste, [this]
            { m_scene->paste(m_view->cursorScenePos()); }));
        graph->addAction(make("D&uplicate", QKeySequence("Ctrl+D"), [this]
            { m_scene->duplicateSelection(); }));
        graph->addAction(make("Select A&ll", QKeySequence::SelectAll, [this]
            { m_scene->selectNodes(m_graph->nodeIds()); }));
        graph->addSeparator();
        QAction* frameAll = make("&Frame All", QKeySequence(), [this]
            { m_view->frameAll(); });
        graph->addAction(frameAll);
        auto* f = new QShortcut(QKeySequence(Qt::Key_F), m_view, [this]
            { m_view->frameAll(); });
        f->setContext(Qt::WidgetShortcut);
        graph->addAction(make("&Restart Preview", QKeySequence("Ctrl+R"), [this]
            { m_preview->restartGraph(); }));
        graph->addAction(make("Use &Open Model", QKeySequence(), [this]
            {
        const QString rel = toContentPath(m_doc->path());
        m_graph->edit("Set graph model", [&] { m_graph->setModelPath(rel); });
        statusBar()->showMessage(QString("Graph model set to %1").arg(rel), 4000); }));

        QMenu* view = menuBar()->addMenu("&View");
        const auto overlay = [this, view](const QString& text, bool ModelViewport::Overlays::* member)
        {
            auto* a = view->addAction(text);
            a->setCheckable(true);
            a->setChecked(m_modelView->overlays().*member);
            connect(a, &QAction::toggled, this, [this, member](bool on)
                {
            for (ModelViewport* v : {m_modelView, m_graphPreview}) {
                ModelViewport::Overlays o = v->overlays();
                o.*member = on;
                v->setOverlays(o);
            } });
        };
        overlay("Show &Mesh", &ModelViewport::Overlays::mesh);
        overlay("Show S&keleton", &ModelViewport::Overlays::skeleton);
        overlay("Show &Sockets", &ModelViewport::Overlays::sockets);
        overlay("Show &Colliders", &ModelViewport::Overlays::colliders);
        overlay("Show C&hild Models", &ModelViewport::Overlays::children);
        overlay("Show &Grid", &ModelViewport::Overlays::grid);
        view->addSeparator();
        for (QDockWidget* d : findChildren<QDockWidget*>())
            view->addAction(d->toggleViewAction());

        auto* bar = addToolBar("Main");
        bar->setObjectName("MainToolbar");
        bar->setMovable(false);
        bar->addAction(openModelAct);
        bar->addAction(saveAct);
        bar->addSeparator();
        bar->addAction(undo);
        bar->addAction(redo);
        bar->addSeparator();
        bar->addAction(moveAct);
        bar->addAction(rotateAct);

        updateActiveUndo();
    }

    bool MainWindow::graphTabActive() const
    {
        return m_tabs->currentIndex() == 1;
    }

    void MainWindow::updateActiveUndo()
    {
        m_undoGroup->setActiveStack(graphTabActive() ? m_graph->undoStack() : m_doc->undoStack());
    }

    void MainWindow::showAddNodePopup()
    {
        m_tabs->setCurrentIndex(1);
        m_popupPos = m_view->cursorScenePos();
        m_pendingFromNode = -1;
        QPoint global = QCursor::pos();
        if (!m_view->rect().contains(m_view->mapFromGlobal(global))) global = m_view->mapToGlobal(m_view->rect().center());
        m_popup->popup(global);
    }

    QString MainWindow::lastDir() const
    {
        const QString dir = QSettings().value("lastDir").toString();
        if (!dir.isEmpty()) return dir;
        return m_preview->contentRoot();
    }

    void MainWindow::rememberDir(const QString& file)
    {
        QSettings().setValue("lastDir", QFileInfo(file).absolutePath());
    }

    QString MainWindow::toContentPath(const QString& absolutePath) const
    {
        const QString rel = contentRelativePath(m_preview->contentRoot(), absolutePath);
        return rel.isEmpty() ? QDir::fromNativeSeparators(absolutePath) : rel;
    }

    void MainWindow::updateContentRoot(const QString& fromPath)
    {
        const QString root = findContentRoot(fromPath);
        if (!root.isEmpty()) m_preview->setContentRoot(root);
        m_statusContent->setText(m_preview->contentRoot().isEmpty() ? QStringLiteral("No content folder found")
                                                                    : QString("Content: %1").arg(QDir::toNativeSeparators(m_preview->contentRoot())));
        rescanClips();
    }

    void MainWindow::rescanClips()
    {
        m_clips.clear();
        const QString root = m_preview->contentRoot();
        const QString searchDir = !root.isEmpty() ? root : (m_doc->isOpen() ? QFileInfo(m_doc->path()).absolutePath() : QString());
        if (!searchDir.isEmpty())
        {
            QDirIterator it(searchDir, { "*.kanim" }, QDir::Files, QDirIterator::Subdirectories);
            while (it.hasNext())
                m_clips << toContentPath(it.next());
        }
        // <model>_<clip>.kanim first
        const QString stem = m_doc->isOpen() ? QFileInfo(m_doc->path()).completeBaseName() + "_" : QString();
        std::stable_sort(m_clips.begin(), m_clips.end(), [&](const QString& a, const QString& b)
            {
        const bool am = !stem.isEmpty() && QFileInfo(a).fileName().startsWith(stem, Qt::CaseInsensitive);
        const bool bm = !stem.isEmpty() && QFileInfo(b).fileName().startsWith(stem, Qt::CaseInsensitive);
        if (am != bm) return am;
        return a.compare(b, Qt::CaseInsensitive) < 0; });
        m_animation->setClips(m_clips);
        QStringList bones;
        for (const KModelNode& n : m_doc->model().nodes)
            bones << QString::fromStdString(n.name);
        m_inspector->setChoices(m_clips, bones);
    }

    void MainWindow::openPath(const QString& path)
    {
        if (path.endsWith(".ag", Qt::CaseInsensitive))
            openGraph(path);
        else
            openModel(path);
    }

    bool MainWindow::openModel(const QString& path)
    {
        QString error;
        if (!m_doc->load(path, &error))
        {
            QMessageBox::warning(this, "Open Model", QString("Could not open %1:\n%2").arg(QDir::toNativeSeparators(path), error));
            return false;
        }
        rememberDir(path);
        updateContentRoot(path);
        if (m_graph->modelPath().isEmpty() && m_graph->undoStack()->isClean()) m_graph->setModelPath(toContentPath(m_doc->path()));
        const KModelData& model = m_doc->model();
        statusBar()->showMessage(QString("%1: %2 meshes, %3 bones, %4 sockets, %5 colliders")
                                     .arg(QFileInfo(path).fileName())
                                     .arg(model.meshes.size())
                                     .arg(model.bones.size())
                                     .arg(model.sockets.size())
                                     .arg(model.colliders.size()),
            6000);
        updateTitle();
        return true;
    }

    bool MainWindow::openGraph(const QString& path)
    {
        AnimBlendTreeData tree;
        std::string error;
        if (!LoadAnimBlendTreeFile(QFileInfo(path).absoluteFilePath().toUtf8().constData(), tree, &error))
        {
            QMessageBox::warning(this, "Open Graph", QString("Could not open %1:\n%2").arg(QDir::toNativeSeparators(path), QString::fromStdString(error)));
            return false;
        }
        QStringList warnings;
        m_graph->loadBlendTree(tree, &warnings);
        m_graph->undoStack()->clear();
        m_graphPath = QFileInfo(path).absoluteFilePath();
        rememberDir(path);
        updateContentRoot(path);

        const QString modelFile = m_preview->resolvePath(m_graph->modelPath());
        if (!modelFile.isEmpty() && QFileInfo(modelFile).absoluteFilePath() != m_doc->path())
        {
            if (QFileInfo::exists(modelFile))
                openModel(modelFile);
            else
                warnings << QString("The graph's model %1 was not found.").arg(m_graph->modelPath());
        }
        if (!warnings.isEmpty()) QMessageBox::information(this, "Open Graph", warnings.join("\n"));

        m_tabs->setCurrentIndex(1);
        m_preview->restartGraph();
        m_view->frameAll();
        updateTitle();
        return true;
    }

    void MainWindow::newGraph()
    {
        m_graph->loadJson(QJsonObject{ { "format", "kesAnimGraph" }, { "nodes", QJsonArray() }, { "connections", QJsonArray() } });
        m_graph->addNode("Output", QPointF(300, 0));
        if (m_doc->isOpen()) m_graph->setModelPath(toContentPath(m_doc->path()));
        m_graph->undoStack()->clear();
        m_graphPath.clear();
        m_preview->restartGraph();
        m_view->frameAll();
        updateTitle();
    }

    bool MainWindow::saveModel()
    {
        if (!m_doc->isOpen()) return true;
        QString error;
        if (!m_doc->save(&error))
        {
            QMessageBox::warning(this, "Save Model", QString("Could not save the model:\n%1").arg(error));
            return false;
        }
        statusBar()->showMessage(QString("Saved %1").arg(QFileInfo(m_doc->path()).fileName()), 4000);
        return true;
    }

    bool MainWindow::saveGraph()
    {
        if (m_graphPath.isEmpty()) return saveGraphAs();
        std::string error;
        const AnimBlendTreeData tree = m_graph->toBlendTree();
        if (!SaveAnimBlendTreeFile(m_graphPath.toUtf8().constData(), tree, &error))
        {
            QMessageBox::warning(this, "Save Graph", QString("Could not save the graph:\n%1").arg(QString::fromStdString(error)));
            return false;
        }
        m_graph->undoStack()->setClean();
        statusBar()->showMessage(QString("Saved %1").arg(QFileInfo(m_graphPath).fileName()), 4000);
        AnimBlendTreeInstance check;
        std::string buildError;
        if (!check.Build(tree, &buildError))
            statusBar()->showMessage(QString("Saved, but the game will not play it yet: %1").arg(QString::fromStdString(buildError)), 8000);
        updateTitle();
        return true;
    }

    bool MainWindow::saveGraphAs()
    {
        QString suggestion = m_graphPath;
        if (suggestion.isEmpty())
        {
            const QString dir = m_doc->isOpen() ? QFileInfo(m_doc->path()).absolutePath() : lastDir();
            const QString stem = m_doc->isOpen() ? QFileInfo(m_doc->path()).completeBaseName() : QStringLiteral("animation");
            suggestion = QDir(dir).absoluteFilePath(stem + ".ag");
        }
        QString path = QFileDialog::getSaveFileName(this, "Save Animation Graph", suggestion, "Animation graphs (*.ag)");
        if (path.isEmpty()) return false;
        if (!path.endsWith(".ag", Qt::CaseInsensitive)) path += ".ag";
        m_graphPath = path;
        rememberDir(path);
        if (m_preview->contentRoot().isEmpty()) updateContentRoot(path);
        return saveGraph();
    }

    void MainWindow::saveAll()
    {
        if (m_doc->isModified()) saveModel();
        if (!m_graph->undoStack()->isClean() || (graphTabActive() && m_graphPath.isEmpty())) saveGraph();
    }

    bool MainWindow::maybeSave()
    {
        QStringList dirty;
        if (m_doc->isModified()) dirty << QFileInfo(m_doc->path()).fileName();
        if (!m_graph->undoStack()->isClean()) dirty << (m_graphPath.isEmpty() ? QStringLiteral("the new anim graph") : QFileInfo(m_graphPath).fileName());
        if (dirty.isEmpty()) return true;
        const auto answer = QMessageBox::question(this, "Unsaved Changes", QString("Save changes to %1?").arg(dirty.join(" and ")),
            QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
        if (answer == QMessageBox::Cancel) return false;
        if (answer == QMessageBox::Save)
        {
            if (m_doc->isModified() && !saveModel()) return false;
            if (!m_graph->undoStack()->isClean() && !saveGraph()) return false;
        }
        return true;
    }

    void MainWindow::browseClipForPreview()
    {
        const QString path = QFileDialog::getOpenFileName(this, "Open Clip", lastDir(), "Animation clips (*.kanim)");
        if (path.isEmpty()) return;
        rememberDir(path);
        m_animation->selectClip(toContentPath(path));
    }

    void MainWindow::browseClipForNode(int nodeId, const QString& param)
    {
        const QString path = QFileDialog::getOpenFileName(this, "Pick Clip", lastDir(), "Animation clips (*.kanim)");
        if (path.isEmpty()) return;
        rememberDir(path);
        const QString rel = toContentPath(path);
        if (QDir::isAbsolutePath(rel))
            statusBar()->showMessage("The clip is outside the content folder; the game will not find it.", 8000);
        m_graph->edit("Pick clip", [&]
            { m_graph->setParam(nodeId, param, rel); });
    }

    void MainWindow::updateTitle()
    {
        QString doc = m_doc->isOpen() ? QFileInfo(m_doc->path()).fileName() : QStringLiteral("No model");
        doc += QString("  ·  %1").arg(m_graphPath.isEmpty() ? QStringLiteral("untitled graph") : QFileInfo(m_graphPath).fileName());
        setWindowTitle(kes::style::windowTitle(doc, "Model Editor"));
        setWindowModified(m_doc->isModified() || !m_graph->undoStack()->isClean());
    }

    void MainWindow::closeEvent(QCloseEvent* e)
    {
        if (!maybeSave())
        {
            e->ignore();
            return;
        }
        QSettings settings;
        settings.setValue("geometry", saveGeometry());
        settings.setValue("windowState", saveState());
        e->accept();
    }

} // namespace kme
