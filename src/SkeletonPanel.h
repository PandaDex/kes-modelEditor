#pragma once

#include <QWidget>

class QLineEdit;
class QTreeWidget;
class QTreeWidgetItem;

namespace kme {

class ModelDocument;

class SkeletonPanel : public QWidget {
    Q_OBJECT
public:
    explicit SkeletonPanel(ModelDocument* doc, QWidget* parent = nullptr);

    void addSocket();
    void addCollider();
    void deleteSelected();

private:
    void rebuild();
    void syncSelection();
    void applyFilter();
    void onCurrentChanged(QTreeWidgetItem* item);
    void showContextMenu(const QPoint& pos);
    int selectedBone() const;

    ModelDocument* m_doc;
    QTreeWidget* m_tree;
    QLineEdit* m_filter;
    bool m_syncing = false;
};

} // namespace kme
