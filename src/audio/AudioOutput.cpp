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

void AudioOutput::on_data(ma_device* dev, void* output, const void* /*input*/, uint32_t frames) {
    auto* self = static_cast<AudioOutput*>(dev->pUserData);
    auto* out = static_cast<float*>(output);
    const size_t samples_wanted = static_cast<size_t>(frames) * self->channels_;
    const size_t got = self->ring_->read(out, samples_wanted);
    if (got < samples_wanted) {
        std::memset(out + got, 0, (samples_wanted - got) * sizeof(float));
    }
}

}  // namespace auralbit::audio
