#pragma once

#include <QDialog>
#include <memory>
#include <thread>

#include "sync/PlaylistSyncer.h"

class QLabel;
class QProgressBar;
class QPushButton;
class QTimer;

namespace auralbit::library { class Database; }
namespace auralbit::sync { class SyncTarget; }

namespace auralbit::ui {

// Modal dialog that drives a PlaylistSyncer on a worker thread. Polls the
// shared SyncProgress at 100ms and updates labels / progress bar; the
// Cancel button flips progress.canceled, which the worker checks per file.
class SyncDialog : public QDialog {
    Q_OBJECT
public:
    SyncDialog(library::Database& db,
               std::unique_ptr<sync::SyncTarget> target,
               int64_t playlist_id,
               const QString& playlist_name,
               QWidget* parent = nullptr);
    ~SyncDialog() override;

protected:
    void closeEvent(QCloseEvent* event) override;

private slots:
    void poll();

private:
    sync::SyncProgress progress_;
    std::thread worker_;

    QLabel* current_label_ = nullptr;
    QLabel* counts_label_ = nullptr;
    QProgressBar* bar_ = nullptr;
    QPushButton* cancel_btn_ = nullptr;
    QPushButton* close_btn_ = nullptr;
    QTimer* timer_ = nullptr;
};

}  // namespace auralbit::ui
