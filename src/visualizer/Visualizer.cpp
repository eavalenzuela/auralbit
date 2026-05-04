#include "Visualizer.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <vector>

#include <QPainter>
#include <QTimer>

#include "audio/AudioOutput.h"
#include "audio/Player.h"
#include "kissfft/kiss_fftr.h"

namespace auralbit::visualizer {

namespace {

// Maps display column [0, kBins) → start FFT bin (logarithmic spacing from
// ~80 Hz up to Nyquist).
int displayToFftBin(int col, int bins, int fft_bins, double sample_rate) {
    const double f_min = 80.0;
    const double f_max = sample_rate * 0.5 * 0.95;
    const double frac = static_cast<double>(col) / bins;
    const double freq = f_min * std::pow(f_max / f_min, frac);
    int b = static_cast<int>((freq / sample_rate) * fft_bins * 2.0);
    if (b < 1) b = 1;
    if (b >= fft_bins) b = fft_bins - 1;
    return b;
}

}  // namespace

Visualizer::Visualizer(QWidget* parent)
    : QWidget(parent),
      input_(kFftN, 0.0f),
      hann_(kFftN, 0.0f),
      magnitudes_(kFftN / 2 + 1, 0.0f),
      levels_(kBins, 0.0f) {
    setMinimumHeight(28);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    for (int i = 0; i < kFftN; ++i) {
        hann_[i] = 0.5f * (1.0f - std::cos(2.0f * 3.14159265358979f *
                                            static_cast<float>(i) / (kFftN - 1)));
    }

    fft_cfg_ = kiss_fftr_alloc(kFftN, /*inverse=*/0, nullptr, nullptr);

    timer_ = new QTimer(this);
    timer_->setInterval(33);  // ~30 fps
    connect(timer_, &QTimer::timeout, this, &Visualizer::tick);
    timer_->start();
}

Visualizer::~Visualizer() {
    if (fft_cfg_) {
        kiss_fftr_free(static_cast<kiss_fftr_cfg>(fft_cfg_));
        fft_cfg_ = nullptr;
    }
}

void Visualizer::setPlayer(audio::Player* player) { player_ = player; }

void Visualizer::tick() {
    if (!player_ || !fft_cfg_) {
        // Even with no audio, decay the visible levels so the view goes quiet.
        for (auto& l : levels_) l = std::max(0.0f, l - 0.04f);
        update();
        return;
    }

    const size_t got = player_->output().peek_viz(input_.data(), kFftN);
    // Zero-pad the front if we don't have a full window yet.
    if (got < static_cast<size_t>(kFftN)) {
        const size_t pad = kFftN - got;
        // Shift available samples to the end.
        for (size_t i = got; i-- > 0; ) input_[i + pad] = input_[i];
        for (size_t i = 0; i < pad; ++i) input_[i] = 0.0f;
    }

    // Apply Hann window in place.
    for (int i = 0; i < kFftN; ++i) input_[i] *= hann_[i];

    std::vector<kiss_fft_cpx> spectrum(kFftN / 2 + 1);
    kiss_fftr(static_cast<kiss_fftr_cfg>(fft_cfg_), input_.data(), spectrum.data());

    const float norm = 2.0f / kFftN;
    for (size_t i = 0; i < spectrum.size(); ++i) {
        magnitudes_[i] = std::sqrt(spectrum[i].r * spectrum[i].r +
                                   spectrum[i].i * spectrum[i].i) *
                         norm;
    }

    const double sr = player_->sample_rate() ? player_->sample_rate() : 44100;
    const int last_bin = static_cast<int>(magnitudes_.size()) - 1;

    for (int col = 0; col < kBins; ++col) {
        const int b0 = displayToFftBin(col, kBins, last_bin, sr);
        const int b1 = displayToFftBin(col + 1, kBins, last_bin, sr);
        float peak = 0.0f;
        for (int b = b0; b <= b1 && b <= last_bin; ++b) {
            if (magnitudes_[b] > peak) peak = magnitudes_[b];
        }
        // Convert to dB-ish, then to [0, 1]. Floor at -60 dB.
        const float db = 20.0f * std::log10(peak + 1e-6f);
        const float lvl = std::clamp((db + 60.0f) / 60.0f, 0.0f, 1.0f);
        // Peak-decay smoothing: snap up, fade down.
        if (lvl > levels_[col]) levels_[col] = lvl;
        else                    levels_[col] = std::max(0.0f, levels_[col] - 0.05f);
    }

    update();
}

void Visualizer::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.fillRect(rect(), QColor("#1a1b1f"));

    const int cols = kBins;
    const int rows = kRows;
    const int cell_w = std::max(2, width() / cols);
    const int cell_h = std::max(2, height() / rows);

    for (int x = 0; x < cols; ++x) {
        const int filled = static_cast<int>(levels_[x] * rows + 0.5f);
        for (int y = 0; y < rows; ++y) {
            if (rows - 1 - y > filled - 1) continue;
            QColor c("#d6a64a");
            c.setAlphaF(0.35f + 0.65f * (1.0f - static_cast<float>(y) / rows));
            p.fillRect(x * cell_w + 2, y * cell_h + 2, cell_w - 3, cell_h - 3, c);
        }
    }
}

}  // namespace auralbit::visualizer
