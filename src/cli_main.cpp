#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>

#include "audio/Player.h"
#include "library/Database.h"
#include "library/PlaylistIO.h"
#include "library/Scanner.h"

namespace {

int cmd_play(const char* path) {
    auralbit::audio::Player player;
    if (!player.load(path)) {
        std::fprintf(stderr, "failed to load: %s\n", path);
        return 1;
    }
    std::printf("loaded: %s  duration=%.2fs\n", path, player.duration_seconds());
    player.play();
    while (player.state() != auralbit::audio::PlayerState::Stopped) {
        std::printf("\rpos %.2f / %.2f", player.position_seconds(), player.duration_seconds());
        std::fflush(stdout);
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
    std::printf("\ndone\n");
    return 0;
}

int cmd_scan(const char* root) {
    auralbit::library::Database db;
    const std::string dbpath = auralbit::library::Database::default_path();
    if (!db.open(dbpath)) {
        std::fprintf(stderr, "cannot open db: %s\n", dbpath.c_str());
        return 1;
    }
    std::printf("scanning %s -> %s\n", root, dbpath.c_str());
    db.add_root(root);  // Remembered so a later Rescan can re-walk it.

    auralbit::library::Scanner scanner(db, auralbit::library::Scanner::default_cover_cache_dir());
    int last_print = 0;
    auto stats = scanner.scan(root, [&](const std::string&, const auralbit::library::ScanStats& s) {
        if (s.scanned - last_print >= 25) {
            last_print = static_cast<int>(s.scanned);
            std::printf("\rscanned=%lld added=%lld updated=%lld skipped=%lld failed=%lld",
                        (long long)s.scanned, (long long)s.added, (long long)s.updated,
                        (long long)s.skipped, (long long)s.failed);
            std::fflush(stdout);
        }
    });
    std::printf("\nfinal: scanned=%lld added=%lld updated=%lld skipped=%lld failed=%lld\n",
                (long long)stats.scanned, (long long)stats.added, (long long)stats.updated,
                (long long)stats.skipped, (long long)stats.failed);
    std::printf("total tracks in db: %lld\n", (long long)db.track_count());
    return 0;
}

int cmd_rescan() {
    auralbit::library::Database db;
    const std::string dbpath = auralbit::library::Database::default_path();
    if (!db.open(dbpath)) {
        std::fprintf(stderr, "cannot open db: %s\n", dbpath.c_str());
        return 1;
    }
    auralbit::library::Scanner scanner(db, auralbit::library::Scanner::default_cover_cache_dir());
    auto s = scanner.rescan_all();
    std::printf("rescan: scanned=%lld added=%lld updated=%lld skipped=%lld "
                "removed=%lld failed=%lld\n",
                (long long)s.scanned, (long long)s.added, (long long)s.updated,
                (long long)s.skipped, (long long)s.removed, (long long)s.failed);
    std::printf("total tracks in db: %lld\n", (long long)db.track_count());
    return 0;
}

int cmd_import(const char* file) {
    auralbit::library::Database db;
    if (!db.open(auralbit::library::Database::default_path())) return 1;
    auto r = auralbit::library::import_playlist(db, file);
    if (!r.ok) {
        std::fprintf(stderr, "import failed: %s\n", r.error.c_str());
        return 1;
    }
    std::printf("import: matched=%d relinked=%d missing=%d (playlist id %lld)\n",
                r.matched, r.relinked, r.missing, (long long)r.playlist_id);
    return 0;
}

void print_track(const auralbit::library::TrackRow& r, void* /*user*/) {
    std::printf("  [%lld] %s — %s — %s  (%lldms, %dHz)\n",
                (long long)r.id, r.artist.c_str(), r.album.c_str(), r.title.c_str(),
                (long long)r.duration_ms, r.sample_rate);
}

int cmd_list(int limit) {
    auralbit::library::Database db;
    if (!db.open(auralbit::library::Database::default_path())) return 1;
    std::printf("first %d of %lld tracks:\n", limit, (long long)db.track_count());
    db.list_tracks(limit, &print_track, nullptr);
    return 0;
}

void usage() {
    std::fprintf(stderr,
                 "usage:\n"
                 "  auralbit-cli play <file>      play one audio file\n"
                 "  auralbit-cli scan <folder>    scan folder into the library DB\n"
                 "  auralbit-cli rescan           re-walk known roots, sync with disk\n"
                 "  auralbit-cli import <file>    import an m3u/pls playlist\n"
                 "  auralbit-cli list [N]         list first N tracks (default 20)\n");
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) { usage(); return 1; }

    const std::string cmd = argv[1];
    if (cmd == "play" && argc >= 3) return cmd_play(argv[2]);
    if (cmd == "scan" && argc >= 3) return cmd_scan(argv[2]);
    if (cmd == "rescan")            return cmd_rescan();
    if (cmd == "import" && argc >= 3) return cmd_import(argv[2]);
    if (cmd == "list")              return cmd_list(argc >= 3 ? std::atoi(argv[2]) : 20);

    // Back-compat: a single positional arg is treated as `play`.
    if (argc == 2 && std::strchr(argv[1], '/') != nullptr) return cmd_play(argv[1]);

    usage();
    return 1;
}
