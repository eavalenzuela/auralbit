#include <QApplication>
#include <QPalette>
#include <QStyleFactory>

#include "ui/MainWindow.h"

static void applyDarkPalette(QApplication& app) {
    app.setStyle(QStyleFactory::create("Fusion"));

    QPalette p;
    const QColor base(30, 30, 32);
    const QColor alt(38, 38, 42);
    const QColor text(220, 220, 220);
    const QColor accent(120, 170, 255);

    p.setColor(QPalette::Window, base);
    p.setColor(QPalette::WindowText, text);
    p.setColor(QPalette::Base, alt);
    p.setColor(QPalette::AlternateBase, base);
    p.setColor(QPalette::Text, text);
    p.setColor(QPalette::Button, alt);
    p.setColor(QPalette::ButtonText, text);
    p.setColor(QPalette::Highlight, accent);
    p.setColor(QPalette::HighlightedText, Qt::black);
    p.setColor(QPalette::ToolTipBase, alt);
    p.setColor(QPalette::ToolTipText, text);

    app.setPalette(p);
}

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QApplication::setApplicationName("auralbit");
    QApplication::setOrganizationName("auralbit");

    applyDarkPalette(app);

    MainWindow w;
    w.show();
    return app.exec();
}
