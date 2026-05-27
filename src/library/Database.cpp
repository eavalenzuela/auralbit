#include "Database.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>

#include "Schema.h"
#include "sqlite/sqlite3.h"

namespace auralbit::library {

namespace {

class Stmt {
public:
    Stmt(sqlite3* db, const char* sql) {
        if (sqlite3_prepare_v2(db, sql, -1, &s_, nullptr) != SQLITE_OK) {
            std::fprintf(stderr, "sqlite prepare failed: %s\n", sqlite3_errmsg(db));
        }
    }
    ~Stmt() { if (s_) sqlite3_finalize(s_); }
    sqlite3_stmt* get() const { return s_; }
    operator sqlite3_stmt*() const { return s_; }

private:
    sqlite3_stmt* s_ = nullptr;
};

void bind_text(sqlite3_stmt* s, int idx, std::string_view v) {
    sqlite3_bind_text(s, idx, v.data(), static_cast<int>(v.size()), SQLITE_TRANSIENT);
}

}  // namespace

Database::Database() = default;
Database::~Database() { close(); }

std::string Database::default_path() {
    namespace fs = std::filesystem;
    const char* xdg = std::getenv("XDG_DATA_HOME");
    fs::path dir = xdg && *xdg ? fs::path(xdg) : fs::path(std::getenv("HOME") ? std::getenv("HOME") : "/tmp") / ".local/share";
    dir /= "auralbit";
    std::error_code ec;
    fs::create_directories(dir, ec);
    return (dir / "library.db").string();
}

bool Database::open(const std::string& path) {
    if (sqlite3_open(path.c_str(), &db_) != SQLITE_OK) {
        std::fprintf(stderr, "sqlite open failed: %s\n", sqlite3_errmsg(db_));
        return false;
    }

    if (!exec("PRAGMA journal_mode = WAL;")) return false;
    if (!exec("PRAGMA foreign_keys = ON;")) return false;
    if (!exec("PRAGMA synchronous = NORMAL;")) return false;
    if (!exec(kSchemaSql)) return false;
    if (!exec(("PRAGMA user_version = " + std::to_string(kSchemaVersion) + ";").c_str())) return false;

    return true;
}

void Database::close() {
    if (db_) {
        sqlite3_close(db_);
        db_ = nullptr;
    }
}

bool Database::exec(const char* sql) {
    char* err = nullptr;
    if (sqlite3_exec(db_, sql, nullptr, nullptr, &err) != SQLITE_OK) {
        std::fprintf(stderr, "sqlite exec failed: %s\n", err ? err : "(unknown)");
        sqlite3_free(err);
        return false;
    }
    return true;
}

std::optional<Database::ExistingTrack> Database::find_track(std::string_view path) {
    Stmt s(db_, "SELECT id, mtime, size FROM tracks WHERE path = ?");
    bind_text(s, 1, path);
    if (sqlite3_step(s) == SQLITE_ROW) {
        return ExistingTrack{
            sqlite3_column_int64(s, 0),
            sqlite3_column_int64(s, 1),
            sqlite3_column_int64(s, 2),
        };
    }
    return std::nullopt;
}

int64_t Database::upsert_artist(std::string_view name) {
    if (name.empty()) return 0;
    {
        Stmt s(db_, "SELECT id FROM artists WHERE name = ?");
        bind_text(s, 1, name);
        if (sqlite3_step(s) == SQLITE_ROW) {
            return sqlite3_column_int64(s, 0);
        }
    }
    Stmt ins(db_, "INSERT INTO artists(name) VALUES (?)");
    bind_text(ins, 1, name);
    if (sqlite3_step(ins) != SQLITE_DONE) return 0;
    return sqlite3_last_insert_rowid(db_);
}

int64_t Database::upsert_album(std::string_view name, int64_t artist_id, int year,
                               std::string_view cover_path) {
    if (name.empty()) return 0;
    {
        Stmt s(db_, "SELECT id FROM albums WHERE name = ? AND artist_id IS ?");
        bind_text(s, 1, name);
        if (artist_id) sqlite3_bind_int64(s, 2, artist_id);
        else           sqlite3_bind_null(s, 2);
        if (sqlite3_step(s) == SQLITE_ROW) {
            const int64_t id = sqlite3_column_int64(s, 0);
            if (!cover_path.empty()) {
                Stmt up(db_, "UPDATE albums SET cover_path = COALESCE(NULLIF(cover_path,''), ?) WHERE id = ?");
                bind_text(up, 1, cover_path);
                sqlite3_bind_int64(up, 2, id);
                sqlite3_step(up);
            }
            return id;
        }
    }
    Stmt ins(db_, "INSERT INTO albums(name, artist_id, year, cover_path) VALUES (?, ?, ?, ?)");
    bind_text(ins, 1, name);
    if (artist_id) sqlite3_bind_int64(ins, 2, artist_id);
    else           sqlite3_bind_null(ins, 2);
    if (year > 0)  sqlite3_bind_int(ins, 3, year);
    else           sqlite3_bind_null(ins, 3);
    bind_text(ins, 4, cover_path);
    if (sqlite3_step(ins) != SQLITE_DONE) return 0;
    return sqlite3_last_insert_rowid(db_);
}

bool Database::upsert_track(const ScanRecord& r) {
    const int64_t artist_id = upsert_artist(r.artist);
    const int64_t album_id = upsert_album(r.album, artist_id, r.year, r.cover_path);

    Stmt s(db_,
           "INSERT INTO tracks(path, mtime, size, title, artist_id, album_id, track_no, disc_no, "
           "                   duration_ms, bitrate, sample_rate, channels) "
           "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?) "
           "ON CONFLICT(path) DO UPDATE SET "
           "  mtime=excluded.mtime, size=excluded.size, title=excluded.title, "
           "  artist_id=excluded.artist_id, album_id=excluded.album_id, "
           "  track_no=excluded.track_no, disc_no=excluded.disc_no, "
           "  duration_ms=excluded.duration_ms, bitrate=excluded.bitrate, "
           "  sample_rate=excluded.sample_rate, channels=excluded.channels");
    bind_text(s, 1, r.path);
    sqlite3_bind_int64(s, 2, r.mtime);
    sqlite3_bind_int64(s, 3, r.size);
    bind_text(s, 4, r.title);
    if (artist_id) sqlite3_bind_int64(s, 5, artist_id); else sqlite3_bind_null(s, 5);
    if (album_id)  sqlite3_bind_int64(s, 6, album_id);  else sqlite3_bind_null(s, 6);
    sqlite3_bind_int(s, 7, r.track_no);
    sqlite3_bind_int(s, 8, r.disc_no);
    sqlite3_bind_int64(s, 9, r.duration_ms);
    sqlite3_bind_int(s, 10, r.bitrate);
    sqlite3_bind_int(s, 11, r.sample_rate);
    sqlite3_bind_int(s, 12, r.channels);

    return sqlite3_step(s) == SQLITE_DONE;
}

int64_t Database::track_count() {
    Stmt s(db_, "SELECT COUNT(*) FROM tracks");
    if (sqlite3_step(s) != SQLITE_ROW) return 0;
    return sqlite3_column_int64(s, 0);
}

int64_t Database::artist_count() {
    Stmt s(db_,
           "SELECT COUNT(DISTINCT artist_id) FROM tracks WHERE artist_id IS NOT NULL");
    if (sqlite3_step(s) != SQLITE_ROW) return 0;
    return sqlite3_column_int64(s, 0);
}

std::vector<ArtistAggregate> Database::all_artists() {
    std::vector<ArtistAggregate> out;
    Stmt s(db_,
           "SELECT ar.id, ar.name, COUNT(t.id) "
           "FROM artists ar "
           "LEFT JOIN tracks t ON t.artist_id = ar.id "
           "GROUP BY ar.id "
           "HAVING COUNT(t.id) > 0 "
           "ORDER BY LOWER(ar.name)");
    while (sqlite3_step(s) == SQLITE_ROW) {
        ArtistAggregate a;
        a.id = sqlite3_column_int64(s, 0);
        if (auto* n = sqlite3_column_text(s, 1)) a.name = reinterpret_cast<const char*>(n);
        a.track_count = sqlite3_column_int(s, 2);
        out.push_back(std::move(a));
    }
    return out;
}

std::vector<AlbumAggregate> Database::albums_for_artist(int64_t artist_id) {
    std::vector<AlbumAggregate> out;
    Stmt s(db_,
           "SELECT al.id, al.name, COALESCE(al.year, 0), COALESCE(al.cover_path, ''), "
           "       COUNT(t.id) "
           "FROM albums al "
           "LEFT JOIN tracks t ON t.album_id = al.id "
           "WHERE al.artist_id = ? "
           "GROUP BY al.id "
           "ORDER BY al.year, LOWER(al.name)");
    sqlite3_bind_int64(s, 1, artist_id);
    while (sqlite3_step(s) == SQLITE_ROW) {
        AlbumAggregate a;
        a.id = sqlite3_column_int64(s, 0);
        a.artist_id = artist_id;
        if (auto* n = sqlite3_column_text(s, 1)) a.name = reinterpret_cast<const char*>(n);
        a.year = sqlite3_column_int(s, 2);
        if (auto* c = sqlite3_column_text(s, 3)) a.cover_path = reinterpret_cast<const char*>(c);
        a.track_count = sqlite3_column_int(s, 4);
        out.push_back(std::move(a));
    }
    return out;
}

std::vector<TrackAggregate> Database::tracks_for_album(int64_t album_id) {
    std::vector<TrackAggregate> out;
    Stmt s(db_,
           "SELECT id, COALESCE(artist_id, 0), path, COALESCE(title, ''), "
           "       COALESCE(track_no, 0), COALESCE(disc_no, 0), COALESCE(duration_ms, 0) "
           "FROM tracks WHERE album_id = ? "
           "ORDER BY COALESCE(disc_no, 0), COALESCE(track_no, 0), LOWER(COALESCE(title, ''))");
    sqlite3_bind_int64(s, 1, album_id);
    while (sqlite3_step(s) == SQLITE_ROW) {
        TrackAggregate t;
        t.id = sqlite3_column_int64(s, 0);
        t.album_id = album_id;
        t.artist_id = sqlite3_column_int64(s, 1);
        if (auto* p = sqlite3_column_text(s, 2)) t.path = reinterpret_cast<const char*>(p);
        if (auto* tt = sqlite3_column_text(s, 3)) t.title = reinterpret_cast<const char*>(tt);
        t.track_no = sqlite3_column_int(s, 4);
        t.disc_no = sqlite3_column_int(s, 5);
        t.duration_ms = sqlite3_column_int64(s, 6);
        out.push_back(std::move(t));
    }
    return out;
}

std::optional<std::string> Database::track_path(int64_t id) {
    Stmt s(db_, "SELECT path FROM tracks WHERE id = ?");
    sqlite3_bind_int64(s, 1, id);
    if (sqlite3_step(s) != SQLITE_ROW) return std::nullopt;
    return std::string(reinterpret_cast<const char*>(sqlite3_column_text(s, 0)));
}

std::optional<TrackRow> Database::track_info(int64_t id) {
    Stmt s(db_,
           "SELECT t.id, t.path, t.mtime, t.size, COALESCE(t.title,''), "
           "       COALESCE(ar.name,''), COALESCE(al.name,''), "
           "       COALESCE(t.track_no,0), COALESCE(t.disc_no,0), "
           "       COALESCE(t.duration_ms,0), COALESCE(t.bitrate,0), "
           "       COALESCE(t.sample_rate,0), COALESCE(t.channels,0), "
           "       COALESCE(al.cover_path,'') "
           "FROM tracks t "
           "LEFT JOIN artists ar ON ar.id = t.artist_id "
           "LEFT JOIN albums  al ON al.id = t.album_id "
           "WHERE t.id = ?");
    sqlite3_bind_int64(s, 1, id);
    if (sqlite3_step(s) != SQLITE_ROW) return std::nullopt;
    TrackRow r;
    r.id = sqlite3_column_int64(s, 0);
    r.path = reinterpret_cast<const char*>(sqlite3_column_text(s, 1));
    r.mtime = sqlite3_column_int64(s, 2);
    r.size = sqlite3_column_int64(s, 3);
    r.title = reinterpret_cast<const char*>(sqlite3_column_text(s, 4));
    r.artist = reinterpret_cast<const char*>(sqlite3_column_text(s, 5));
    r.album = reinterpret_cast<const char*>(sqlite3_column_text(s, 6));
    r.track_no = sqlite3_column_int(s, 7);
    r.disc_no = sqlite3_column_int(s, 8);
    r.duration_ms = sqlite3_column_int64(s, 9);
    r.bitrate = sqlite3_column_int(s, 10);
    r.sample_rate = sqlite3_column_int(s, 11);
    r.channels = sqlite3_column_int(s, 12);
    if (auto* c = sqlite3_column_text(s, 13)) {
        r.cover_path = reinterpret_cast<const char*>(c);
    }
    return r;
}

std::vector<PlaylistAggregate> Database::all_playlists() {
    std::vector<PlaylistAggregate> out;
    Stmt s(db_,
           "SELECT p.id, p.name, COUNT(pt.track_id) "
           "FROM playlists p "
           "LEFT JOIN playlist_tracks pt ON pt.playlist_id = p.id "
           "GROUP BY p.id "
           "ORDER BY LOWER(p.name)");
    while (sqlite3_step(s) == SQLITE_ROW) {
        PlaylistAggregate p;
        p.id = sqlite3_column_int64(s, 0);
        if (auto* n = sqlite3_column_text(s, 1)) p.name = reinterpret_cast<const char*>(n);
        p.track_count = sqlite3_column_int(s, 2);
        out.push_back(std::move(p));
    }
    return out;
}

std::vector<TrackAggregate> Database::tracks_for_playlist(int64_t playlist_id) {
    std::vector<TrackAggregate> out;
    Stmt s(db_,
           "SELECT t.id, COALESCE(t.album_id, 0), COALESCE(t.artist_id, 0), "
           "       t.path, COALESCE(t.title, ''), COALESCE(t.track_no, 0), "
           "       COALESCE(t.disc_no, 0), COALESCE(t.duration_ms, 0) "
           "FROM playlist_tracks pt "
           "JOIN tracks t ON t.id = pt.track_id "
           "WHERE pt.playlist_id = ? "
           "ORDER BY pt.position");
    sqlite3_bind_int64(s, 1, playlist_id);
    while (sqlite3_step(s) == SQLITE_ROW) {
        TrackAggregate t;
        t.id = sqlite3_column_int64(s, 0);
        t.album_id = sqlite3_column_int64(s, 1);
        t.artist_id = sqlite3_column_int64(s, 2);
        if (auto* p = sqlite3_column_text(s, 3)) t.path = reinterpret_cast<const char*>(p);
        if (auto* tt = sqlite3_column_text(s, 4)) t.title = reinterpret_cast<const char*>(tt);
        t.track_no = sqlite3_column_int(s, 5);
        t.disc_no = sqlite3_column_int(s, 6);
        t.duration_ms = sqlite3_column_int64(s, 7);
        out.push_back(std::move(t));
    }
    return out;
}

int64_t Database::create_playlist(std::string_view name) {
    if (name.empty()) return 0;
    Stmt s(db_, "INSERT INTO playlists(name, created_at) VALUES (?, strftime('%s','now'))");
    bind_text(s, 1, name);
    if (sqlite3_step(s) != SQLITE_DONE) return 0;
    return sqlite3_last_insert_rowid(db_);
}

bool Database::rename_playlist(int64_t id, std::string_view name) {
    if (name.empty()) return false;
    Stmt s(db_, "UPDATE playlists SET name = ? WHERE id = ?");
    bind_text(s, 1, name);
    sqlite3_bind_int64(s, 2, id);
    return sqlite3_step(s) == SQLITE_DONE;
}

bool Database::delete_playlist(int64_t id) {
    Stmt s(db_, "DELETE FROM playlists WHERE id = ?");
    sqlite3_bind_int64(s, 1, id);
    return sqlite3_step(s) == SQLITE_DONE;
}

bool Database::add_track_to_playlist(int64_t playlist_id, int64_t track_id) {
    int next_pos = 0;
    {
        Stmt s(db_, "SELECT COALESCE(MAX(position), -1) + 1 "
                    "FROM playlist_tracks WHERE playlist_id = ?");
        sqlite3_bind_int64(s, 1, playlist_id);
        if (sqlite3_step(s) == SQLITE_ROW) next_pos = sqlite3_column_int(s, 0);
    }
    Stmt ins(db_, "INSERT INTO playlist_tracks(playlist_id, track_id, position) "
                  "VALUES (?, ?, ?)");
    sqlite3_bind_int64(ins, 1, playlist_id);
    sqlite3_bind_int64(ins, 2, track_id);
    sqlite3_bind_int(ins, 3, next_pos);
    return sqlite3_step(ins) == SQLITE_DONE;
}

bool Database::remove_track_from_playlist(int64_t playlist_id, int64_t track_id) {
    Stmt s(db_, "DELETE FROM playlist_tracks WHERE playlist_id = ? AND track_id = ?");
    sqlite3_bind_int64(s, 1, playlist_id);
    sqlite3_bind_int64(s, 2, track_id);
    return sqlite3_step(s) == SQLITE_DONE;
}

bool Database::set_playlist_positions(int64_t playlist_id,
                                      const std::vector<int64_t>& track_ids) {
    if (sqlite3_exec(db_, "BEGIN", nullptr, nullptr, nullptr) != SQLITE_OK) return false;
    {
        Stmt del(db_, "DELETE FROM playlist_tracks WHERE playlist_id = ?");
        sqlite3_bind_int64(del, 1, playlist_id);
        if (sqlite3_step(del) != SQLITE_DONE) {
            sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
            return false;
        }
    }
    Stmt ins(db_,
             "INSERT INTO playlist_tracks(playlist_id, track_id, position) VALUES (?, ?, ?)");
    for (size_t i = 0; i < track_ids.size(); ++i) {
        sqlite3_reset(ins);
        sqlite3_bind_int64(ins, 1, playlist_id);
        sqlite3_bind_int64(ins, 2, track_ids[i]);
        sqlite3_bind_int(ins, 3, static_cast<int>(i));
        if (sqlite3_step(ins) != SQLITE_DONE) {
            sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
            return false;
        }
    }
    return sqlite3_exec(db_, "COMMIT", nullptr, nullptr, nullptr) == SQLITE_OK;
}

std::optional<int64_t> Database::track_id_for_path(std::string_view path) {
    Stmt s(db_, "SELECT id FROM tracks WHERE path = ?");
    bind_text(s, 1, path);
    if (sqlite3_step(s) != SQLITE_ROW) return std::nullopt;
    return sqlite3_column_int64(s, 0);
}

std::optional<int64_t> Database::track_id_for_basename(std::string_view basename,
                                                       int64_t exclude_id) {
    if (basename.empty()) return std::nullopt;
    // Match paths ending in "/<basename>". Escape LIKE metacharacters so a
    // filename containing % or _ doesn't turn into a wildcard.
    std::string esc;
    esc.reserve(basename.size() + 2);
    for (char c : basename) {
        if (c == '\\' || c == '%' || c == '_') esc.push_back('\\');
        esc.push_back(c);
    }
    const std::string pattern = "%/" + esc;

    Stmt s(db_, "SELECT id FROM tracks WHERE path LIKE ? ESCAPE '\\' AND id <> ?");
    bind_text(s, 1, pattern);
    sqlite3_bind_int64(s, 2, exclude_id);
    std::optional<int64_t> found;
    while (sqlite3_step(s) == SQLITE_ROW) {
        if (found) return std::nullopt;  // More than one match — ambiguous.
        found = sqlite3_column_int64(s, 0);
    }
    return found;
}

bool Database::repoint_playlist_tracks(int64_t from_id, int64_t to_id) {
    Stmt s(db_, "UPDATE playlist_tracks SET track_id = ? WHERE track_id = ?");
    sqlite3_bind_int64(s, 1, to_id);
    sqlite3_bind_int64(s, 2, from_id);
    return sqlite3_step(s) == SQLITE_DONE;
}

std::vector<std::pair<int64_t, std::string>> Database::all_track_paths() {
    std::vector<std::pair<int64_t, std::string>> out;
    Stmt s(db_, "SELECT id, path FROM tracks");
    while (sqlite3_step(s) == SQLITE_ROW) {
        const int64_t id = sqlite3_column_int64(s, 0);
        std::string path;
        if (auto* p = sqlite3_column_text(s, 1)) path = reinterpret_cast<const char*>(p);
        out.emplace_back(id, std::move(path));
    }
    return out;
}

bool Database::delete_track(int64_t id) {
    Stmt s(db_, "DELETE FROM tracks WHERE id = ?");
    sqlite3_bind_int64(s, 1, id);
    return sqlite3_step(s) == SQLITE_DONE;
}

bool Database::add_root(std::string_view path) {
    if (path.empty()) return false;
    Stmt s(db_, "INSERT OR IGNORE INTO library_roots(path, added_at) "
                "VALUES (?, strftime('%s','now'))");
    bind_text(s, 1, path);
    return sqlite3_step(s) == SQLITE_DONE;
}

bool Database::remove_root(std::string_view path) {
    Stmt s(db_, "DELETE FROM library_roots WHERE path = ?");
    bind_text(s, 1, path);
    return sqlite3_step(s) == SQLITE_DONE;
}

std::vector<std::string> Database::all_roots() {
    std::vector<std::string> out;
    Stmt s(db_, "SELECT path FROM library_roots ORDER BY path");
    while (sqlite3_step(s) == SQLITE_ROW) {
        if (auto* p = sqlite3_column_text(s, 0)) {
            out.emplace_back(reinterpret_cast<const char*>(p));
        }
    }
    return out;
}

void Database::clear_library() {
    exec("DELETE FROM tracks");
    exec("DELETE FROM albums");
    exec("DELETE FROM artists");
    exec("DELETE FROM library_roots");
}

void Database::prune_orphans() {
    exec("DELETE FROM albums "
         "WHERE id NOT IN (SELECT DISTINCT album_id FROM tracks WHERE album_id IS NOT NULL)");
    exec("DELETE FROM artists "
         "WHERE id NOT IN (SELECT DISTINCT artist_id FROM tracks WHERE artist_id IS NOT NULL)");
}

void Database::list_tracks(int limit, void (*cb)(const TrackRow&, void*), void* user) {
    Stmt s(db_,
           "SELECT t.id, t.path, t.mtime, t.size, COALESCE(t.title,''), "
           "       COALESCE(ar.name,''), COALESCE(al.name,''), "
           "       COALESCE(t.track_no,0), COALESCE(t.disc_no,0), "
           "       COALESCE(t.duration_ms,0), COALESCE(t.bitrate,0), "
           "       COALESCE(t.sample_rate,0), COALESCE(t.channels,0) "
           "FROM tracks t "
           "LEFT JOIN artists ar ON ar.id = t.artist_id "
           "LEFT JOIN albums  al ON al.id = t.album_id "
           "ORDER BY ar.name, al.name, t.disc_no, t.track_no LIMIT ?");
    sqlite3_bind_int(s, 1, limit);
    while (sqlite3_step(s) == SQLITE_ROW) {
        TrackRow r;
        r.id = sqlite3_column_int64(s, 0);
        r.path = reinterpret_cast<const char*>(sqlite3_column_text(s, 1));
        r.mtime = sqlite3_column_int64(s, 2);
        r.size = sqlite3_column_int64(s, 3);
        r.title = reinterpret_cast<const char*>(sqlite3_column_text(s, 4));
        r.artist = reinterpret_cast<const char*>(sqlite3_column_text(s, 5));
        r.album = reinterpret_cast<const char*>(sqlite3_column_text(s, 6));
        r.track_no = sqlite3_column_int(s, 7);
        r.disc_no = sqlite3_column_int(s, 8);
        r.duration_ms = sqlite3_column_int64(s, 9);
        r.bitrate = sqlite3_column_int(s, 10);
        r.sample_rate = sqlite3_column_int(s, 11);
        r.channels = sqlite3_column_int(s, 12);
        cb(r, user);
    }
}

}  // namespace auralbit::library
