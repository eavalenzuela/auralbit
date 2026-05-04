#include "MainWindow.h"

#include <thread>

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMetaObject>
#include <QPainter>
#include <QPixmap>
#include <QScreen>
#include <QSettings>
#include <QStatusBar>
#include <QSystemTrayIcon>
#include <QTabBar>
#include <QTabWidget>
#include <QTimer>
#include <QVBoxLayout>

#include "LibraryModel.h"
#include "LibraryTree.h"
#include "PlaylistsModel.h"
#include "TransportBar.h"
#include "audio/Player.h"
#include "SyncDialog.h"
#include "desktop/MprisAdapter.h"
#include "library/Database.h"
#include "library/PlaylistIO.h"
#include "library/Scanner.h"
#include "sync/MountedFsTarget.h"

#include <QDBusObjectPath>

#include <QInputDialog>
#include <QMessageBox>

namespace auralbit::ui {

namespace {
constexpr const char* kGeomKey = "window/geometry";
constexpr const char* kStateKey = "window/state";

// QTabBar that distributes the available width equally across all tabs.
// Native QTabBar packs tabs to the left at their content width, which leaves
// dead space when there are only a few tabs in a wide window.
class EquallyDividedTabBar : public QTabBar {
public:
    using QTabBar::QTabBar;

protected:
    QSize tabSizeHint(int index) const override {
        const QSize base = QTabBar::tabSizeHint(index);
        const QWidget* p = parentWidget();
        if (p && count() > 0 && p->width() > 0) {
            return QSize(p->width() / count(), base.height());
        }
        return base;
    }
};

// QTabWidget exposes setTabBar() only to subclasses, so this trivial wrapper
// installs our equally-divided tab bar at construction time.
class EquallyDividedTabWidget : public QTabWidget {
public:
    explicit EquallyDividedTabWidget(QWidget* parent = nullptr) : QTabWidget(parent) {
        setTabBar(new EquallyDividedTabBar());
    }
};
}  // namespace

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent),
      db_(std::make_unique<library::Database>()),
      player_(std::make_unique<audio::Player>()) {
    setWindowTitle("auralbit");

    if (!db_->open(library::Database::default_path())) {
        // Fatal — we have nowhere to store the library.
        qFatal("auralbit: cannot open library database");
    }

    auto* central = new QWidget(this);
    auto* root = new QVBoxLayout(central);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    auto* tabs = new EquallyDividedTabWidget(central);
    tabs->setDocumentMode(true);
    tabs->setTabPosition(QTabWidget::North);

    auto* libraryPage = new QWidget(tabs);
    buildLibraryTab(libraryPage);
    tabs->addTab(libraryPage, "Library");

    auto* playlistsPage = new QWidget(tabs);
    buildPlaylistsTab(playlistsPage);
    tabs->addTab(playlistsPage, "Playlists");

    root->addWidget(tabs, 1);

    transport_ = new TransportBar(central);
    transport_->setPlayer(player_.get());
    root->addWidget(transport_);

    setCentralWidget(central);

    status_ = statusBar();
    status_->setSizeGripEnabled(false);

    reloadLibrary();
    reloadPlaylists();
    restoreResume();

    progress_timer_ = new QTimer(this);
    progress_timer_->setInterval(100);
    connect(progress_timer_, &QTimer::timeout, this, &MainWindow::onPositionTick);
    progress_timer_->start();

    buildTrayIcon();
    buildMpris();

    connect(transport_, &TransportBar::playPauseClicked, this, &MainWindow::onPlayPauseClicked);
    connect(transport_, &TransportBar::prevClicked, this, &MainWindow::onPrevClicked);
    connect(transport_, &TransportBar::nextClicked, this, &MainWindow::onNextClicked);

    restoreGeometry();
}

MainWindow::~MainWindow() = default;

