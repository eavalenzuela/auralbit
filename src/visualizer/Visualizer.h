#pragma once

#include <QWidget>
#include <vector>

class QTimer;

namespace auralbit::audio { class Player; }

namespace auralbit::visualizer {

// Live spectrum visualizer driven by KissFFT. Reads PCM from the Player's
// AudioOutput viz tap on a ~30Hz timer, applies a Hann window, runs a real
// FFT, log-bins into ~kBins buckets, and paints the orange grid pattern from
// the mockup with peak-decay smoothing.
class Visualizer : public QWidget {
    Q_OBJECT
public:
    explicit Visualizer(QWidget* parent = nullptr);
    ~Visualizer() override;

    void setPlayer(audio::Player* player);

protected:
    void paintEvent(QPaintEvent* event) override;

private slots:
    void tick();

private:
    audio::Player* player_ = nullptr;
    QTimer* timer_ = nullptr;

    static constexpr int kFftN = 1024;          // FFT input size (power of 2).
    static constexpr int kBins = 60;            // Display columns.
    static constexpr int kRows = 7;             // Display rows.

    std::vector<float> input_;                  // Hann-windowed real input.
    std::vector<float> hann_;                   // Window coefficients.
    std::vector<float> magnitudes_;             // Linear magnitudes per FFT bin.
    std::vector<float> levels_;                 // Smoothed [0,1] per display bin.

    void* fft_cfg_ = nullptr;                   // kiss_fftr_cfg
};

}  // namespace auralbit::visualizer
