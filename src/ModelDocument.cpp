#include "ModelDocument.h"

#include <QFileInfo>
#include <QUndoCommand>

namespace kme {

class AttachmentCommand : public QUndoCommand {
public:
    AttachmentCommand(ModelDocument* doc, ModelAttachments before, ModelAttachments after, const QString& text, const QString& mergeKey)
        : QUndoCommand(text), m_doc(doc), m_mergeKey(mergeKey), m_before(std::move(before)), m_after(std::move(after))
    {
    }

    int id() const override { return m_mergeKey.isEmpty() ? -1 : int(qHash(m_mergeKey) & 0x7fffffff); }

    bool mergeWith(const QUndoCommand* other) override
    {
        auto* o = dynamic_cast<const AttachmentCommand*>(other);
        if (!o || m_mergeKey.isEmpty() || o->m_mergeKey != m_mergeKey) return false;
        m_after = o->m_after;
        return true;
    }

    void undo() override { m_doc->setAttachments(m_before); }
    void redo() override
    {
        if (m_first) { m_first = false; return; }
        m_doc->setAttachments(m_after);
    }

private:
    ModelDocument* m_doc;
    QString m_mergeKey;
    ModelAttachments m_before, m_after;
    bool m_first = true;
};

ModelDocument::ModelDocument(QObject* parent) : QObject(parent)
{
    connect(&m_undo, &QUndoStack::cleanChanged, this, [this](bool clean) { emit modifiedChanged(!clean); });
}

bool ModelDocument::load(const QString& path, QString* error)
{
    KModelData model;
    std::string err;
    if (!LoadKModelFile(QFileInfo(path).absoluteFilePath().toUtf8().constData(), model, &err)) {
        if (error) *error = QString::fromStdString(err);
        return false;
    }
    m_model = std::move(model);
    m_path = QFileInfo(path).absoluteFilePath();
    m_undo.clear();
    m_selection = {};
    emit modelReset();
    emit selectionChanged();
    return true;
}

bool ModelDocument::save(QString* error)
{
    if (!isOpen()) return false;
    std::string err;
    if (!SaveKModelFile(m_path.toUtf8().constData(), m_model, &err)) {
        if (error) *error = QString::fromStdString(err);
        return false;
    }
    m_undo.setClean();
    return true;
}

void ModelDocument::close()
{
    m_model = {};
    m_path.clear();
    m_undo.clear();
    m_selection = {};
    emit modelReset();
    emit selectionChanged();
}

void ModelDocument::edit(const QString& text, const std::function<void(KModelData&)>& fn, const QString& mergeKey)
{
    if (!isOpen()) return;
    ModelAttachments before = attachments();
    fn(m_model);
    m_undo.push(new AttachmentCommand(this, std::move(before), attachments(), text, mergeKey));
    emit attachmentsChanged();
}

ModelAttachments ModelDocument::attachments() const
{
    ModelAttachments a{m_model.sockets, m_model.colliders, m_model.children, m_model.defaultMaterial, m_model.materials, {}};
    for (const KModelMesh& mesh : m_model.meshes) {
        std::vector<int32_t>& ids = a.primitiveSlots.emplace_back();
        for (const KModelPrimitive& primitive : mesh.primitives) ids.push_back(primitive.materialIndex);
    }
    return a;
}

void ModelDocument::setAttachments(const ModelAttachments& a)
{
    m_model.sockets = a.sockets;
    m_model.colliders = a.colliders;
    m_model.children = a.children;
    m_model.defaultMaterial = a.defaultMaterial;
    m_model.materials = a.materials;
    for (size_t m = 0; m < m_model.meshes.size() && m < a.primitiveSlots.size(); ++m) {
        std::vector<KModelPrimitive>& primitives = m_model.meshes[m].primitives;
        for (size_t p = 0; p < primitives.size() && p < a.primitiveSlots[m].size(); ++p)
            primitives[p].materialIndex = a.primitiveSlots[m][p];
    }
    if ((m_selection.kind == Selection::Socket && m_selection.index >= int(a.sockets.size())) ||
        (m_selection.kind == Selection::Collider && m_selection.index >= int(a.colliders.size())) ||
        (m_selection.kind == Selection::Child && m_selection.index >= int(a.children.size()))) {
        m_selection = {};
        emit selectionChanged();
    }
    emit attachmentsChanged();
}

void ModelDocument::setSelection(const Selection& s)
{
    if (s == m_selection) return;
    m_selection = s;
    emit selectionChanged();
}

int ModelDocument::boneForNode(int nodeIndex) const
{
    if (nodeIndex < 0 || nodeIndex >= int(m_model.nodes.size())) return -1;
    return m_model.nodes[size_t(nodeIndex)].boneIndex;
}

QString ModelDocument::boneName(int boneIndex) const
{
    if (boneIndex < 0 || boneIndex >= int(m_model.bones.size())) return QStringLiteral("(model root)");
    return QString::fromStdString(m_model.bones[size_t(boneIndex)].name);
}

QString ModelDocument::uniqueSocketName(const QString& base) const
{
    const auto taken = [&](const QString& n) {
        for (const KModelSocket& s : m_model.sockets)
            if (QString::fromStdString(s.name) == n) return true;
        return false;
    };
    if (!taken(base)) return base;
    for (int i = 1;; ++i) {
        const QString n = QString("%1_%2").arg(base).arg(i);
        if (!taken(n)) return n;
    }
}

QString ModelDocument::uniqueChildName(const QString& base) const
{
    const auto taken = [&](const QString& n) {
        for (const KModelChild& c : m_model.children)
            if (QString::fromStdString(c.name) == n) return true;
        return false;
    };
    if (!taken(base)) return base;
    for (int i = 1;; ++i) {
        const QString n = QString("%1_%2").arg(base).arg(i);
        if (!taken(n)) return n;
    }
}

QString ModelDocument::uniqueSlotName(const QString& base) const
{
    const auto taken = [&](const QString& n) { return FindKModelMaterialSlot(m_model, n.toStdString().c_str()) >= 0; };
    if (!taken(base)) return base;
    for (int i = 1;; ++i) {
        const QString n = QString("%1_%2").arg(base).arg(i);
        if (!taken(n)) return n;
    }
}

} // namespace kme
