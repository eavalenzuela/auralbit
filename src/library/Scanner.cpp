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

ScanStats Scanner::scan(const std::string& root, ProgressFn on_progress) {
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
        if (existing && existing->mtime == mtime_ns &&
            existing->size == static_cast<int64_t>(file_size)) {
            ++stats.skipped;
            if (on_progress) on_progress(path_str, stats);
            continue;
        }

        TagLib::FileRef ref(path_str.c_str(), true, TagLib::AudioProperties::Average);
        if (ref.isNull() || !ref.tag()) {
            ++stats.failed;
            continue;
        }

        ScanRecord rec;
        rec.path = path_str;
        rec.mtime = mtime_ns;
        rec.size = static_cast<int64_t>(file_size);

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
                const auto s = list.front().to8Bit(true);
                rec.disc_no = std::atoi(s.c_str());
            }
        }

        if (const auto* ap = ref.audioProperties()) {
            rec.duration_ms = ap->lengthInMilliseconds();
            rec.bitrate = ap->bitrate();
            rec.sample_rate = ap->sampleRate();
            rec.channels = ap->channels();
        }

        // Cover art: key by "artist|album" so two albums with the same name
        // from different artists don't collide.
        if (!rec.album.empty()) {
            const std::string album_key = rec.artist + "|" + rec.album;
            rec.cover_path = extract_cover(ref, cover_cache_dir_, album_key);
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
    return stats;
}

ScanStats Scanner::rescan_all(ProgressFn on_progress) {
    ScanStats stats;
    const auto rows = db_.all_track_paths();

    sqlite3_exec(db_.handle(), "BEGIN", nullptr, nullptr, nullptr);

    for (const auto& [id, path_str] : rows) {
        ++stats.scanned;
        std::error_code ec;
        const fs::path p(path_str);
        if (!fs::exists(p, ec) || !fs::is_regular_file(p, ec)) {
            // Don't drop the row — the drive might just be unmounted.
            ++stats.failed;
            if (on_progress) on_progress(path_str, stats);
            continue;
        }
        const auto file_size = fs::file_size(p, ec);
        const auto mtime = fs::last_write_time(p, ec);
        const int64_t mtime_ns =
            std::chrono::duration_cast<std::chrono::nanoseconds>(mtime.time_since_epoch())
                .count();

        TagLib::FileRef ref(path_str.c_str(), true, TagLib::AudioProperties::Average);
        if (ref.isNull() || !ref.tag()) {
            ++stats.failed;
            if (on_progress) on_progress(path_str, stats);
            continue;
        }

        ScanRecord rec;
        rec.path = path_str;
        rec.mtime = mtime_ns;
        rec.size = static_cast<int64_t>(file_size);

        const auto* tag = ref.tag();
        rec.title = charset::maybe_recover_cjk(tag->title().to8Bit(true));
        rec.artist = charset::maybe_recover_cjk(tag->artist().to8Bit(true));
        rec.album = charset::maybe_recover_cjk(tag->album().to8Bit(true));
        rec.year = static_cast<int>(tag->year());
        rec.track_no = static_cast<int>(tag->track());

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

        if (!rec.album.empty()) {
            const std::string album_key = rec.artist + "|" + rec.album;
            rec.cover_path = extract_cover(ref, cover_cache_dir_, album_key);
        }

        if (db_.upsert_track(rec)) ++stats.updated;
        else                       ++stats.failed;

        if (on_progress) on_progress(path_str, stats);
    }

    sqlite3_exec(db_.handle(), "COMMIT", nullptr, nullptr, nullptr);
    db_.prune_orphans();
    return stats;
}

}  // namespace auralbit::library
