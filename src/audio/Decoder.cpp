#include "Decoder.h"

#include <algorithm>
#include <cctype>
#include <filesystem>

#include "decoders/FlacDecoder.h"
#include "decoders/Mp3Decoder.h"
#include "decoders/VorbisDecoder.h"
#include "decoders/WavDecoder.h"

namespace auralbit::audio {

namespace {
std::string to_lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}
}  // namespace

std::unique_ptr<Decoder> open_decoder(const std::string& path) {
    const auto ext = to_lower(std::filesystem::path(path).extension().string());

    if (ext == ".mp3") {
        return Mp3Decoder::open(path);
    }
    if (ext == ".flac") {
        return FlacDecoder::open(path);
    }
    if (ext == ".wav") {
        return WavDecoder::open(path);
    }
    if (ext == ".ogg" || ext == ".oga") {
        return VorbisDecoder::open(path);
    }

    // Fallback: try each in order. Cheap because each open returns nullptr fast on mismatch.
    if (auto d = Mp3Decoder::open(path)) return d;
    if (auto d = FlacDecoder::open(path)) return d;
    if (auto d = WavDecoder::open(path)) return d;
    if (auto d = VorbisDecoder::open(path)) return d;
    return nullptr;
}

}  // namespace auralbit::audio
