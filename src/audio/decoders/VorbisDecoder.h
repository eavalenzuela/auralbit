#pragma once

#include "audio/Decoder.h"

struct stb_vorbis;

namespace auralbit::audio {

class VorbisDecoder : public Decoder {
public:
    static std::unique_ptr<VorbisDecoder> open(const std::string& path);
    ~VorbisDecoder() override;

    AudioFormat format() const override { return format_; }
    uint64_t total_frames() const override { return total_frames_; }
    size_t read(float* out, size_t frame_count) override;
    bool seek(uint64_t frame) override;
    uint64_t cursor() const override { return cursor_; }

private:
    VorbisDecoder() = default;

    stb_vorbis* vorbis_ = nullptr;
    AudioFormat format_{};
    uint64_t total_frames_ = 0;
    uint64_t cursor_ = 0;
};

}  // namespace auralbit::audio
