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
    explicit AttachmentPanel(ModelDocument* doc, QWidget* parent = nullptr);

private:
    void rebuild();
    void refreshValues();
    void buildSocket(QFormLayout* form, int index);
    void buildCollider(QFormLayout* form, int index);
    void buildNode(QVBoxLayout* lay, int index);
    QComboBox* boneCombo(int current);
    void fitColliderToBone(int index);
    void fitColliderToMesh(int index);

    ModelDocument* m_doc;
    QVBoxLayout* m_root;
    QWidget* m_content = nullptr;
    std::function<void()> m_refresh;
    bool m_editing = false;
};

} // namespace kme
