#pragma once

#include <QWidget>

class QLabel;
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

signals:
    void prevClicked();
    void playPauseClicked();
    void nextClicked();

private:
    QToolButton* btn_prev_ = nullptr;
    QToolButton* btn_play_ = nullptr;
    QToolButton* btn_next_ = nullptr;
    visualizer::Visualizer* visualizer_ = nullptr;
    QLabel* chip_codec_ = nullptr;
    QLabel* chip_rate_ = nullptr;
};

}  // namespace auralbit::ui
