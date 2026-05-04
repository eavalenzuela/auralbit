#include "SyncDialog.h"

#include <QCloseEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

#include "library/Database.h"

namespace auralbit::ui {

SyncDialog::SyncDialog(library::Database& db,
                       std::unique_ptr<sync::SyncTarget> target,
                       int64_t playlist_id,
                       const QString& playlist_name,
                       QWidget* parent)
    : QDialog(parent) {
    setWindowTitle("Sync to Device");
    setModal(true);
    resize(420, 160);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(16, 16, 16, 16);
    root->setSpacing(8);

    auto* heading = new QLabel("Syncing playlist: " + playlist_name, this);
    heading->setStyleSheet("font-weight: 600;");
    root->addWidget(heading);

    bar_ = new QProgressBar(this);
    bar_->setRange(0, 0);  // Indeterminate until totals arrive.
    root->addWidget(bar_);

    counts_label_ = new QLabel("Preparing…", this);
    root->addWidget(counts_label_);

    current_label_ = new QLabel("", this);
    current_label_->setStyleSheet("color: #7a7c84;");
    current_label_->setWordWrap(true);
    root->addWidget(current_label_);

    auto* btn_row = new QHBoxLayout();
    btn_row->addStretch();
    cancel_btn_ = new QPushButton("Cancel", this);
    close_btn_ = new QPushButton("Close", this);
    close_btn_->setEnabled(false);
    btn_row->addWidget(cancel_btn_);
    btn_row->addWidget(close_btn_);
    root->addLayout(btn_row);

    connect(cancel_btn_, &QPushButton::clicked, this, [this] {
        progress_.canceled.store(true);
        cancel_btn_->setEnabled(false);
    });
    connect(close_btn_, &QPushButton::clicked, this, &QDialog::accept);

    // Spin up the worker. The PlaylistSyncer opens its own DB connection
    // (SQLite isn't safe to share across threads), so we just need to pass
    // the path, not the existing handle.
    (void)db;  // Reserved for future use (e.g. cancelling related queries).
    auto syncer = std::make_shared<sync::PlaylistSyncer>(std::move(target));
    const std::string name_std = playlist_name.toStdString();
    const std::string db_path = library::Database::default_path();
    worker_ = std::thread([syncer, db_path, playlist_id, name_std, this] {
        syncer->run(db_path, playlist_id, name_std, progress_);
    });

    timer_ = new QTimer(this);
    timer_->setInterval(100);
    connect(timer_, &QTimer::timeout, this, &SyncDialog::poll);
    timer_->start();
}

SyncDialog::~SyncDialog() {
    progress_.canceled.store(true);
    if (worker_.joinable()) worker_.join();
}

void SyncDialog::closeEvent(QCloseEvent* event) {
    if (!progress_.done.load()) {
        event->ignore();
        return;
    }
    QDialog::closeEvent(event);
}

void SyncDialog::poll() {
    const int total = progress_.total.load();
    const int processed = progress_.processed.load();
    if (total > 0) {
        bar_->setRange(0, total);
        bar_->setValue(processed);
    }
    counts_label_->setText(QString("%1 / %2 — copied %3, skipped %4, failed %5")
                               .arg(processed)
                               .arg(total)
                               .arg(progress_.copied.load())
                               .arg(progress_.skipped.load())
                               .arg(progress_.failed.load()));
    current_label_->setText(QString::fromStdString(progress_.get_current()));

    if (progress_.done.load()) {
        timer_->stop();
        cancel_btn_->setEnabled(false);
        close_btn_->setEnabled(true);
        if (!progress_.error.empty()) {
            QMessageBox::warning(this, "Sync failed",
                                 QString::fromStdString(progress_.error));
        } else if (progress_.canceled.load()) {
            counts_label_->setText(counts_label_->text() + "  (cancelled)");
        } else {
            counts_label_->setText(counts_label_->text() + "  (done)");
        }
    }
}

}  // namespace auralbit::ui
