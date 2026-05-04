#pragma once

#include <QMainWindow>
#include <QPersistentModelIndex>

class QLineEdit;
class QLabel;
class QStandardItemModel;
class QTimer;

namespace auralbit::ui {

class LibraryTree;
class TransportBar;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    void buildLibraryTab(QWidget* parent);
    void populatePlaceholderModel();
    void restoreGeometry();
    void persistGeometry();

    QLabel* header_label_ = nullptr;
    QLineEdit* filter_edit_ = nullptr;
    LibraryTree* tree_ = nullptr;
    QStandardItemModel* model_ = nullptr;
    TransportBar* transport_ = nullptr;

    QPersistentModelIndex demo_track_index_;
    QTimer* demo_timer_ = nullptr;
    double demo_progress_ = 0.0;
};

}  // namespace auralbit::ui
