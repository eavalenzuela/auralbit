#include "Mp3Decoder.h"

namespace auralbit::audio {

std::unique_ptr<Mp3Decoder> Mp3Decoder::open(const std::string& path) {
    auto d = std::unique_ptr<Mp3Decoder>(new Mp3Decoder());
    if (!drmp3_init_file(&d->mp3_, path.c_str(), nullptr)) {
        return nullptr;
    }
    d->initialized_ = true;
    d->format_.sample_rate = d->mp3_.sampleRate;
    d->format_.channels = static_cast<uint16_t>(d->mp3_.channels);
    d->total_frames_ = drmp3_get_pcm_frame_count(&d->mp3_);
    return d;
}

Mp3Decoder::~Mp3Decoder() {
    if (initialized_) {
        drmp3_uninit(&mp3_);
    }
}

size_t Mp3Decoder::read(float* out, size_t frame_count) {
    const drmp3_uint64 read = drmp3_read_pcm_frames_f32(&mp3_, frame_count, out);
    cursor_ += read;
    return static_cast<size_t>(read);
}

bool Mp3Decoder::seek(uint64_t frame) {
    if (!drmp3_seek_to_pcm_frame(&mp3_, frame)) {
        return false;
    }
    cursor_ = frame;
    return true;
}

}  // namespace auralbit::audio
