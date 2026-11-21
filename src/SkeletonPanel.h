#pragma once

#include <QWidget>

class QLineEdit;
class QTreeWidget;
class QTreeWidgetItem;

namespace kme {

class ModelDocument;
class PreviewState;

class SkeletonPanel : public QWidget {
    Q_OBJECT
public:
    SkeletonPanel(ModelDocument* doc, const PreviewState* preview, QWidget* parent = nullptr);

    void addSocket();
    void addCollider();
    void addChildModel();
    void deleteSelected();

private:
    void rebuild();
    void syncSelection();
    void applyFilter();
    void onCurrentChanged(QTreeWidgetItem* item);
    void showContextMenu(const QPoint& pos);
    int selectedBone() const;

    ModelDocument* m_doc;
    const PreviewState* m_preview;
    QTreeWidget* m_tree;
    QLineEdit* m_filter;
    bool m_syncing = false;
};

} // namespace kme
