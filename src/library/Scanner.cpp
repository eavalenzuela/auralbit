#include "Scanner.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include <taglib/audioproperties.h>
#include <taglib/fileref.h>
#include <taglib/tag.h>
#include <taglib/tpropertymap.h>
#include <taglib/tstring.h>
#include <taglib/tvariant.h>

#include "Charset.h"
#include "Database.h"
#include "sqlite/sqlite3.h"

namespace auralbit::library {

namespace {

namespace fs = std::filesystem;

bool is_supported(const fs::path& p) {
    auto ext = p.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext == ".mp3" || ext == ".flac" || ext == ".wav" || ext == ".ogg" || ext == ".oga";
}

// Directories the scanner refuses to descend into. Covers the library-cleanup
// "_quarantine" holding area (dup/backup files we don't want re-imported) and
// hidden dot-directories, which never hold a user's music.
bool is_ignored_dir(const fs::path& p) {
    const std::string name = p.filename().string();
    if (name == "_quarantine") return true;
    return name.size() > 1 && name[0] == '.';
}

std::string sanitize(std::string_view in) {
    std::string out;
    out.reserve(in.size());
    for (char c : in) {
        if (std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_') {
            out.push_back(c);
        } else if (c == ' ') {
            out.push_back('_');
        }
    }
    if (out.empty()) out = "untitled";
    if (out.size() > 80) out.resize(80);
    return out;
}

std::string ext_from_mime(std::string_view mime) {
    if (mime == "image/png")  return ".png";
    if (mime == "image/webp") return ".webp";
    return ".jpg";  // covers image/jpeg, image/jpg, and unknown.
}

// Returns path on disk of the cached cover, or empty string if no embedded art.
std::string extract_cover(TagLib::FileRef& ref, const std::string& cache_dir,
                          const std::string& album_key) {
    const auto pictures = ref.complexProperties("PICTURE");
    if (pictures.isEmpty()) return {};

    const auto& first = pictures[0];
    auto data_it = first.find("data");
    if (data_it == first.end()) return {};
    const TagLib::ByteVector bytes = data_it->second.value<TagLib::ByteVector>();
    if (bytes.isEmpty()) return {};

    std::string mime;
    if (auto m = first.find("mimeType"); m != first.end()) {
        mime = m->second.value<TagLib::String>().to8Bit(true);
    }

    const std::string filename = sanitize(album_key) + ext_from_mime(mime);
    const fs::path full = fs::path(cache_dir) / filename;

    std::error_code ec;
    if (fs::exists(full, ec)) {
        return full.string();
    }
    fs::create_directories(cache_dir, ec);

    std::ofstream out(full, std::ios::binary);
    if (!out) return {};
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    out.close();
    return out ? full.string() : std::string{};
}

// Reads tags + audio properties for `path` into a ScanRecord. Returns false if
// the file can't be opened or has no tag.
bool read_record(const std::string& path, int64_t mtime_ns, int64_t size,
                 const std::string& cover_cache_dir, ScanRecord& rec) {
    TagLib::FileRef ref(path.c_str(), true, TagLib::AudioProperties::Average);
    if (ref.isNull() || !ref.tag()) return false;

    rec.path = path;
    rec.mtime = mtime_ns;
    rec.size = size;

    const auto* tag = ref.tag();
    rec.title = charset::maybe_recover_cjk(tag->title().to8Bit(true));
    rec.artist = charset::maybe_recover_cjk(tag->artist().to8Bit(true));
    rec.album = charset::maybe_recover_cjk(tag->album().to8Bit(true));
    rec.year = static_cast<int>(tag->year());
    rec.track_no = static_cast<int>(tag->track());

    // Disc number lives in the property map under "DISCNUMBER".
    const auto props = ref.properties();
    if (props.contains("DISCNUMBER")) {
        const auto& list = props["DISCNUMBER"];
        if (!list.isEmpty()) {
            rec.disc_no = std::atoi(list.front().to8Bit(true).c_str());
        }
    }

    if (const auto* ap = ref.audioProperties()) {
        rec.duration_ms = ap->lengthInMilliseconds();
        rec.bitrate = ap->bitrate();
        rec.sample_rate = ap->sampleRate();
        rec.channels = ap->channels();
    }

    // Cover art: key by "artist|album" so two albums with the same name from
    // different artists don't collide.
    if (!rec.album.empty()) {
        const std::string album_key = rec.artist + "|" + rec.album;
        rec.cover_path = extract_cover(ref, cover_cache_dir, album_key);
    }
    return true;
}

}  // namespace

Scanner::Scanner(Database& db, std::string cover_cache_dir)
    : db_(db), cover_cache_dir_(std::move(cover_cache_dir)) {}

std::string Scanner::default_cover_cache_dir() {
    const char* xdg = std::getenv("XDG_CACHE_HOME");
    fs::path dir = xdg && *xdg ? fs::path(xdg)
                               : fs::path(std::getenv("HOME") ? std::getenv("HOME") : "/tmp") / ".cache";
    dir /= "auralbit/covers";
    std::error_code ec;
    fs::create_directories(dir, ec);
    return dir.string();
}

ScanStats Scanner::scan(const std::string& root, ProgressFn on_progress, bool force) {
    ScanStats stats;

    std::error_code ec;
    fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec);
    if (ec) {
        std::fprintf(stderr, "scan: cannot open %s: %s\n", root.c_str(), ec.message().c_str());
        return stats;
    }

    // Wrap the whole scan in one transaction — ~100x faster for large libs.
    sqlite3_exec(db_.handle(), "BEGIN", nullptr, nullptr, nullptr);

