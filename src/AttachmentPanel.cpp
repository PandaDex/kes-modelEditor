#include "AttachmentPanel.h"

#include "KesProject.h"
#include "KesStyle.h"
#include "ModelDocument.h"
#include "PreviewState.h"

#include <QComboBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QSettings>
#include <QToolButton>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QQuaternion>
#include <QSignalBlocker>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>
#include <cfloat>

namespace kme
{

    namespace
    {

        QVector3D toVec(const float v[3])
        {
            return { v[0], v[1], v[2] };
        }
        void fromVec(const QVector3D& v, float out[3])
        {
            out[0] = v.x();
            out[1] = v.y();
            out[2] = v.z();
        }

        // stored as x, y, z, w but QQuaternion takes w first
        QVector3D eulerOf(const float q[4])
        {
            return QQuaternion(q[3], q[0], q[1], q[2]).toEulerAngles();
        }
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

    QString browseContentFile(QWidget* parent, const PreviewState* preview, const QString& title, const QString& filter,
        const QString& current)
    {
        const QString root = preview->contentRoot();
        QString start = current.isEmpty() ? QString() : QFileInfo(preview->resolvePath(current)).absolutePath();
        if (start.isEmpty() || !QFileInfo(start).isDir()) start = QSettings().value("lastContentDir", root).toString();
        const QString file = QFileDialog::getOpenFileName(parent, title, start, filter);
        if (file.isEmpty()) return {};
        QSettings().setValue("lastContentDir", QFileInfo(file).absolutePath());
        const QString rel = contentRelativePath(root, file);
        return rel.isEmpty() ? QDir::fromNativeSeparators(file) : rel;
    }

    PathEdit::PathEdit(const PreviewState* preview, const QString& title, const QString& filter, QWidget* parent) : QWidget(parent)
    {
        auto* h = new QHBoxLayout(this);
        h->setContentsMargins(0, 0, 0, 0);
        h->setSpacing(3);
        m_edit = new QLineEdit;
        m_edit->setClearButtonEnabled(true);
        auto* browse = new QToolButton;
        browse->setText("…");
        browse->setToolTip("Browse");
        h->addWidget(m_edit, 1);
        h->addWidget(browse);
        connect(m_edit, &QLineEdit::editingFinished, this, [this]
            { emit edited(path()); });
        // the clear button doesn't finish editing
        connect(m_edit, &QLineEdit::textChanged, this, [this](const QString& t)
            {
        if (t.isEmpty() && !m_edit->hasFocus()) emit edited({}); });
        connect(browse, &QToolButton::clicked, this, [this, preview, title, filter]
            {
        const QString picked = browseContentFile(this, preview, title, filter, path());
        if (picked.isEmpty()) return;
        m_edit->setText(picked);
        emit edited(picked); });
    }

    void PathEdit::setPath(const QString& path)
    {
        if (m_edit->hasFocus()) return;
        const QSignalBlocker block(m_edit);
        m_edit->setText(path);
    }

    QString PathEdit::path() const
    {
        return QDir::fromNativeSeparators(m_edit->text().trimmed());
    }

    void PathEdit::setPlaceholder(const QString& text)
    {
        m_edit->setPlaceholderText(text);
    }

    Vec3Edit::Vec3Edit(double step, QWidget* parent) : QWidget(parent)
    {
        auto* h = new QHBoxLayout(this);
        h->setContentsMargins(0, 0, 0, 0);
        h->setSpacing(3);
        const char* axes[3] = { "X", "Y", "Z" };
        for (int i = 0; i < 3; ++i)
        {
            m_spin[i] = new QDoubleSpinBox;
            m_spin[i]->setDecimals(4);
            m_spin[i]->setRange(-1e5, 1e5);
            m_spin[i]->setSingleStep(step);
            m_spin[i]->setPrefix(QString("%1 ").arg(axes[i]));
            m_spin[i]->setMinimumWidth(70);
            m_spin[i]->setKeyboardTracking(false);
            h->addWidget(m_spin[i], 1);
            connect(m_spin[i], &QDoubleSpinBox::valueChanged, this, [this](double)
                { emit edited(value()); });
        }
    }

    void Vec3Edit::setValue(const QVector3D& v)
    {
        for (int i = 0; i < 3; ++i)
        {
            if (m_spin[i]->hasFocus() && qFuzzyCompare(float(m_spin[i]->value()) + 1.0f, v[i] + 1.0f)) continue;
            const QSignalBlocker block(m_spin[i]);
            m_spin[i]->setValue(v[i]);
        }
    }

