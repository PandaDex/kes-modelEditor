#include "SkeletonPanel.h"

#include "AttachmentPanel.h"
#include "KesStyle.h"
#include "ModelDocument.h"
#include "PreviewState.h"

#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLineEdit>
#include <QMenu>
#include <QToolButton>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <QVBoxLayout>
#include <functional>

namespace kme {

namespace {

constexpr int kKindRole = Qt::UserRole + 1;
constexpr int kIndexRole = Qt::UserRole + 2;
constexpr int kColliderHeader = 100;

QToolButton* toolButton(const QString& icon, const QString& text, const QString& tip)
{
    auto* b = new QToolButton;
    b->setIcon(kes::style::icon(icon));
    b->setText(text);
    b->setToolTip(tip);
    b->setToolButtonStyle(text.isEmpty() ? Qt::ToolButtonIconOnly : Qt::ToolButtonTextBesideIcon);
    return b;
}

const char* shapeName(KModelColliderShape s)
{
    switch (s) {
    case KModelColliderShape::Sphere: return "Sphere";
    case KModelColliderShape::Capsule: return "Capsule";
    default: return "Box";
    }
}

} // namespace

SkeletonPanel::SkeletonPanel(ModelDocument* doc, const PreviewState* preview, QWidget* parent)
    : QWidget(parent), m_doc(doc), m_preview(preview)
{
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(6, 6, 6, 6);
    lay->setSpacing(4);

    auto* bar = new QHBoxLayout;
    bar->setSpacing(2);
    auto* addSocketBtn = toolButton("plus", "Socket", "Add a socket to the selected bone");
    auto* addColliderBtn = toolButton("plus", "Collider", "Add a collider on the selected bone");
    auto* addChildBtn = toolButton("plus", "Model", "Attach another .kmodel to the selected bone (e.g. a weapon in a hand)");
    auto* deleteBtn = toolButton("trash", QString(), "Delete the selected socket, collider or child model");
    bar->addWidget(addSocketBtn);
    bar->addWidget(addColliderBtn);
    bar->addWidget(addChildBtn);
    bar->addStretch(1);
    bar->addWidget(deleteBtn);
    lay->addLayout(bar);

    m_filter = new QLineEdit;
    m_filter->setPlaceholderText("Filter bones…");
    m_filter->setClearButtonEnabled(true);
    lay->addWidget(m_filter);

    m_tree = new QTreeWidget;
    m_tree->setHeaderHidden(true);
    m_tree->setContextMenuPolicy(Qt::CustomContextMenu);
    m_tree->setUniformRowHeights(true);
    lay->addWidget(m_tree, 1);

    connect(addSocketBtn, &QToolButton::clicked, this, &SkeletonPanel::addSocket);
    connect(addColliderBtn, &QToolButton::clicked, this, &SkeletonPanel::addCollider);
    connect(addChildBtn, &QToolButton::clicked, this, &SkeletonPanel::addChildModel);
    connect(deleteBtn, &QToolButton::clicked, this, &SkeletonPanel::deleteSelected);
    connect(m_filter, &QLineEdit::textChanged, this, &SkeletonPanel::applyFilter);
    connect(m_tree, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem* item) { onCurrentChanged(item); });
    connect(m_tree, &QTreeWidget::customContextMenuRequested, this, &SkeletonPanel::showContextMenu);
    connect(m_doc, &ModelDocument::modelReset, this, &SkeletonPanel::rebuild);
    connect(m_doc, &ModelDocument::attachmentsChanged, this, &SkeletonPanel::rebuild);
    connect(m_doc, &ModelDocument::selectionChanged, this, &SkeletonPanel::syncSelection);
    rebuild();
}

