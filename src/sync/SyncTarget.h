#pragma once

#include <cstdint>
#include <string>

namespace auralbit::sync {

// Abstract sync destination. All paths are POSIX-style and relative to the
// target's root (set at construction). Implementations: MountedFsTarget
// today; MtpTarget (libmtp) is a planned follow-up.
class SyncTarget {
public:
    virtual ~SyncTarget() = default;

    // Validates the target is reachable / writable. Called once at the start.
    virtual bool ready(std::string* error) = 0;

    // Returns the size in bytes of the file at rel_path, or -1 if absent.
    virtual int64_t file_size(const std::string& rel_path) = 0;

    // Recursively creates the directory at rel_dir (mkdir -p semantics).
    virtual bool make_dirs(const std::string& rel_dir) = 0;

    // Copies a local file into the target at rel_path. Overwrites any
    // existing file at that location.
    virtual bool copy_file(const std::string& src_local_path,
                           const std::string& rel_path) = 0;

    // Overwrites (or creates) a UTF-8 text file at rel_path.
    virtual bool write_text(const std::string& rel_path, const std::string& content) = 0;
};

}  // namespace auralbit::sync
