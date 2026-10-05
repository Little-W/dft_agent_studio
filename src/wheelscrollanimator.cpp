#include "wheelscrollanimator.h"

#include <QMetaObject>
#include <QVariant>
#include <algorithm>
#include <cmath>

namespace {
constexpr qreal kPositionEpsilon = 0.04;
constexpr qreal kVelocityEpsilon = 0.8;
}

WheelScrollAnimator::WheelScrollAnimator(QObject *parent) : QObject(parent) {}

QObject *WheelScrollAnimator::flickable() const { return m_flickable; }

void WheelScrollAnimator::setFlickable(QObject *flickable) {
    if (m_flickable == flickable)
        return;
    cancel();
    if (m_flickable) {
        disconnect(m_flickable, nullptr, this, nullptr);
        disconnect(m_flickable, SIGNAL(contentYChanged()), this, SLOT(externalContentPositionChanged()));
        disconnect(m_flickable, SIGNAL(contentXChanged()), this, SLOT(externalContentPositionChanged()));
        disconnect(m_flickable, SIGNAL(draggingChanged()), this, SLOT(flickableMotionStateChanged()));
        disconnect(m_flickable, SIGNAL(flickingChanged()), this, SLOT(flickableMotionStateChanged()));
    }
    m_flickable = flickable;
    if (m_flickable) {
        connect(m_flickable, m_horizontal ? SIGNAL(contentXChanged()) : SIGNAL(contentYChanged()),
                this, SLOT(externalContentPositionChanged()));
        connect(m_flickable, SIGNAL(draggingChanged()), this, SLOT(flickableMotionStateChanged()));
        connect(m_flickable, SIGNAL(flickingChanged()), this, SLOT(flickableMotionStateChanged()));
        connect(m_flickable, &QObject::destroyed, this, [this] {
            m_flickable = nullptr;
            cancel();
            emit flickableChanged();
        });
        m_destinationY = currentContentY();
    }
    emit flickableChanged();
}

qreal WheelScrollAnimator::angularFrequency() const { return m_angularFrequency; }

void WheelScrollAnimator::setAngularFrequency(qreal frequency) {
    frequency = std::clamp(frequency, 8.0, 80.0);
    if (qFuzzyCompare(m_angularFrequency, frequency))
        return;
    m_angularFrequency = frequency;
    emit angularFrequencyChanged();
}

bool WheelScrollAnimator::scrolling() const { return m_scrolling; }
qreal WheelScrollAnimator::destinationPosition() const { return m_destinationY; }

bool WheelScrollAnimator::horizontal() const { return m_horizontal; }

void WheelScrollAnimator::setHorizontal(bool horizontal) {
    if (m_horizontal == horizontal)
        return;
    cancel();
    if (m_flickable) {
        disconnect(m_flickable, SIGNAL(contentXChanged()), this, SLOT(externalContentPositionChanged()));
        disconnect(m_flickable, SIGNAL(contentYChanged()), this, SLOT(externalContentPositionChanged()));
    }
    m_horizontal = horizontal;
    if (m_flickable)
        connect(m_flickable, m_horizontal ? SIGNAL(contentXChanged()) : SIGNAL(contentYChanged()),
                this, SLOT(externalContentPositionChanged()));
    m_destinationY = currentContentY();
    emit horizontalChanged();
}

qreal WheelScrollAnimator::minimumContentY() const {
    if (!m_flickable)
        return 0.0;
    return m_horizontal
        ? m_flickable->property("originX").toReal() - m_flickable->property("leftMargin").toReal()
        : m_flickable->property("originY").toReal() - m_flickable->property("topMargin").toReal();
}

