#pragma once

#include <QString>
#include <QWidget>

class QLabel;
class QSlider;

namespace auralbit::ui {

// The "now playing" strip above the transport row: album-art thumbnail, track
// title + "artist — album" subtitle, an elapsed/total time readout, and a
// draggable seek bar. Mirrors the layout in mockup.png.
class NowPlayingStrip : public QWidget {
    Q_OBJECT
public:
    explicit NowPlayingStrip(QWidget* parent = nullptr);

    // Populate for a newly loaded track. Empty cover_path shows a placeholder.
    void setTrack(const QString& title, const QString& subtitle,
                  const QString& cover_path, double duration_seconds);
    // Update the elapsed-time readout and the seek bar. Ignored while the user
    // is actively dragging the seek bar.
    void setPosition(double seconds);
    // Reset to the empty/stopped state.
    void clear();

signals:
    // Emitted when the user moves or clicks the seek bar.
    void seekRequested(double seconds);

private:
    void updateTimeLabel(double position_seconds);

    QLabel* cover_ = nullptr;
    QLabel* title_ = nullptr;
    QLabel* subtitle_ = nullptr;
    QLabel* time_ = nullptr;
    QSlider* seek_ = nullptr;
    double duration_seconds_ = 0.0;
};

}  // namespace auralbit::ui