void SkeletonPanel::rebuild()
{
    m_syncing = true;
    m_tree->clear();
    const KModelData& model = m_doc->model();
    if (!m_doc->isOpen()) {
        m_syncing = false;
        return;
    }

    QFont boneFont = m_tree->font();
    boneFont.setBold(true);
    QFont attachFont = m_tree->font();
    attachFont.setItalic(true);

    std::vector<QTreeWidgetItem*> items(model.nodes.size(), nullptr);
    auto* rootItem = new QTreeWidgetItem(m_tree, {"Model root"});
    rootItem->setData(0, kKindRole, int(Selection::Node));
    rootItem->setData(0, kIndexRole, -1);
    rootItem->setForeground(0, kes::style::muted());

    // parents can come after children in the file
    for (size_t i = 0; i < model.nodes.size(); ++i) {
        const KModelNode& node = model.nodes[i];
        auto* item = new QTreeWidgetItem({QString::fromStdString(node.name)});
        item->setData(0, kKindRole, int(Selection::Node));
        item->setData(0, kIndexRole, int(i));
        if (node.boneIndex >= 0) {
            item->setFont(0, boneFont);
            item->setToolTip(0, QString("Bone %1").arg(node.boneIndex));
        } else {
            item->setForeground(0, kes::style::muted());
            item->setToolTip(0, node.meshIndex >= 0 ? "Mesh node (not a bone)" : "Node (not a bone)");
        }
        items[i] = item;
    }
    for (size_t i = 0; i < model.nodes.size(); ++i) {
        const int parent = model.nodes[i].parentIndex;
        if (parent >= 0 && parent < int(items.size())) items[size_t(parent)]->addChild(items[i]);
        else rootItem->addChild(items[i]);
    }

    for (size_t i = 0; i < model.sockets.size(); ++i) {
        const KModelSocket& s = model.sockets[i];
        auto* item = new QTreeWidgetItem({QString("◆ %1").arg(QString::fromStdString(s.name))});
        item->setData(0, kKindRole, int(Selection::Socket));
        item->setData(0, kIndexRole, int(i));
        item->setFont(0, attachFont);
        item->setForeground(0, kes::style::info());
        item->setToolTip(0, "Socket");
        QTreeWidgetItem* parent = rootItem;
        if (s.boneIndex >= 0 && s.boneIndex < int(model.bones.size())) {
            const int node = model.bones[size_t(s.boneIndex)].nodeIndex;
            if (node >= 0 && node < int(items.size())) parent = items[size_t(node)];
        }
        parent->insertChild(0, item);
    }

    for (size_t i = 0; i < model.children.size(); ++i) {
        const KModelChild& c = model.children[i];
        const QString file = QFileInfo(QString::fromStdString(c.modelPath)).fileName();
        auto* item = new QTreeWidgetItem({QString("▣ %1").arg(c.name.empty() ? file : QString::fromStdString(c.name))});
        item->setData(0, kKindRole, int(Selection::Child));
        item->setData(0, kIndexRole, int(i));
        item->setFont(0, attachFont);
        item->setForeground(0, QColor(225, 170, 90));
        item->setToolTip(0, QString("Child model: %1").arg(QString::fromStdString(c.modelPath)));
        QTreeWidgetItem* parent = rootItem;
        if (c.boneIndex >= 0 && c.boneIndex < int(model.bones.size())) {
            const int node = model.bones[size_t(c.boneIndex)].nodeIndex;
            if (node >= 0 && node < int(items.size())) parent = items[size_t(node)];
        }
        parent->insertChild(0, item);
    }

    auto* colliders = new QTreeWidgetItem(m_tree, {QString("Colliders (%1)").arg(model.colliders.size())});
    colliders->setData(0, kKindRole, kColliderHeader);
    colliders->setFont(0, boneFont);
    for (size_t i = 0; i < model.colliders.size(); ++i) {
        const KModelCollider& c = model.colliders[i];
        const QString name = c.name.empty() ? QString("Collider %1").arg(i) : QString::fromStdString(c.name);
        auto* item = new QTreeWidgetItem(colliders, {QString("%1  ·  %2 on %3").arg(name, shapeName(c.shape), m_doc->boneName(c.boneIndex))});
        item->setData(0, kKindRole, int(Selection::Collider));
        item->setData(0, kIndexRole, int(i));
        item->setFont(0, attachFont);
        item->setForeground(0, QColor(110, 210, 120));
    }

    m_tree->expandAll();
    applyFilter();
    m_syncing = false;
    syncSelection();
}

void SkeletonPanel::applyFilter()
{
    const QString f = m_filter->text().trimmed();
    std::function<bool(QTreeWidgetItem*)> visit = [&](QTreeWidgetItem* item) {
        bool any = f.isEmpty() || item->text(0).contains(f, Qt::CaseInsensitive);
        for (int i = 0; i < item->childCount(); ++i) any = visit(item->child(i)) || any;
        item->setHidden(!any);
        return any;
    };
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) visit(m_tree->topLevelItem(i));
}

void SkeletonPanel::syncSelection()
{
    if (m_syncing) return;
    m_syncing = true;
    const Selection& sel = m_doc->selection();
    QTreeWidgetItem* found = nullptr;
    for (QTreeWidgetItemIterator it(m_tree); *it && !found; ++it) {
        const int kind = (*it)->data(0, kKindRole).toInt();
        if (kind == int(sel.kind) && (*it)->data(0, kIndexRole).toInt() == sel.index && sel.kind != Selection::None)
            found = *it;
    }
    m_tree->setCurrentItem(found);
    if (found) m_tree->scrollToItem(found);
    m_syncing = false;
}

void SkeletonPanel::onCurrentChanged(QTreeWidgetItem* item)
{
    if (m_syncing) return;
    Selection s;
    if (item) {
        const int kind = item->data(0, kKindRole).toInt();
        if (kind == int(Selection::Node) || kind == int(Selection::Socket) || kind == int(Selection::Collider) ||
            kind == int(Selection::Child)) {
            s.kind = Selection::Kind(kind);
            s.index = item->data(0, kIndexRole).toInt();
            if (s.kind == Selection::Node && s.index < 0) s = {};
        }
    }
    m_syncing = true;
    m_doc->setSelection(s);
    m_syncing = false;
}

