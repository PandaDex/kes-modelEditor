#pragma once

#include <QList>
#include <QStringList>
#include <QWidget>

class QVBoxLayout;

namespace kme {

class GraphModel;

class InspectorWidget : public QWidget {
    Q_OBJECT
public:
    explicit InspectorWidget(GraphModel* model, QWidget* parent = nullptr);

    void setSelection(const QList<int>& ids);
    void setChoices(const QStringList& clips, const QStringList& bones);

signals:
    void browseClipRequested(int nodeId, const QString& paramName);

private slots:
    void scheduleRebuild();
    void rebuild();

private:
    QWidget* makeParamEditor(int nodeId, int paramIndex);
    QWidget* makeDefaultEditor(int nodeId, int inputIndex);

    GraphModel* m_model;
    QVBoxLayout* m_root;
    QWidget* m_content = nullptr;
    QList<int> m_selection;
    QStringList m_clips;
    QStringList m_bones;
    bool m_internalEdit = false;
    bool m_rebuildPending = false;
};

} // namespace kme