void MainWindow::buildLibraryTab(QWidget* parent) {
    auto* layout = new QVBoxLayout(parent);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto* header = new QWidget(parent);
    header->setObjectName("headerStrip");
    auto* hLayout = new QHBoxLayout(header);
    hLayout->setContentsMargins(12, 6, 12, 6);
    hLayout->setSpacing(8);

    header_label_ = new QLabel("LIBRARY · 0 TRACKS · 0 ARTISTS", header);
    header_label_->setObjectName("headerLabel");
    hLayout->addWidget(header_label_);
    hLayout->addStretch();

    filter_edit_ = new QLineEdit(header);
    filter_edit_->setObjectName("filterEdit");
    filter_edit_->setPlaceholderText("filter...");
    filter_edit_->setFixedWidth(140);
    hLayout->addWidget(filter_edit_);

    layout->addWidget(header);

    tree_ = new LibraryTree(parent);
    tree_->setHeaderHidden(true);
    tree_->setRootIsDecorated(true);
    tree_->setUniformRowHeights(true);
    tree_->setIndentation(14);
    tree_->setAnimated(false);
    tree_->setExpandsOnDoubleClick(false);  // double-click plays instead.
    tree_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    tree_->setSelectionBehavior(QAbstractItemView::SelectRows);
    tree_->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    tree_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    tree_->setContextMenuPolicy(Qt::CustomContextMenu);

    model_ = new LibraryModel(tree_);
    tree_->setModel(model_);

    connect(tree_, &QTreeView::doubleClicked, this, &MainWindow::onTrackActivated);
    connect(tree_, &QTreeView::customContextMenuRequested, this,
            &MainWindow::onTreeContextMenu);

    layout->addWidget(tree_, 1);
}

void MainWindow::onTreeContextMenu(const QPoint& pos) {
    QMenu menu(this);

    const QModelIndex idx = tree_->indexAt(pos);
    if (idx.isValid()) {
        const int kind = idx.data(roles::Kind).toInt();
        const int64_t entity_id = idx.data(roles::EntityId).toLongLong();
        auto* playAction = menu.addAction("Play");
        if (kind == static_cast<int>(RowKind::Track)) {
            connect(playAction, &QAction::triggered, this,
                    [this, idx] { onTrackActivated(idx); });
        } else if (kind == static_cast<int>(RowKind::Album)) {
            connect(playAction, &QAction::triggered, this,
                    [this, entity_id] { playAlbum(entity_id); });
        } else if (kind == static_cast<int>(RowKind::Artist)) {
            connect(playAction, &QAction::triggered, this,
                    [this, entity_id] { playArtist(entity_id); });
        }

        const auto track_ids = tracksForLibraryIndex(idx);
        if (!track_ids.empty()) {
            buildAddToPlaylistMenu(&menu, track_ids);
        }
        menu.addSeparator();
    }

    auto* addAction = menu.addAction("Add Folder…");
    connect(addAction, &QAction::triggered, this, &MainWindow::onAddFolder);

    auto* rescanAction = menu.addAction("Rescan Library");
    connect(rescanAction, &QAction::triggered, this, &MainWindow::onRescanLibrary);

    menu.exec(tree_->viewport()->mapToGlobal(pos));
}

void MainWindow::buildPlaylistsTab(QWidget* parent) {
    auto* layout = new QVBoxLayout(parent);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    playlists_tree_ = new LibraryTree(parent);
    playlists_tree_->setHeaderHidden(true);
    playlists_tree_->setRootIsDecorated(true);
    playlists_tree_->setUniformRowHeights(true);
    playlists_tree_->setIndentation(14);
    playlists_tree_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    playlists_tree_->setSelectionBehavior(QAbstractItemView::SelectRows);
    playlists_tree_->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    playlists_tree_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    playlists_tree_->setExpandsOnDoubleClick(false);
    playlists_tree_->setContextMenuPolicy(Qt::CustomContextMenu);

    playlists_tree_->setDragEnabled(true);
    playlists_tree_->setAcceptDrops(true);
    playlists_tree_->setDropIndicatorShown(true);
    playlists_tree_->setDragDropMode(QAbstractItemView::InternalMove);
    playlists_tree_->setDefaultDropAction(Qt::MoveAction);

    playlists_model_ = new PlaylistsModel(playlists_tree_);
    playlists_tree_->setModel(playlists_model_);

    connect(playlists_tree_, &QTreeView::doubleClicked, this,
            &MainWindow::onPlaylistTrackActivated);
    connect(playlists_tree_, &QTreeView::customContextMenuRequested, this,
            &MainWindow::onPlaylistsContextMenu);

    // After a track row is dragged, push the new order under its parent
    // playlist back into the database. We hit rowsMoved AND dataChanged paths;
    // rowsMoved is the canonical signal for QStandardItemModel reorders.
    connect(playlists_model_, &QAbstractItemModel::rowsMoved, this,
            [this](const QModelIndex& src, int, int, const QModelIndex&, int) {
                if (!src.isValid()) return;
                if (src.data(roles::Kind).toInt() !=
                    static_cast<int>(RowKind::Playlist)) {
                    return;
                }
                const int64_t playlist_id = src.data(roles::EntityId).toLongLong();
                std::vector<int64_t> ids;
                for (const QString& s : playlists_model_->orderedTrackIdsFor(src)) {
                    ids.push_back(s.toLongLong());
                }
                db_->set_playlist_positions(playlist_id, ids);
            });

    layout->addWidget(playlists_tree_, 1);
}

