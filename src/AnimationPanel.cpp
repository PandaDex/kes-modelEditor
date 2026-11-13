#include "AnimationPanel.h"

#include "KesStyle.h"
#include "PreviewState.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QSignalBlocker>
#include <QSlider>
#include <QToolButton>
#include <QVBoxLayout>
#include <cmath>

namespace kme {

AnimationPanel::AnimationPanel(PreviewState* preview, QWidget* parent) : QWidget(parent), m_preview(preview)
{
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(8, 6, 8, 6);
    lay->setSpacing(6);

    auto* top = new QHBoxLayout;
    m_mode = new QComboBox;
    m_mode->addItems({"Bind Pose", "Clip", "Anim Graph"});
    m_mode->setToolTip("What the viewports show");
    m_clip = new QComboBox;
    m_clip->setMinimumContentsLength(28);
    m_clip->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_browse = new QToolButton;
    m_browse->setIcon(kes::style::icon("open"));
    m_browse->setToolTip("Open a .kanim clip");
    m_play = new QToolButton;
    m_play->setToolTip("Play / pause (Space in the viewport)");
    m_play->setMinimumWidth(56);
    m_restart = new QToolButton;
    m_restart->setIcon(kes::style::icon("undo"));
    m_restart->setToolTip("Back to the start (restarts the graph)");
    m_loop = new QCheckBox("Loop");
    m_loop->setChecked(true);
    m_speed = new QDoubleSpinBox;
    m_speed->setRange(-4.0, 4.0);
    m_speed->setSingleStep(0.1);
    m_speed->setValue(1.0);
    m_speed->setPrefix("Speed ");
    m_speed->setSuffix("×");

    top->addWidget(new QLabel("Preview"));
    top->addWidget(m_mode);
    top->addSpacing(8);
    top->addWidget(m_clip, 1);
    top->addWidget(m_browse);
    top->addSpacing(8);
    top->addWidget(m_restart);
    top->addWidget(m_play);
    top->addWidget(m_loop);
    top->addWidget(m_speed);
    lay->addLayout(top);

    auto* bottom = new QHBoxLayout;
    m_slider = new QSlider(Qt::Horizontal);
    m_slider->setRange(0, 1000);
    m_timeLabel = new QLabel;
    m_timeLabel->setMinimumWidth(150);
    m_timeLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    bottom->addWidget(m_slider, 1);
    bottom->addWidget(m_timeLabel);
    lay->addLayout(bottom);

    connect(m_mode, &QComboBox::currentIndexChanged, this, [this](int i) {
        if (!m_updating) m_preview->setMode(PreviewState::Mode(i));
    });
    connect(m_clip, &QComboBox::currentIndexChanged, this, [this](int) {
        if (m_updating) return;
        m_preview->setClip(m_clip->currentData().toString());
        if (m_preview->mode() == PreviewState::Mode::BindPose && !m_clip->currentData().toString().isEmpty())
            m_preview->setMode(PreviewState::Mode::Clip);
    });
    connect(m_browse, &QToolButton::clicked, this, &AnimationPanel::browseClipRequested);
    connect(m_play, &QToolButton::clicked, this, [this] {
        if (m_preview->mode() == PreviewState::Mode::BindPose && m_preview->clip()) m_preview->setMode(PreviewState::Mode::Clip);
        m_preview->setPlaying(!m_preview->isPlaying());
    });
    connect(m_restart, &QToolButton::clicked, this, [this] {
        if (m_preview->mode() == PreviewState::Mode::Graph) m_preview->restartGraph();
        else m_preview->setTime(0.0f);
    });
    connect(m_loop, &QCheckBox::toggled, this, [this](bool on) { m_preview->setLoop(on); });
    connect(m_speed, &QDoubleSpinBox::valueChanged, this, [this](double v) { m_preview->setSpeed(float(v)); });
    connect(m_slider, &QSlider::valueChanged, this, [this](int v) {
        if (m_updating) return;
        m_preview->setPlaying(false);
        m_preview->setTime(float(v) / 1000.0f * m_preview->duration());
    });
    connect(m_preview, &PreviewState::playbackChanged, this, &AnimationPanel::refresh);
    setClips({});
}

void AnimationPanel::setClips(const QStringList& contentPaths)
{
    m_updating = true;
    const QString current = m_preview->clipPath();
    m_clip->clear();
    m_clip->addItem(contentPaths.isEmpty() ? "(no .kanim clips found)" : "(choose a clip)", QString());
    for (const QString& p : contentPaths) m_clip->addItem(p, p);
    if (!current.isEmpty() && m_clip->findData(current) < 0) m_clip->addItem(current, current);
    m_clip->setCurrentIndex(std::max(0, m_clip->findData(current)));
    m_updating = false;
    refresh();
}

void AnimationPanel::selectClip(const QString& path)
{
    if (m_clip->findData(path) < 0) m_clip->addItem(path, path);
    m_clip->setCurrentIndex(m_clip->findData(path));
}

void AnimationPanel::refresh()
{
    m_updating = true;
    const bool graph = m_preview->mode() == PreviewState::Mode::Graph;
    m_mode->setCurrentIndex(int(m_preview->mode()));
    m_clip->setEnabled(!graph);
    m_browse->setEnabled(!graph);
    m_loop->setEnabled(!graph);
    m_slider->setEnabled(!graph && m_preview->clip());
    m_play->setText(m_preview->isPlaying() ? "❚❚ Pause" : "▶ Play");

    const float d = m_preview->duration();
    if (graph) {
        m_timeLabel->setText(m_preview->graphError().isEmpty() ? "live graph" : m_preview->graphError());
        m_slider->setValue(0);
    } else if (m_preview->clip()) {
        m_slider->setValue(d > 0.0f ? int(std::lround(m_preview->time() / d * 1000.0f)) : 0);
        m_timeLabel->setText(QString("%1 / %2 s   frame %3")
                                 .arg(m_preview->time(), 0, 'f', 2)
                                 .arg(d, 0, 'f', 2)
                                 .arg(int(m_preview->time() * 30.0f)));
    } else {
        m_slider->setValue(0);
        m_timeLabel->setText("bind pose");
    }
    m_updating = false;
}

} // namespace kme
