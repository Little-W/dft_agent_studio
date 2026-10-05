#pragma once

#include <QQuickAsyncImageProvider>
#include <QImage>

QImage renderLatexFormula(const QString &source, bool display = false);

class LatexMathImageProvider final : public QQuickAsyncImageProvider {
public:
    QQuickImageResponse *requestImageResponse(const QString &id, const QSize &requestedSize) override;
};
