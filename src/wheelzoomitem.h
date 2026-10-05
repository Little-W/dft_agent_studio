#pragma once

#include <QPointer>
#include <QQuickItem>

class QQuickWindow;

class WheelZoomItem : public QQuickItem {
    Q_OBJECT
    Q_PROPERTY(int fontPixelSize READ fontPixelSize WRITE setFontPixelSize NOTIFY fontPixelSizeChanged)
    Q_PROPERTY(int minimumFontPixelSize READ minimumFontPixelSize WRITE setMinimumFontPixelSize NOTIFY limitsChanged)
    Q_PROPERTY(int maximumFontPixelSize READ maximumFontPixelSize WRITE setMaximumFontPixelSize NOTIFY limitsChanged)

public:
    explicit WheelZoomItem(QQuickItem *parent = nullptr);
    ~WheelZoomItem() override;

    int fontPixelSize() const;
    void setFontPixelSize(int value);
    int minimumFontPixelSize() const;
    void setMinimumFontPixelSize(int value);
    int maximumFontPixelSize() const;
    void setMaximumFontPixelSize(int value);

signals:
    void fontPixelSizeChanged();
    void limitsChanged();
    void fontPixelSizeRequested(int pixelSize);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    QPointer<QQuickWindow> m_filteredWindow;
    int m_fontPixelSize = 11;
    int m_minimumFontPixelSize = 8;
    int m_maximumFontPixelSize = 28;
    qreal m_wheelAccumulator = 0;

    void attachToWindow(QQuickWindow *quickWindow);
};