void MainWindow::reloadPlaylists() {
    playlists_model_->reload(*db_);
    playlists_tree_->header()->setStretchLastSection(false);
    playlists_tree_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    playlists_tree_->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);

    // Re-apply current track marker after the model is rebuilt.
    if (queue_index_ >= 0 && queue_index_ < static_cast<int>(queue_.size())) {
        const int64_t id = queue_[queue_index_];
        playlists_tree_->setCurrentTrack(playlists_model_->indexForTrackId(id));
    }
}

void MainWindow::reloadLibrary() {
    model_->reload(*db_);
    // QStandardItemModel::clear() resets QHeaderView resize modes to Interactive,
    // which collapses column 0 to its content width. Re-apply after every reload.
    tree_->header()->setStretchLastSection(false);
    tree_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    tree_->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    refreshHeaderLabel();
}

void MainWindow::refreshHeaderLabel() {
    header_label_->setText(QString("LIBRARY · %1 TRACKS · %2 ARTISTS")
                               .arg(model_->trackCount())
                               .arg(model_->artistCount()));
}

void MainWindow::onAddFolder() {
    const QString dir = QFileDialog::getExistingDirectory(
        this, "Add folder to library", QString(), QFileDialog::ShowDirsOnly);
    if (dir.isEmpty()) return;

    status_->showMessage("Scanning " + dir + "…");

    // Background scan with its own DB connection (SQLite is single-threaded
    // per connection, so we don't share with the UI thread's connection).
    std::thread([this, dir = dir.toStdString()] {
        library::Database scanDb;
        if (!scanDb.open(library::Database::default_path())) return;
        library::Scanner scanner(scanDb, library::Scanner::default_cover_cache_dir());
        scanner.scan(dir);
        QMetaObject::invokeMethod(this, "onScanFinished", Qt::QueuedConnection);
    }).detach();
}

void MainWindow::onScanFinished() {
    reloadLibrary();
    reloadPlaylists();
    status_->showMessage(
        QString("Library: %1 tracks").arg(model_->trackCount()), 3000);
}

void MainWindow::onRescanLibrary() {
    status_->showMessage("Rescanning library — re-reading tags…");

    std::thread([this] {
        library::Database scanDb;
        if (!scanDb.open(library::Database::default_path())) return;
        library::Scanner scanner(scanDb, library::Scanner::default_cover_cache_dir());
        scanner.rescan_all();
        QMetaObject::invokeMethod(this, "onScanFinished", Qt::QueuedConnection);
    }).detach();
}

void MainWindow::onTrackActivated(const QModelIndex& index) {
    if (!index.isValid()) return;

    const int kind = index.data(roles::Kind).toInt();
    if (kind != static_cast<int>(RowKind::Track)) {
        // Toggle expansion on artist/album rows.
        tree_->setExpanded(index, !tree_->isExpanded(index));
        return;
    }

    // Walk up to the album to enqueue the whole album starting from this track.
    const QModelIndex album_idx = index.parent();
    if (!album_idx.isValid()) return;
    const int64_t album_id = album_idx.data(roles::EntityId).toLongLong();
    const int64_t track_id = index.data(roles::EntityId).toLongLong();
    playAlbumFromTrack(album_id, track_id);
}

