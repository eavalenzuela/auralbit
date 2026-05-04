#include "MainWindow.h"

#include <QCloseEvent>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QScreen>
#include <QSettings>
#include <QStandardItemModel>
#include <QTabWidget>
#include <QTimer>
#include <QVBoxLayout>

#include "LibraryTree.h"
#include "TransportBar.h"

namespace auralbit::ui {

namespace {
constexpr const char* kGeomKey = "window/geometry";
constexpr const char* kStateKey = "window/state";
}  // namespace

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle("auralbit");

    auto* central = new QWidget(this);
    auto* root = new QVBoxLayout(central);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    auto* tabs = new QTabWidget(central);
    tabs->setDocumentMode(true);
    tabs->setTabPosition(QTabWidget::North);

    auto* libraryPage = new QWidget(tabs);
    buildLibraryTab(libraryPage);
    tabs->addTab(libraryPage, "Library  1,284");

    auto* playlistsPage = new QWidget(tabs);
    auto* placeholder = new QVBoxLayout(playlistsPage);
    placeholder->addStretch();
    auto* lbl = new QLabel("Playlists land here in Phase 4", playlistsPage);
    lbl->setAlignment(Qt::AlignCenter);
    lbl->setStyleSheet("color: #6f717a;");
    placeholder->addWidget(lbl);
    placeholder->addStretch();
    tabs->addTab(playlistsPage, "Playlists  0");

    root->addWidget(tabs, 1);

    transport_ = new TransportBar(central);
    transport_->setFormatChips("FLAC", "44.1");
    root->addWidget(transport_);

    setCentralWidget(central);

    // Demo: tick the progress wash across the highlighted row so the visual
    // is alive when reviewing. Replaced with real Player position in Phase 6.
    demo_timer_ = new QTimer(this);
    demo_timer_->setInterval(120);
    connect(demo_timer_, &QTimer::timeout, this, [this] {
        demo_progress_ += 0.005;
        if (demo_progress_ > 1.0) demo_progress_ = 0.0;
        tree_->setProgress(demo_progress_);
    });
    demo_timer_->start();

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
    tree_->setExpandsOnDoubleClick(true);
    tree_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    tree_->setSelectionBehavior(QAbstractItemView::SelectRows);
    tree_->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);

    model_ = new QStandardItemModel(tree_);
    model_->setColumnCount(2);
    tree_->setModel(model_);
    tree_->header()->setStretchLastSection(false);
    tree_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    tree_->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);

    populatePlaceholderModel();
    layout->addWidget(tree_, 1);
}

void MainWindow::populatePlaceholderModel() {
    struct AlbumStub { const char* name; int track_count; };
    struct ArtistStub {
        const char* name;
        std::vector<AlbumStub> albums;
    };

    const std::vector<ArtistStub> demo = {
        {"Aphex Twin", {{"Selected Ambient Works 85–92", 12}, {"Drukqs", 30}}},
        {"Boards of Canada",
         {{"Music Has the Right to Children", 17}, {"Geogaddi", 23}}},
        {"Burial", {}},
        {"Caribou", {}},
        {"Four Tet", {{"Rounds", 10}, {"There Is Love in You", 9}}},
        {"Mount Kimbie", {}},
        {"Nils Frahm", {}},
        {"Oneohtrix Point Never", {}},
    };

    auto rightAlign = [](QStandardItem* it) {
        it->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        it->setForeground(QBrush(QColor("#6f717a")));
    };

    for (const auto& a : demo) {
        auto* artistItem = new QStandardItem(a.name);
        artistItem->setForeground(QBrush(QColor("#e6e6e6")));
        QStandardItem* artistRight = new QStandardItem();
        model_->appendRow({artistItem, artistRight});

        for (const auto& al : a.albums) {
            auto* albumItem = new QStandardItem(al.name);
            albumItem->setForeground(QBrush(QColor("#d8dade")));
            auto* albumRight = new QStandardItem(QString::number(al.track_count));
            rightAlign(albumRight);
            artistItem->appendRow({albumItem, albumRight});
        }
    }

    if (auto* boc = model_->findItems("Boards of Canada").value(0, nullptr)) {
        tree_->expand(boc->index());
        for (int r = 0; r < boc->rowCount(); ++r) {
            auto* album = boc->child(r);
            if (album && album->text() == "Geogaddi") {
                tree_->expand(album->index());

                struct TrackStub { const char* name; const char* time; bool current; };
                const TrackStub tracks[] = {
                    {"Music Is Math", "3:48", true},
                    {"Beware the Friendly Stranger", "0:38", false},
                    {"Gyroscope", "3:34", false},
                    {"Dandelion", "1:09", false},
                    {"Sunshine Recorder", "6:11", false},
                    {"In the Annexe", "1:21", false},
                    {"Julie and Candy", "5:30", false},
                };
                for (const auto& t : tracks) {
                    auto* trackItem = new QStandardItem(t.name);
                    auto* timeItem = new QStandardItem(t.time);
                    timeItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
                    if (t.current) {
                        trackItem->setForeground(QBrush(QColor("#e8a85c")));
                        timeItem->setForeground(QBrush(QColor("#e8a85c")));
                        trackItem->setText(QString("♪  ") + t.name);
                    } else {
                        trackItem->setForeground(QBrush(QColor("#c8cad0")));
                        timeItem->setForeground(QBrush(QColor("#7a7c84")));
                    }
                    album->appendRow({trackItem, timeItem});
                    if (t.current) {
                        demo_track_index_ = trackItem->index();
                    }
                }
            }
        }
    }

    if (demo_track_index_.isValid()) {
        tree_->setCurrentTrack(demo_track_index_);
        tree_->setProgress(0.45);
    }

    header_label_->setText("LIBRARY · 1,284 TRACKS · 87 ARTISTS");
}

void MainWindow::closeEvent(QCloseEvent* event) {
    persistGeometry();
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