qreal WheelScrollAnimator::maximumContentY() const {
    if (!m_flickable)
        return 0.0;
    const qreal minimum = minimumContentY();
    return m_horizontal
        ? std::max(minimum, m_flickable->property("contentWidth").toReal()
            + m_flickable->property("originX").toReal() - m_flickable->property("width").toReal()
            + m_flickable->property("rightMargin").toReal())
        : std::max(minimum, m_flickable->property("contentHeight").toReal()
            + m_flickable->property("originY").toReal() - m_flickable->property("height").toReal()
            + m_flickable->property("bottomMargin").toReal());
}

qreal WheelScrollAnimator::currentContentY() const {
    return m_flickable
        ? m_flickable->property(m_horizontal ? "contentX" : "contentY").toReal() : 0.0;
}

void WheelScrollAnimator::setContentY(qreal value) {
    if (!m_flickable)
        return;
    m_applyingContentY = true;
    m_flickable->setProperty(m_horizontal ? "contentX" : "contentY", value);
    m_applyingContentY = false;
}

bool WheelScrollAnimator::scrollBy(qreal delta) {
    if (!m_flickable || !std::isfinite(delta))
        return false;
    const qreal base = m_scrolling ? m_destinationY : currentContentY();
    return scrollTo(base + delta);
}

bool WheelScrollAnimator::scrollTo(qreal position) {
    if (!m_flickable || !std::isfinite(position))
        return false;
    const qreal minimum = minimumContentY();
    const qreal maximum = maximumContentY();
    if (maximum <= minimum)
        return false;
    const qreal next = std::clamp(position, minimum, maximum);
    if (std::abs(next - currentContentY()) < kPositionEpsilon
        && std::abs(next - m_destinationY) < kPositionEpsilon)
        return false;

    QMetaObject::invokeMethod(m_flickable, "cancelFlick", Qt::DirectConnection);
    m_destinationY = next;
    if (!m_scrolling) {
        m_velocityY = 0.0;
        m_scrolling = true;
        emit scrollingChanged();
    }
    return true;
}

void WheelScrollAnimator::cancel() {
    const bool wasScrolling = m_scrolling;
    m_scrolling = false;
    m_velocityY = 0.0;
    m_destinationY = currentContentY();
    if (wasScrolling)
        emit scrollingChanged();
}

void WheelScrollAnimator::stopAt(qreal value) {
    setContentY(value);
    m_velocityY = 0.0;
    m_destinationY = value;
    const bool wasScrolling = m_scrolling;
    m_scrolling = false;
    if (wasScrolling)
        emit scrollingChanged();
}

void WheelScrollAnimator::advance(qreal frameTimeSeconds) {
    if (!m_scrolling)
        return;
    if (!m_flickable) {
        cancel();
        return;
    }
    const qreal minimum = minimumContentY();
    const qreal maximum = maximumContentY();
    m_destinationY = std::clamp(m_destinationY, minimum, maximum);
    if (!std::isfinite(frameTimeSeconds) || frameTimeSeconds <= 0.0)
        return;

    const qreal dt = std::clamp(frameTimeSeconds, 0.001, 0.025);
    const qreal current = currentContentY();
    const qreal error = current - m_destinationY;
    const qreal omega = m_angularFrequency;
    const qreal c = m_velocityY + omega * error;
    const qreal decay = std::exp(-omega * dt);
    const qreal nextError = (error + c * dt) * decay;
    m_velocityY = (m_velocityY - omega * c * dt) * decay;
    setContentY(std::clamp(m_destinationY + nextError, minimum, maximum));
    if (std::abs(nextError) < kPositionEpsilon && std::abs(m_velocityY) < kVelocityEpsilon)
        stopAt(m_destinationY);
}

void WheelScrollAnimator::externalContentPositionChanged() {
    if (m_applyingContentY || !m_flickable)
        return;
    if (m_flickable->property("dragging").toBool() || m_flickable->property("flicking").toBool())
        return;
    cancel();
}

void WheelScrollAnimator::flickableMotionStateChanged() {
    if (m_flickable && (m_flickable->property("dragging").toBool()
                        || m_flickable->property("flicking").toBool()))
        cancel();
}
