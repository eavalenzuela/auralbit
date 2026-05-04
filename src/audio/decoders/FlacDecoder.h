#pragma once

#include "audio/Decoder.h"
#include "dr_libs/dr_flac.h"

namespace auralbit::audio {

class FlacDecoder : public Decoder {
public:
    static std::unique_ptr<FlacDecoder> open(const std::string& path);
    ~FlacDecoder() override;

    AudioFormat format() const override { return format_; }
    uint64_t total_frames() const override { return total_frames_; }
    size_t read(float* out, size_t frame_count) override;
    bool seek(uint64_t frame) override;
    uint64_t cursor() const override { return cursor_; }

private:
    FlacDecoder() = default;

    drflac* flac_ = nullptr;
    AudioFormat format_{};
    uint64_t total_frames_ = 0;
    uint64_t cursor_ = 0;
};

}  // namespace auralbit::audio
