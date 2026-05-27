#pragma once

#include <QWidget>

class QLabel;
class QSlider;
class QToolButton;

namespace auralbit::audio { class Player; }
namespace auralbit::visualizer { class Visualizer; }

namespace auralbit::ui {

// Transport row at the bottom of the window: prev / play-pause / next,
// live spectrum visualizer, format chips. Per-row playback progress is
// shown on the current track's row in the library tree, not here.
class TransportBar : public QWidget {
    Q_OBJECT
public:
    explicit TransportBar(QWidget* parent = nullptr);

    void setFormatChips(const QString& codec, const QString& sample_rate_khz);
    void setPlayer(audio::Player* player);

    // Reflect an externally-set volume [0, 1] without re-emitting volumeChanged
    // (used at startup and when a D-Bus client changes the volume).
    void setVolume(double volume);

signals:
    void prevClicked();
    void playPauseClicked();
    void nextClicked();
    void volumeChanged(double volume);  // [0, 1]

private:
    void updateMuteGlyph(int slider_value);

    QToolButton* btn_prev_ = nullptr;
    QToolButton* btn_play_ = nullptr;
    QToolButton* btn_next_ = nullptr;
    visualizer::Visualizer* visualizer_ = nullptr;
    QToolButton* btn_mute_ = nullptr;
    QSlider* volume_ = nullptr;
    int volume_before_mute_ = 100;  // Restored when un-muting.
    QLabel* chip_codec_ = nullptr;
    QLabel* chip_rate_ = nullptr;
};

}  // namespace auralbit::ui
