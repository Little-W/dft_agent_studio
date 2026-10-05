#include "konsoleterminalitem.h"

#if DFT_AGENT_HAS_KONSOLE_PART
#include <KParts/PartLoader>
#include <KParts/ReadOnlyPart>
#include <KPluginMetaData>
#include <kde_terminal_interface.h>
#endif

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QFont>
#include <QFocusEvent>
#include <QInputMethodEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QMutexLocker>
#include <QQuickWindow>
#include <QSGImageNode>
#include <QSGTexture>
#include <QStandardPaths>
#include <QWidget>
#include <QWheelEvent>

namespace {

QString konsolePartPath() {
#if DFT_AGENT_HAS_KONSOLE_PART
    for (const QString &libraryPath : QCoreApplication::libraryPaths()) {
        const QString candidate = QDir(libraryPath).filePath(QStringLiteral("kf6/parts/konsolepart.so"));
        if (QFileInfo(candidate).isFile())
            return candidate;
    }
#endif
    return {};
}

QWidget *terminalDisplay(QWidget *root) {
#if DFT_AGENT_HAS_KONSOLE_PART
    if (!root)
        return nullptr;
    const auto children = root->findChildren<QWidget *>();
    for (QWidget *child : children) {
        if (QString::fromLatin1(child->metaObject()->className()).contains(QStringLiteral("TerminalDisplay")))
            return child;
    }
    return root;
#else
    Q_UNUSED(root)
    return nullptr;
#endif
}

} // namespace

KonsoleTerminalItem::KonsoleTerminalItem(QQuickItem *parent)
    : QQuickItem(parent) {
    setAcceptedMouseButtons(Qt::AllButtons);
    setAcceptHoverEvents(true);
    setFlag(ItemIsFocusScope, true);
    setFlag(ItemAcceptsInputMethod, true);
    setFlag(ItemHasContents, true);
    m_captureTimer.setInterval(33);
    connect(&m_captureTimer, &QTimer::timeout, this, &KonsoleTerminalItem::captureFrame);
    m_restartTimer.setSingleShot(true);
    connect(&m_restartTimer, &QTimer::timeout, this, &KonsoleTerminalItem::restart);
}

KonsoleTerminalItem::~KonsoleTerminalItem() {
    m_captureTimer.stop();
    m_restartTimer.stop();
    destroyTerminal();
}

QString KonsoleTerminalItem::workingDirectory() const { return m_workingDirectory; }

void KonsoleTerminalItem::setWorkingDirectory(const QString &value) {
    const QString trimmed = value.trimmed();
    const QString normalized = trimmed.isEmpty() ? QString{} : QFileInfo(trimmed).absoluteFilePath();
    if (m_workingDirectory == normalized)
        return;
    m_workingDirectory = normalized;
    emit workingDirectoryChanged();
    scheduleRestart();
}

QString KonsoleTerminalItem::program() const { return m_program; }

void KonsoleTerminalItem::setProgram(const QString &value) {
    const QString normalized = value.trimmed();
    if (m_program == normalized)
        return;
    m_program = normalized;
    emit programChanged();
    scheduleRestart();
}

QStringList KonsoleTerminalItem::arguments() const { return m_arguments; }

void KonsoleTerminalItem::setArguments(const QStringList &value) {
    if (m_arguments == value)
        return;
    m_arguments = value;
    emit argumentsChanged();
    scheduleRestart();
}

QString KonsoleTerminalItem::fontFamily() const { return m_fontFamily; }

void KonsoleTerminalItem::setFontFamily(const QString &value) {
    const QString normalized = value.trimmed();
    if (m_fontFamily == normalized)
        return;
    m_fontFamily = normalized;
    emit fontChanged();
    applyTerminalFont();
}

int KonsoleTerminalItem::fontPointSize() const { return m_fontPointSize; }

void KonsoleTerminalItem::setFontPointSize(int value) {
    const int normalized = qBound(8, value, 28);
    if (m_fontPointSize == normalized)
        return;
    m_fontPointSize = normalized;
    emit fontChanged();
    applyTerminalFont();
}

bool KonsoleTerminalItem::active() const { return m_active; }

