#include "wheelzoomitem.h"

#include <QEvent>
#include <QQuickWindow>
#include <QWheelEvent>

#include <cmath>

WheelZoomItem::WheelZoomItem(QQuickItem *parent)
    : QQuickItem(parent) {
    connect(this, &QQuickItem::windowChanged, this, &WheelZoomItem::attachToWindow);
    attachToWindow(window());
}

WheelZoomItem::~WheelZoomItem() {
    disconnect(this, &QQuickItem::windowChanged, this, &WheelZoomItem::attachToWindow);
    if (m_filteredWindow)
        m_filteredWindow->removeEventFilter(this);
}

int WheelZoomItem::fontPixelSize() const { return m_fontPixelSize; }

void WheelZoomItem::setFontPixelSize(int value) {
    const int normalized = qBound(m_minimumFontPixelSize, value, m_maximumFontPixelSize);
    if (m_fontPixelSize == normalized)
        return;
    m_fontPixelSize = normalized;
    emit fontPixelSizeChanged();
}

int WheelZoomItem::minimumFontPixelSize() const { return m_minimumFontPixelSize; }

void WheelZoomItem::setMinimumFontPixelSize(int value) {
    const int normalized = qBound(1, value, m_maximumFontPixelSize);
    if (m_minimumFontPixelSize == normalized)
        return;
    m_minimumFontPixelSize = normalized;
    setFontPixelSize(m_fontPixelSize);
    emit limitsChanged();
}

int WheelZoomItem::maximumFontPixelSize() const { return m_maximumFontPixelSize; }

void WheelZoomItem::setMaximumFontPixelSize(int value) {
    const int normalized = qMax(m_minimumFontPixelSize, value);
    if (m_maximumFontPixelSize == normalized)
        return;
    m_maximumFontPixelSize = normalized;
    setFontPixelSize(m_fontPixelSize);
    emit limitsChanged();
}

bool WheelZoomItem::eventFilter(QObject *watched, QEvent *event) {
    if (watched != m_filteredWindow || event->type() != QEvent::Wheel || !isVisible() || !isEnabled())
        return false;

    auto *wheelEvent = static_cast<QWheelEvent *>(event);
    if (!(wheelEvent->modifiers() & Qt::ControlModifier))
        return false;

    const QPointF localPosition = mapFromScene(wheelEvent->position());
    if (!contains(localPosition))
        return false;

    const qreal wheelUnits = wheelEvent->angleDelta().y() != 0
        ? wheelEvent->angleDelta().y() / 120.0
        : wheelEvent->pixelDelta().y() / 40.0;
    m_wheelAccumulator += wheelUnits;
    const int steps = m_wheelAccumulator > 0
        ? static_cast<int>(std::floor(m_wheelAccumulator))
        : static_cast<int>(std::ceil(m_wheelAccumulator));
    if (steps != 0) {
        m_wheelAccumulator -= steps;
        const int requested = qBound(
            m_minimumFontPixelSize,
            m_fontPixelSize + steps,
            m_maximumFontPixelSize
        );
        if (requested != m_fontPixelSize)
            emit fontPixelSizeRequested(requested);
    }
    wheelEvent->accept();
    return true;
}

void WheelZoomItem::attachToWindow(QQuickWindow *quickWindow) {
    if (m_filteredWindow == quickWindow)
        return;
    if (m_filteredWindow)
        m_filteredWindow->removeEventFilter(this);
    m_filteredWindow = quickWindow;
    if (m_filteredWindow)
        m_filteredWindow->installEventFilter(this);
}
