#pragma once

#include "kmodel.h"

#include <QObject>
#include <QString>
#include <QUndoStack>
#include <functional>

namespace kme {

struct Selection {
    enum Kind { None, Node, Socket, Collider, Child };
    Kind kind = None;
    int index = -1;

    bool operator==(const Selection& o) const { return kind == o.kind && index == o.index; }
    bool operator!=(const Selection& o) const { return !(*this == o); }
};

// The parts of a .kmodel the editor can change. Mesh and skeleton come from the import,
// except which material slot each primitive uses.
struct ModelAttachments {
    std::vector<KModelSocket> sockets;
    std::vector<KModelCollider> colliders;
    std::vector<KModelChild> children;
    std::string defaultMaterial;
    std::vector<KModelMaterial> materials;
    std::vector<std::vector<int32_t>> primitiveSlots; // [mesh][primitive]
};

class ModelDocument : public QObject {
    Q_OBJECT
public:
    explicit ModelDocument(QObject* parent = nullptr);

    bool isOpen() const { return !m_path.isEmpty(); }
    const QString& path() const { return m_path; }
    const KModelData& model() const { return m_model; }
    QUndoStack* undoStack() { return &m_undo; }
    bool isModified() const { return !m_undo.isClean(); }

    bool load(const QString& path, QString* error);
    bool save(QString* error);
    void close();

    // one undo step; repeated edits with the same mergeKey collapse (spin box drags)
    void edit(const QString& text, const std::function<void(KModelData&)>& fn, const QString& mergeKey = {});

    const Selection& selection() const { return m_selection; }
    void setSelection(const Selection& s);

    int boneForNode(int nodeIndex) const;
    QString boneName(int boneIndex) const;
    QString uniqueSocketName(const QString& base) const;
    QString uniqueChildName(const QString& base) const;
    QString uniqueSlotName(const QString& base) const;

signals:
    void modelReset();
    void attachmentsChanged();
    void selectionChanged();
    void modifiedChanged(bool modified);

private:
    friend class AttachmentCommand;
    void setAttachments(const ModelAttachments& attachments);
    ModelAttachments attachments() const;

    KModelData m_model;
    QString m_path;
    QUndoStack m_undo;
    Selection m_selection;
};

} // namespace kme
