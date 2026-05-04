#pragma once

#include <cstdint>
#include <functional>
#include <string>

namespace auralbit::library {

class Database;

struct ScanStats {
    int64_t scanned = 0;   // Files visited.
    int64_t added = 0;     // New tracks inserted.
    int64_t updated = 0;   // Existing tracks refreshed (mtime/size changed).
    int64_t skipped = 0;   // Unchanged on disk.
    int64_t failed = 0;    // Tag read or DB write failures.
};

class Scanner {
public:
    Scanner(Database& db, std::string cover_cache_dir);

    using ProgressFn = std::function<void(const std::string& path, const ScanStats& s)>;

    // Recursively walks `root`, upserting supported audio files into the DB.
    // Calls `on_progress` (if set) once per file. Returns final stats.
    ScanStats scan(const std::string& root, ProgressFn on_progress = {});

    // Default cache dir: $XDG_CACHE_HOME/auralbit/covers (created on first use).
    static std::string default_cover_cache_dir();

private:
    Database& db_;
    std::string cover_cache_dir_;
};

}  // namespace auralbit::library
