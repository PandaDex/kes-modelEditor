#include "AttachmentPanel.h"

#include "KesStyle.h"
#include "ModelDocument.h"

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QQuaternion>
#include <QSignalBlocker>
#include <QVBoxLayout>
#include <algorithm>
#include <cfloat>

namespace kme {

namespace {

QVector3D toVec(const float v[3]) { return {v[0], v[1], v[2]}; }
void fromVec(const QVector3D& v, float out[3])
{
    out[0] = v.x();
    out[1] = v.y();
    out[2] = v.z();
}

// stored as x, y, z, w but QQuaternion takes w first
QVector3D eulerOf(const float q[4]) { return QQuaternion(q[3], q[0], q[1], q[2]).toEulerAngles(); }
void quatFromEuler(const QVector3D& e, float out[4])
{
    const QQuaternion q = QQuaternion::fromEulerAngles(e).normalized();
    out[0] = q.x();
    out[1] = q.y();
    out[2] = q.z();
    out[3] = q.scalar();
}

QLabel* note(const QString& text)
{
    auto* l = new QLabel(text);
    l->setWordWrap(true);
    l->setObjectName("Muted");
    return l;
}

} // namespace

Vec3Edit::Vec3Edit(double step, QWidget* parent) : QWidget(parent)
{
    auto* h = new QHBoxLayout(this);
    h->setContentsMargins(0, 0, 0, 0);
    h->setSpacing(3);
    const char* axes[3] = {"X", "Y", "Z"};
    for (int i = 0; i < 3; ++i) {
        m_spin[i] = new QDoubleSpinBox;
        m_spin[i]->setDecimals(4);
        m_spin[i]->setRange(-1e5, 1e5);
        m_spin[i]->setSingleStep(step);
        m_spin[i]->setPrefix(QString("%1 ").arg(axes[i]));
        m_spin[i]->setMinimumWidth(70);
        m_spin[i]->setKeyboardTracking(false);
        h->addWidget(m_spin[i], 1);
        connect(m_spin[i], &QDoubleSpinBox::valueChanged, this, [this](double) { emit edited(value()); });
    }
}

void Vec3Edit::setValue(const QVector3D& v)
{
    for (int i = 0; i < 3; ++i) {
        if (m_spin[i]->hasFocus() && qFuzzyCompare(float(m_spin[i]->value()) + 1.0f, v[i] + 1.0f)) continue;
        const QSignalBlocker block(m_spin[i]);
        m_spin[i]->setValue(v[i]);
    }
}

QVector3D Vec3Edit::value() const
{
    return {float(m_spin[0]->value()), float(m_spin[1]->value()), float(m_spin[2]->value())};
}

AttachmentPanel::AttachmentPanel(ModelDocument* doc, QWidget* parent) : QWidget(parent), m_doc(doc)
{
    m_root = new QVBoxLayout(this);
    m_root->setContentsMargins(8, 8, 8, 8);
    m_root->addStretch(1);
    connect(m_doc, &ModelDocument::selectionChanged, this, &AttachmentPanel::rebuild);
    connect(m_doc, &ModelDocument::modelReset, this, &AttachmentPanel::rebuild);
    connect(m_doc, &ModelDocument::attachmentsChanged, this, &AttachmentPanel::refreshValues);
    rebuild();
}

void AttachmentPanel::refreshValues()
{
    if (!m_editing && m_refresh) m_refresh();
}

QComboBox* AttachmentPanel::boneCombo(int current)
{
    auto* combo = new QComboBox;
    combo->addItem("(model root)", -1);
    const KModelData& model = m_doc->model();
    for (size_t b = 0; b < model.bones.size(); ++b) combo->addItem(QString::fromStdString(model.bones[b].name), int(b));
    combo->setCurrentIndex(std::max(0, combo->findData(current)));
    combo->setMaxVisibleItems(24);
    return combo;
}

void AttachmentPanel::rebuild()
{
    m_refresh = nullptr;
    if (m_content) {
        m_root->removeWidget(m_content);
        m_content->deleteLater();
    }
    m_content = new QWidget;
    auto* lay = new QVBoxLayout(m_content);
    lay->setContentsMargins(0, 0, 0, 0);
    m_root->insertWidget(0, m_content);

    const Selection sel = m_doc->selection();
    if (!m_doc->isOpen()) {
        lay->addWidget(note("No model open."));
        return;
    }
    if (sel.kind == Selection::Node) {
        buildNode(lay, sel.index);
        return;
    }
    if (sel.kind != Selection::Socket && sel.kind != Selection::Collider) {
        lay->addWidget(note("Select a bone, socket or collider in the Skeleton panel or click one in the viewport.\n\n"
                            "Left-drag: orbit   Shift/middle-drag: pan\nWheel or right-drag: zoom   F: frame"));
        return;
    }

    auto* title = new QLabel(sel.kind == Selection::Socket ? "Socket" : "Collider");
    title->setObjectName("Title");
    lay->addWidget(title);
    auto* form = new QFormLayout;
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    lay->addLayout(form);
    if (sel.kind == Selection::Socket) buildSocket(form, sel.index);
    else buildCollider(form, sel.index);
    if (m_refresh) m_refresh();
}

void AttachmentPanel::buildNode(QVBoxLayout* lay, int index)
{
    const KModelData& model = m_doc->model();
    if (index < 0 || index >= int(model.nodes.size())) return;
    const KModelNode& node = model.nodes[size_t(index)];
    auto* title = new QLabel(QString::fromStdString(node.name));
    title->setObjectName("Title");
    lay->addWidget(title);
    QString info = node.boneIndex >= 0 ? QString("Bone %1").arg(node.boneIndex) : QString("Node (not a bone)");
    if (node.parentIndex >= 0) info += QString("  ·  parent %1").arg(QString::fromStdString(model.nodes[size_t(node.parentIndex)].name));
    lay->addWidget(note(info));
    lay->addWidget(note("Use the Skeleton panel's Socket and Collider buttons to attach to this bone."));
}

void AttachmentPanel::buildSocket(QFormLayout* form, int index)
{
    const auto socket = [this, index]() -> const KModelSocket& { return m_doc->model().sockets[size_t(index)]; };
    auto* name = new QLineEdit;
    auto* bone = boneCombo(socket().boneIndex);
    auto* pos = new Vec3Edit(0.01);
    auto* rot = new Vec3Edit(1.0);
    auto* scale = new Vec3Edit(0.05);
    form->addRow("Name", name);
    form->addRow("Bone", bone);
    form->addRow("Position", pos);
    form->addRow("Rotation", rot);
    form->addRow("Scale", scale);

    const auto apply = [this, index](const QString& text, const QString& key, const std::function<void(KModelSocket&)>& fn) {
        m_editing = true;
        m_doc->edit(text, [&](KModelData& m) { fn(m.sockets[size_t(index)]); }, QString("socket:%1:%2").arg(index).arg(key));
        m_editing = false;
    };
    connect(name, &QLineEdit::editingFinished, this, [this, apply, name, index] {
        QString n = name->text().trimmed();
        if (n.isEmpty() || n.toStdString() == m_doc->model().sockets[size_t(index)].name) return;
        n = m_doc->uniqueSocketName(n);
        apply("Rename socket", "name", [&](KModelSocket& s) { s.name = n.toStdString(); });
    });
    connect(bone, &QComboBox::currentIndexChanged, this, [apply, bone](int) {
        apply("Change socket bone", "bone", [&](KModelSocket& s) { s.boneIndex = bone->currentData().toInt(); });
    });
    connect(pos, &Vec3Edit::edited, this, [apply](const QVector3D& v) {
        apply("Move socket", "pos", [&](KModelSocket& s) { fromVec(v, s.translation); });
    });
    connect(rot, &Vec3Edit::edited, this, [apply](const QVector3D& v) {
        apply("Rotate socket", "rot", [&](KModelSocket& s) { quatFromEuler(v, s.rotation); });
    });
    connect(scale, &Vec3Edit::edited, this, [apply](const QVector3D& v) {
        apply("Scale socket", "scale", [&](KModelSocket& s) { fromVec(v, s.scale); });
    });

    m_refresh = [=] {
        if (index >= int(m_doc->model().sockets.size())) return;
        const KModelSocket& s = socket();
        if (!name->hasFocus()) name->setText(QString::fromStdString(s.name));
        {
            const QSignalBlocker b(bone);
            bone->setCurrentIndex(std::max(0, bone->findData(s.boneIndex)));
        }
        pos->setValue(toVec(s.translation));
        rot->setValue(eulerOf(s.rotation));
        scale->setValue(toVec(s.scale));
    };
}

void AttachmentPanel::buildCollider(QFormLayout* form, int index)
{
    const auto collider = [this, index]() -> const KModelCollider& { return m_doc->model().colliders[size_t(index)]; };
    auto* name = new QLineEdit;
    auto* shape = new QComboBox;
    shape->addItems({"Box", "Sphere", "Capsule"});
    auto* bone = boneCombo(collider().boneIndex);
    auto* center = new Vec3Edit(0.01);
    auto* rot = new Vec3Edit(1.0);
    auto* half = new Vec3Edit(0.01);
    auto* radius = new QDoubleSpinBox;
    auto* halfHeight = new QDoubleSpinBox;
    for (QDoubleSpinBox* sb : {radius, halfHeight}) {
        sb->setDecimals(4);
        sb->setRange(0.0, 1e5);
        sb->setSingleStep(0.01);
        sb->setKeyboardTracking(false);
    }
    form->addRow("Name", name);
    form->addRow("Shape", shape);
    form->addRow("Bone", bone);
    form->addRow("Center", center);
    form->addRow("Rotation", rot);
    form->addRow("Half size", half);
    form->addRow("Radius", radius);
    form->addRow("Half height", halfHeight);

    auto* buttons = new QHBoxLayout;
    auto* fitBone = new QPushButton("Fit to Bone");
    fitBone->setToolTip("Capsule from this bone to its first child bone");
    auto* fitMesh = new QPushButton("Fit to Mesh");
    fitMesh->setToolTip("Fits the current shape around the whole mesh in the bind pose, on the model root");
    buttons->addWidget(fitBone);
    buttons->addWidget(fitMesh);
    form->addRow(buttons);
    form->addRow(note("Capsules run along their local Y axis. Colliders on a bone follow its animation in the game; "
                      "on a Dynamic model all colliders form one rigid body in the bind pose."));

    const auto apply = [this, index](const QString& text, const QString& key, const std::function<void(KModelCollider&)>& fn) {
        m_editing = true;
        m_doc->edit(text, [&](KModelData& m) { fn(m.colliders[size_t(index)]); }, QString("collider:%1:%2").arg(index).arg(key));
        m_editing = false;
    };
    connect(name, &QLineEdit::editingFinished, this, [this, apply, name, index] {
        const QString n = name->text().trimmed();
        if (n.toStdString() == m_doc->model().colliders[size_t(index)].name) return;
        apply("Rename collider", "name", [&](KModelCollider& c) { c.name = n.toStdString(); });
    });
    connect(shape, &QComboBox::currentIndexChanged, this, [this, apply](int i) {
        apply("Change collider shape", "shape", [&](KModelCollider& c) { c.shape = KModelColliderShape(i); });
        if (m_refresh) m_refresh();
    });
    connect(bone, &QComboBox::currentIndexChanged, this, [apply, bone](int) {
        apply("Change collider bone", "bone", [&](KModelCollider& c) { c.boneIndex = bone->currentData().toInt(); });
    });
    connect(center, &Vec3Edit::edited, this, [apply](const QVector3D& v) {
        apply("Move collider", "center", [&](KModelCollider& c) { fromVec(v, c.center); });
    });
    connect(rot, &Vec3Edit::edited, this, [apply](const QVector3D& v) {
        apply("Rotate collider", "rot", [&](KModelCollider& c) { quatFromEuler(v, c.rotation); });
    });
    connect(half, &Vec3Edit::edited, this, [apply](const QVector3D& v) {
        apply("Resize collider", "half", [&](KModelCollider& c) { fromVec(v, c.halfExtents); });
    });
    connect(radius, &QDoubleSpinBox::valueChanged, this, [apply](double v) {
        apply("Resize collider", "radius", [&](KModelCollider& c) { c.radius = float(v); });
    });
    connect(halfHeight, &QDoubleSpinBox::valueChanged, this, [apply](double v) {
        apply("Resize collider", "halfHeight", [&](KModelCollider& c) { c.halfHeight = float(v); });
    });
    connect(fitBone, &QPushButton::clicked, this, [this, index] { fitColliderToBone(index); });
    connect(fitMesh, &QPushButton::clicked, this, [this, index] { fitColliderToMesh(index); });

    m_refresh = [=] {
        if (index >= int(m_doc->model().colliders.size())) return;
        const KModelCollider& c = collider();
        if (!name->hasFocus()) name->setText(QString::fromStdString(c.name));
        {
            const QSignalBlocker b1(shape), b2(bone);
            shape->setCurrentIndex(int(c.shape));
            bone->setCurrentIndex(std::max(0, bone->findData(c.boneIndex)));
        }
        center->setValue(toVec(c.center));
        rot->setValue(eulerOf(c.rotation));
        half->setValue(toVec(c.halfExtents));
        for (auto [sb, v] : {std::pair{radius, c.radius}, {halfHeight, c.halfHeight}}) {
            const QSignalBlocker b(sb);
            sb->setValue(v);
        }
        form->setRowVisible(half, c.shape == KModelColliderShape::Box);
        form->setRowVisible(radius, c.shape != KModelColliderShape::Box);
        form->setRowVisible(halfHeight, c.shape == KModelColliderShape::Capsule);
        fitBone->setEnabled(c.boneIndex >= 0);
    };
}

void AttachmentPanel::fitColliderToBone(int index)
{
    const KModelData& model = m_doc->model();
    const KModelCollider& c = model.colliders[size_t(index)];
    if (c.boneIndex < 0) return;
    const int boneNode = model.bones[size_t(c.boneIndex)].nodeIndex;

    // Child translations are already in the bone's space.
    QVector3D offset;
    for (const KModelBone& b : model.bones) {
        if (b.nodeIndex >= 0 && model.nodes[size_t(b.nodeIndex)].parentIndex == boneNode) {
            offset = toVec(model.nodes[size_t(b.nodeIndex)].translation);
            break;
        }
    }
    if (offset.lengthSquared() < 1e-10f) {
        const float len = toVec(model.nodes[size_t(boneNode)].translation).length();
        offset = QVector3D(0, len > 1e-5f ? len : 0.2f, 0);
    }
    const float len = offset.length();
    const QQuaternion q = QQuaternion::rotationTo(QVector3D(0, 1, 0), offset / len);
    m_doc->edit("Fit collider to bone", [&](KModelData& m) {
        KModelCollider& col = m.colliders[size_t(index)];
        col.shape = KModelColliderShape::Capsule;
        fromVec(offset * 0.5f, col.center);
        col.rotation[0] = q.x();
        col.rotation[1] = q.y();
        col.rotation[2] = q.z();
        col.rotation[3] = q.scalar();
        col.radius = len * 0.2f;
        col.halfHeight = std::max(0.0f, len * 0.5f - col.radius);
    });
    rebuild();
}

void AttachmentPanel::fitColliderToMesh(int index)
{
    MeshData mesh;
    if (!BuildMeshDataFromKModel(m_doc->model(), mesh)) return;
    QVector3D lo(FLT_MAX, FLT_MAX, FLT_MAX), hi(-FLT_MAX, -FLT_MAX, -FLT_MAX);
    for (const MeshVertex& v : mesh.vertices) {
        lo = QVector3D(std::min(lo.x(), v.px), std::min(lo.y(), v.py), std::min(lo.z(), v.pz));
        hi = QVector3D(std::max(hi.x(), v.px), std::max(hi.y(), v.py), std::max(hi.z(), v.pz));
    }
    if (lo.x() > hi.x()) return;
    const QVector3D half = (hi - lo) * 0.5f;
    int longest = 0;
    for (int k = 1; k < 3; ++k)
        if (half[k] > half[longest]) longest = k;

    m_doc->edit("Fit collider to mesh", [&](KModelData& m) {
        KModelCollider& col = m.colliders[size_t(index)];
        col.boneIndex = -1;
        fromVec((lo + hi) * 0.5f, col.center);
        col.rotation[0] = col.rotation[1] = col.rotation[2] = 0.0f;
        col.rotation[3] = 1.0f;
        switch (col.shape) {
        case KModelColliderShape::Box:
            fromVec(half, col.halfExtents);
            break;
        case KModelColliderShape::Sphere:
            col.radius = half[longest];
            break;
        case KModelColliderShape::Capsule: {
// capsule runs along the longest side
            col.radius = std::max(half[(longest + 1) % 3], half[(longest + 2) % 3]);
            col.halfHeight = std::max(0.0f, half[longest] - col.radius);
            QVector3D axis;
            axis[longest] = 1.0f;
            const QQuaternion q = QQuaternion::rotationTo(QVector3D(0, 1, 0), axis);
            col.rotation[0] = q.x();
            col.rotation[1] = q.y();
            col.rotation[2] = q.z();
            col.rotation[3] = q.scalar();
            break;
        }
        }
    });
    rebuild();
}

} // namespace kme
