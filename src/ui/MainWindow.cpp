#include "MainWindow.h"

#include <QLabel>
#include <QScreen>
#include <QVBoxLayout>
#include <QWidget>

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle("auralbit");

    auto* central = new QWidget(this);
    auto* layout = new QVBoxLayout(central);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->addWidget(new QLabel("auralbit — Phase 0 scaffold", central));
    setCentralWidget(central);

    if (auto* screen = this->screen()) {
        const QSize avail = screen->availableSize();
        resize(avail.width() / 4, avail.height() / 2);
    } else {
        resize(480, 540);
    }
}