void MainWindow::playAlbumFromTrack(int64_t album_id, int64_t start_track_id) {
    const auto tracks = db_->tracks_for_album(album_id);
    if (tracks.empty()) return;

    queue_.clear();
    int start = 0;
    for (size_t i = 0; i < tracks.size(); ++i) {
        queue_.push_back(tracks[i].id);
        if (tracks[i].id == start_track_id) start = static_cast<int>(i);
    }
    playQueueAt(start);
}

void MainWindow::playAlbum(int64_t album_id) {
    const auto tracks = db_->tracks_for_album(album_id);
    if (tracks.empty()) return;
    queue_.clear();
    for (const auto& t : tracks) queue_.push_back(t.id);
    playQueueAt(0);
}

void MainWindow::playArtist(int64_t artist_id) {
    queue_.clear();
    for (const auto& al : db_->albums_for_artist(artist_id)) {
        for (const auto& t : db_->tracks_for_album(al.id)) {
            queue_.push_back(t.id);
        }
    }
    if (queue_.empty()) return;
    playQueueAt(0);
}

void MainWindow::playQueueAt(int index) {
    if (index < 0 || index >= static_cast<int>(queue_.size())) return;
    queue_index_ = index;
    loadCurrent();
}

bool MainWindow::loadCurrentPaused() {
    if (queue_index_ < 0 || queue_index_ >= static_cast<int>(queue_.size())) return false;
    const int64_t id = queue_[queue_index_];
    const auto path_opt = db_->track_path(id);
    if (!path_opt) {
        status_->showMessage("Track missing from DB", 3000);
        return false;
    }
    const QString path = QString::fromStdString(*path_opt);

    if (!player_->load(path.toStdString())) {
        status_->showMessage("Failed to load: " + path, 4000);
        return false;
    }

    const QModelIndex lib_idx = model_->indexForTrackId(id);
    if (lib_idx.isValid()) {
        current_track_ = QPersistentModelIndex(lib_idx);
        tree_->scrollTo(lib_idx, QAbstractItemView::PositionAtCenter);
    }
    tree_->setCurrentTrack(lib_idx);
    tree_->setProgress(0.0);
    tree_->selectionModel()->clearSelection();

    const QModelIndex pl_idx = playlists_model_->indexForTrackId(id);
    if (pl_idx.isValid()) {
        playlists_tree_->scrollTo(pl_idx, QAbstractItemView::PositionAtCenter);
    }
    playlists_tree_->setCurrentTrack(pl_idx);
    playlists_tree_->setProgress(0.0);
    playlists_tree_->selectionModel()->clearSelection();

    const auto fmt = path.section('.', -1).toUpper();
    transport_->setFormatChips(fmt, "—");

    publishMprisMetadata(id);
    if (mpris_) mpris_->notifyPlaybackStateChanged();
    return true;
}

void MainWindow::loadCurrent() {
    if (!loadCurrentPaused()) return;
    player_->play();
    was_playing_ = true;
    if (mpris_) mpris_->notifyPlaybackStateChanged();
}

void MainWindow::onPlayPauseClicked() {
    using audio::PlayerState;
    switch (player_->state()) {
        case PlayerState::Playing: player_->pause(); was_playing_ = false; break;
        case PlayerState::Paused:  player_->play(); was_playing_ = true; break;
        case PlayerState::Stopped: /* nothing loaded */ break;
    }
    if (mpris_) mpris_->notifyPlaybackStateChanged();
}

void MainWindow::onPrevClicked() {
    if (queue_index_ > 0) {
        --queue_index_;
        loadCurrent();
    }
}

void MainWindow::onNextClicked() {
    if (queue_index_ + 1 < static_cast<int>(queue_.size())) {
        ++queue_index_;
        loadCurrent();
    }
}

void MainWindow::onPositionTick() {
    using audio::PlayerState;
    const double dur = player_->duration_seconds();
    const double frac = dur > 0 ? player_->position_seconds() / dur : 0.0;
    tree_->setProgress(frac);
    playlists_tree_->setProgress(frac);

    // Auto-advance: a track that was playing reaches EOF → Player goes Stopped.
    if (was_playing_ && player_->state() == PlayerState::Stopped) {
        was_playing_ = false;
        if (queue_index_ + 1 < static_cast<int>(queue_.size())) {
            ++queue_index_;
            loadCurrent();
        }
    }
}

