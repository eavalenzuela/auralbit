#include "VorbisDecoder.h"

extern "C" {
#define STB_VORBIS_HEADER_ONLY
#include "stb/stb_vorbis.c"
#undef STB_VORBIS_HEADER_ONLY
}

namespace auralbit::audio {

std::unique_ptr<VorbisDecoder> VorbisDecoder::open(const std::string& path) {
    int err = 0;
    stb_vorbis* v = stb_vorbis_open_filename(path.c_str(), &err, nullptr);
    if (!v) {
        return nullptr;
    }
    auto d = std::unique_ptr<VorbisDecoder>(new VorbisDecoder());
    d->vorbis_ = v;
    const stb_vorbis_info info = stb_vorbis_get_info(v);
    d->format_.sample_rate = info.sample_rate;
    d->format_.channels = static_cast<uint16_t>(info.channels);
    d->total_frames_ = stb_vorbis_stream_length_in_samples(v);
    return d;
}

VorbisDecoder::~VorbisDecoder() {
    if (vorbis_) {
        stb_vorbis_close(vorbis_);
    }
}

size_t VorbisDecoder::read(float* out, size_t frame_count) {
    // stb_vorbis interleaved API takes total *floats*, not frames.
    const int read = stb_vorbis_get_samples_float_interleaved(
        vorbis_, format_.channels, out, static_cast<int>(frame_count * format_.channels));
    if (read < 0) {
        return 0;
    }
    cursor_ += static_cast<uint64_t>(read);
    return static_cast<size_t>(read);
}

bool VorbisDecoder::seek(uint64_t frame) {
    if (!stb_vorbis_seek(vorbis_, static_cast<unsigned int>(frame))) {
        return false;
    }
    cursor_ = frame;
    return true;
}

}  // namespace auralbit::audio