    QVector3D Vec3Edit::value() const
    {
        return { float(m_spin[0]->value()), float(m_spin[1]->value()), float(m_spin[2]->value()) };
    }

    AttachmentPanel::AttachmentPanel(ModelDocument* doc, const PreviewState* preview, QWidget* parent)
        : QWidget(parent), m_doc(doc), m_preview(preview)
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
        for (size_t b = 0; b < model.bones.size(); ++b)
            combo->addItem(QString::fromStdString(model.bones[b].name), int(b));
        combo->setCurrentIndex(std::max(0, combo->findData(current)));
        combo->setMaxVisibleItems(24);
        return combo;
    }

    void AttachmentPanel::rebuild()
    {
        m_refresh = nullptr;
        if (m_content)
        {
            m_root->removeWidget(m_content);
            m_content->deleteLater();
        }
        m_content = new QWidget;
        auto* lay = new QVBoxLayout(m_content);
        lay->setContentsMargins(0, 0, 0, 0);
        m_root->insertWidget(0, m_content);

        const Selection sel = m_doc->selection();
        if (!m_doc->isOpen())
        {
            lay->addWidget(note("No model open."));
            return;
        }
        if (sel.kind == Selection::Node)
        {
            buildNode(lay, sel.index);
            return;
        }
        if (sel.kind != Selection::Socket && sel.kind != Selection::Collider && sel.kind != Selection::Child)
        {
            buildModel(lay);
            return;
        }

        auto* title = new QLabel(sel.kind == Selection::Socket ? "Socket" : sel.kind == Selection::Collider ? "Collider"
                                                                                                            : "Child Model");
        title->setObjectName("Title");
        lay->addWidget(title);
        auto* form = new QFormLayout;
        form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
        lay->addLayout(form);
        if (sel.kind == Selection::Socket)
            buildSocket(form, sel.index);
        else if (sel.kind == Selection::Collider)
            buildCollider(form, sel.index);
        else
            buildChild(form, sel.index);
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

    void AttachmentPanel::buildModel(QVBoxLayout* lay)
    {
        auto* title = new QLabel("Model");
        title->setObjectName("Title");
        lay->addWidget(title);
        auto* form = new QFormLayout;
        form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
        lay->addLayout(form);

        auto* material = new PathEdit(m_preview, "Default Material", "Materials (*.mat)");
        material->setPlaceholder("(none: white)");
        form->addRow("Default material", material);
        lay->addWidget(note("Used wherever this model is placed without its own Material, for every part whose slot has "
                            "no material. A Material set on the entity (Forge's Material field) replaces it; child models "
                            "keep theirs."));
        connect(material, &PathEdit::edited, this, [this](const QString& path)
            {
        if (path.toStdString() == m_doc->model().defaultMaterial) return;
        m_editing = true;
        m_doc->edit("Set default material", [&](KModelData& m) { m.defaultMaterial = path.toStdString(); });
        m_editing = false; });

        std::vector<std::function<void()>> refreshers;
        refreshers.push_back([=, this]
            { material->setPath(QString::fromStdString(m_doc->model().defaultMaterial)); });
        buildMaterialSlots(lay, refreshers);
        const size_t builtSlots = m_doc->model().materials.size();
        m_refresh = [this, refreshers, builtSlots]
        {
            // adding or removing a slot (or undoing it) changes the rows, so build them again
            if (m_doc->model().materials.size() != builtSlots)
            {
                QTimer::singleShot(0, this, &AttachmentPanel::rebuild);
                return;
            }
            for (const auto& refresh : refreshers)
                refresh();
        };
        m_refresh();

        lay->addSpacing(12);
        lay->addWidget(note("Select a bone, socket, collider or child model in the Skeleton panel or click one in the viewport.\n\n"
                            "Left-drag: orbit   Shift/middle-drag: pan\nWheel or right-drag: zoom   F: frame"));
    }

    // Slots (name + material) and, per mesh part, which slot it draws with.
    void AttachmentPanel::buildMaterialSlots(QVBoxLayout* lay, std::vector<std::function<void()>>& refreshers)
    {
        lay->addSpacing(10);
        auto* header = new QHBoxLayout;
        auto* title = new QLabel("Material slots");
        title->setObjectName("Title");
        auto* add = new QPushButton("+ Slot");
        add->setToolTip("Add a material slot, then pick it for a mesh part below");
        header->addWidget(title);
        header->addStretch(1);
        header->addWidget(add);
        lay->addLayout(header);
        connect(add, &QPushButton::clicked, this, [this]
            {
        const std::string name = m_doc->uniqueSlotName("Slot").toStdString();
        m_doc->edit("Add material slot", [&](KModelData& m) { m.materials.push_back({name, {}}); }); });

        const auto slotList = [this]() -> const std::vector<KModelMaterial>&
        { return m_doc->model().materials; };
        auto* form = new QFormLayout;
        form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
        lay->addLayout(form);
        for (int index = 0; index < int(slotList().size()); ++index)
        {
            auto* row = new QWidget;
            auto* h = new QHBoxLayout(row);
            h->setContentsMargins(0, 0, 0, 0);
            auto* name = new QLineEdit;
            name->setFixedWidth(110);
            name->setToolTip("Slot name. Forge overrides a slot with an entity key Material.<name>, so renaming "
                             "drops those overrides.");
            auto* path = new PathEdit(m_preview, "Slot Material", "Materials (*.mat)");
            path->setPlaceholder("(Material / default)");
            auto* remove = new QToolButton;
            remove->setText("x");
            remove->setToolTip("Remove the slot; its parts fall back to the Material / default material");
            h->addWidget(name);
            h->addWidget(path, 1);
            h->addWidget(remove);
            form->addRow(QString::number(index), row);

            connect(name, &QLineEdit::editingFinished, this, [this, name, index]
                {
            const QString n = name->text().trimmed();
            if (n.isEmpty() || n.toStdString() == m_doc->model().materials[size_t(index)].name) {
                name->setText(QString::fromStdString(m_doc->model().materials[size_t(index)].name));
                return;
            }
            const std::string unique = m_doc->uniqueSlotName(n).toStdString();
            m_doc->edit("Rename material slot", [&](KModelData& m) { m.materials[size_t(index)].name = unique; }); });
            connect(path, &PathEdit::edited, this, [this, index](const QString& p)
                {
            if (p.toStdString() == m_doc->model().materials[size_t(index)].materialPath) return;
            m_editing = true;
            m_doc->edit("Set slot material", [&](KModelData& m) { m.materials[size_t(index)].materialPath = p.toStdString(); });
            m_editing = false; });
            connect(remove, &QToolButton::clicked, this, [this, index]
                { m_doc->edit("Remove material slot", [&](KModelData& m)
                      {
                m.materials.erase(m.materials.begin() + index);
                for (KModelMesh& mesh : m.meshes) {
                    for (KModelPrimitive& primitive : mesh.primitives) {
                        if (primitive.materialIndex == index) primitive.materialIndex = -1;
                        else if (primitive.materialIndex > index) --primitive.materialIndex;
                    }
                } }); });
            refreshers.push_back([this, name, path, index]
                {
            const KModelMaterial& slot = m_doc->model().materials[size_t(index)];
            if (!name->hasFocus()) name->setText(QString::fromStdString(slot.name));
            path->setPath(QString::fromStdString(slot.materialPath)); });
        }
        if (slotList().empty()) lay->addWidget(note("No slots: the whole model uses one material."));

        const KModelData& model = m_doc->model();
        bool anyPart = false;
        auto* parts = new QFormLayout;
        parts->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
        for (int m = 0; m < int(model.meshes.size()); ++m)
        {
            const KModelMesh& mesh = model.meshes[size_t(m)];
            for (int p = 0; p < int(mesh.primitives.size()); ++p)
            {
                anyPart = true;
                QString label = QString::fromStdString(mesh.name);
                if (label.isEmpty()) label = QString("Mesh %1").arg(m);
                if (mesh.primitives.size() > 1) label += QString(" / %1").arg(p);
                auto* combo = new QComboBox;
                combo->setToolTip(QString("%1 triangles").arg(mesh.primitives[size_t(p)].indexCount / 3));
                parts->addRow(label, combo);
                const auto fill = [this, combo, m, p]
                {
                    const QSignalBlocker block(combo);
                    const KModelData& md = m_doc->model();
                    combo->clear();
                    combo->addItem("(no slot)", -1);
                    for (int s = 0; s < int(md.materials.size()); ++s)
                        combo->addItem(QString::fromStdString(md.materials[size_t(s)].name), s);
                    combo->setCurrentIndex(std::max(0, combo->findData(md.meshes[size_t(m)].primitives[size_t(p)].materialIndex)));
                };
                fill();
                refreshers.push_back(fill);
                connect(combo, &QComboBox::activated, this, [this, combo, m, p](int item)
                    {
                const int slot = combo->itemData(item).toInt();
                if (slot == m_doc->model().meshes[size_t(m)].primitives[size_t(p)].materialIndex) return;
                m_doc->edit("Assign material slot",
                            [&](KModelData& md) { md.meshes[size_t(m)].primitives[size_t(p)].materialIndex = slot; }); });
            }
        }
        if (anyPart)
        {
            lay->addSpacing(6);
            auto* partsTitle = new QLabel("Mesh parts");
            partsTitle->setObjectName("Title");
            lay->addWidget(partsTitle);
            lay->addLayout(parts);
            lay->addWidget(note("Each part draws with its slot's material. A part with no slot, or a slot with no material, "
                                "uses the entity Material, then the default material. Forge can override one slot per entity "
                                "(Material.<slot>)."));
        }
        else
        {
            delete parts;
        }
    }

    void AttachmentPanel::buildChild(QFormLayout* form, int index)
    {
        const auto child = [this, index]() -> const KModelChild&
        { return m_doc->model().children[size_t(index)]; };
        auto* name = new QLineEdit;
        auto* modelPath = new PathEdit(m_preview, "Child Model", "KES models (*.kmodel)");
        auto* material = new PathEdit(m_preview, "Child Material", "Materials (*.mat)");
        auto* animation = new PathEdit(m_preview, "Child Animation", "Animations (*.kanim *.ag)");
        animation->setPlaceholder("(none: bind pose)");
        auto* bone = boneCombo(child().boneIndex);
        auto* pos = new Vec3Edit(0.01);
        auto* rot = new Vec3Edit(1.0);
        auto* scale = new Vec3Edit(0.05);
        form->addRow("Name", name);
        form->addRow("Model", modelPath);
        form->addRow("Material", material);
        form->addRow("Animation", animation);
        form->addRow("Bone", bone);
        form->addRow("Position", pos);
        form->addRow("Rotation", rot);
        form->addRow("Scale", scale);
        form->addRow(note("Drawn attached to the bone in the game and in Forge. An empty Material uses the child model's "
                          "default material. The Animation (a .kanim clip or a .ag) plays on its own; game code "
                          "drives it with SetChildAnimation / SetChildAnim* by this name. Press Space to preview it."));

        const auto apply = [this, index](const QString& text, const QString& key, const std::function<void(KModelChild&)>& fn)
        {
            m_editing = true;
            m_doc->edit(text, [&](KModelData& m)
                { fn(m.children[size_t(index)]); }, QString("child:%1:%2").arg(index).arg(key));
            m_editing = false;
        };
        const auto showChildDefault = [this, child, material]
        {
            KModelData childModel;
            const QString file = m_preview->resolvePath(QString::fromStdString(child().modelPath));
            if (LoadKModelFile(file.toUtf8().constData(), childModel) && !childModel.defaultMaterial.empty())
                material->setPlaceholder(QString("(model default: %1)").arg(QString::fromStdString(childModel.defaultMaterial)));
            else
                material->setPlaceholder("(model default: none)");
        };
        connect(name, &QLineEdit::editingFinished, this, [this, apply, name, index]
            {
        QString n = name->text().trimmed();
        if (n.isEmpty() || n.toStdString() == m_doc->model().children[size_t(index)].name) return;
        n = m_doc->uniqueChildName(n);
        apply("Rename child model", "name", [&](KModelChild& c) { c.name = n.toStdString(); }); });
        connect(modelPath, &PathEdit::edited, this, [this, apply, index, showChildDefault](const QString& path)
            {
        if (path.isEmpty() || path.toStdString() == m_doc->model().children[size_t(index)].modelPath) return;
        apply("Change child model", "model", [&](KModelChild& c) { c.modelPath = path.toStdString(); });
        showChildDefault(); });
        connect(material, &PathEdit::edited, this, [this, apply, index](const QString& path)
            {
        if (path.toStdString() == m_doc->model().children[size_t(index)].materialPath) return;
        apply("Change child material", "material", [&](KModelChild& c) { c.materialPath = path.toStdString(); }); });
        connect(animation, &PathEdit::edited, this, [this, apply, index](const QString& path)
            {
        if (path.toStdString() == m_doc->model().children[size_t(index)].animationPath) return;
        apply("Change child animation", "animation", [&](KModelChild& c) { c.animationPath = path.toStdString(); }); });
        connect(bone, &QComboBox::currentIndexChanged, this, [apply, bone](int)
            { apply("Change child bone", "bone", [&](KModelChild& c)
                  { c.boneIndex = bone->currentData().toInt(); }); });
        connect(pos, &Vec3Edit::edited, this, [apply](const QVector3D& v)
            { apply("Move child model", "pos", [&](KModelChild& c)
                  { fromVec(v, c.translation); }); });
        connect(rot, &Vec3Edit::edited, this, [apply](const QVector3D& v)
            { apply("Rotate child model", "rot", [&](KModelChild& c)
                  { quatFromEuler(v, c.rotation); }); });
        connect(scale, &Vec3Edit::edited, this, [apply](const QVector3D& v)
            { apply("Scale child model", "scale", [&](KModelChild& c)
                  { fromVec(v, c.scale); }); });

        m_refresh = [=, this]
        {
            if (index >= int(m_doc->model().children.size())) return;
            const KModelChild& c = child();
            if (!name->hasFocus()) name->setText(QString::fromStdString(c.name));
            modelPath->setPath(QString::fromStdString(c.modelPath));
            material->setPath(QString::fromStdString(c.materialPath));
            animation->setPath(QString::fromStdString(c.animationPath));
            {
                const QSignalBlocker b(bone);
                bone->setCurrentIndex(std::max(0, bone->findData(c.boneIndex)));
            }
            pos->setValue(toVec(c.translation));
            rot->setValue(eulerOf(c.rotation));
            scale->setValue(toVec(c.scale));
        };
        showChildDefault();
    }

    void AttachmentPanel::buildSocket(QFormLayout* form, int index)
    {
        const auto socket = [this, index]() -> const KModelSocket&
        { return m_doc->model().sockets[size_t(index)]; };
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

        const auto apply = [this, index](const QString& text, const QString& key, const std::function<void(KModelSocket&)>& fn)
        {
            m_editing = true;
            m_doc->edit(text, [&](KModelData& m)
                { fn(m.sockets[size_t(index)]); }, QString("socket:%1:%2").arg(index).arg(key));
            m_editing = false;
        };
        connect(name, &QLineEdit::editingFinished, this, [this, apply, name, index]
            {
        QString n = name->text().trimmed();
        if (n.isEmpty() || n.toStdString() == m_doc->model().sockets[size_t(index)].name) return;
        n = m_doc->uniqueSocketName(n);
        apply("Rename socket", "name", [&](KModelSocket& s) { s.name = n.toStdString(); }); });
        connect(bone, &QComboBox::currentIndexChanged, this, [apply, bone](int)
            { apply("Change socket bone", "bone", [&](KModelSocket& s)
                  { s.boneIndex = bone->currentData().toInt(); }); });
        connect(pos, &Vec3Edit::edited, this, [apply](const QVector3D& v)
            { apply("Move socket", "pos", [&](KModelSocket& s)
                  { fromVec(v, s.translation); }); });
        connect(rot, &Vec3Edit::edited, this, [apply](const QVector3D& v)
            { apply("Rotate socket", "rot", [&](KModelSocket& s)
                  { quatFromEuler(v, s.rotation); }); });
        connect(scale, &Vec3Edit::edited, this, [apply](const QVector3D& v)
            { apply("Scale socket", "scale", [&](KModelSocket& s)
                  { fromVec(v, s.scale); }); });

        m_refresh = [=]
        {
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
        const auto collider = [this, index]() -> const KModelCollider&
        { return m_doc->model().colliders[size_t(index)]; };
        auto* name = new QLineEdit;
        auto* shape = new QComboBox;
        shape->addItems({ "Box", "Sphere", "Capsule" });
        auto* bone = boneCombo(collider().boneIndex);
        auto* center = new Vec3Edit(0.01);
        auto* rot = new Vec3Edit(1.0);
        auto* half = new Vec3Edit(0.01);
        auto* radius = new QDoubleSpinBox;
        auto* halfHeight = new QDoubleSpinBox;
        for (QDoubleSpinBox* sb : { radius, halfHeight })
        {
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

        const auto apply = [this, index](const QString& text, const QString& key, const std::function<void(KModelCollider&)>& fn)
        {
            m_editing = true;
            m_doc->edit(text, [&](KModelData& m)
                { fn(m.colliders[size_t(index)]); }, QString("collider:%1:%2").arg(index).arg(key));
            m_editing = false;
        };
        connect(name, &QLineEdit::editingFinished, this, [this, apply, name, index]
            {
        const QString n = name->text().trimmed();
        if (n.toStdString() == m_doc->model().colliders[size_t(index)].name) return;
        apply("Rename collider", "name", [&](KModelCollider& c) { c.name = n.toStdString(); }); });
        connect(shape, &QComboBox::currentIndexChanged, this, [this, apply](int i)
            {
        apply("Change collider shape", "shape", [&](KModelCollider& c) { c.shape = KModelColliderShape(i); });
        if (m_refresh) m_refresh(); });
        connect(bone, &QComboBox::currentIndexChanged, this, [apply, bone](int)
            { apply("Change collider bone", "bone", [&](KModelCollider& c)
                  { c.boneIndex = bone->currentData().toInt(); }); });
        connect(center, &Vec3Edit::edited, this, [apply](const QVector3D& v)
            { apply("Move collider", "center", [&](KModelCollider& c)
                  { fromVec(v, c.center); }); });
        connect(rot, &Vec3Edit::edited, this, [apply](const QVector3D& v)
            { apply("Rotate collider", "rot", [&](KModelCollider& c)
                  { quatFromEuler(v, c.rotation); }); });
        connect(half, &Vec3Edit::edited, this, [apply](const QVector3D& v)
            { apply("Resize collider", "half", [&](KModelCollider& c)
                  { fromVec(v, c.halfExtents); }); });
        connect(radius, &QDoubleSpinBox::valueChanged, this, [apply](double v)
            { apply("Resize collider", "radius", [&](KModelCollider& c)
                  { c.radius = float(v); }); });
        connect(halfHeight, &QDoubleSpinBox::valueChanged, this, [apply](double v)
            { apply("Resize collider", "halfHeight", [&](KModelCollider& c)
                  { c.halfHeight = float(v); }); });
        connect(fitBone, &QPushButton::clicked, this, [this, index]
            { fitColliderToBone(index); });
        connect(fitMesh, &QPushButton::clicked, this, [this, index]
            { fitColliderToMesh(index); });

        m_refresh = [=]
        {
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
            for (auto [sb, v] : { std::pair{ radius, c.radius }, { halfHeight, c.halfHeight } })
            {
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
        for (const KModelBone& b : model.bones)
        {
            if (b.nodeIndex >= 0 && model.nodes[size_t(b.nodeIndex)].parentIndex == boneNode)
            {
                offset = toVec(model.nodes[size_t(b.nodeIndex)].translation);
                break;
            }
        }
        if (offset.lengthSquared() < 1e-10f)
        {
            const float len = toVec(model.nodes[size_t(boneNode)].translation).length();
            offset = QVector3D(0, len > 1e-5f ? len : 0.2f, 0);
        }
        const float len = offset.length();
        const QQuaternion q = QQuaternion::rotationTo(QVector3D(0, 1, 0), offset / len);
        m_doc->edit("Fit collider to bone", [&](KModelData& m)
            {
        KModelCollider& col = m.colliders[size_t(index)];
        col.shape = KModelColliderShape::Capsule;
        fromVec(offset * 0.5f, col.center);
        col.rotation[0] = q.x();
        col.rotation[1] = q.y();
        col.rotation[2] = q.z();
        col.rotation[3] = q.scalar();
        col.radius = len * 0.2f;
        col.halfHeight = std::max(0.0f, len * 0.5f - col.radius); });
        rebuild();
    }

    void AttachmentPanel::fitColliderToMesh(int index)
    {
        MeshData mesh;
        if (!BuildMeshDataFromKModel(m_doc->model(), mesh)) return;
        QVector3D lo(FLT_MAX, FLT_MAX, FLT_MAX), hi(-FLT_MAX, -FLT_MAX, -FLT_MAX);
        for (const MeshVertex& v : mesh.vertices)
        {
            lo = QVector3D(std::min(lo.x(), v.px), std::min(lo.y(), v.py), std::min(lo.z(), v.pz));
            hi = QVector3D(std::max(hi.x(), v.px), std::max(hi.y(), v.py), std::max(hi.z(), v.pz));
        }
        if (lo.x() > hi.x()) return;
        const QVector3D half = (hi - lo) * 0.5f;
        int longest = 0;
        for (int k = 1; k < 3; ++k)
            if (half[k] > half[longest]) longest = k;

        m_doc->edit("Fit collider to mesh", [&](KModelData& m)
            {
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
        } });
        rebuild();
    }

} // namespace kme
