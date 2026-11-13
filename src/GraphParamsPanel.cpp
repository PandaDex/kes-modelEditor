#include "GraphParamsPanel.h"

#include "PreviewState.h"

#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>
#include <QVBoxLayout>
#include <cmath>

namespace kme {

GraphParamsPanel::GraphParamsPanel(PreviewState* preview, QWidget* parent) : QWidget(parent), m_preview(preview)
{
    m_root = new QVBoxLayout(this);
    m_root->setContentsMargins(8, 8, 8, 8);
    m_root->addStretch(1);
    connect(m_preview, &PreviewState::graphRebuilt, this, &GraphParamsPanel::rebuild);
    rebuild();
}

void GraphParamsPanel::rebuild()
{
    if (m_content) {
        m_root->removeWidget(m_content);
        m_content->deleteLater();
    }
    m_content = new QWidget;
    auto* lay = new QVBoxLayout(m_content);
    lay->setContentsMargins(0, 0, 0, 0);
    m_root->insertWidget(0, m_content);

    AnimBlendTreeInstance& instance = m_preview->graphInstance();
    if (!m_preview->graphError().isEmpty()) {
        auto* err = new QLabel(m_preview->graphError());
        err->setObjectName("Warning");
        err->setWordWrap(true);
        lay->addWidget(err);
    }
    if (instance.Parameters().empty()) {
        auto* l = new QLabel("Add Float, Bool or Int Parameter nodes to drive the graph. Their values can be tried out here; "
                             "in the game, code sets them with SetAnimFloat / SetAnimBool / SetAnimInt.");
        l->setObjectName("Muted");
        l->setWordWrap(true);
        lay->addWidget(l);
        return;
    }

    auto* form = new QFormLayout;
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    for (const AnimBlendTreeParameter& p : instance.Parameters()) {
        const std::string name = p.name;
        const float value = instance.GetValue(name);
        QWidget* editor = nullptr;
        if (p.type == AnimPortType::Bool) {
            auto* cb = new QCheckBox;
            cb->setChecked(value > 0.5f);
            connect(cb, &QCheckBox::toggled, this, [this, name](bool v) { m_preview->graphInstance().SetBool(name, v); });
            editor = cb;
        } else if (p.type == AnimPortType::Int) {
            auto* sb = new QSpinBox;
            sb->setRange(-1000, 1000);
            sb->setValue(int(std::lround(value)));
            connect(sb, &QSpinBox::valueChanged, this, [this, name](int v) { m_preview->graphInstance().SetInt(name, v); });
            editor = sb;
        } else {
            auto* row = new QWidget;
            auto* h = new QHBoxLayout(row);
            h->setContentsMargins(0, 0, 0, 0);
            auto* slider = new QSlider(Qt::Horizontal);
            const float range = std::max({1.0f, std::fabs(p.defaultValue) * 2.0f, std::fabs(value) * 1.5f});
            slider->setRange(int(-range * 100), int(range * 100));
            slider->setValue(int(value * 100));
            auto* sb = new QDoubleSpinBox;
            sb->setRange(-1e5, 1e5);
            sb->setDecimals(2);
            sb->setSingleStep(0.1);
            sb->setValue(value);
            h->addWidget(slider, 1);
            h->addWidget(sb);
            connect(slider, &QSlider::valueChanged, this, [this, name, sb](int v) {
                const QSignalBlocker b(sb);
                sb->setValue(v / 100.0);
                m_preview->graphInstance().SetFloat(name, float(v) / 100.0f);
            });
            connect(sb, &QDoubleSpinBox::valueChanged, this, [this, name, slider](double v) {
                const QSignalBlocker b(slider);
                slider->setValue(int(std::lround(v * 100)));
                m_preview->graphInstance().SetFloat(name, float(v));
            });
            editor = row;
        }
        form->addRow(QString::fromStdString(name), editor);
    }
    lay->addLayout(form);

    auto* reset = new QPushButton("Reset to Defaults");
    connect(reset, &QPushButton::clicked, this, [this] {
        m_preview->restartGraph();
        rebuild();
    });
    lay->addWidget(reset);
}

} // namespace kme