void MainWindow::closeEvent(QCloseEvent* event) {
    persistGeometry();
    persistResume();
    if (player_) player_->stop();
    QMainWindow::closeEvent(event);
}

void MainWindow::buildMpris() {
    mpris_ = new desktop::MprisAdapter(player_.get(), this);
    if (!mpris_->registerOnBus()) {
        // Not fatal — D-Bus may be unavailable (chroot, headless, etc.).
        return;
    }

    using desktop::MprisAdapter;
    connect(mpris_, &MprisAdapter::requestPlay,      this, [this] {
        if (player_->state() == audio::PlayerState::Stopped) loadCurrent();
        else { player_->play(); was_playing_ = true; mpris_->notifyPlaybackStateChanged(); }
    });
    connect(mpris_, &MprisAdapter::requestPause, this, [this] {
        if (player_->state() == audio::PlayerState::Playing) {
            player_->pause();
            was_playing_ = false;
            mpris_->notifyPlaybackStateChanged();
        }
    });
    connect(mpris_, &MprisAdapter::requestPlayPause, this, &MainWindow::onPlayPauseClicked);
    connect(mpris_, &MprisAdapter::requestStop, this, [this] {
        player_->stop();
        was_playing_ = false;
        mpris_->notifyPlaybackStateChanged();
    });
    connect(mpris_, &MprisAdapter::requestNext,     this, &MainWindow::onNextClicked);
    connect(mpris_, &MprisAdapter::requestPrevious, this, &MainWindow::onPrevClicked);
    connect(mpris_, &MprisAdapter::requestRaise, this, [this] {
        showNormal();
        raise();
        activateWindow();
    });
    connect(mpris_, &MprisAdapter::requestQuit, qApp, &QApplication::quit);
    connect(mpris_, &MprisAdapter::requestSeek, this, [this](qint64 offset_us) {
        const double new_pos =
            player_->position_seconds() + static_cast<double>(offset_us) / 1'000'000.0;
        player_->seek_seconds(new_pos < 0 ? 0 : new_pos);
        mpris_->notifySeeked(static_cast<qint64>(player_->position_seconds() * 1'000'000));
    });
    connect(mpris_, &MprisAdapter::requestSetPosition, this, [this](qint64 position_us) {
        player_->seek_seconds(static_cast<double>(position_us) / 1'000'000.0);
        mpris_->notifySeeked(position_us);
    });
}

void MainWindow::publishMprisMetadata(int64_t track_id) {
    if (!mpris_) return;
    const auto info = db_->track_info(track_id);
    QVariantMap m;
    m["mpris:trackid"] = QVariant::fromValue(QDBusObjectPath(
        QString("/org/mpris/MediaPlayer2/auralbit/track/%1").arg(track_id)));
    if (info) {
        m["mpris:length"] = static_cast<qlonglong>(info->duration_ms) * 1000;  // µs
        if (!info->title.empty()) m["xesam:title"] = QString::fromStdString(info->title);
        if (!info->artist.empty()) {
            m["xesam:artist"] = QStringList{QString::fromStdString(info->artist)};
        }
        if (!info->album.empty()) m["xesam:album"] = QString::fromStdString(info->album);
        if (info->track_no > 0) m["xesam:trackNumber"] = info->track_no;
        if (info->disc_no > 0) m["xesam:discNumber"] = info->disc_no;
        if (!info->path.empty()) {
            m["xesam:url"] = QString("file://") + QString::fromStdString(info->path);
        }
    }
    mpris_->notifyTrackChanged(m);
    mpris_->notifyCanGoNextChanged(queue_index_ + 1 < static_cast<int>(queue_.size()));
    mpris_->notifyCanGoPreviousChanged(queue_index_ > 0);
}

