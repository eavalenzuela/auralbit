#pragma once

#include <QWidget>

class QLabel;
class QToolButton;

namespace auralbit::ui {

// Transport row at the bottom of the window: prev / play-pause / next,
// visualizer placeholder (real FFT in Phase 5), format chips. The "now
// playing" text/cover/time strip was intentionally removed — playback
// progress is shown on the current track's row in the library tree instead.
class TransportBar : public QWidget {
    Q_OBJECT
public:
    explicit TransportBar(QWidget* parent = nullptr);

    void setFormatChips(const QString& codec, const QString& sample_rate_khz);

signals:
    void prevClicked();
    void playPauseClicked();
    void nextClicked();

private:
    QToolButton* btn_prev_ = nullptr;
    QToolButton* btn_play_ = nullptr;
    QToolButton* btn_next_ = nullptr;
    QWidget* visualizer_ = nullptr;
    QLabel* chip_codec_ = nullptr;
    QLabel* chip_rate_ = nullptr;
};

}  // namespace auralbit::ui
