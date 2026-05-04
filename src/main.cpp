#include <QApplication>
#include <QFile>
#include <QStyleFactory>

#include "ui/MainWindow.h"

namespace {

QString loadStylesheet() {
    QFile f(":/theme.qss");
    if (!f.open(QFile::ReadOnly | QFile::Text)) return {};
    return QString::fromUtf8(f.readAll());
}

}  // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QApplication::setApplicationName("auralbit");
    QApplication::setOrganizationName("auralbit");

    app.setStyle(QStyleFactory::create("Fusion"));
    app.setStyleSheet(loadStylesheet());

    auralbit::ui::MainWindow w;
    w.show();
    return app.exec();
}