void MainWindow::buildTrayIcon() {
    if (!QSystemTrayIcon::isSystemTrayAvailable()) return;

    // Generate an accent-orange dot at runtime since we don't ship an icon yet.
    QPixmap pm(32, 32);
    pm.fill(Qt::transparent);
    {
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing);
        p.setBrush(QColor("#e8a85c"));
        p.setPen(Qt::NoPen);
        p.drawEllipse(QRectF(4, 4, 24, 24));
    }
    const QIcon icon(pm);

    tray_ = new QSystemTrayIcon(icon, this);
    setWindowIcon(icon);

    auto* menu = new QMenu(this);
    auto* playPause = menu->addAction("Play / Pause");
    connect(playPause, &QAction::triggered, this, &MainWindow::onPlayPauseClicked);
    auto* prev = menu->addAction("Previous");
    connect(prev, &QAction::triggered, this, &MainWindow::onPrevClicked);
    auto* next = menu->addAction("Next");
    connect(next, &QAction::triggered, this, &MainWindow::onNextClicked);
    menu->addSeparator();
    auto* show = menu->addAction("Show / Hide");
    connect(show, &QAction::triggered, this, [this] {
        if (isVisible() && !isMinimized()) hide();
        else { showNormal(); raise(); activateWindow(); }
    });
    auto* quit = menu->addAction("Quit");
    connect(quit, &QAction::triggered, qApp, &QApplication::quit);

    tray_->setContextMenu(menu);
    tray_->setToolTip("auralbit");
    connect(tray_, &QSystemTrayIcon::activated, this,
            [this](QSystemTrayIcon::ActivationReason r) {
                if (r == QSystemTrayIcon::Trigger) {
                    if (isVisible() && !isMinimized()) hide();
                    else { showNormal(); raise(); activateWindow(); }
                }
            });
    tray_->show();
}

void MainWindow::persistResume() {
    QSettings s;
    if (queue_index_ < 0 || queue_index_ >= static_cast<int>(queue_.size()) || !player_) {
        s.remove("resume/queue");
        s.remove("resume/index");
        s.remove("resume/position_seconds");
        return;
    }
    QStringList ids;
    ids.reserve(static_cast<int>(queue_.size()));
    for (int64_t id : queue_) ids << QString::number(id);
    s.setValue("resume/queue", ids.join(','));
    s.setValue("resume/index", queue_index_);
    s.setValue("resume/position_seconds", player_->position_seconds());
}

void MainWindow::restoreResume() {
    QSettings s;
    const QString queue_str = s.value("resume/queue").toString();
    const int idx = s.value("resume/index", -1).toInt();
    const double pos = s.value("resume/position_seconds", 0.0).toDouble();
    if (queue_str.isEmpty() || idx < 0) return;

    queue_.clear();
    for (const QString& token : queue_str.split(',', Qt::SkipEmptyParts)) {
        queue_.push_back(token.toLongLong());
    }
    if (idx >= static_cast<int>(queue_.size())) {
        queue_.clear();
        return;
    }
    queue_index_ = idx;

    if (!loadCurrentPaused()) {
        queue_.clear();
        queue_index_ = -1;
        return;
    }
    if (pos > 0) player_->seek_seconds(pos);
    was_playing_ = false;  // Stays paused until the user presses play.
}

void MainWindow::restoreGeometry() {
    QSettings s;
    const QByteArray geo = s.value(kGeomKey).toByteArray();
    if (!geo.isEmpty()) {
        QMainWindow::restoreGeometry(geo);
        QMainWindow::restoreState(s.value(kStateKey).toByteArray());
        return;
    }
    if (auto* screen = this->screen()) {
        const QSize avail = screen->availableSize();
        resize(avail.width() / 4, avail.height() * 3 / 4);
    } else {
        resize(440, 720);
    }
}

void MainWindow::persistGeometry() {
    QSettings s;
    s.setValue(kGeomKey, saveGeometry());
    s.setValue(kStateKey, saveState());
}

void MainWindow::onPlaylistTrackActivated(const QModelIndex& index) {
    if (!index.isValid()) return;
    const int kind = index.data(roles::Kind).toInt();
    if (kind == static_cast<int>(RowKind::Playlist)) {
        playlists_tree_->setExpanded(index, !playlists_tree_->isExpanded(index));
        return;
    }
    if (kind != static_cast<int>(RowKind::Track)) return;

    const QModelIndex pl_idx = index.parent();
    if (!pl_idx.isValid()) return;
    const int64_t playlist_id = pl_idx.data(roles::EntityId).toLongLong();
    const int64_t track_id = index.data(roles::EntityId).toLongLong();
    playPlaylist(playlist_id, track_id);
}