void KonsoleTerminalItem::setActive(bool value) {
    if (m_active == value)
        return;
    m_active = value;
    emit activeChanged();
    if (m_active) {
        forceActiveFocus();
        ensureTerminal();
        m_captureTimer.start();
        captureFrame();
    } else {
        m_captureTimer.stop();
    }
}

bool KonsoleTerminalItem::ready() const {
#if DFT_AGENT_HAS_KONSOLE_PART
    return m_terminalWidget && m_terminalInterface;
#else
    return false;
#endif
}
QString KonsoleTerminalItem::terminalError() const { return m_terminalError; }

void KonsoleTerminalItem::restart() {
    destroyTerminal();
    if (m_active)
        ensureTerminal();
}

bool KonsoleTerminalItem::sendText(const QString &text) {
#if DFT_AGENT_HAS_KONSOLE_PART
    ensureTerminal();
    if (!m_terminalInterface || text.isEmpty())
        return false;
    m_terminalInterface->sendInput(text);
    return true;
#else
    Q_UNUSED(text)
    return false;
#endif
}

void KonsoleTerminalItem::geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry) {
    QQuickItem::geometryChange(newGeometry, oldGeometry);
    resizeTerminal();
    if (m_active)
        captureFrame();
}

QSGNode *KonsoleTerminalItem::updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *data) {
    Q_UNUSED(data)
    auto *node = static_cast<QSGImageNode *>(oldNode);

    QImage frame;
    {
        QMutexLocker locker(&m_frameMutex);
        frame = m_frame;
    }
    if (frame.isNull() || !window()) {
        delete node;
        return nullptr;
    }

    if (!node) {
        node = window()->createImageNode();
        node->setOwnsTexture(true);
        node->setFiltering(QSGTexture::Nearest);
    }
    QSGTexture *oldTexture = node->texture();
    node->setOwnsTexture(false);
    node->setTexture(window()->createTextureFromImage(frame));
    delete oldTexture;
    node->setOwnsTexture(true);
    node->setRect(boundingRect());
    return node;
}

void KonsoleTerminalItem::ensureTerminal() {
    if (ready() || !m_active)
        return;
    if (!QFileInfo(m_workingDirectory).isDir()) {
        setTerminalError(QStringLiteral("项目工作目录不可用"));
        return;
    }
#if DFT_AGENT_HAS_KONSOLE_PART
    const QString pluginPath = konsolePartPath();
    if (pluginPath.isEmpty()) {
        setTerminalError(QStringLiteral("未安装 Konsole 终端组件"));
        return;
    }
    const KPluginMetaData metadata(pluginPath);
    if (!metadata.isValid()) {
        setTerminalError(QStringLiteral("Konsole 终端组件信息无效"));
        return;
    }
    const auto result = KParts::PartLoader::instantiatePart<KParts::ReadOnlyPart>(metadata, nullptr, this);
    if (!result.plugin) {
        setTerminalError(result.errorString.isEmpty() ? QStringLiteral("无法载入 Konsole 终端组件") : result.errorString);
        return;
    }
    m_part = result.plugin;
    m_terminalInterface = qobject_cast<TerminalInterface *>(m_part.data());
    m_terminalWidget = m_part->widget();
    if (!m_terminalInterface || !m_terminalWidget) {
        setTerminalError(QStringLiteral("Konsole 终端接口不可用"));
        destroyTerminal();
        return;
    }
    m_terminalWidget->setAttribute(Qt::WA_DontShowOnScreen, true);
    m_terminalWidget->setWindowFlag(Qt::Tool, true);
    m_terminalWidget->setFocusPolicy(Qt::StrongFocus);
    if (window() && window()->screen())
        m_terminalWidget->setScreen(window()->screen());
    resizeTerminal();
    m_terminalWidget->show();
    m_terminalWidget->ensurePolished();
    if (m_program.isEmpty()) {
        m_terminalInterface->showShellInDir(m_workingDirectory);
    } else {
        const QString executable = QStandardPaths::findExecutable(m_program);
        if (executable.isEmpty()) {
            setTerminalError(tr("找不到程序：%1").arg(m_program));
            destroyTerminal();
            return;
        }
        const QString environment = QStandardPaths::findExecutable(QStringLiteral("env"));
        if (!m_workingDirectory.isEmpty() && !environment.isEmpty()) {
            QStringList launchArguments{
                QFileInfo(environment).fileName(),
                QStringLiteral("-C"),
                m_workingDirectory,
                executable,
            };
            launchArguments.append(m_arguments);
            m_terminalInterface->startProgram(environment, launchArguments);
        } else {
            QStringList launchArguments{QFileInfo(executable).fileName()};
            launchArguments.append(m_arguments);
            m_terminalInterface->startProgram(executable, launchArguments);
        }
    }
    applyTerminalFont();
    QTimer::singleShot(0, this, [this] {
        if (ready()) {
            applyTerminalFont();
            captureFrame();
        }
    });
    setTerminalError({});
    emit readyChanged();
    captureFrame();
#else
    setTerminalError(QStringLiteral("未安装 Konsole 终端组件，请使用外部终端"));
#endif
}

