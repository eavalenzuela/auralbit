#include "Charset.h"

#include <cstdint>
#include <iconv.h>
#include <vector>

namespace auralbit::library::charset {

namespace {

// Decode UTF-8 into [low_bytes_out]. Returns true if every codepoint fits in
// 0x00..0xFF (i.e. could plausibly be a Latin-1 misread of single-byte data),
// and at least one codepoint is in the high-half (0x80..0xFF). False if the
// string is pure ASCII (no recovery needed) or contains any codepoint above
// U+00FF (already proper Unicode).
bool collect_latin1_bytes(std::string_view utf8, std::vector<uint8_t>& out) {
    bool any_high = false;
    for (size_t i = 0; i < utf8.size();) {
        const auto b = static_cast<uint8_t>(utf8[i]);
        uint32_t cp = 0;
        if (b < 0x80) { cp = b; ++i; }
        else if ((b & 0xE0) == 0xC0) {
            if (i + 1 >= utf8.size()) return false;
            cp = static_cast<uint32_t>(b & 0x1F) << 6 |
                 (static_cast<uint8_t>(utf8[i + 1]) & 0x3F);
            i += 2;
        } else if ((b & 0xF0) == 0xE0) {
            if (i + 2 >= utf8.size()) return false;
            cp = static_cast<uint32_t>(b & 0x0F) << 12 |
                 (static_cast<uint32_t>(utf8[i + 1] & 0x3F) << 6) |
                 (static_cast<uint8_t>(utf8[i + 2]) & 0x3F);
            i += 3;
        } else {
            return false;  // 4-byte UTF-8 → genuine non-BMP, leave alone.
        }
        if (cp > 0xFF) return false;
        if (cp >= 0x80) any_high = true;
        out.push_back(static_cast<uint8_t>(cp));
    }
    return any_high;
}

}  // namespace

std::string maybe_recover_cjk(std::string_view utf8) {
    std::vector<uint8_t> bytes;
    bytes.reserve(utf8.size());
    if (!collect_latin1_bytes(utf8, bytes) || bytes.empty()) {
        return std::string(utf8);
    }

    iconv_t cd = iconv_open("UTF-8", "CP932");
    if (cd == reinterpret_cast<iconv_t>(-1)) return std::string(utf8);

    std::string out(bytes.size() * 4, '\0');
    char* in_buf = reinterpret_cast<char*>(bytes.data());
    char* out_buf = out.data();
    size_t in_left = bytes.size();
    size_t out_left = out.size();

    const size_t r = iconv(cd, &in_buf, &in_left, &out_buf, &out_left);
    iconv_close(cd);

    if (r == static_cast<size_t>(-1) || in_left > 0) {
        return std::string(utf8);
    }
    out.resize(out.size() - out_left);
    return out;
}

}  // namespace auralbit::library::charset