void MainWindow::playPlaylist(int64_t playlist_id, int64_t start_track_id) {
    const auto tracks = db_->tracks_for_playlist(playlist_id);
    if (tracks.empty()) return;
    queue_.clear();
    int start = 0;
    for (size_t i = 0; i < tracks.size(); ++i) {
        queue_.push_back(tracks[i].id);
        if (start_track_id != 0 && tracks[i].id == start_track_id) {
            start = static_cast<int>(i);
        }
    }
    playQueueAt(start);
}

void MainWindow::promptCreatePlaylist() {
    bool ok = false;
    const QString name = QInputDialog::getText(this, "New Playlist",
                                               "Playlist name:", QLineEdit::Normal,
                                               QString(), &ok);
    if (!ok || name.trimmed().isEmpty()) return;
    if (!db_->create_playlist(name.trimmed().toStdString())) {
        QMessageBox::warning(this, "auralbit",
                             "Could not create playlist (name in use?).");
        return;
    }
    reloadPlaylists();
}

void MainWindow::addTracksToPlaylist(int64_t playlist_id,
                                     const std::vector<int64_t>& track_ids) {
    int added = 0;
    for (int64_t id : track_ids) {
        if (db_->add_track_to_playlist(playlist_id, id)) ++added;
    }
    reloadPlaylists();
    status_->showMessage(QString("Added %1 track(s) to playlist").arg(added), 2500);
}

std::vector<int64_t> MainWindow::tracksForLibraryIndex(const QModelIndex& idx) {
    std::vector<int64_t> out;
    if (!idx.isValid()) return out;
    const int kind = idx.data(roles::Kind).toInt();
    const int64_t entity_id = idx.data(roles::EntityId).toLongLong();
    if (kind == static_cast<int>(RowKind::Track)) {
        out.push_back(entity_id);
    } else if (kind == static_cast<int>(RowKind::Album)) {
        for (const auto& t : db_->tracks_for_album(entity_id)) out.push_back(t.id);
    } else if (kind == static_cast<int>(RowKind::Artist)) {
        for (const auto& al : db_->albums_for_artist(entity_id)) {
            for (const auto& t : db_->tracks_for_album(al.id)) out.push_back(t.id);
        }
    }
    return out;
}

void MainWindow::buildAddToPlaylistMenu(QMenu* parent,
                                        const std::vector<int64_t>& track_ids) {
    auto* sub = parent->addMenu("Add to Playlist");
    auto* newAction = sub->addAction("New Playlist…");
    connect(newAction, &QAction::triggered, this, [this, track_ids] {
        bool ok = false;
        const QString name = QInputDialog::getText(this, "New Playlist",
                                                   "Playlist name:", QLineEdit::Normal,
                                                   QString(), &ok);
        if (!ok || name.trimmed().isEmpty()) return;
        const int64_t pid = db_->create_playlist(name.trimmed().toStdString());
        if (!pid) {
            QMessageBox::warning(this, "auralbit",
                                 "Could not create playlist (name in use?).");
            return;
        }
        addTracksToPlaylist(pid, track_ids);
    });

    const auto playlists = db_->all_playlists();
    if (!playlists.empty()) sub->addSeparator();
    for (const auto& p : playlists) {
        auto* a = sub->addAction(QString::fromStdString(p.name));
        const int64_t pid = p.id;
        connect(a, &QAction::triggered, this,
                [this, pid, track_ids] { addTracksToPlaylist(pid, track_ids); });
    }
}

