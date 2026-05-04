#include "MainWindow.h"

#include <thread>

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMetaObject>
#include <QScreen>
#include <QSettings>
#include <QStatusBar>
#include <QTabBar>
#include <QTabWidget>
#include <QTimer>
#include <QVBoxLayout>

#include "LibraryModel.h"
#include "LibraryTree.h"
#include "TransportBar.h"
#include "audio/Player.h"
#include "library/Database.h"
#include "library/Scanner.h"

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
    auto* placeholder = new QVBoxLayout(playlistsPage);
    placeholder->addStretch();
    auto* lbl = new QLabel("Playlists land here in Phase 4", playlistsPage);
    lbl->setAlignment(Qt::AlignCenter);
    lbl->setStyleSheet("color: #6f717a;");
    placeholder->addWidget(lbl);
    placeholder->addStretch();
    tabs->addTab(playlistsPage, "Playlists");

    root->addWidget(tabs, 1);

    transport_ = new TransportBar(central);
    root->addWidget(transport_);

    setCentralWidget(central);

    status_ = statusBar();
    status_->setSizeGripEnabled(false);

    reloadLibrary();

    progress_timer_ = new QTimer(this);
    progress_timer_->setInterval(100);
    connect(progress_timer_, &QTimer::timeout, this, &MainWindow::onPositionTick);
    progress_timer_->start();

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
        menu.addSeparator();
    }

    auto* addAction = menu.addAction("Add Folder…");
    connect(addAction, &QAction::triggered, this, &MainWindow::onAddFolder);

    menu.exec(tree_->viewport()->mapToGlobal(pos));
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
    status_->showMessage(
        QString("Library: %1 tracks").arg(model_->trackCount()), 3000);
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

void MainWindow::loadCurrent() {
    if (queue_index_ < 0 || queue_index_ >= static_cast<int>(queue_.size())) return;
    const int64_t id = queue_[queue_index_];
    const auto path_opt = db_->track_path(id);
    if (!path_opt) {
        status_->showMessage("Track missing from DB", 3000);
        return;
    }
    const QString path = QString::fromStdString(*path_opt);

    if (!player_->load(path.toStdString())) {
        status_->showMessage("Failed to load: " + path, 4000);
        return;
    }
    player_->play();
    was_playing_ = true;

    const QModelIndex idx = model_->indexForTrackId(id);
    if (idx.isValid()) {
        current_track_ = QPersistentModelIndex(idx);
        tree_->setCurrentTrack(current_track_);
        tree_->scrollTo(idx, QAbstractItemView::PositionAtCenter);
    }
    tree_->setProgress(0.0);
    tree_->selectionModel()->clearSelection();

    const auto fmt = path.section('.', -1).toUpper();
    transport_->setFormatChips(fmt, "—");
}

void MainWindow::onPlayPauseClicked() {
    using audio::PlayerState;
    switch (player_->state()) {
        case PlayerState::Playing: player_->pause(); was_playing_ = false; break;
        case PlayerState::Paused:  player_->play(); was_playing_ = true; break;
        case PlayerState::Stopped: /* nothing loaded */ break;
    }
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
    if (dur > 0) {
        tree_->setProgress(player_->position_seconds() / dur);
    } else {
        tree_->setProgress(0.0);
    }

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
    if (player_) player_->stop();
    QMainWindow::closeEvent(event);
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

}  // namespace auralbit::ui
