#include "AudioOutput.h"

#include <algorithm>
#include <cstring>

#include "miniaudio/miniaudio.h"

namespace auralbit::audio {

namespace {
// ~1s of stereo audio at 48kHz → 384KB. Plenty of slack between decode thread and rt callback.
constexpr size_t kRingFrames = 96000;
}  // namespace

AudioOutput::AudioOutput() : device_(std::make_unique<ma_device>()) {}

AudioOutput::~AudioOutput() { stop(); }

bool AudioOutput::start(uint32_t sample_rate, uint16_t channels) {
    if (started_) {
        stop();
    }

    ring_ = std::make_unique<RingBuffer>(kRingFrames * channels);

    ma_device_config cfg = ma_device_config_init(ma_device_type_playback);
    cfg.playback.format = ma_format_f32;
    cfg.playback.channels = channels;
    cfg.sampleRate = sample_rate;
    cfg.dataCallback = &AudioOutput::on_data;
    cfg.pUserData = this;

    if (ma_device_init(nullptr, &cfg, device_.get()) != MA_SUCCESS) {
        return false;
    }
    if (ma_device_start(device_.get()) != MA_SUCCESS) {
        ma_device_uninit(device_.get());
        return false;
    }
    // Re-apply the carried-over volume to the freshly opened device.
    ma_device_set_master_volume(device_.get(), volume_.load(std::memory_order_acquire));

    sample_rate_ = sample_rate;
    channels_ = channels;
    started_ = true;
    return true;
}

void AudioOutput::stop() {
    if (!started_) return;
    ma_device_stop(device_.get());
    ma_device_uninit(device_.get());
    started_ = false;
    sample_rate_ = 0;
    channels_ = 0;
    if (ring_) ring_->clear();
}

void AudioOutput::set_volume(float v) {
    if (v < 0.0f) v = 0.0f;
    if (v > 1.0f) v = 1.0f;
    volume_.store(v, std::memory_order_release);
    if (started_) ma_device_set_master_volume(device_.get(), v);
}

void AudioOutput::on_data(ma_device* dev, void* output, const void* /*input*/, uint32_t frames) {
    auto* self = static_cast<AudioOutput*>(dev->pUserData);
    auto* out = static_cast<float*>(output);
    const size_t samples_wanted = static_cast<size_t>(frames) * self->channels_;
    const size_t got = self->ring_->read(out, samples_wanted);
    if (got < samples_wanted) {
        std::memset(out + got, 0, (samples_wanted - got) * sizeof(float));
    }
    self->write_viz(out, frames);
}

void AudioOutput::write_viz(const float* interleaved, uint32_t frame_count) {
    const uint64_t base = viz_written_.load(std::memory_order_relaxed);
    const uint16_t ch = channels_ ? channels_ : 1;
    constexpr size_t mask = kVizCapacity - 1;
    static_assert((kVizCapacity & (kVizCapacity - 1)) == 0,
                  "kVizCapacity must be a power of two");
    for (uint32_t i = 0; i < frame_count; ++i) {
        float mono = 0.0f;
        for (uint16_t c = 0; c < ch; ++c) mono += interleaved[i * ch + c];
        mono /= ch;
        viz_[(base + i) & mask] = mono;
    }
    viz_written_.store(base + frame_count, std::memory_order_release);
}

size_t AudioOutput::peek_viz(float* dst, size_t count) const {
    const uint64_t total = viz_written_.load(std::memory_order_acquire);
    if (total == 0 || count == 0) return 0;
    const size_t available = total < kVizCapacity ? static_cast<size_t>(total) : kVizCapacity;
    const size_t n = count < available ? count : available;
    constexpr size_t mask = kVizCapacity - 1;
    // Copy oldest-first so the caller sees samples in playback order.
    const uint64_t start = total - n;
    for (size_t i = 0; i < n; ++i) {
        dst[i] = viz_[(start + i) & mask];
    }
    return n;
}

}  // namespace auralbit::audio
