#include "InspectorWidget.h"

#include "GraphModel.h"
#include "KesStyle.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QSpinBox>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

namespace kme {

namespace {

QLabel* heading(const QString& text)
{
    auto* l = new QLabel(text);
    l->setObjectName("Heading");
    return l;
}

QLabel* note(const QString& text)
{
    auto* l = new QLabel(text);
    l->setWordWrap(true);
    l->setObjectName("Muted");
    return l;
}

QComboBox* pathCombo(const QStringList& choices, const QString& current, const QString& emptyLabel)
{
    auto* combo = new QComboBox;
    combo->addItem(emptyLabel, QString());
    for (const QString& c : choices) combo->addItem(c, c);
    if (!current.isEmpty() && combo->findData(current) < 0) combo->addItem(current + "  (missing)", current);
    combo->setCurrentIndex(std::max(0, combo->findData(current)));
    combo->setMinimumContentsLength(14);
    combo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    return combo;
}

} // namespace

InspectorWidget::InspectorWidget(GraphModel* model, QWidget* parent) : QWidget(parent), m_model(model)
{
    m_root = new QVBoxLayout(this);
    m_root->setContentsMargins(8, 8, 8, 8);
    m_root->addStretch(1);

    connect(m_model, &GraphModel::graphReset, this, &InspectorWidget::scheduleRebuild);
    connect(m_model, &GraphModel::nodeRemoved, this, &InspectorWidget::scheduleRebuild);
    connect(m_model, &GraphModel::connectionAdded, this, [this](int id) {
        const GraphConnection* c = m_model->connection(id);
        if (c && m_selection.contains(c->toNode)) scheduleRebuild();
    });
    connect(m_model, &GraphModel::connectionRemoved, this, [this](int) { scheduleRebuild(); });
    connect(m_model, &GraphModel::nodeChanged, this, [this](int id) {
        if (!m_internalEdit && m_selection.contains(id)) scheduleRebuild();
    });
    rebuild();
}

void InspectorWidget::setSelection(const QList<int>& ids)
{
    if (ids == m_selection) return;
    m_selection = ids;
    rebuild();
}

void InspectorWidget::setChoices(const QStringList& clips, const QStringList& bones)
{
    if (clips == m_clips && bones == m_bones) return;
    m_clips = clips;
    m_bones = bones;
    scheduleRebuild();
}

void InspectorWidget::scheduleRebuild()
{
    if (m_rebuildPending) return;
    m_rebuildPending = true;
    QTimer::singleShot(0, this, [this] { rebuild(); });
}

void InspectorWidget::rebuild()
{
    m_rebuildPending = false;
    if (m_content) {
        m_root->removeWidget(m_content);
        m_content->deleteLater();
        m_content = nullptr;
    }
    m_content = new QWidget;
    auto* lay = new QVBoxLayout(m_content);
    lay->setContentsMargins(0, 0, 0, 0);
    m_root->insertWidget(0, m_content);

    if (m_selection.size() != 1 || !m_model->node(m_selection.first())) {
        lay->addWidget(note(m_selection.isEmpty()
                                ? "Select a node to edit it.\n\n"
                                  "Tab or right-click: add a node\n"
                                  "Drag from a port: connect\n"
                                  "Middle-drag / Space-drag: pan\n"
                                  "Wheel: zoom   F: frame all\n\n"
                                  "Orange ports carry poses; the others carry values."
                                : QString("%1 nodes selected.").arg(m_selection.size())));
        return;
    }

    const int id = m_selection.first();
    const NodeDef* d = m_model->def(id);
    if (!d) return;

    auto* title = new QLabel(d->title);
    title->setObjectName("Title");
    lay->addWidget(title);
    lay->addWidget(note(QString("%1  ·  node #%2").arg(d->category).arg(id)));
    lay->addWidget(note(d->description));

    if (!d->params.isEmpty()) {
        lay->addWidget(heading("Parameters"));
        auto* form = new QFormLayout;
        form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
        for (int i = 0; i < d->params.size(); ++i) form->addRow(d->params[i].name, makeParamEditor(id, i));
        lay->addLayout(form);
    }

    auto* defForm = new QFormLayout;
    defForm->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    for (int i = 0; i < d->inputs.size(); ++i) {
        if (d->inputs[i].type == PortType::Pose || m_model->inputConnection(id, i)) continue;
        defForm->addRow(d->inputs[i].name, makeDefaultEditor(id, i));
    }
    if (defForm->rowCount() > 0) {
        lay->addWidget(heading("Unconnected inputs"));
        lay->addLayout(defForm);
    } else {
        delete defForm;
    }
}

QWidget* InspectorWidget::makeParamEditor(int id, int pi)
{
    const ParamDef pd = m_model->def(id)->params[pi];
    const QString name = pd.name;
    const QString mergeKey = QString("param:%1:%2").arg(id).arg(name);
    auto apply = [this, id, name, mergeKey](const QVariant& v) {
        m_internalEdit = true;
        m_model->edit(QString("Change %1").arg(name), [&] { m_model->setParam(id, name, v); }, mergeKey);
        m_internalEdit = false;
    };
    const QVariant cur = m_model->param(id, name);

    switch (pd.kind) {
    case ParamKind::Float: {
        auto* sb = new QDoubleSpinBox;
        sb->setDecimals(3);
        sb->setRange(-1e6, 1e6);
        sb->setSingleStep(0.05);
        sb->setValue(cur.toDouble());
        connect(sb, &QDoubleSpinBox::valueChanged, this, [apply](double v) { apply(v); });
        return sb;
    }
    case ParamKind::Int: {
        auto* sb = new QSpinBox;
        sb->setRange(-1000000, 1000000);
        sb->setValue(cur.toInt());
        connect(sb, &QSpinBox::valueChanged, this, [apply](int v) { apply(v); });
        return sb;
    }
    case ParamKind::Bool: {
        auto* cb = new QCheckBox;
        cb->setChecked(cur.toBool());
        connect(cb, &QCheckBox::toggled, this, [apply](bool v) { apply(v); });
        return cb;
    }
    case ParamKind::Enum: {
        auto* combo = new QComboBox;
        combo->addItems(pd.options);
        combo->setCurrentText(cur.toString());
        connect(combo, &QComboBox::currentTextChanged, this, [apply](const QString& v) { apply(v); });
        return combo;
    }
    case ParamKind::Name: {
        auto* le = new QLineEdit(cur.toString());
        le->setValidator(new QRegularExpressionValidator(QRegularExpression("[A-Za-z_][A-Za-z0-9_]*"), le));
        connect(le, &QLineEdit::editingFinished, this, [apply, le] { if (!le->text().isEmpty()) apply(le->text()); });
        return le;
    }
    case ParamKind::Clip: {
        auto* row = new QWidget;
        auto* h = new QHBoxLayout(row);
        h->setContentsMargins(0, 0, 0, 0);
        h->setSpacing(3);
        auto* combo = pathCombo(m_clips, cur.toString(), "(no clip)");
        auto* browse = new QToolButton;
        browse->setIcon(kes::style::icon("open"));
        browse->setToolTip("Pick a .kanim file");
        h->addWidget(combo, 1);
        h->addWidget(browse);
        connect(combo, &QComboBox::currentIndexChanged, this, [apply, combo](int) { apply(combo->currentData().toString()); });
        connect(browse, &QToolButton::clicked, this, [this, id, name] { emit browseClipRequested(id, name); });
        return row;
    }
    case ParamKind::Bone: {
        auto* combo = pathCombo(m_bones, cur.toString(), "(whole model)");
        connect(combo, &QComboBox::currentIndexChanged, this, [apply, combo](int) { apply(combo->currentData().toString()); });
        return combo;
    }
    }
    return new QWidget;
}

QWidget* InspectorWidget::makeDefaultEditor(int id, int ii)
{
    const PortDef port = m_model->def(id)->inputs[ii];
    const QString name = port.name;
    const QVariantList cur = m_model->inputDefault(id, ii);
    const double value = cur.isEmpty() ? port.defaultValue : cur[0].toDouble();
    const QString mergeKey = QString("default:%1:%2").arg(id).arg(name);
    auto apply = [this, id, name, mergeKey](double v) {
        m_internalEdit = true;
        m_model->edit("Change default", [&] { m_model->setInputDefault(id, name, {v}); }, mergeKey);
        m_internalEdit = false;
    };

    if (port.type == PortType::Bool) {
        auto* cb = new QCheckBox;
        cb->setChecked(value != 0.0);
        connect(cb, &QCheckBox::toggled, this, [apply](bool v) { apply(v ? 1.0 : 0.0); });
        return cb;
    }
    if (port.type == PortType::Int) {
        auto* sb = new QSpinBox;
        sb->setRange(-1000000, 1000000);
        sb->setValue(qRound(value));
        connect(sb, &QSpinBox::valueChanged, this, [apply](int v) { apply(v); });
        return sb;
    }
    auto* sb = new QDoubleSpinBox;
    sb->setDecimals(3);
    sb->setRange(-1e6, 1e6);
    sb->setSingleStep(0.05);
    sb->setValue(value);
    connect(sb, &QDoubleSpinBox::valueChanged, this, [apply](double v) { apply(v); });
    return sb;
}

} // namespace kme
