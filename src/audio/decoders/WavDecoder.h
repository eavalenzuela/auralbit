#pragma once

#include "audio/Decoder.h"
#include "dr_libs/dr_wav.h"

namespace auralbit::audio {

class WavDecoder : public Decoder {
public:
    static std::unique_ptr<WavDecoder> open(const std::string& path);
    ~WavDecoder() override;

    AudioFormat format() const override { return format_; }
    uint64_t total_frames() const override { return total_frames_; }
    size_t read(float* out, size_t frame_count) override;
    bool seek(uint64_t frame) override;
    uint64_t cursor() const override { return cursor_; }

private:
    WavDecoder() = default;

    drwav wav_{};
    bool initialized_ = false;
    AudioFormat format_{};
    uint64_t total_frames_ = 0;
    uint64_t cursor_ = 0;
};

}  // namespace auralbit::audio
