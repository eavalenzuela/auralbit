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
    int64_t removed = 0;   // Rows dropped because their file vanished from disk.
    int64_t failed = 0;    // Tag read or DB write failures.
};

class Scanner {
public:
    Scanner(Database& db, std::string cover_cache_dir);

    using ProgressFn = std::function<void(const std::string& path, const ScanStats& s)>;

    // Recursively walks `root`, upserting supported audio files into the DB.
    // Calls `on_progress` (if set) once per file. Returns final stats.
    // When `force` is true, re-reads tags even for files whose mtime/size are
    // unchanged (used by rescan to pick up tagging code changes in place).
    ScanStats scan(const std::string& root, ProgressFn on_progress = {},
                   bool force = false);

    // Reconciles the catalog with disk by re-walking every recorded library
    // root: new and moved/renamed files are picked up, tags are re-read in
    // place (so e.g. CP932 mojibake recovery applies), and rows whose file has
    // vanished are dropped — but only when their root is currently accessible,
    // so an unmounted drive doesn't lose its catalog. Tracks under no known
    // root, or under an inaccessible one, are left untouched. If no roots are
    // recorded yet (pre-migration libraries), falls back to re-reading tags for
    // the existing rows in place. Calls prune_orphans() at the end.
    ScanStats rescan_all(ProgressFn on_progress = {});

    // Default cache dir: $XDG_CACHE_HOME/auralbit/covers (created on first use).
    static std::string default_cover_cache_dir();

private:
    Database& db_;
    std::string cover_cache_dir_;
};

}  // namespace auralbit::library
