#pragma once

#include <QObject>
#include <QPointer>

class WheelScrollAnimator : public QObject {
    Q_OBJECT
    Q_PROPERTY(QObject *flickable READ flickable WRITE setFlickable NOTIFY flickableChanged)
    Q_PROPERTY(bool horizontal READ horizontal WRITE setHorizontal NOTIFY horizontalChanged)
    Q_PROPERTY(qreal angularFrequency READ angularFrequency WRITE setAngularFrequency NOTIFY angularFrequencyChanged)
    Q_PROPERTY(bool scrolling READ scrolling NOTIFY scrollingChanged)
    Q_PROPERTY(qreal destinationPosition READ destinationPosition NOTIFY scrollingChanged)

public:
    explicit WheelScrollAnimator(QObject *parent = nullptr);

    QObject *flickable() const;
    void setFlickable(QObject *flickable);
    bool horizontal() const;
    void setHorizontal(bool horizontal);
    qreal angularFrequency() const;
    void setAngularFrequency(qreal frequency);
    bool scrolling() const;
    qreal destinationPosition() const;

    Q_INVOKABLE bool scrollBy(qreal delta);
    Q_INVOKABLE bool scrollTo(qreal position);
    Q_INVOKABLE void cancel();
    Q_INVOKABLE void advance(qreal frameTimeSeconds);

signals:
    void flickableChanged();
    void horizontalChanged();
    void angularFrequencyChanged();
    void scrollingChanged();

private slots:
    void externalContentPositionChanged();
    void flickableMotionStateChanged();

private:
    qreal minimumContentY() const;
    qreal maximumContentY() const;
    qreal currentContentY() const;
    void setContentY(qreal value);
    void stopAt(qreal value);

    QPointer<QObject> m_flickable;
    bool m_horizontal = false;
    qreal m_angularFrequency = 40.0;
    qreal m_destinationY = 0.0;
    qreal m_velocityY = 0.0;
    bool m_scrolling = false;
    bool m_applyingContentY = false;
};