    for (; it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if (ec) {
            ec.clear();
            continue;
        }
        // Prune ignored directories before descending into them.
        if (it->is_directory(ec) && is_ignored_dir(it->path())) {
            it.disable_recursion_pending();
            continue;
        }
        if (!it->is_regular_file(ec)) continue;
        const fs::path& p = it->path();
        if (!is_supported(p)) continue;

        ++stats.scanned;
        const std::string path_str = p.string();

        const auto file_size = fs::file_size(p, ec);
        if (ec) { ++stats.failed; continue; }
        const auto mtime = fs::last_write_time(p, ec);
        if (ec) { ++stats.failed; continue; }
        const int64_t mtime_ns =
            std::chrono::duration_cast<std::chrono::nanoseconds>(mtime.time_since_epoch()).count();

        const auto existing = db_.find_track(path_str);
        if (!force && existing && existing->mtime == mtime_ns &&
            existing->size == static_cast<int64_t>(file_size)) {
            ++stats.skipped;
            if (on_progress) on_progress(path_str, stats);
            continue;
        }

        ScanRecord rec;
        if (!read_record(path_str, mtime_ns, static_cast<int64_t>(file_size),
                         cover_cache_dir_, rec)) {
            ++stats.failed;
            continue;
        }

        if (db_.upsert_track(rec)) {
            if (existing) ++stats.updated;
            else ++stats.added;
        } else {
            ++stats.failed;
        }

        if (on_progress) on_progress(path_str, stats);
    }

    sqlite3_exec(db_.handle(), "COMMIT", nullptr, nullptr, nullptr);
    // Reconnect any playlist entries that were waiting on these files (e.g.
    // after a Remove Library + re-add, or a newly added folder).
    db_.reconcile_playlists();
    return stats;
}

ScanStats Scanner::rescan_all(ProgressFn on_progress) {
    ScanStats stats;
    const auto roots = db_.all_roots();

    // Pre-migration libraries have no recorded roots, so there's nothing to
    // re-walk. Fall back to re-reading tags for the existing rows in place,
    // which is what rescan did historically (e.g. for mojibake recovery).
    // Missing files are kept, since we can't tell an unmounted drive from a
    // genuinely deleted file without a known, accessible root to anchor on.
    if (roots.empty()) {
        sqlite3_exec(db_.handle(), "BEGIN", nullptr, nullptr, nullptr);
        for (const auto& [id, path_str] : db_.all_track_paths()) {
            ++stats.scanned;
            std::error_code ec;
            const fs::path p(path_str);
            if (!fs::exists(p, ec) || !fs::is_regular_file(p, ec)) {
                ++stats.failed;
                if (on_progress) on_progress(path_str, stats);
                continue;
            }
            const int64_t size = static_cast<int64_t>(fs::file_size(p, ec));
            const int64_t mtime_ns =
                std::chrono::duration_cast<std::chrono::nanoseconds>(
                    fs::last_write_time(p, ec).time_since_epoch())
                    .count();
            ScanRecord rec;
            if (read_record(path_str, mtime_ns, size, cover_cache_dir_, rec) &&
                db_.upsert_track(rec)) {
                ++stats.updated;
            } else {
                ++stats.failed;
            }
            if (on_progress) on_progress(path_str, stats);
        }
        sqlite3_exec(db_.handle(), "COMMIT", nullptr, nullptr, nullptr);
        db_.prune_orphans();
        return stats;
    }

    // Re-walk each accessible root, force-re-reading tags so moves, renames,
    // new files, and in-place tag changes are all picked up. Each scan() runs
    // in its own transaction.
    std::vector<std::string> accessible;
    for (const auto& root : roots) {
        std::error_code ec;
        if (!fs::exists(root, ec) || !fs::is_directory(root, ec)) continue;
        accessible.push_back(root);
        const ScanStats s = scan(root, on_progress, /*force=*/true);
        stats.scanned += s.scanned;
        stats.added += s.added;
        stats.updated += s.updated;
        stats.skipped += s.skipped;
        stats.failed += s.failed;
    }

    // Drop rows whose file has vanished — but only under a root we just
    // confirmed is mounted, so an unmounted drive keeps its catalog. Tracks
    // under no known root are left untouched.
    auto under = [](const std::string& path, const std::string& root) {
        if (path.size() <= root.size()) return false;
        if (path.compare(0, root.size(), root) != 0) return false;
        // Guard against /music matching /music-extra: require a separator.
        const char sep = path[root.size()];
        return root.back() == '/' || sep == '/';
    };

    sqlite3_exec(db_.handle(), "BEGIN", nullptr, nullptr, nullptr);
    for (const auto& [id, path_str] : db_.all_track_paths()) {
        bool in_accessible_root = false;
        for (const auto& root : accessible) {
            if (under(path_str, root)) { in_accessible_root = true; break; }
        }
        if (!in_accessible_root) continue;
        std::error_code ec;
        if (!fs::exists(path_str, ec)) {
            // The file is gone from a mounted root. If the same filename now
            // lives elsewhere in the library (scan() just added it under a new
            // row), the file was moved/renamed — carry any playlist entries
            // over to the new row before dropping the stale one.
            const std::string base = fs::path(path_str).filename().string();
            if (auto moved = db_.track_id_for_basename(base, /*exclude_id=*/id)) {
                db_.repoint_playlist_tracks(id, *moved);
            }
            if (db_.delete_track(id)) ++stats.removed;
        }
    }
    sqlite3_exec(db_.handle(), "COMMIT", nullptr, nullptr, nullptr);

    db_.prune_orphans();
    db_.reconcile_playlists();
    return stats;
}

}  // namespace auralbit::library
