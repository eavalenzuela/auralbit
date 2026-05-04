#pragma once

#include <QMainWindow>
#include <QPersistentModelIndex>
#include <cstdint>
#include <memory>
#include <vector>

class QLineEdit;
class QLabel;
class QStatusBar;
class QTimer;

namespace auralbit::audio { class Player; }
namespace auralbit::library { class Database; }

namespace auralbit::ui {

class LibraryModel;
class LibraryTree;
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
    void onTrackActivated(const QModelIndex& index);
    void onPlayPauseClicked();
    void onPrevClicked();
    void onNextClicked();
    void onPositionTick();
    void onScanFinished();
    void onTreeContextMenu(const QPoint& pos);

private:
    void buildLibraryTab(QWidget* parent);
    void playAlbumFromTrack(int64_t album_id, int64_t start_track_id);
    void playAlbum(int64_t album_id);
    void playArtist(int64_t artist_id);
    void playQueueAt(int index);
    void loadCurrent();
    void refreshHeaderLabel();
    void reloadLibrary();
    void restoreGeometry();
    void persistGeometry();

    std::unique_ptr<library::Database> db_;
    std::unique_ptr<audio::Player> player_;

    QLabel* header_label_ = nullptr;
    QLineEdit* filter_edit_ = nullptr;
    LibraryTree* tree_ = nullptr;
    LibraryModel* model_ = nullptr;
    TransportBar* transport_ = nullptr;
    QStatusBar* status_ = nullptr;
    QTimer* progress_timer_ = nullptr;

    QPersistentModelIndex current_track_;

    std::vector<int64_t> queue_;
    int queue_index_ = -1;
    bool was_playing_ = false;  // Tracks state at last tick for EOF detection.
};

}  // namespace auralbit::ui
