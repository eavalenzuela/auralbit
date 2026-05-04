#include "PlaylistSyncer.h"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <sstream>
#include <vector>

#include "library/Database.h"

namespace auralbit::sync {

namespace fs = std::filesystem;

namespace {

// Strip characters that FAT32 / common phone filesystems reject from a path
// component. We replace with '_' rather than removing so collisions are rare.
std::string sanitise(std::string s) {
    static constexpr const char* kBad = "/\\:*?\"<>|";
    for (char& c : s) {
        if (std::strchr(kBad, c) || static_cast<unsigned char>(c) < 0x20) {
            c = '_';
        }
    }
    // Trim trailing dots/spaces — FAT32 won't allow them.
    while (!s.empty() && (s.back() == '.' || s.back() == ' ')) s.pop_back();
    if (s.empty()) s = "_";
    return s;
}

std::vector<std::string> path_components(const std::string& s) {
    std::vector<std::string> parts;
    for (const auto& c : fs::path(s)) {
        const std::string seg = c.string();
        if (seg.empty() || seg == "/") continue;
        parts.push_back(seg);
    }
    return parts;
}

// Longest dir-aligned shared prefix of all track parent directories. This
// becomes the implicit "library root" for the sync — everything beneath it
// is what gets preserved on the target.
std::vector<std::string> common_parent_prefix(const std::vector<std::string>& paths) {
    if (paths.empty()) return {};
    std::vector<std::string> base = path_components(fs::path(paths[0]).parent_path().string());
    for (size_t i = 1; i < paths.size() && !base.empty(); ++i) {
        const auto p = path_components(fs::path(paths[i]).parent_path().string());
        size_t common = 0;
        while (common < base.size() && common < p.size() && base[common] == p[common]) {
            ++common;
        }
        base.resize(common);
    }
    return base;
}

// Strip the prefix from src and sanitise each remaining component. Falls
// back to the last 3 components if the prefix doesn't actually match (which
// can happen on a heterogeneous playlist where the common prefix is empty).
std::string remap(const std::string& src_path,
                  const std::vector<std::string>& prefix) {
    auto parts = path_components(src_path);
    if (parts.empty()) return "track";

    bool matched = !prefix.empty() && parts.size() > prefix.size();
    if (matched) {
        for (size_t i = 0; i < prefix.size(); ++i) {
            if (parts[i] != prefix[i]) { matched = false; break; }
        }
    }

    size_t start = 0;
    if (matched) {
        start = prefix.size();
    } else {
        // Best-effort fallback for tracks outside the common root.
        const size_t take = std::min<size_t>(3, parts.size());
        start = parts.size() - take;
    }

    std::string out;
    for (size_t i = start; i < parts.size(); ++i) {
        if (!out.empty()) out += "/";
        out += sanitise(parts[i]);
    }
    return out;
}

}  // namespace

PlaylistSyncer::PlaylistSyncer(std::unique_ptr<SyncTarget> target)
    : target_(std::move(target)) {}

void PlaylistSyncer::run(const std::string& db_path, int64_t playlist_id,
                         const std::string& playlist_name,
                         SyncProgress& progress) {
    library::Database db;
    if (!db.open(db_path)) {
        progress.error = "cannot open library database on worker thread";
        progress.done.store(true);
        return;
    }

    if (std::string err; !target_->ready(&err)) {
        progress.error = std::move(err);
        progress.done.store(true);
        return;
    }

    const auto tracks = db.tracks_for_playlist(playlist_id);
    progress.total.store(static_cast<int>(tracks.size()));

    // Plan: derive the implicit library root once, then remap every track
    // relative to it. Write the M3U after the file copies.
    std::vector<std::string> all_paths;
    all_paths.reserve(tracks.size());
    for (const auto& t : tracks) all_paths.push_back(t.path);
    const auto prefix = common_parent_prefix(all_paths);

    std::vector<std::pair<std::string, std::string>> mapped;  // src_path → rel_dst
    mapped.reserve(tracks.size());
    for (const auto& t : tracks) {
        mapped.emplace_back(t.path, remap(t.path, prefix));
    }

    for (const auto& [src, rel] : mapped) {
        if (progress.canceled.load()) break;
        progress.set_current(rel);

        std::error_code ec;
        if (!fs::exists(src, ec) || !fs::is_regular_file(src, ec)) {
            ++progress.failed;
            if (progress.error.empty()) {
                progress.error = "missing source: " + src;
            }
            ++progress.processed;
            continue;
        }
        const int64_t local_size = static_cast<int64_t>(fs::file_size(src, ec));
        const int64_t remote_size = target_->file_size(rel);

        if (remote_size == local_size && local_size > 0) {
            ++progress.skipped;
        } else if (target_->copy_file(src, rel)) {
            ++progress.copied;
        } else {
            ++progress.failed;
            if (progress.error.empty()) {
                progress.error = "copy failed: " + src + " -> " + rel;
            }
        }
        ++progress.processed;
    }

    if (!progress.canceled.load()) {
        // M3U with paths relative to the target root. Quote nothing — M3U is
        // line-oriented and we've already sanitised path components.
        std::ostringstream m3u;
        m3u << "#EXTM3U\n";
        for (size_t i = 0; i < tracks.size(); ++i) {
            const auto& t = tracks[i];
            const int seconds = static_cast<int>(t.duration_ms / 1000);
            m3u << "#EXTINF:" << seconds << "," << t.title << "\n";
            m3u << mapped[i].second << "\n";
        }
        const std::string manifest_name = sanitise(playlist_name) + ".m3u";
        target_->write_text(manifest_name, m3u.str());
    }

    progress.done.store(true);
}

}  // namespace auralbit::sync
