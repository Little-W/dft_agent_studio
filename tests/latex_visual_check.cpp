#include "latexmathimageprovider.h"

#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQuickWindow>
#include <QElapsedTimer>
#include <QImage>
#include <QThread>
#include <cstdio>

int main(int argc, char *argv[]) {
    QGuiApplication application(argc, argv);
    QQmlApplicationEngine engine;
    engine.addImageProvider(QStringLiteral("latex"), new LatexMathImageProvider);
    const QString expression = QStringLiteral("\\le 99.00\\%");
    const QString imageId = QStringLiteral("i")
        + QString::fromLatin1(expression.toUtf8().toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
    engine.rootContext()->setContextProperty(
        QStringLiteral("formulaMarkup"), QStringLiteral("<img src=\"image://latex/%1\" />").arg(imageId));

    QQmlComponent component(&engine);
    component.setData(R"QML(
import QtQuick
import QtQuick.Window
    Window {
    width: 320
    height: 80
    visible: true
    color: "white"
    Text {
        anchors.centerIn: parent
        textFormat: Text.RichText
        text: formulaMarkup
        font.pixelSize: 15
    }
}
)QML", QUrl());
    QObject *created = component.create();
    auto *window = qobject_cast<QQuickWindow *>(created);
    if (!window) {
        std::fprintf(stderr, "Could not create LaTeX Quick preview: %s\n", qPrintable(component.errorString()));
        delete created;
        return 1;
    }
    window->show();

    bool formulaPainted = false;
    QImage capturedFrame;
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < 8000 && !formulaPainted) {
        application.processEvents();
        const QImage frame = window->grabWindow();
        int darkPixelCount = 0;
        for (int y = 0; y < frame.height() && darkPixelCount < 8; ++y) {
            for (int x = 0; x < frame.width() && darkPixelCount < 8; ++x) {
                const QColor pixel = frame.pixelColor(x, y);
                if (pixel.red() < 150 && pixel.green() < 150 && pixel.blue() < 150)
                    ++darkPixelCount;
            }
        }
        formulaPainted = darkPixelCount >= 8;
        if (formulaPainted)
            capturedFrame = frame;
        if (!formulaPainted)
            QThread::msleep(50);
    }
    delete window;
    if (!formulaPainted) {
        std::fprintf(stderr, "LaTeX image did not appear in the Qt Quick RichText preview.\n");
        return 1;
    }
    const QString outputPath = qEnvironmentVariable("DFT_LATEX_VISUAL_OUTPUT");
    if (!outputPath.isEmpty() && !capturedFrame.save(outputPath)) {
        std::fprintf(stderr, "Could not save LaTeX visual preview: %s\n", qPrintable(outputPath));
        return 1;
    }
    std::puts("LaTeX formula rendered in Qt Quick RichText.");
    return 0;
}
