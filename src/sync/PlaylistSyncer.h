#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>

#include "SyncTarget.h"

namespace auralbit::sync {

// Shared state between the worker thread and the UI dialog. Counters are
// atomics; current_path is guarded by a mutex because std::string isn't
// trivially copyable across threads.
struct SyncProgress {
    std::atomic<int> total{0};
    std::atomic<int> processed{0};
    std::atomic<int> copied{0};
    std::atomic<int> skipped{0};
    std::atomic<int> failed{0};
    std::atomic<bool> done{false};
    std::atomic<bool> canceled{false};

    mutable std::mutex current_mu;
    std::string current;  // Currently-processing relative path; for display.
    std::string error;    // Set when the run aborts (target unreachable, etc.).

    void set_current(std::string path) {
        std::lock_guard lk(current_mu);
        current = std::move(path);
    }
    std::string get_current() const {
        std::lock_guard lk(current_mu);
        return current;
    }
};

// Drives a one-shot playlist → SyncTarget transfer. Opens its own DB
// connection in run() so the worker thread doesn't share SQLite state with
// the UI thread (SQLITE_THREADSAFE=2 only permits per-connection access).
// Remaps each track path relative to the longest shared parent prefix
// (typically the user's library root), sanitises components for FAT32,
// copies missing/changed files, and writes an M3U manifest at the target
// root. Skips files whose target already has the same byte size.
class PlaylistSyncer {
public:
    explicit PlaylistSyncer(std::unique_ptr<SyncTarget> target);

    // Runs synchronously; intended to be invoked on a worker thread.
    void run(const std::string& db_path, int64_t playlist_id,
             const std::string& playlist_name, SyncProgress& progress);

private:
    std::unique_ptr<SyncTarget> target_;
};

}  // namespace auralbit::sync
