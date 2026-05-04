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
