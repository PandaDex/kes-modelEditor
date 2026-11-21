#pragma once

#include <QVector3D>
#include <QWidget>
#include <functional>

class QComboBox;
class QDoubleSpinBox;
class QFormLayout;
class QLineEdit;
class QVBoxLayout;

namespace kme {

class ModelDocument;
class PreviewState;

// returns a content-relative path, or empty if cancelled
QString browseContentFile(QWidget* parent, const PreviewState* preview, const QString& title, const QString& filter,
                          const QString& current = {});

// edited() fires on Enter, focus loss, browse and clear
class PathEdit : public QWidget {
    Q_OBJECT
public:
    PathEdit(const PreviewState* preview, const QString& title, const QString& filter, QWidget* parent = nullptr);
    void setPath(const QString& path);
    QString path() const;
    void setPlaceholder(const QString& text);

signals:
    void edited(const QString& path);

private:
    QLineEdit* m_edit;
};

class Vec3Edit : public QWidget {
    Q_OBJECT
public:
    explicit Vec3Edit(double step, QWidget* parent = nullptr);
    void setValue(const QVector3D& v);
    QVector3D value() const;

signals:
    void edited(const QVector3D& v);

private:
    QDoubleSpinBox* m_spin[3];
};

class AttachmentPanel : public QWidget {
    Q_OBJECT
public:
    AttachmentPanel(ModelDocument* doc, const PreviewState* preview, QWidget* parent = nullptr);

private:
    void rebuild();
    void refreshValues();
    void buildSocket(QFormLayout* form, int index);
    void buildCollider(QFormLayout* form, int index);
    void buildNode(QVBoxLayout* lay, int index);
    void buildModel(QVBoxLayout* lay);
    void buildChild(QFormLayout* form, int index);
    QComboBox* boneCombo(int current);
    void fitColliderToBone(int index);
    void fitColliderToMesh(int index);

    ModelDocument* m_doc;
    const PreviewState* m_preview;
    QVBoxLayout* m_root;
    QWidget* m_content = nullptr;
    std::function<void()> m_refresh;
    bool m_editing = false;
};

} // namespace kme