void KonsoleTerminalItem::destroyTerminal() {
#if DFT_AGENT_HAS_KONSOLE_PART
    const bool wasReady = ready();
    m_terminalInterface = nullptr;
    if (m_part)
        delete m_part.data();
    m_part = nullptr;
    m_terminalWidget = nullptr;
    {
        QMutexLocker locker(&m_frameMutex);
        m_frame = {};
    }
    update();
    if (wasReady)
        emit readyChanged();
#else
    m_terminalInterface = nullptr;
    m_terminalWidget = nullptr;
    m_frame = {};
    update();
#endif
}

void KonsoleTerminalItem::captureFrame() {
#if DFT_AGENT_HAS_KONSOLE_PART
    if (!m_active || !ready() || width() < 2 || height() < 2)
        return;
    resizeTerminal();
    const qreal devicePixelRatio = window() ? window()->effectiveDevicePixelRatio() : 1.0;
    const QSize logicalSize(qMax(1, qRound(width())), qMax(1, qRound(height())));
    QImage frame(logicalSize * devicePixelRatio, QImage::Format_RGB32);
    frame.setDevicePixelRatio(devicePixelRatio);
    frame.fill(Qt::black);
    m_terminalWidget->render(&frame, QPoint(), QRegion(), QWidget::DrawChildren);
    {
        QMutexLocker locker(&m_frameMutex);
        m_frame = std::move(frame);
    }
    update();
#endif
}

void KonsoleTerminalItem::resizeTerminal() {
#if DFT_AGENT_HAS_KONSOLE_PART
    if (!m_terminalWidget)
        return;
    m_terminalWidget->resize(qMax(1, qRound(width())), qMax(1, qRound(height())));
#endif
}

void KonsoleTerminalItem::applyTerminalFont() {
#if DFT_AGENT_HAS_KONSOLE_PART
    if (!ready())
        return;

    QFont profileFont = m_terminalInterface->profileProperty(QStringLiteral("Font")).value<QFont>();
    const int profileSize = profileFont.pointSize() > 0 ? profileFont.pointSize() : 10;
    QObject *sessionController = nullptr;
    const auto findController = [&sessionController](QObject *root) {
        if (!root || sessionController)
            return;
        const auto children = root->findChildren<QObject *>();
        for (QObject *child : children) {
            if (QString::fromLatin1(child->metaObject()->className()).contains(QStringLiteral("SessionController"))) {
                sessionController = child;
                return;
            }
        }
    };
    findController(m_part.data());
    findController(m_terminalWidget.data());
    if (sessionController && QMetaObject::invokeMethod(sessionController, "resetFontSize", Qt::DirectConnection)) {
        const char *method = m_fontPointSize >= profileSize ? "increaseFontSize" : "decreaseFontSize";
        for (int step = 0; step < qAbs(m_fontPointSize - profileSize); ++step)
            QMetaObject::invokeMethod(sessionController, method, Qt::DirectConnection);
    }

    if (QWidget *display = terminalDisplay(m_terminalWidget.data())) {
        QFont displayFont = display->font();
        if (!m_fontFamily.isEmpty())
            displayFont.setFamily(m_fontFamily);
        displayFont.setPointSize(m_fontPointSize);
        display->setFont(displayFont);
        display->update();
    }
    captureFrame();
#endif
}

