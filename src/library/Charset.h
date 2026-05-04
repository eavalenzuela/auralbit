#pragma once

#include <string>
#include <string_view>

namespace auralbit::library::charset {

// TagLib reads ID3 frames marked as ISO-8859-1 byte-for-byte, so Japanese
// tags written in CP932/Shift-JIS but mis-flagged as Latin-1 come back as
// mojibake (each high byte becomes a U+0080..U+00FF codepoint).
//
// This helper detects that pattern (the input UTF-8 has high-bit chars but
// no codepoints above U+00FF) and runs the bytes through iconv as CP932,
// returning the recovered UTF-8 string. If detection fails, conversion fails,
// or the input is plain ASCII / already-valid Unicode, the original string
// is returned unchanged.
std::string maybe_recover_cjk(std::string_view utf8);

}  // namespace auralbit::library::charset
