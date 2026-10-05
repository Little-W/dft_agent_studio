#include "latexmathimageprovider.h"

#include <QDir>
#include <QFile>
#include <QFont>
#include <QGuiApplication>
#include <QFontMetrics>
#include <QImageReader>
#include <QMetaObject>
#include <QMutex>
#include <QMutexLocker>
#include <QPainter>
#include <QProcess>
#include <QPointer>
#include <QQuickTextureFactory>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QThreadPool>
#include <QTemporaryDir>
#include <QRunnable>

namespace {
QMutex imageCacheMutex;
QHash<QString, QImage> imageCache;

QThreadPool &mathThreadPool() {
    static QThreadPool pool;
    static const bool configured = (pool.setMaxThreadCount(2), pool.setExpiryTimeout(30'000), true);
    Q_UNUSED(configured)
    return pool;
}

QString sansMathPackage() {
    static const QString package = [] {
        const QString kpsewhich = QStandardPaths::findExecutable(QStringLiteral("kpsewhich"));
        if (kpsewhich.isEmpty())
            return QString();
        QProcess process;
        process.start(kpsewhich, {QStringLiteral("sansmathfonts.sty")});
        if (!process.waitForStarted(500) || !process.waitForFinished(1000)
            || process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0
            || process.readAllStandardOutput().trimmed().isEmpty())
            return QString();
        return QStringLiteral("\\usepackage{sansmathfonts}\n");
    }();
    return package;
}

bool containsUnsafeTex(const QString &source) {
    static const QRegularExpression unsafe(
        QStringLiteral(R"(\\(?:input|include|includegraphics|openin|openout|write|write18|read|catcode|usepackage|documentclass|immediate|special|csname|newread|newwrite|loop|shipout|def|gdef|xdef|edef|let|futurelet|newcommand|renewcommand|providecommand|everyjob|everypar|afterassignment|aftergroup)\b)"),
        QRegularExpression::CaseInsensitiveOption);
    return unsafe.match(source).hasMatch();
}

QImage compileMath(const QString &source, bool display) {
    const QString latex = QStandardPaths::findExecutable(QStringLiteral("latex"));
    const QString dvisvgm = QStandardPaths::findExecutable(QStringLiteral("dvisvgm"));
    if (latex.isEmpty() || dvisvgm.isEmpty() || source.isEmpty() || source.size() > 2048 || containsUnsafeTex(source))
        return {};

    QTemporaryDir temporary;
    if (!temporary.isValid())
        return {};
    QFile texFile(temporary.filePath(QStringLiteral("formula.tex")));
    if (!texFile.open(QIODevice::WriteOnly | QIODevice::Text))
        return {};
    QString safeSource;
    safeSource.reserve(source.size() + 8);
    for (qsizetype i = 0; i < source.size(); ++i) {
        if (source.at(i) == QLatin1Char('%') && (i == 0 || source.at(i - 1) != QLatin1Char('\\')))
            safeSource += QStringLiteral("\\%");
        else
            safeSource += source.at(i);
    }
    QString texDocument = QStringLiteral(
        "\\documentclass{article}\n"
        "\\pagestyle{empty}\n"
        "\\usepackage{amsmath,amssymb}\n"
        "__DFT_MATH_FONT_PACKAGE__"
        "\\begin{document}\n"
        "\\boldmath\n"
        "$\\DFTMATHSTYLEPLACEHOLDER DFTMATHSOURCEPLACEHOLDER$\n"
        "\\end{document}\n");
    texDocument.replace(QStringLiteral("__DFT_MATH_FONT_PACKAGE__"), sansMathPackage());
    texDocument.replace(QStringLiteral("DFTMATHSTYLEPLACEHOLDER"),
                         display ? QStringLiteral("displaystyle") : QStringLiteral("textstyle"));
    texDocument.replace(QStringLiteral("DFTMATHSOURCEPLACEHOLDER"), safeSource);
    const QByteArray document = texDocument.toUtf8();
    if (texFile.write(document) != document.size())
        return {};
    texFile.close();

    QProcess process;
    process.setWorkingDirectory(temporary.path());
    process.setProcessChannelMode(QProcess::SeparateChannels);
    process.start(latex, {QStringLiteral("-no-shell-escape"), QStringLiteral("-interaction=nonstopmode"),
                          QStringLiteral("-halt-on-error"), QStringLiteral("formula.tex")});
    if (!process.waitForStarted(1000) || !process.waitForFinished(5000)) {
        process.kill();
        process.waitForFinished(500);
        return {};
    }
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0)
        return {};

    process.start(dvisvgm, {QStringLiteral("--no-fonts"), QStringLiteral("--bbox=min"),
                            QStringLiteral("-o"), QStringLiteral("formula.svg"), QStringLiteral("formula.dvi")});
    if (!process.waitForStarted(1000) || !process.waitForFinished(5000)) {
        process.kill();
        process.waitForFinished(500);
        return {};
    }
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0)
        return {};

    QImageReader reader(temporary.filePath(QStringLiteral("formula.svg")));
    const QSize naturalSize = reader.size();
    if (!naturalSize.isValid() || naturalSize.width() > 4096 || naturalSize.height() > 1024)
        return {};
    return reader.read();
}

QImage fallbackMath(const QString &source) {
    QString readable = source.left(512).replace(QLatin1Char('\n'), QStringLiteral(" ")).simplified();
    QFont font = QGuiApplication::font();
    font.setPixelSize(15);
    font.setWeight(QFont::DemiBold);
    const QFontMetrics metrics(font);
    const QRect bounds = metrics.boundingRect(QRect(0, 0, 1600, 256), Qt::TextWordWrap, readable);
    if (!bounds.isValid())
        return {};
    QImage image(bounds.width() + 8, bounds.height() + 6, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::TextAntialiasing, true);
    painter.setFont(font);
    painter.setPen(QColor(QStringLiteral("#34404d")));
    painter.drawText(image.rect().adjusted(3, 2, -3, -2), Qt::TextWordWrap, readable);
    return image;
}

QImage renderMath(const QString &source, bool display) {
    QImage image = compileMath(source, display);
    return image.isNull() ? fallbackMath(source) : image;
}

class MathImageResponse final : public QQuickImageResponse {
public:
    explicit MathImageResponse(const QString &id) {
        const bool display = id.startsWith(QLatin1Char('d'));
        const QString encoded = id.mid(1);
        const QString expression = QString::fromUtf8(QByteArray::fromBase64(encoded.toLatin1(), QByteArray::Base64UrlEncoding));
        const QString cacheKey = QString(display ? "d" : "i") + expression;
        QPointer<MathImageResponse> guard(this);
        auto *task = QRunnable::create([guard, expression, display, cacheKey] {
            QImage image;
            {
                QMutexLocker lock(&imageCacheMutex);
                const auto found = imageCache.constFind(cacheKey);
                if (found != imageCache.cend())
                    image = found.value();
            }
            if (image.isNull()) {
                image = renderMath(expression, display);
                if (!image.isNull()) {
                    QMutexLocker lock(&imageCacheMutex);
                    if (imageCache.size() >= 128)
                        imageCache.clear();
                    imageCache.insert(cacheKey, image);
                }
            }
            if (guard) {
                QMetaObject::invokeMethod(guard, [guard, image = std::move(image)]() mutable {
                    if (guard) {
                        guard->m_image = std::move(image);
                        emit guard->finished();
                    }
                }, Qt::QueuedConnection);
            }
        });
        task->setAutoDelete(true);
        mathThreadPool().start(task);
    }

    QQuickTextureFactory *textureFactory() const override {
        return m_image.isNull() ? nullptr : QQuickTextureFactory::textureFactoryForImage(m_image);
    }

private:
    QImage m_image;
};
}

QQuickImageResponse *LatexMathImageProvider::requestImageResponse(const QString &id, const QSize &) {
    return new MathImageResponse(id);
}

QImage renderLatexFormula(const QString &source, bool display) {
    return renderMath(source, display);
}