void KonsoleTerminalItem::setTerminalError(const QString &value) {
    if (m_terminalError == value)
        return;
    m_terminalError = value;
    emit terminalErrorChanged();
}

void KonsoleTerminalItem::scheduleRestart() {
    if (m_active || m_terminalWidget)
        m_restartTimer.start(0);
}

QWidget *KonsoleTerminalItem::eventTarget(const QPointF &position) const {
#if DFT_AGENT_HAS_KONSOLE_PART
    QWidget *display = terminalDisplay(m_terminalWidget.data());
    if (!display || position.isNull())
        return display;
    QWidget *child = m_terminalWidget->childAt(position.toPoint());
    return child ? child : display;
#else
    Q_UNUSED(position)
    return nullptr;
#endif
}

void KonsoleTerminalItem::forwardKeyEvent(QKeyEvent *event) {
    QWidget *target = eventTarget();
    if (!target)
        return;
    QKeyEvent forwarded(
        event->type(), event->key(), event->modifiers(), event->nativeScanCode(),
        event->nativeVirtualKey(), event->nativeModifiers(), event->text(),
        event->isAutoRepeat(), event->count(), event->device()
    );
    QApplication::sendEvent(target, &forwarded);
    event->setAccepted(forwarded.isAccepted());
}

void KonsoleTerminalItem::forwardMouseEvent(QMouseEvent *event) {
    QWidget *target = eventTarget(event->position());
    if (!target || !m_terminalWidget)
        return;
    const QPoint localPoint = target->mapFrom(m_terminalWidget, event->position().toPoint());
    const QPoint globalPoint = target->mapToGlobal(localPoint);
    QMouseEvent forwarded(
        event->type(), QPointF(localPoint), QPointF(globalPoint), event->button(),
        event->buttons(), event->modifiers(), event->pointingDevice()
    );
    QApplication::sendEvent(target, &forwarded);
    event->setAccepted(forwarded.isAccepted());
}

void KonsoleTerminalItem::forwardFocusEvent(QFocusEvent *event) {
    QWidget *target = eventTarget();
    if (!target)
        return;
    QFocusEvent forwarded(event->type(), event->reason());
    QApplication::sendEvent(target, &forwarded);
}

void KonsoleTerminalItem::keyPressEvent(QKeyEvent *event) { forwardKeyEvent(event); }
void KonsoleTerminalItem::keyReleaseEvent(QKeyEvent *event) { forwardKeyEvent(event); }
void KonsoleTerminalItem::mousePressEvent(QMouseEvent *event) { forceActiveFocus(); forwardMouseEvent(event); }
void KonsoleTerminalItem::mouseReleaseEvent(QMouseEvent *event) { forwardMouseEvent(event); }
void KonsoleTerminalItem::mouseMoveEvent(QMouseEvent *event) { forwardMouseEvent(event); }
void KonsoleTerminalItem::mouseDoubleClickEvent(QMouseEvent *event) { forceActiveFocus(); forwardMouseEvent(event); }

void KonsoleTerminalItem::wheelEvent(QWheelEvent *event) {
    QWidget *target = eventTarget(event->position());
    if (!target || !m_terminalWidget)
        return;
    const QPoint localPoint = target->mapFrom(m_terminalWidget, event->position().toPoint());
    QWheelEvent forwarded(
        QPointF(localPoint), event->globalPosition(), event->pixelDelta(), event->angleDelta(),
        event->buttons(), event->modifiers(), event->phase(), event->inverted(),
        event->source(), event->pointingDevice()
    );
    QApplication::sendEvent(target, &forwarded);
    event->setAccepted(forwarded.isAccepted());
}

void KonsoleTerminalItem::focusInEvent(QFocusEvent *event) { forwardFocusEvent(event); }
void KonsoleTerminalItem::focusOutEvent(QFocusEvent *event) { forwardFocusEvent(event); }

void KonsoleTerminalItem::inputMethodEvent(QInputMethodEvent *event) {
    QWidget *target = eventTarget();
    if (target)
        QApplication::sendEvent(target, event);
}
