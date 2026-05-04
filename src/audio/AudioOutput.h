#pragma once

#include <cstdint>
#include <memory>

#include "RingBuffer.h"

typedef struct ma_device ma_device;

namespace auralbit::audio {

// Pulls float32 interleaved PCM from a SPSC ring buffer and pushes to miniaudio.
// Audio thread is owned by miniaudio; only the realtime callback touches the ring.
class AudioOutput {
public:
    AudioOutput();
    ~AudioOutput();

    AudioOutput(const AudioOutput&) = delete;
    AudioOutput& operator=(const AudioOutput&) = delete;

    bool start(uint32_t sample_rate, uint16_t channels);
    void stop();

    // Producer side. Called from the decode thread.
    RingBuffer& ring() { return *ring_; }

    bool is_started() const { return started_; }
    uint32_t sample_rate() const { return sample_rate_; }
    uint16_t channels() const { return channels_; }

private:
    static void on_data(ma_device* dev, void* output, const void* input, uint32_t frame_count);

    std::unique_ptr<ma_device> device_;
    std::unique_ptr<RingBuffer> ring_;
    bool started_ = false;
    uint32_t sample_rate_ = 0;
    uint16_t channels_ = 0;
};

}  // namespace auralbit::audio