void MainWindow::onPlaylistsContextMenu(const QPoint& pos) {
    QMenu menu(this);
    const QModelIndex idx = playlists_tree_->indexAt(pos);

    if (idx.isValid()) {
        const int kind = idx.data(roles::Kind).toInt();
        const int64_t entity_id = idx.data(roles::EntityId).toLongLong();

        auto* playAction = menu.addAction("Play");
        if (kind == static_cast<int>(RowKind::Playlist)) {
            connect(playAction, &QAction::triggered, this,
                    [this, entity_id] { playPlaylist(entity_id, 0); });

            menu.addSeparator();
            auto* renameAction = menu.addAction("Rename…");
            connect(renameAction, &QAction::triggered, this, [this, entity_id, idx] {
                bool ok = false;
                const QString cur = idx.data(Qt::DisplayRole).toString();
                const QString name = QInputDialog::getText(
                    this, "Rename Playlist", "Name:", QLineEdit::Normal, cur, &ok);
                if (!ok || name.trimmed().isEmpty()) return;
                db_->rename_playlist(entity_id, name.trimmed().toStdString());
                reloadPlaylists();
            });

            auto* deleteAction = menu.addAction("Delete");
            connect(deleteAction, &QAction::triggered, this, [this, entity_id, idx] {
                const QString name = idx.data(Qt::DisplayRole).toString();
                if (QMessageBox::question(this, "Delete Playlist",
                                          "Delete playlist \"" + name + "\"?") !=
                    QMessageBox::Yes) {
                    return;
                }
                db_->delete_playlist(entity_id);
                reloadPlaylists();
            });

            auto* syncAction = menu.addAction("Sync to…");
            connect(syncAction, &QAction::triggered, this, [this, entity_id, idx] {
                const QString dir = QFileDialog::getExistingDirectory(
                    this, "Sync to (target folder)", QString(),
                    QFileDialog::ShowDirsOnly);
                if (dir.isEmpty()) return;
                auto target = std::make_unique<sync::MountedFsTarget>(dir.toStdString());
                const QString name = idx.data(Qt::DisplayRole).toString();
                auto* dlg = new SyncDialog(*db_, std::move(target), entity_id, name, this);
                dlg->setAttribute(Qt::WA_DeleteOnClose);
                dlg->show();
            });

            auto* exportAction = menu.addAction("Export…");
            connect(exportAction, &QAction::triggered, this, [this, entity_id, idx] {
                const QString name = idx.data(Qt::DisplayRole).toString();
                const QString file = QFileDialog::getSaveFileName(
                    this, "Export Playlist", name + ".m3u",
                    "M3U Playlist (*.m3u *.m3u8);;PLS Playlist (*.pls)");
                if (file.isEmpty()) return;
                const auto res = library::export_playlist(*db_, entity_id,
                                                          file.toStdString());
                if (!res.ok) {
                    QMessageBox::warning(this, "Export failed",
                                         QString::fromStdString(res.error));
                    return;
                }
                status_->showMessage(
                    QString("Exported %1 tracks to %2").arg(res.written).arg(file), 3000);
            });
        } else if (kind == static_cast<int>(RowKind::Track)) {
            connect(playAction, &QAction::triggered, this,
                    [this, idx] { onPlaylistTrackActivated(idx); });

            menu.addSeparator();
            auto* removeAction = menu.addAction("Remove from Playlist");
            const QModelIndex pl_idx = idx.parent();
            const int64_t playlist_id = pl_idx.data(roles::EntityId).toLongLong();
            const int64_t track_id = entity_id;
            connect(removeAction, &QAction::triggered, this,
                    [this, playlist_id, track_id] {
                        db_->remove_track_from_playlist(playlist_id, track_id);
                        reloadPlaylists();
                    });
        }
        menu.addSeparator();
    }

    auto* newAction = menu.addAction("New Playlist…");
    connect(newAction, &QAction::triggered, this, &MainWindow::promptCreatePlaylist);

    auto* importAction = menu.addAction("Import Playlist…");
    connect(importAction, &QAction::triggered, this, [this] {
        const QString file = QFileDialog::getOpenFileName(
            this, "Import Playlist", QString(),
            "Playlists (*.m3u *.m3u8 *.pls);;All Files (*)");
        if (file.isEmpty()) return;
        const auto res = library::import_playlist(*db_, file.toStdString());
        if (!res.ok) {
            QMessageBox::warning(this, "Import failed",
                                 QString::fromStdString(res.error));
            return;
        }
        reloadPlaylists();
        QString msg = QString("Imported %1 tracks").arg(res.matched);
        if (res.missing > 0) {
            msg += QString(" — %1 path(s) not in library").arg(res.missing);
        }
        status_->showMessage(msg, 4500);
    });

    menu.exec(playlists_tree_->viewport()->mapToGlobal(pos));
}

}  // namespace auralbit::ui
