#pragma once

#include "SyncTarget.h"

namespace auralbit::sync {

// SyncTarget backed by a directory on a mounted filesystem (USB drives,
// gvfs-MTP phone mounts, NFS, etc.). All operations resolve to
// `<root>/<rel_path>` using std::filesystem.
class MountedFsTarget : public SyncTarget {
public:
    explicit MountedFsTarget(std::string root);

    bool ready(std::string* error) override;
    int64_t file_size(const std::string& rel_path) override;
    bool make_dirs(const std::string& rel_dir) override;
    bool copy_file(const std::string& src_local_path,
                   const std::string& rel_path) override;
    bool write_text(const std::string& rel_path, const std::string& content) override;

private:
    std::string root_;
};

}  // namespace auralbit::sync
