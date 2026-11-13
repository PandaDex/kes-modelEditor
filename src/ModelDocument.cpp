#include "ModelDocument.h"

#include <QFileInfo>
#include <QUndoCommand>

namespace kme {

class AttachmentCommand : public QUndoCommand {
public:
    AttachmentCommand(ModelDocument* doc, KModelData before, KModelData after, const QString& text, const QString& mergeKey)
        : QUndoCommand(text), m_doc(doc), m_mergeKey(mergeKey)
    {
        m_beforeSockets = std::move(before.sockets);
        m_beforeColliders = std::move(before.colliders);
        m_afterSockets = std::move(after.sockets);
        m_afterColliders = std::move(after.colliders);
    }

    int id() const override { return m_mergeKey.isEmpty() ? -1 : int(qHash(m_mergeKey) & 0x7fffffff); }

    bool mergeWith(const QUndoCommand* other) override
    {
        auto* o = dynamic_cast<const AttachmentCommand*>(other);
        if (!o || m_mergeKey.isEmpty() || o->m_mergeKey != m_mergeKey) return false;
        m_afterSockets = o->m_afterSockets;
        m_afterColliders = o->m_afterColliders;
        return true;
    }

    void undo() override { m_doc->setAttachments(m_beforeSockets, m_beforeColliders); }
    void redo() override
    {
        if (m_first) { m_first = false; return; }
        m_doc->setAttachments(m_afterSockets, m_afterColliders);
    }

private:
    ModelDocument* m_doc;
    QString m_mergeKey;
    std::vector<KModelSocket> m_beforeSockets, m_afterSockets;
    std::vector<KModelCollider> m_beforeColliders, m_afterColliders;
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
    KModelData before;
    before.sockets = m_model.sockets;
    before.colliders = m_model.colliders;
    fn(m_model);
    KModelData after;
    after.sockets = m_model.sockets;
    after.colliders = m_model.colliders;
    m_undo.push(new AttachmentCommand(this, std::move(before), std::move(after), text, mergeKey));
    emit attachmentsChanged();
}

void ModelDocument::setAttachments(const std::vector<KModelSocket>& sockets, const std::vector<KModelCollider>& colliders)
{
    m_model.sockets = sockets;
    m_model.colliders = colliders;
    if ((m_selection.kind == Selection::Socket && m_selection.index >= int(sockets.size())) ||
        (m_selection.kind == Selection::Collider && m_selection.index >= int(colliders.size()))) {
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

} // namespace kme
