#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

namespace auralbit::audio {

struct AudioFormat {
    uint32_t sample_rate = 0;
    uint16_t channels = 0;
};

class Decoder {
public:
    virtual ~Decoder() = default;

    virtual AudioFormat format() const = 0;

    // Total length in PCM frames, or 0 if unknown / unseekable.
    virtual uint64_t total_frames() const = 0;

    // Decode up to `frame_count` frames of interleaved float32 PCM.
    // Returns frames actually written. 0 means end of stream.
    virtual size_t read(float* out, size_t frame_count) = 0;

    // Seek to the given PCM frame from the start. Returns true on success.
    virtual bool seek(uint64_t frame) = 0;

    // Current playback position in PCM frames.
    virtual uint64_t cursor() const = 0;
};

// Sniffs by extension first, then by file magic. Returns nullptr on failure.
std::unique_ptr<Decoder> open_decoder(const std::string& path);

}  // namespace auralbit::audio
