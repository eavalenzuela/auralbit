#pragma once

#include <cstdint>
#include <string>

namespace auralbit::library {

class Database;

struct ImportResult {
    bool ok = false;
    int64_t playlist_id = 0;
    int matched = 0;     // Tracks resolved against the library DB.
    int missing = 0;     // Paths in the file not present in the DB.
    std::string error;   // Set when ok == false.
};

struct ExportResult {
    bool ok = false;
    int written = 0;
    std::string error;
};

// Dispatches by extension (.m3u / .m3u8 / .pls). Creates a new playlist
// (named after the file basename, with a numeric suffix on collision) and
// fills it with whichever paths resolve in the library.
ImportResult import_playlist(Database& db, const std::string& file_path);

// Dispatches by extension. Writes title + path entries; PLS also writes
// length in seconds.
ExportResult export_playlist(Database& db, int64_t playlist_id,
                             const std::string& file_path);

}  // namespace auralbit::library
