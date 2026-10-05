#include "wheelzoomitem.h"

#include <QCoreApplication>
#include <QGuiApplication>
#include <QQuickWindow>
#include <QWheelEvent>

#include <cstdio>

namespace {

bool check(bool condition, const char *message) {
    if (condition)
        return true;
    std::fprintf(stderr, "Check failed: %s\n", message);
    return false;
}

QWheelEvent wheelEvent(int delta, Qt::KeyboardModifiers modifiers) {
    return QWheelEvent(
        QPointF(120, 90),
        QPointF(120, 90),
        QPoint(),
        QPoint(0, delta),
        Qt::NoButton,
        modifiers,
        Qt::NoScrollPhase,
        false
    );
}

} // namespace

int main(int argc, char *argv[]) {
    QGuiApplication application(argc, argv);
    QQuickWindow window;
    window.setGeometry(0, 0, 360, 240);
    WheelZoomItem zoomItem(window.contentItem());
    zoomItem.setPosition(QPointF(20, 20));
    zoomItem.setSize(QSizeF(300, 180));
    window.show();
    QCoreApplication::processEvents();

    int requestedSize = -1;
    QObject::connect(
        &zoomItem,
        &WheelZoomItem::fontPixelSizeRequested,
        [&](int pixelSize) { requestedSize = pixelSize; }
    );

    auto zoomIn = wheelEvent(120, Qt::ControlModifier);
    QCoreApplication::sendEvent(&window, &zoomIn);
    if (!check(requestedSize == 12 && zoomIn.isAccepted(), "Ctrl+wheel increases the IC log font size"))
        return 1;

    zoomItem.setFontPixelSize(requestedSize);
    requestedSize = -1;
    auto zoomOut = wheelEvent(-120, Qt::ControlModifier);
    QCoreApplication::sendEvent(&window, &zoomOut);
    if (!check(requestedSize == 11 && zoomOut.isAccepted(), "Ctrl+wheel decreases the IC log font size"))
        return 1;

    requestedSize = -1;
    auto plainWheel = wheelEvent(120, Qt::NoModifier);
    QCoreApplication::sendEvent(&window, &plainWheel);
    if (!check(requestedSize == -1, "plain wheel is left to the IC log scroll view"))
        return 1;

    std::puts("IC tool log wheel zoom check passed.");
    return 0;
}
