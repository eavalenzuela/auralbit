#pragma once

#include <QMainWindow>
#include <QPersistentModelIndex>
#include <cstdint>
#include <memory>
#include <vector>

class QLineEdit;
class QLabel;
class QMenu;
class QSortFilterProxyModel;
class QStatusBar;
class QSystemTrayIcon;
class QTimer;

namespace auralbit::audio { class Player; }
namespace auralbit::desktop { class MprisAdapter; }
namespace auralbit::library { class Database; }

namespace auralbit::ui {

class LibraryModel;
class LibraryTree;
class PlaylistsModel;
class TransportBar;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

protected:
    void closeEvent(QCloseEvent* event) override;

private slots:
    void onAddFolder();
    void onRescanLibrary();
    void onTrackActivated(const QModelIndex& index);
    void onPlayPauseClicked();
    void onPrevClicked();
    void onNextClicked();
    void onPositionTick();
    void onScanFinished();
    void onTreeContextMenu(const QPoint& pos);
    void onPlaylistsContextMenu(const QPoint& pos);
    void onPlaylistTrackActivated(const QModelIndex& index);

private:
    void buildLibraryTab(QWidget* parent);
    void buildPlaylistsTab(QWidget* parent);
    void buildTrayIcon();
    void buildMpris();
    void publishMprisMetadata(int64_t track_id);
    void reloadPlaylists();

    // Queue helpers.
    void playAlbumFromTrack(int64_t album_id, int64_t start_track_id);
    void playAlbum(int64_t album_id);
    void playArtist(int64_t artist_id);
    void playPlaylist(int64_t playlist_id, int64_t start_track_id = 0);
    void playQueueAt(int index);
    void loadCurrent();
    bool loadCurrentPaused();
    void persistResume();
    void restoreResume();

    // Playlist helpers.
    void promptCreatePlaylist();
    void addTracksToPlaylist(int64_t playlist_id, const std::vector<int64_t>& track_ids);
    void buildAddToPlaylistMenu(QMenu* parent, const std::vector<int64_t>& track_ids);
    std::vector<int64_t> tracksForLibraryIndex(const QModelIndex& idx);
    void refreshHeaderLabel();
    void reloadLibrary();
    void restoreGeometry();
    void persistGeometry();

    std::unique_ptr<library::Database> db_;
    std::unique_ptr<audio::Player> player_;
    desktop::MprisAdapter* mpris_ = nullptr;  // Owned by Qt parent.

    QLabel* header_label_ = nullptr;
    QLineEdit* filter_edit_ = nullptr;
    LibraryTree* tree_ = nullptr;
    LibraryModel* model_ = nullptr;
    QSortFilterProxyModel* filter_proxy_ = nullptr;
    LibraryTree* playlists_tree_ = nullptr;
    PlaylistsModel* playlists_model_ = nullptr;
    TransportBar* transport_ = nullptr;
    QStatusBar* status_ = nullptr;
    QTimer* progress_timer_ = nullptr;
    QSystemTrayIcon* tray_ = nullptr;

    QPersistentModelIndex current_track_;

    std::vector<int64_t> queue_;
    int queue_index_ = -1;
    bool was_playing_ = false;  // Tracks state at last tick for EOF detection.
};

}  // namespace auralbit::ui
