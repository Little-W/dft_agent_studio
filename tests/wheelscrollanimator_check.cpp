#include "wheelscrollanimator.h"

#include <QCoreApplication>
#include <cmath>
#include <cstdio>

class FakeFlickable final : public QObject {
    Q_OBJECT
    Q_PROPERTY(qreal contentY READ contentY WRITE setContentY NOTIFY contentYChanged)
    Q_PROPERTY(qreal contentX READ contentX WRITE setContentX NOTIFY contentXChanged)
    Q_PROPERTY(qreal originY MEMBER m_originY CONSTANT)
    Q_PROPERTY(qreal topMargin MEMBER m_topMargin CONSTANT)
    Q_PROPERTY(qreal bottomMargin MEMBER m_bottomMargin CONSTANT)
    Q_PROPERTY(qreal contentHeight MEMBER m_contentHeight CONSTANT)
    Q_PROPERTY(qreal height MEMBER m_height CONSTANT)
    Q_PROPERTY(qreal originX MEMBER m_originX CONSTANT)
    Q_PROPERTY(qreal leftMargin MEMBER m_leftMargin CONSTANT)
    Q_PROPERTY(qreal rightMargin MEMBER m_rightMargin CONSTANT)
    Q_PROPERTY(qreal contentWidth MEMBER m_contentWidth CONSTANT)
    Q_PROPERTY(qreal width MEMBER m_width CONSTANT)
    Q_PROPERTY(bool dragging MEMBER m_dragging NOTIFY draggingChanged)
    Q_PROPERTY(bool flicking MEMBER m_flicking NOTIFY flickingChanged)

public:
    qreal contentY() const { return m_contentY; }
    void setContentY(qreal value) {
        if (std::abs(m_contentY - value) < 0.0001)
            return;
        m_contentY = value;
        emit contentYChanged();
    }
    qreal contentX() const { return m_contentX; }
    void setContentX(qreal value) {
        if (std::abs(m_contentX - value) < 0.0001)
            return;
        m_contentX = value;
        emit contentXChanged();
    }
    Q_INVOKABLE void cancelFlick() { m_flicking = false; }

signals:
    void contentYChanged();
    void contentXChanged();
    void draggingChanged();
    void flickingChanged();

private:
    qreal m_contentY = 100;
    qreal m_contentX = 100;
    qreal m_originY = 0;
    qreal m_topMargin = 0;
    qreal m_bottomMargin = 0;
    qreal m_contentHeight = 1000;
    qreal m_height = 100;
    qreal m_originX = 0;
    qreal m_leftMargin = 0;
    qreal m_rightMargin = 0;
    qreal m_contentWidth = 1000;
    qreal m_width = 100;
    bool m_dragging = false;
    bool m_flicking = false;
};

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    FakeFlickable flickable;
    WheelScrollAnimator animator;
    animator.setFlickable(&flickable);

    if (!animator.scrollBy(120) || !animator.scrolling() || animator.destinationPosition() != 220) {
        std::fprintf(stderr, "wheel target did not accumulate from current contentY\n");
        return 1;
    }
    animator.advance(1.0 / 165.0);
    const qreal firstStep = flickable.contentY();
    if (!(firstStep > 100 && firstStep < 220) || !animator.scrollBy(80)
        || animator.destinationPosition() != 300) {
        std::fprintf(stderr, "frame advance or in-flight target retargeting failed\n");
        return 1;
    }
    for (int frame = 0; frame < 600 && animator.scrolling(); ++frame)
        animator.advance(1.0 / 165.0);
    if (animator.scrolling() || std::abs(flickable.contentY() - 300) > 0.05) {
        std::fprintf(stderr, "spring did not settle at its accumulated destination\n");
        return 1;
    }
    if (!animator.scrollTo(500) || !animator.scrolling())
        return 1;
    flickable.setContentY(350);
    if (animator.scrolling() || animator.destinationPosition() != 350) {
        std::fprintf(stderr, "external scrollbar positioning did not cancel wheel motion\n");
        return 1;
    }
    if (!animator.scrollTo(5000) || animator.destinationPosition() != 900) {
        std::fprintf(stderr, "scroll bounds were not clamped\n");
        return 1;
    }
    animator.setHorizontal(true);
    if (!animator.scrollBy(120) || animator.destinationPosition() != 220) {
        std::fprintf(stderr, "horizontal wheel target did not use contentX\n");
        return 1;
    }
    for (int frame = 0; frame < 600 && animator.scrolling(); ++frame)
        animator.advance(1.0 / 165.0);
    if (animator.scrolling() || std::abs(flickable.contentX() - 220) > 0.05) {
        std::fprintf(stderr, "horizontal spring did not settle at its destination\n");
        return 1;
    }

    FakeFlickable coarseFlickable;
    FakeFlickable fineFlickable;
    WheelScrollAnimator coarseAnimator;
    WheelScrollAnimator fineAnimator;
    coarseAnimator.setFlickable(&coarseFlickable);
    fineAnimator.setFlickable(&fineFlickable);
    coarseAnimator.scrollBy(-64);
    for (int event = 0; event < 8; ++event)
        fineAnimator.scrollBy(-8);
    if (coarseAnimator.destinationPosition() != fineAnimator.destinationPosition()) {
        std::fprintf(stderr, "scroll target changed with input event resolution\n");
        return 1;
    }
    std::puts("wheel scroll trajectory, retargeting, external cancellation, and bounds passed");
    return 0;
}

#include "wheelscrollanimator_check.moc"
