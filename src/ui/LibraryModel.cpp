#include "LibraryModel.h"

#include <QBrush>
#include <QColor>

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

LibraryModel::LibraryModel(QObject* parent) : QStandardItemModel(parent) {
    setColumnCount(2);
}

void LibraryModel::reload(library::Database& db) {
    clear();
    setColumnCount(2);

    const auto artists = db.all_artists();
    artist_count_ = static_cast<int>(artists.size());
    track_count_ = 0;

    const QBrush primary(QColor("#e6e6e6"));
    const QBrush albumColor(QColor("#d8dade"));
    const QBrush trackColor(QColor("#c8cad0"));
    const QBrush mutedColor(QColor("#7a7c84"));

    for (const auto& a : artists) {
        const QString artistName = QString::fromStdString(a.name);
        auto* artistItem = new QStandardItem(artistName);
        artistItem->setForeground(primary);
        artistItem->setData(static_cast<int>(RowKind::Artist), roles::Kind);
        artistItem->setData(QVariant::fromValue<qlonglong>(a.id), roles::EntityId);
        artistItem->setData(artistName, roles::FilterText);
        QStandardItem* artistRight = new QStandardItem();
        invisibleRootItem()->appendRow({artistItem, artistRight});

        const auto albums = db.albums_for_artist(a.id);
        for (const auto& al : albums) {
            const QString albumName = QString::fromStdString(al.name);
            auto* albumItem = new QStandardItem(albumName);
            albumItem->setForeground(albumColor);
            albumItem->setData(static_cast<int>(RowKind::Album), roles::Kind);
            albumItem->setData(QVariant::fromValue<qlonglong>(al.id), roles::EntityId);
            albumItem->setData(artistName + " " + albumName, roles::FilterText);

            auto* albumRight = new QStandardItem(QString::number(al.track_count));
            albumRight->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
            albumRight->setForeground(mutedColor);

            artistItem->appendRow({albumItem, albumRight});

            const auto tracks = db.tracks_for_album(al.id);
            for (const auto& t : tracks) {
                ++track_count_;
                const QString display = t.title.empty()
                                            ? QString::fromStdString(t.path)
                                            : QString::fromStdString(t.title);
                auto* trackItem = new QStandardItem(display);
                trackItem->setForeground(trackColor);
                trackItem->setData(static_cast<int>(RowKind::Track), roles::Kind);
                trackItem->setData(QVariant::fromValue<qlonglong>(t.id), roles::EntityId);
                trackItem->setData(QString::fromStdString(t.path), roles::TrackPath);
                trackItem->setData(QVariant::fromValue<qlonglong>(t.duration_ms),
                                   roles::DurationMs);
                trackItem->setData(artistName + " " + albumName + " " + display,
                                   roles::FilterText);

                auto* timeItem = new QStandardItem(formatDuration(t.duration_ms));
                timeItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
                timeItem->setForeground(mutedColor);
                timeItem->setData(static_cast<int>(RowKind::Track), roles::Kind);

                albumItem->appendRow({trackItem, timeItem});
            }
        }
    }
}

QModelIndex LibraryModel::indexForTrackId(qint64 id) const {
    for (int ai = 0; ai < rowCount(); ++ai) {
        QStandardItem* artist = item(ai, 0);
        if (!artist) continue;
        for (int li = 0; li < artist->rowCount(); ++li) {
            QStandardItem* album = artist->child(li, 0);
            if (!album) continue;
            for (int ti = 0; ti < album->rowCount(); ++ti) {
                QStandardItem* track = album->child(ti, 0);
                if (track && track->data(roles::EntityId).toLongLong() == id) {
                    return track->index();
                }
            }
        }
    }
    return {};
}

}  // namespace auralbit::ui
