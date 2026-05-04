#include "MtpTarget.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <vector>

#include <unistd.h>

#include <libmtp.h>

namespace auralbit::sync {

namespace fs = std::filesystem;

namespace {

std::once_flag g_init_flag;
void ensure_init() { std::call_once(g_init_flag, [] { LIBMTP_Init(); }); }

constexpr uint32_t kRootFolder = 0xFFFFFFFFu;  // libmtp's "device root" sentinel.

}  // namespace

std::vector<MtpTarget::DeviceInfo> MtpTarget::list_devices() {
    ensure_init();
    std::vector<DeviceInfo> out;

    LIBMTP_raw_device_t* raw = nullptr;
    int n = 0;
    if (LIBMTP_Detect_Raw_Devices(&raw, &n) != 0 || !raw) return out;

    for (int i = 0; i < n; ++i) {
        DeviceInfo info;
        if (raw[i].device_entry.vendor) info.vendor = raw[i].device_entry.vendor;
        if (raw[i].device_entry.product) info.product = raw[i].device_entry.product;
        // Friendly serial isn't on raw_device_t — we'd have to open the device
        // to read it. Use bus:dev-num as a stable-enough identifier instead.
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%d:%d", raw[i].bus_location, raw[i].devnum);
        info.serial = buf;
        out.push_back(std::move(info));
    }
    free(raw);
    return out;
}

MtpTarget::MtpTarget(int device_index) : device_index_(device_index) {}

MtpTarget::~MtpTarget() {
    if (device_) LIBMTP_Release_Device(device_);
}

bool MtpTarget::ready(std::string* error) {
    if (device_) return true;
    ensure_init();

    LIBMTP_raw_device_t* raw = nullptr;
    int n = 0;
    if (LIBMTP_Detect_Raw_Devices(&raw, &n) != 0 || !raw) {
        if (error) *error = "no MTP devices detected";
        return false;
    }
    if (device_index_ < 0 || device_index_ >= n) {
        free(raw);
        if (error) *error = "MTP device index out of range";
        return false;
    }
    device_ = LIBMTP_Open_Raw_Device_Uncached(&raw[device_index_]);
    free(raw);
    if (!device_) {
        if (error) *error = "failed to open MTP device (already in use?)";
        return false;
    }

    LIBMTP_devicestorage_t* storage = device_->storage;
    if (!storage) {
        if (error) *error = "MTP device has no storage";
        LIBMTP_Release_Device(device_);
        device_ = nullptr;
        return false;
    }
    default_storage_id_ = storage->id;
    folder_cache_["/"] = kRootFolder;
    return true;
}

MtpTarget::Folder MtpTarget::resolve_folder(const std::string& rel_dir, bool create) {
    if (rel_dir.empty() || rel_dir == "/") return kRootFolder;

    if (auto it = folder_cache_.find(rel_dir); it != folder_cache_.end()) {
        return it->second;
    }

    Folder parent = kRootFolder;
    std::string accumulated;
    for (const auto& component : fs::path(rel_dir)) {
        const std::string seg = component.string();
        if (seg.empty() || seg == "/") continue;
        if (!accumulated.empty()) accumulated += "/";
        accumulated += seg;

        if (auto it = folder_cache_.find(accumulated); it != folder_cache_.end()) {
            parent = it->second;
            continue;
        }

        Folder found = 0;
        LIBMTP_file_t* files =
            LIBMTP_Get_Files_And_Folders(device_, default_storage_id_, parent);
        for (LIBMTP_file_t* f = files; f; f = f->next) {
            if (f->filetype == LIBMTP_FILETYPE_FOLDER && seg == f->filename) {
                found = f->item_id;
                break;
            }
        }
        while (files) {
            LIBMTP_file_t* nxt = files->next;
            LIBMTP_destroy_file_t(files);
            files = nxt;
        }

        if (found == 0) {
            if (!create) return 0;
            // LIBMTP_Create_Folder takes a non-const char*.
            std::vector<char> name_buf(seg.begin(), seg.end());
            name_buf.push_back('\0');
            found = LIBMTP_Create_Folder(device_, name_buf.data(), parent,
                                         default_storage_id_);
            if (found == 0) return 0;
        }

        folder_cache_[accumulated] = found;
        parent = found;
    }

    return parent;
}

uint32_t MtpTarget::find_file_in_folder(Folder parent, const std::string& filename) {
    LIBMTP_file_t* files =
        LIBMTP_Get_Files_And_Folders(device_, default_storage_id_, parent);
    uint32_t id = 0;
    int64_t size = -1;
    for (LIBMTP_file_t* f = files; f; f = f->next) {
        if (f->filetype != LIBMTP_FILETYPE_FOLDER && filename == f->filename) {
            id = f->item_id;
            size = static_cast<int64_t>(f->filesize);
            break;
        }
    }
    while (files) {
        LIBMTP_file_t* nxt = files->next;
        LIBMTP_destroy_file_t(files);
        files = nxt;
    }
    (void)size;  // size returned via separate file_size() call.
    return id;
}

int64_t MtpTarget::file_size(const std::string& rel_path) {
    if (!device_) return -1;
    fs::path p(rel_path);
    const std::string parent_dir = p.parent_path().string();
    const std::string filename = p.filename().string();

    Folder parent = resolve_folder(parent_dir, /*create=*/false);
    if (parent == 0 && !parent_dir.empty()) return -1;

    LIBMTP_file_t* files =
        LIBMTP_Get_Files_And_Folders(device_, default_storage_id_, parent);
    int64_t size = -1;
    for (LIBMTP_file_t* f = files; f; f = f->next) {
        if (f->filetype != LIBMTP_FILETYPE_FOLDER && filename == f->filename) {
            size = static_cast<int64_t>(f->filesize);
            break;
        }
    }
    while (files) {
        LIBMTP_file_t* nxt = files->next;
        LIBMTP_destroy_file_t(files);
        files = nxt;
    }
    return size;
}

bool MtpTarget::make_dirs(const std::string& rel_dir) {
    if (!device_) return false;
    return resolve_folder(rel_dir, /*create=*/true) != 0;
}

bool MtpTarget::copy_file(const std::string& src_local_path,
                          const std::string& rel_path) {
    if (!device_) return false;
    fs::path p(rel_path);
    const std::string parent_dir = p.parent_path().string();
    const std::string filename = p.filename().string();

    Folder parent = resolve_folder(parent_dir, /*create=*/true);
    if (parent == 0 && !parent_dir.empty()) return false;

    // Overwrite by deleting any existing same-named entry. MTP doesn't have
    // a transactional rename, so a transient absence is the cleanest option.
    if (uint32_t existing = find_file_in_folder(parent, filename); existing != 0) {
        LIBMTP_Delete_Object(device_, existing);
    }

    std::error_code ec;
    const uint64_t sz = fs::file_size(src_local_path, ec);
    if (ec) return false;

    LIBMTP_file_t* meta = LIBMTP_new_file_t();
    meta->filename = strdup(filename.c_str());
    meta->parent_id = parent;
    meta->storage_id = default_storage_id_;
    meta->filesize = sz;
    meta->filetype = LIBMTP_FILETYPE_UNKNOWN;  // device usually re-detects.

    const int rc = LIBMTP_Send_File_From_File(device_, src_local_path.c_str(),
                                              meta, nullptr, nullptr);
    LIBMTP_destroy_file_t(meta);
    return rc == 0;
}

bool MtpTarget::write_text(const std::string& rel_path, const std::string& content) {
    if (!device_) return false;
    // libmtp only sends from local files, so stage to a temp file first.
    char tmpl[] = "/tmp/auralbit_mtp_XXXXXX";
    int fd = mkstemp(tmpl);
    if (fd < 0) return false;
    {
        std::ofstream f(tmpl);
        f << content;
        if (!f) { ::close(fd); ::unlink(tmpl); return false; }
    }
    ::close(fd);
    const bool ok = copy_file(tmpl, rel_path);
    ::unlink(tmpl);
    return ok;
}

}  // namespace auralbit::sync
