#pragma once

#include <QStringList>
#include <QWidget>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QSlider;
class QToolButton;

namespace kme {

class PreviewState;

class AnimationPanel : public QWidget {
    Q_OBJECT
public:
    explicit AnimationPanel(PreviewState* preview, QWidget* parent = nullptr);

    void setClips(const QStringList& contentPaths);
    void selectClip(const QString& path);

signals:
    void browseClipRequested();

private:
    void refresh();

    PreviewState* m_preview;
    QComboBox* m_mode;
    QComboBox* m_clip;
    QToolButton* m_browse;
    QToolButton* m_play;
    QToolButton* m_restart;
    QCheckBox* m_loop;
    QDoubleSpinBox* m_speed;
    QSlider* m_slider;
    QLabel* m_timeLabel;
    bool m_updating = false;
};

} // namespace kme
