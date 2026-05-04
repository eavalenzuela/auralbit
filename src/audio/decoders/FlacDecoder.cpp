#include "FlacDecoder.h"

namespace auralbit::audio {

std::unique_ptr<FlacDecoder> FlacDecoder::open(const std::string& path) {
    auto d = std::unique_ptr<FlacDecoder>(new FlacDecoder());
    d->flac_ = drflac_open_file(path.c_str(), nullptr);
    if (!d->flac_) {
        return nullptr;
    }
    d->format_.sample_rate = d->flac_->sampleRate;
    d->format_.channels = static_cast<uint16_t>(d->flac_->channels);
    d->total_frames_ = d->flac_->totalPCMFrameCount;
    return d;
}

FlacDecoder::~FlacDecoder() {
    if (flac_) {
        drflac_close(flac_);
    }
}

size_t FlacDecoder::read(float* out, size_t frame_count) {
    const drflac_uint64 read = drflac_read_pcm_frames_f32(flac_, frame_count, out);
    cursor_ += read;
    return static_cast<size_t>(read);
}

bool FlacDecoder::seek(uint64_t frame) {
    if (!drflac_seek_to_pcm_frame(flac_, frame)) {
        return false;
    }
    cursor_ = frame;
    return true;
}

}  // namespace auralbit::audio
