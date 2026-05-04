#include "WavDecoder.h"

namespace auralbit::audio {

std::unique_ptr<WavDecoder> WavDecoder::open(const std::string& path) {
    auto d = std::unique_ptr<WavDecoder>(new WavDecoder());
    if (!drwav_init_file(&d->wav_, path.c_str(), nullptr)) {
        return nullptr;
    }
    d->initialized_ = true;
    d->format_.sample_rate = d->wav_.sampleRate;
    d->format_.channels = static_cast<uint16_t>(d->wav_.channels);
    d->total_frames_ = d->wav_.totalPCMFrameCount;
    return d;
}

WavDecoder::~WavDecoder() {
    if (initialized_) {
        drwav_uninit(&wav_);
    }
}

size_t WavDecoder::read(float* out, size_t frame_count) {
    const drwav_uint64 read = drwav_read_pcm_frames_f32(&wav_, frame_count, out);
    cursor_ += read;
    return static_cast<size_t>(read);
}

bool WavDecoder::seek(uint64_t frame) {
    if (!drwav_seek_to_pcm_frame(&wav_, frame)) {
        return false;
    }
    cursor_ = frame;
    return true;
}

}  // namespace auralbit::audio
