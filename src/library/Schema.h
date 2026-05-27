#pragma once

namespace auralbit::library {

// Bumped on any incompatible schema change. The DB wrapper runs migrations
// up to this version on open.
constexpr int kSchemaVersion = 2;

constexpr const char* kSchemaSql = R"SQL(
CREATE TABLE IF NOT EXISTS artists (
    id     INTEGER PRIMARY KEY,
    name   TEXT NOT NULL UNIQUE
);

CREATE TABLE IF NOT EXISTS albums (
    id         INTEGER PRIMARY KEY,
    name       TEXT NOT NULL,
    artist_id  INTEGER REFERENCES artists(id) ON DELETE SET NULL,
    year       INTEGER,
    cover_path TEXT,
    UNIQUE(name, artist_id)
);

CREATE TABLE IF NOT EXISTS tracks (
    id           INTEGER PRIMARY KEY,
    path         TEXT NOT NULL UNIQUE,
    mtime        INTEGER NOT NULL,
    size         INTEGER NOT NULL,
    title        TEXT,
    artist_id    INTEGER REFERENCES artists(id) ON DELETE SET NULL,
    album_id     INTEGER REFERENCES albums(id)  ON DELETE SET NULL,
    track_no     INTEGER,
    disc_no      INTEGER,
    duration_ms  INTEGER,
    bitrate      INTEGER,
    sample_rate  INTEGER,
    channels     INTEGER
);

CREATE INDEX IF NOT EXISTS idx_tracks_album  ON tracks(album_id);
CREATE INDEX IF NOT EXISTS idx_tracks_artist ON tracks(artist_id);

CREATE TABLE IF NOT EXISTS playlists (
    id         INTEGER PRIMARY KEY,
    name       TEXT NOT NULL UNIQUE,
    created_at INTEGER NOT NULL
);

CREATE TABLE IF NOT EXISTS playlist_tracks (
    playlist_id INTEGER NOT NULL REFERENCES playlists(id) ON DELETE CASCADE,
    track_id    INTEGER NOT NULL REFERENCES tracks(id)    ON DELETE CASCADE,
    position    INTEGER NOT NULL,
    PRIMARY KEY (playlist_id, position)
);

CREATE INDEX IF NOT EXISTS idx_playlist_tracks_track ON playlist_tracks(track_id);

-- Folders the user added to the library. Rescan re-walks these to reconcile
-- moved/renamed/deleted files against what's on disk.
CREATE TABLE IF NOT EXISTS library_roots (
    id       INTEGER PRIMARY KEY,
    path     TEXT NOT NULL UNIQUE,
    added_at INTEGER NOT NULL
);
)SQL";

}  // namespace auralbit::library
