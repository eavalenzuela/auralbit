#pragma once

#include <QStandardItemModel>

namespace auralbit::library {
class Database;
}

namespace auralbit::ui {

// Custom data roles attached to model items so the UI can recover the
// underlying entity (artist/album/track id, file path, kind).
namespace roles {
constexpr int Kind = Qt::UserRole + 1;        // RowKind
constexpr int EntityId = Qt::UserRole + 2;    // int64
constexpr int TrackPath = Qt::UserRole + 3;   // QString
constexpr int DurationMs = Qt::UserRole + 4;  // qlonglong
}  // namespace roles

enum class RowKind { Artist, Album, Track, Playlist };

class LibraryModel : public QStandardItemModel {
    Q_OBJECT
public:
    explicit LibraryModel(QObject* parent = nullptr);

    // Rebuild from the DB. Caller must keep `db` alive for the duration.
    // After reload the model has 2 columns (name/title, count/duration) and
    // a hierarchy of Artist > Album > Track.
    void reload(library::Database& db);

    // Returns aggregate counts (handy for the header strip).
    int trackCount() const { return track_count_; }
    int artistCount() const { return artist_count_; }

    // Locate the model index for a given track id. Returns invalid if absent
    // OR if the album is not yet realised in the model. The model lazily
    // populates albums on artist expansion to keep big libraries snappy —
    // for now we eagerly load everything; switch to lazy when libraries grow.
    QModelIndex indexForTrackId(qint64 id) const;

private:
    int track_count_ = 0;
    int artist_count_ = 0;
};

}  // namespace auralbit::ui
