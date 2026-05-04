#include "MountedFsTarget.h"

#include <filesystem>
#include <fstream>
#include <system_error>

namespace auralbit::sync {

namespace fs = std::filesystem;

MountedFsTarget::MountedFsTarget(std::string root) : root_(std::move(root)) {}

bool MountedFsTarget::ready(std::string* error) {
    std::error_code ec;
    if (!fs::exists(root_, ec)) {
        if (error) *error = "target root does not exist: " + root_;
        return false;
    }
    if (!fs::is_directory(root_, ec)) {
        if (error) *error = "target root is not a directory: " + root_;
        return false;
    }
    // Probe writability by creating + removing a tiny marker file.
    const fs::path probe = fs::path(root_) / ".auralbit_sync_probe";
    {
        std::ofstream f(probe);
        if (!f) {
            if (error) *error = "target is not writable: " + root_;
            return false;
        }
        f << "ok";
    }
    fs::remove(probe, ec);
    return true;
}

int64_t MountedFsTarget::file_size(const std::string& rel_path) {
    std::error_code ec;
    const fs::path p = fs::path(root_) / rel_path;
    if (!fs::exists(p, ec) || !fs::is_regular_file(p, ec)) return -1;
    const auto sz = fs::file_size(p, ec);
    if (ec) return -1;
    return static_cast<int64_t>(sz);
}

bool MountedFsTarget::make_dirs(const std::string& rel_dir) {
    std::error_code ec;
    fs::create_directories(fs::path(root_) / rel_dir, ec);
    return !ec;
}

bool MountedFsTarget::copy_file(const std::string& src_local_path,
                                const std::string& rel_path) {
    std::error_code ec;
    const fs::path dst = fs::path(root_) / rel_path;
    fs::create_directories(dst.parent_path(), ec);
    if (ec) return false;
    fs::copy_file(src_local_path, dst, fs::copy_options::overwrite_existing, ec);
    return !ec;
}

bool MountedFsTarget::write_text(const std::string& rel_path, const std::string& content) {
    std::error_code ec;
    const fs::path dst = fs::path(root_) / rel_path;
    fs::create_directories(dst.parent_path(), ec);
    std::ofstream f(dst);
    if (!f) return false;
    f << content;
    return static_cast<bool>(f);
}

}  // namespace auralbit::sync
