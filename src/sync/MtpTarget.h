#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "SyncTarget.h"

struct LIBMTP_mtpdevice_struct;

namespace auralbit::sync {

// SyncTarget that talks MTP directly via libmtp — useful for phones whose
// MTP filesystem isn't exposed by gvfs/kio (or when you want to bypass the
// gvfs round-trip). One MtpTarget instance owns a single open device.
class MtpTarget : public SyncTarget {
public:
    struct DeviceInfo {
        std::string vendor;
        std::string product;
        std::string serial;  // Stable device identifier.
    };

    // Probes attached MTP devices. Triggers libmtp init on first call.
    static std::vector<DeviceInfo> list_devices();

    // Opens the device at the given index in list_devices(). Construction is
    // lazy — call ready() to actually open it.
    explicit MtpTarget(int device_index);
    ~MtpTarget() override;

    bool ready(std::string* error) override;
    int64_t file_size(const std::string& rel_path) override;
    bool make_dirs(const std::string& rel_dir) override;
    bool copy_file(const std::string& src_local_path,
                   const std::string& rel_path) override;
    bool write_text(const std::string& rel_path, const std::string& content) override;

private:
    using Folder = uint32_t;

    Folder resolve_folder(const std::string& rel_dir, bool create);
    uint32_t find_file_in_folder(Folder parent, const std::string& filename);

    int device_index_;
    LIBMTP_mtpdevice_struct* device_ = nullptr;
    uint32_t default_storage_id_ = 0;
    std::map<std::string, Folder> folder_cache_;  // rel_dir → folder id (cached).
};

}  // namespace auralbit::sync
