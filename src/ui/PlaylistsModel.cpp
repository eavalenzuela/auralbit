#include "PlaylistsModel.h"

#include <QBrush>
#include <QColor>

#include "LibraryModel.h"
#include "library/Database.h"

namespace auralbit::ui {

namespace {

QString formatDuration(int64_t ms) {
    if (ms <= 0) return {};
    const int total = static_cast<int>(ms / 1000);
    const int m = total / 60;
    const int s = total % 60;
    return QString("%1:%2").arg(m).arg(s, 2, 10, QChar('0'));
}

}  // namespace

PlaylistsModel::PlaylistsModel(QObject* parent) : QStandardItemModel(parent) {
    setColumnCount(2);
}

void PlaylistsModel::reload(library::Database& db) {
    clear();
    setColumnCount(2);

    const auto playlists = db.all_playlists();
    playlist_count_ = static_cast<int>(playlists.size());

    const QBrush primary(QColor("#e6e6e6"));
    const QBrush trackColor(QColor("#c8cad0"));
    const QBrush mutedColor(QColor("#7a7c84"));

    // Flag bitsets: tracks are draggable but never droppable; playlists accept
    // drops (so reorder lands among that playlist's children) but are not
    // themselves draggable.
    const Qt::ItemFlags playlist_flags =
        Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsDropEnabled;
    const Qt::ItemFlags track_flags =
        Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsDragEnabled;

    for (const auto& p : playlists) {
        auto* plItem = new QStandardItem(QString::fromStdString(p.name));
        plItem->setForeground(primary);
        plItem->setData(static_cast<int>(RowKind::Playlist), roles::Kind);
        plItem->setData(QVariant::fromValue<qlonglong>(p.id), roles::EntityId);
        plItem->setFlags(playlist_flags);

        auto* plRight = new QStandardItem(QString::number(p.track_count));
        plRight->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        plRight->setForeground(mutedColor);
        plRight->setFlags(playlist_flags);

        invisibleRootItem()->appendRow({plItem, plRight});

        const auto tracks = db.tracks_for_playlist(p.id);
        for (const auto& t : tracks) {
            const QString display = t.title.empty() ? QString::fromStdString(t.path)
                                                    : QString::fromStdString(t.title);
            auto* trackItem = new QStandardItem(display);
            trackItem->setForeground(trackColor);
            trackItem->setData(static_cast<int>(RowKind::Track), roles::Kind);
            trackItem->setData(QVariant::fromValue<qlonglong>(t.id), roles::EntityId);
            trackItem->setData(QString::fromStdString(t.path), roles::TrackPath);
            trackItem->setData(QVariant::fromValue<qlonglong>(t.duration_ms),
                               roles::DurationMs);
            trackItem->setFlags(track_flags);

            auto* timeItem = new QStandardItem(formatDuration(t.duration_ms));
            timeItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
            timeItem->setForeground(mutedColor);
            timeItem->setData(static_cast<int>(RowKind::Track), roles::Kind);
            timeItem->setFlags(track_flags);

            plItem->appendRow({trackItem, timeItem});
        }
    }
}

QStringList PlaylistsModel::orderedTrackIdsFor(const QModelIndex& playlist_index) const {
    QStringList out;
    if (!playlist_index.isValid()) return out;
    const QModelIndex pl = playlist_index.sibling(playlist_index.row(), 0);
    QStandardItem* item = itemFromIndex(pl);
    if (!item) return out;
    for (int r = 0; r < item->rowCount(); ++r) {
        QStandardItem* tr = item->child(r, 0);
        if (tr) out << QString::number(tr->data(roles::EntityId).toLongLong());
    }
    return out;
}

QModelIndex PlaylistsModel::indexForTrackId(qint64 id) const {
    for (int pi = 0; pi < rowCount(); ++pi) {
        QStandardItem* pl = item(pi, 0);
        if (!pl) continue;
        for (int ti = 0; ti < pl->rowCount(); ++ti) {
            QStandardItem* track = pl->child(ti, 0);
            if (track && track->data(roles::EntityId).toLongLong() == id) {
                return track->index();
            }
        }
    }
    return {};
}

}  // namespace auralbit::ui
