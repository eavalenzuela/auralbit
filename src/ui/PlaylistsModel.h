#pragma once

#include <QStandardItemModel>

namespace auralbit::library {
class Database;
}

namespace auralbit::ui {

// Two-level tree: playlists at top, tracks underneath. Uses the same custom
// roles (Kind, EntityId, TrackPath, DurationMs) defined in LibraryModel.h so
// the same LibraryTree can render the progress wash on either tab.
class PlaylistsModel : public QStandardItemModel {
    Q_OBJECT
public:
    explicit PlaylistsModel(QObject* parent = nullptr);

    void reload(library::Database& db);

    int playlistCount() const { return playlist_count_; }

    // Locate the model index for a track id appearing in any playlist.
    QModelIndex indexForTrackId(qint64 id) const;

    // Returns the current track-id sequence under the given playlist index
    // (column 0). Used to push the post-drag order to the database.
    QStringList orderedTrackIdsFor(const QModelIndex& playlist_index) const;

private:
    int playlist_count_ = 0;
};

}  // namespace auralbit::ui
