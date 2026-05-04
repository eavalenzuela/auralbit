#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

struct sqlite3;
struct sqlite3_stmt;

namespace auralbit::library {

struct TrackRow {
    int64_t id = 0;
    std::string path;
    int64_t mtime = 0;
    int64_t size = 0;
    std::string title;
    std::string artist;  // Resolved from artists table.
    std::string album;   // Resolved from albums table.
    int track_no = 0;
    int disc_no = 0;
    int64_t duration_ms = 0;
    int bitrate = 0;
    int sample_rate = 0;
    int channels = 0;
};

struct PlaylistAggregate {
    int64_t id = 0;
    std::string name;
    int track_count = 0;
};

struct ArtistAggregate {
    int64_t id = 0;
    std::string name;
    int track_count = 0;
};

struct AlbumAggregate {
    int64_t id = 0;
    int64_t artist_id = 0;
    std::string name;
    int year = 0;
    std::string cover_path;
    int track_count = 0;
};

struct TrackAggregate {
    int64_t id = 0;
    int64_t album_id = 0;
    int64_t artist_id = 0;
    std::string path;
    std::string title;
    int track_no = 0;
    int disc_no = 0;
    int64_t duration_ms = 0;
};

struct ScanRecord {
    std::string path;
    int64_t mtime = 0;
    int64_t size = 0;
    std::string title;
    std::string artist;
    std::string album;
    int year = 0;
    int track_no = 0;
    int disc_no = 0;
    int64_t duration_ms = 0;
    int bitrate = 0;
    int sample_rate = 0;
    int channels = 0;
    std::string cover_path;  // Optional; written to albums.cover_path if non-empty.
};

class Database {
public:
    Database();
    ~Database();

    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;

    // Opens the DB file, applying migrations. Returns false on failure.
    bool open(const std::string& path);
    void close();

    // Default location: ~/.local/share/auralbit/library.db. Creates the dir.
    static std::string default_path();

    // Returns existing track id and (mtime,size) so the scanner can decide to skip.
    struct ExistingTrack {
        int64_t id;
        int64_t mtime;
        int64_t size;
    };
    std::optional<ExistingTrack> find_track(std::string_view path);

    // Inserts or updates a track. Resolves artist/album lookups, creating rows as needed.
    bool upsert_track(const ScanRecord& r);

    // Total row count (debug/CLI).
    int64_t track_count();
    int64_t artist_count();

    // Run a callback for the first N tracks (debug/CLI).
    void list_tracks(int limit, void (*cb)(const TrackRow&, void*), void* user);

    // Tree-building queries: return entire library sorted alphabetically.
    std::vector<ArtistAggregate> all_artists();
    std::vector<AlbumAggregate> albums_for_artist(int64_t artist_id);
    std::vector<TrackAggregate> tracks_for_album(int64_t album_id);

    // Lookup the on-disk path of a track by id (used to load into the Player).
    std::optional<std::string> track_path(int64_t id);

    // ---- Playlists ----
    std::vector<PlaylistAggregate> all_playlists();
    std::vector<TrackAggregate> tracks_for_playlist(int64_t playlist_id);

    // Returns the new playlist id, or 0 on failure (e.g. duplicate name).
    int64_t create_playlist(std::string_view name);
    bool rename_playlist(int64_t id, std::string_view name);
    bool delete_playlist(int64_t id);

    // Appends a track at the end of the playlist (next available position).
    bool add_track_to_playlist(int64_t playlist_id, int64_t track_id);
    bool remove_track_from_playlist(int64_t playlist_id, int64_t track_id);

    // Replaces the playlist's track ordering with the given sequence.
    bool set_playlist_positions(int64_t playlist_id, const std::vector<int64_t>& track_ids);

    // Path → track id lookup (for M3U/PLS import).
    std::optional<int64_t> track_id_for_path(std::string_view path);

    // All tracks as (id, path). Used by the rescan flow.
    std::vector<std::pair<int64_t, std::string>> all_track_paths();

    // Drops any artists/albums with zero remaining tracks.
    void prune_orphans();

    sqlite3* handle() { return db_; }

private:
    bool exec(const char* sql);
    int64_t upsert_artist(std::string_view name);
    int64_t upsert_album(std::string_view name, int64_t artist_id, int year,
                         std::string_view cover_path);

    sqlite3* db_ = nullptr;
};

}  // namespace auralbit::library
