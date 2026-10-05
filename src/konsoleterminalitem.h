#pragma once

#include <QImage>
#include <QMutex>
#include <QPointer>
#include <QQuickItem>
#include <QStringList>
#include <QTimer>

class TerminalInterface;
class QWidget;

#if DFT_AGENT_HAS_KONSOLE_PART
namespace KParts {
class ReadOnlyPart;
}
#endif

class KonsoleTerminalItem : public QQuickItem {
    Q_OBJECT
    Q_PROPERTY(QString workingDirectory READ workingDirectory WRITE setWorkingDirectory NOTIFY workingDirectoryChanged)
    Q_PROPERTY(QString program READ program WRITE setProgram NOTIFY programChanged)
    Q_PROPERTY(QStringList arguments READ arguments WRITE setArguments NOTIFY argumentsChanged)
    Q_PROPERTY(QString fontFamily READ fontFamily WRITE setFontFamily NOTIFY fontChanged)
    Q_PROPERTY(int fontPointSize READ fontPointSize WRITE setFontPointSize NOTIFY fontChanged)
    Q_PROPERTY(bool active READ active WRITE setActive NOTIFY activeChanged)
    Q_PROPERTY(bool ready READ ready NOTIFY readyChanged)
    Q_PROPERTY(QString terminalError READ terminalError NOTIFY terminalErrorChanged)

public:
    explicit KonsoleTerminalItem(QQuickItem *parent = nullptr);
    ~KonsoleTerminalItem() override;

    QString workingDirectory() const;
    void setWorkingDirectory(const QString &value);
    QString program() const;
    void setProgram(const QString &value);
    QStringList arguments() const;
    void setArguments(const QStringList &value);
    QString fontFamily() const;
    void setFontFamily(const QString &value);
    int fontPointSize() const;
    void setFontPointSize(int value);
    bool active() const;
    void setActive(bool value);
    bool ready() const;
    QString terminalError() const;

    Q_INVOKABLE void restart();
    Q_INVOKABLE bool sendText(const QString &text);

signals:
    void workingDirectoryChanged();
    void programChanged();
    void argumentsChanged();
    void fontChanged();
    void activeChanged();
    void readyChanged();
    void terminalErrorChanged();

protected:
    void geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry) override;
    QSGNode *updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *data) override;
    void keyPressEvent(QKeyEvent *event) override;
    void keyReleaseEvent(QKeyEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void focusInEvent(QFocusEvent *event) override;
    void focusOutEvent(QFocusEvent *event) override;
    void inputMethodEvent(QInputMethodEvent *event) override;

private:
    QString m_workingDirectory;
    QString m_program;
    QStringList m_arguments;
    QString m_fontFamily;
    int m_fontPointSize = 11;
    bool m_active = false;
    QString m_terminalError;
#if DFT_AGENT_HAS_KONSOLE_PART
    QPointer<KParts::ReadOnlyPart> m_part;
#endif
    QPointer<QWidget> m_terminalWidget;
    TerminalInterface *m_terminalInterface = nullptr;
    QTimer m_captureTimer;
    QTimer m_restartTimer;
    mutable QMutex m_frameMutex;
    QImage m_frame;

    void ensureTerminal();
    void destroyTerminal();
    void captureFrame();
    void resizeTerminal();
    void applyTerminalFont();
    void setTerminalError(const QString &value);
    void scheduleRestart();
    QWidget *eventTarget(const QPointF &position = {}) const;
    void forwardKeyEvent(QKeyEvent *event);
    void forwardMouseEvent(QMouseEvent *event);
    void forwardFocusEvent(QFocusEvent *event);
};