int SkeletonPanel::selectedBone() const
{
    const Selection& sel = m_doc->selection();
    const KModelData& model = m_doc->model();
    if (sel.kind == Selection::Node) {
        // not a bone, walk up to the nearest one
        for (int n = sel.index; n >= 0 && n < int(model.nodes.size()); n = model.nodes[size_t(n)].parentIndex)
            if (model.nodes[size_t(n)].boneIndex >= 0) return model.nodes[size_t(n)].boneIndex;
    }
    if (sel.kind == Selection::Socket && sel.index < int(model.sockets.size())) return model.sockets[size_t(sel.index)].boneIndex;
    if (sel.kind == Selection::Collider && sel.index < int(model.colliders.size()))
        return model.colliders[size_t(sel.index)].boneIndex;
    if (sel.kind == Selection::Child && sel.index < int(model.children.size())) return model.children[size_t(sel.index)].boneIndex;
    return -1;
}

void SkeletonPanel::addSocket()
{
    if (!m_doc->isOpen()) return;
    const int bone = selectedBone();
    KModelSocket socket;
    socket.boneIndex = bone;
    const QString base = bone >= 0 ? m_doc->boneName(bone) + "_socket" : QStringLiteral("socket");
    socket.name = m_doc->uniqueSocketName(base).toStdString();
    m_doc->edit("Add socket", [&](KModelData& m) { m.sockets.push_back(socket); });
    m_doc->setSelection({Selection::Socket, int(m_doc->model().sockets.size()) - 1});
}

void SkeletonPanel::addCollider()
{
    if (!m_doc->isOpen()) return;
    KModelCollider collider;
    collider.boneIndex = selectedBone();
    collider.shape = collider.boneIndex >= 0 ? KModelColliderShape::Capsule : KModelColliderShape::Box;
    collider.radius = 0.1f;
    collider.halfHeight = 0.15f;
    collider.halfExtents[0] = collider.halfExtents[1] = collider.halfExtents[2] = 0.25f;
    collider.name = (collider.boneIndex >= 0 ? m_doc->boneName(collider.boneIndex) : QStringLiteral("body")).toStdString();
    m_doc->edit("Add collider", [&](KModelData& m) { m.colliders.push_back(collider); });
    m_doc->setSelection({Selection::Collider, int(m_doc->model().colliders.size()) - 1});
}

void SkeletonPanel::addChildModel()
{
    if (!m_doc->isOpen()) return;
    const int bone = selectedBone();
    const QString path = browseContentFile(this, m_preview, "Attach Model", "KES models (*.kmodel)");
    if (path.isEmpty()) return;
    if (QFileInfo(m_preview->resolvePath(path)).absoluteFilePath() == QFileInfo(m_doc->path()).absoluteFilePath()) return;
    KModelChild child;
    child.boneIndex = bone;
    child.modelPath = path.toStdString();
    child.name = m_doc->uniqueChildName(QFileInfo(path).completeBaseName()).toStdString();
    m_doc->edit("Add child model", [&](KModelData& m) { m.children.push_back(child); });
    m_doc->setSelection({Selection::Child, int(m_doc->model().children.size()) - 1});
}

void SkeletonPanel::deleteSelected()
{
    const Selection sel = m_doc->selection();
    if (sel.kind == Selection::Socket) {
        m_doc->setSelection({});
        m_doc->edit("Delete socket", [&](KModelData& m) { m.sockets.erase(m.sockets.begin() + sel.index); });
    } else if (sel.kind == Selection::Collider) {
        m_doc->setSelection({});
        m_doc->edit("Delete collider", [&](KModelData& m) { m.colliders.erase(m.colliders.begin() + sel.index); });
    } else if (sel.kind == Selection::Child) {
        m_doc->setSelection({});
        m_doc->edit("Delete child model", [&](KModelData& m) { m.children.erase(m.children.begin() + sel.index); });
    }
}

void SkeletonPanel::showContextMenu(const QPoint& pos)
{
    if (!m_doc->isOpen()) return;
    if (QTreeWidgetItem* item = m_tree->itemAt(pos)) m_tree->setCurrentItem(item);
    QMenu menu(this);
    menu.addAction(kes::style::icon("plus"), "Add Socket Here", this, &SkeletonPanel::addSocket);
    menu.addAction(kes::style::icon("plus"), "Add Collider Here", this, &SkeletonPanel::addCollider);
    menu.addAction(kes::style::icon("plus"), "Attach Model Here…", this, &SkeletonPanel::addChildModel);
    const Selection::Kind k = m_doc->selection().kind;
    if (k == Selection::Socket || k == Selection::Collider || k == Selection::Child) {
        menu.addSeparator();
        menu.addAction(kes::style::icon("trash"), "Delete", this, &SkeletonPanel::deleteSelected);
    }
    menu.exec(m_tree->viewport()->mapToGlobal(pos));
}

} // namespace kme
