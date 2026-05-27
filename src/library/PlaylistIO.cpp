#include "PlaylistIO.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "Database.h"

namespace auralbit::library {

namespace {

namespace fs = std::filesystem;

std::string toLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

std::string trim(std::string s) {
    auto not_space = [](unsigned char c) { return !std::isspace(c); };
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), not_space));
    s.erase(std::find_if(s.rbegin(), s.rend(), not_space).base(), s.end());
    return s;
}

std::string resolvePath(const std::string& base_dir, const std::string& entry) {
    fs::path p(entry);
    if (p.is_absolute()) return p.lexically_normal().string();
    return (fs::path(base_dir) / p).lexically_normal().string();
}

// Resolves one playlist entry to a library track id, updating the result
// counters. Tries the exact resolved path first; if that misses, falls back to
// a unique-filename match so a playlist written before the library was
// reorganized still links up. Returns nullopt only when nothing matches.
std::optional<int64_t> resolveEntry(Database& db, const std::string& base_dir,
                                    const std::string& entry, ImportResult& r) {
    const std::string resolved = resolvePath(base_dir, entry);
    if (auto id = db.track_id_for_path(resolved)) {
        ++r.matched;
        return id;
    }
    const std::string base = fs::path(resolved).filename().string();
    if (auto id = db.track_id_for_basename(base)) {
        ++r.relinked;
        return id;
    }
    ++r.missing;
    return std::nullopt;
}

// Picks an unused playlist name based on `suggested`, appending " (N)" until
// a free slot is found. Caller-side check (via create_playlist returning 0)
// covers the race; this just makes the first attempt likely to succeed.
std::string uniqueName(Database& db, const std::string& suggested) {
    auto existing = db.all_playlists();
    auto used = [&](const std::string& n) {
        for (const auto& p : existing) if (p.name == n) return true;
        return false;
    };
    if (!used(suggested)) return suggested;
    for (int i = 2; i < 1000; ++i) {
        const std::string candidate = suggested + " (" + std::to_string(i) + ")";
        if (!used(candidate)) return candidate;
    }
    return suggested + " (new)";
}

ImportResult import_m3u(Database& db, const std::string& file_path) {
    ImportResult r;
    std::ifstream in(file_path);
    if (!in) { r.error = "cannot open file"; return r; }

    const std::string base_dir = fs::path(file_path).parent_path().string();
    const std::string name = uniqueName(db, fs::path(file_path).stem().string());

    const int64_t pid = db.create_playlist(name);
    if (!pid) { r.error = "could not create playlist"; return r; }

    std::vector<int64_t> ids;
    std::string line;
    while (std::getline(in, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;
        if (auto id = resolveEntry(db, base_dir, line, r)) ids.push_back(*id);
    }
    db.set_playlist_positions(pid, ids);
    r.ok = true;
    r.playlist_id = pid;
    return r;
}

ImportResult import_pls(Database& db, const std::string& file_path) {
    ImportResult r;
    std::ifstream in(file_path);
    if (!in) { r.error = "cannot open file"; return r; }

    const std::string base_dir = fs::path(file_path).parent_path().string();
    const std::string name = uniqueName(db, fs::path(file_path).stem().string());

    // PLS keys: FileN=<path>. We collect by index then walk in order.
    std::vector<std::pair<int, std::string>> entries;
    std::string line;
    while (std::getline(in, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '[' || line[0] == '#') continue;
        const std::string lower = toLower(line);
        if (lower.rfind("file", 0) != 0) continue;
        const auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = line.substr(0, eq);
        // Trailing digits after "File" are the index.
        std::string num;
        for (size_t i = 4; i < key.size(); ++i) {
            if (std::isdigit(static_cast<unsigned char>(key[i]))) num += key[i];
            else break;
        }
        if (num.empty()) continue;
        entries.emplace_back(std::stoi(num), trim(line.substr(eq + 1)));
    }
    std::sort(entries.begin(), entries.end(),
              [](auto& a, auto& b) { return a.first < b.first; });

    const int64_t pid = db.create_playlist(name);
    if (!pid) { r.error = "could not create playlist"; return r; }

    std::vector<int64_t> ids;
    for (const auto& [idx, path] : entries) {
        if (auto id = resolveEntry(db, base_dir, path, r)) ids.push_back(*id);
    }
    db.set_playlist_positions(pid, ids);
    r.ok = true;
    r.playlist_id = pid;
    return r;
}

ExportResult export_m3u(Database& db, int64_t playlist_id, const std::string& file_path) {
    ExportResult r;
    std::ofstream out(file_path);
    if (!out) { r.error = "cannot write file"; return r; }

    out << "#EXTM3U\n";
    for (const auto& t : db.tracks_for_playlist(playlist_id)) {
        const int seconds = static_cast<int>(t.duration_ms / 1000);
        out << "#EXTINF:" << seconds << "," << t.title << "\n";
        out << t.path << "\n";
        ++r.written;
    }
    r.ok = static_cast<bool>(out);
    if (!r.ok) r.error = "write failed";
    return r;
}

ExportResult export_pls(Database& db, int64_t playlist_id, const std::string& file_path) {
    ExportResult r;
    std::ofstream out(file_path);
    if (!out) { r.error = "cannot write file"; return r; }

    const auto tracks = db.tracks_for_playlist(playlist_id);
    out << "[playlist]\n";
    out << "NumberOfEntries=" << tracks.size() << "\n";
    for (size_t i = 0; i < tracks.size(); ++i) {
        const auto& t = tracks[i];
        const int seconds = static_cast<int>(t.duration_ms / 1000);
        out << "File"   << (i + 1) << "=" << t.path  << "\n";
        out << "Title"  << (i + 1) << "=" << t.title << "\n";
        out << "Length" << (i + 1) << "=" << (seconds > 0 ? seconds : -1) << "\n";
        ++r.written;
    }
    out << "Version=2\n";
    r.ok = static_cast<bool>(out);
    if (!r.ok) r.error = "write failed";
    return r;
}

}  // namespace

ImportResult import_playlist(Database& db, const std::string& file_path) {
    const std::string ext = toLower(fs::path(file_path).extension().string());
    if (ext == ".pls") return import_pls(db, file_path);
    // Default to M3U for .m3u, .m3u8, or anything unknown.
    return import_m3u(db, file_path);
}

ExportResult export_playlist(Database& db, int64_t playlist_id,
                             const std::string& file_path) {
    const std::string ext = toLower(fs::path(file_path).extension().string());
    if (ext == ".pls") return export_pls(db, playlist_id, file_path);
    return export_m3u(db, playlist_id, file_path);
}

}  // namespace auralbit::library
