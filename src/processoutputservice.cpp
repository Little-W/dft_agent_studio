#include "processoutputservice.h"

#include <QDir>
#include <QFileInfo>
#include <algorithm>
#include <cerrno>
#include <utility>

#ifdef Q_OS_UNIX
#include <csignal>
#include <unistd.h>
#endif

namespace {
QList<qint64> descendantsOf(qint64 rootPid) {
    QList<qint64> found;
    QList<qint64> pending{rootPid};
    while (!pending.isEmpty()) {
        const qint64 parent = pending.takeLast();
        QFile children(QStringLiteral("/proc/%1/task/%1/children").arg(parent));
        if (!children.open(QIODevice::ReadOnly))
            continue;
        const QList<QByteArray> values = children.readAll().split(' ');
        for (const QByteArray &value : values) {
            bool ok = false;
            const qint64 pid = value.trimmed().toLongLong(&ok);
            if (ok && pid > 0) {
                found.append(pid);
                pending.append(pid);
            }
        }
    }
    return found;
}

QString exitStatusText(QProcess::ExitStatus status) {
    return status == QProcess::NormalExit ? QStringLiteral("normal") : QStringLiteral("crash");
}
} // namespace

NativeProcessOutputService::NativeProcessOutputService(QObject *parent)
    : QObject(parent), m_process(this), m_timeoutTimer(this), m_killTimer(this) {
    m_process.setProcessChannelMode(QProcess::MergedChannels);
    m_timeoutTimer.setSingleShot(true);
    m_killTimer.setSingleShot(true);
    connect(&m_process, &QProcess::readyReadStandardOutput,
            this, [this] { drainOutput(); });
    connect(&m_process, &QProcess::started, this, [this] {
        if (m_stopRequestedBeforeStart)
            stopProcess(StopReason::Cancelled);
        else {
            m_processPid = m_process.processId();
            m_timeoutTimer.start(m_timeoutMilliseconds);
        }
    });
    connect(&m_timeoutTimer, &QTimer::timeout, this, [this] {
        stopProcess(StopReason::Timeout);
    });
    connect(&m_killTimer, &QTimer::timeout, this, [this] {
#ifdef Q_OS_UNIX
        terminateProcessTree(SIGKILL);
#else
        m_process.kill();
#endif
        if (m_waitingForKillFinalize && m_process.state() == QProcess::NotRunning) {
            m_waitingForKillFinalize = false;
            finalizeProcess(m_pendingExitCode, m_pendingExitStatus);
        }
    });
    connect(&m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            m_error = m_process.errorString();
            finishFailedToStart();
        } else if (error == QProcess::WriteError || error == QProcess::ReadError) {
            m_error = m_process.errorString();
            stopProcess(StopReason::Failed);
        }
    });
    connect(&m_process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
            this, [this](int exitCode, QProcess::ExitStatus status) { finishProcess(exitCode, status); });
}

NativeProcessOutputService::~NativeProcessOutputService() {
    if (m_process.state() != QProcess::NotRunning) {
        stopProcess(StopReason::Cancelled);
        if (!m_process.waitForFinished(3500)) {
#ifdef Q_OS_UNIX
            terminateProcessTree(SIGKILL);
#else
            m_process.kill();
#endif
            m_process.waitForFinished(1000);
        }
    }
#ifdef Q_OS_UNIX
    if (m_killTimer.isActive())
        terminateProcessTree(SIGKILL);
#endif
}

bool NativeProcessOutputService::start(const QString &program, const QStringList &arguments,
                                      const QString &workingDirectory, const QString &outputPath,
                                      int timeoutMilliseconds, qint64 captureLimitBytes,
                                      QProcessEnvironment environment) {
    if (m_process.state() != QProcess::NotRunning || program.trimmed().isEmpty()
        || outputPath.trimmed().isEmpty() || timeoutMilliseconds < 1
        || captureLimitBytes < 256)
        return false;

    m_program = program;
    m_arguments = arguments;
    m_captureLimitBytes = captureLimitBytes;
    m_timeoutMilliseconds = timeoutMilliseconds;
    m_stopReason = StopReason::None;
    m_stopRequestedBeforeStart = false;
    m_processPid = 0;
    m_waitingForKillFinalize = false;
    m_error.clear();
    m_descendantPids.clear();
    m_decoder = QStringDecoder(QStringDecoder::Utf8);
    m_finalized = false;

    const QString absoluteWorkDir = workingDirectory.isEmpty()
        ? QDir::currentPath() : QFileInfo(workingDirectory).absoluteFilePath();
    const QFileInfo requestedLog(outputPath);
    m_outputPath = requestedLog.isAbsolute()
        ? requestedLog.absoluteFilePath() : QDir(absoluteWorkDir).absoluteFilePath(outputPath);
    if (!QDir().mkpath(QFileInfo(m_outputPath).absolutePath())) {
        m_error = QStringLiteral("无法创建命令输出日志目录。");
        m_finalized = true;
        return false;
    }
    m_outputFile.setFileName(m_outputPath);
    if (!m_outputFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        m_error = m_outputFile.errorString();
        m_finalized = true;
        return false;
    }

    m_process.setWorkingDirectory(absoluteWorkDir);
    m_process.setProcessEnvironment(environment.isEmpty()
        ? QProcessEnvironment::systemEnvironment() : environment);
#ifdef Q_OS_UNIX
    m_process.setChildProcessModifier([] {
        if (::setsid() < 0)
            ::_exit(127);
    });
#endif
    m_process.setProgram(program);
    m_process.setArguments(arguments);
    m_process.start();
    return true;
}

void NativeProcessOutputService::cancel() {
    if (m_finalized)
        return;
    if (m_process.state() == QProcess::Starting) {
        m_stopRequestedBeforeStart = true;
        return;
    }
    stopProcess(StopReason::Cancelled);
}

bool NativeProcessOutputService::isRunning() const {
    return !m_finalized && m_process.state() != QProcess::NotRunning;
}

QString NativeProcessOutputService::errorString() const {
    return m_error;
}

void NativeProcessOutputService::setOutputCallback(OutputCallback callback) {
    m_outputCallback = std::move(callback);
}

void NativeProcessOutputService::setFinishedCallback(FinishedCallback callback) {
    m_finishedCallback = std::move(callback);
}

QVariantMap NativeProcessOutputService::boundedFileText(const QString &path, qint64 limitBytes) {
    if (limitBytes < 256)
        return {{QStringLiteral("ok"), false},
                {QStringLiteral("error"), QStringLiteral("limit_bytes must be at least 256")}};
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {{QStringLiteral("ok"), false}, {QStringLiteral("error"), file.errorString()}};

    const qint64 size = file.size();
    bool truncated = false;
    QByteArray content;
    if (size <= limitBytes) {
        content = file.readAll();
    } else {
        truncated = true;
        const qint64 headSize = limitBytes / 2;
        const qint64 tailSize = limitBytes - headSize;
        const QByteArray head = file.read(headSize);
        if (!file.seek(size - tailSize))
            return {{QStringLiteral("ok"), false}, {QStringLiteral("error"), file.errorString()}};
        const QByteArray tail = file.read(tailSize);
        const qint64 omitted = size - headSize - tailSize;
        const QString marker = QStringLiteral("\n... %1 log bytes omitted from the in-memory view; complete output remains in %2 ...\n")
                                   .arg(omitted).arg(path);
        content = head + marker.toUtf8() + tail;
    }
    if (file.error() != QFileDevice::NoError)
        return {{QStringLiteral("ok"), false}, {QStringLiteral("error"), file.errorString()}};
    return {{QStringLiteral("ok"), true},
            {QStringLiteral("text"), QString::fromUtf8(content)},
            {QStringLiteral("truncated"), truncated},
            {QStringLiteral("file_bytes"), size}};
}

void NativeProcessOutputService::drainOutput() {
    const QByteArray bytes = m_process.readAllStandardOutput();
    if (bytes.isEmpty())
        return;
    if (m_outputFile.write(bytes) != bytes.size() || !m_outputFile.flush()) {
        m_error = m_outputFile.errorString();
        stopProcess(StopReason::Failed);
        return;
    }
    const QString text = m_decoder.decode(bytes);
    if (!text.isEmpty() && m_outputCallback)
        m_outputCallback(text);
}

void NativeProcessOutputService::stopProcess(StopReason reason) {
    if (m_finalized || m_process.state() == QProcess::NotRunning)
        return;
    if (m_stopReason == StopReason::None)
        m_stopReason = reason;
    m_timeoutTimer.stop();
#ifdef Q_OS_UNIX
    terminateProcessTree(SIGTERM);
#else
    m_process.terminate();
#endif
    if (!m_killTimer.isActive())
        m_killTimer.start(3000);
}

void NativeProcessOutputService::terminateProcessTree(int signalNumber) {
#ifdef Q_OS_UNIX
    const qint64 currentPid = m_process.processId();
    if (currentPid > 0)
        m_processPid = currentPid;
    const qint64 pid = m_processPid;
    if (pid <= 0)
        return;
    if (signalNumber == SIGTERM || m_descendantPids.isEmpty())
        m_descendantPids = descendantsOf(pid);
    for (auto it = m_descendantPids.crbegin(); it != m_descendantPids.crend(); ++it)
        ::kill(static_cast<pid_t>(*it), signalNumber);
    ::kill(-static_cast<pid_t>(pid), signalNumber);
    ::kill(static_cast<pid_t>(pid), signalNumber);
#else
    Q_UNUSED(signalNumber);
    m_process.kill();
#endif
}

void NativeProcessOutputService::finishProcess(int exitCode, QProcess::ExitStatus exitStatus) {
    if (m_finalized)
        return;
    if (m_stopReason != StopReason::None && m_killTimer.isActive()) {
        bool descendantsRemain = false;
#ifdef Q_OS_UNIX
        for (const qint64 pid : std::as_const(m_descendantPids)) {
            if (::kill(static_cast<pid_t>(pid), 0) == 0 || errno == EPERM) {
                descendantsRemain = true;
                break;
            }
        }
        if (!descendantsRemain && m_processPid > 0
            && (::kill(-static_cast<pid_t>(m_processPid), 0) == 0 || errno == EPERM))
            descendantsRemain = true;
#endif
        if (descendantsRemain) {
            m_waitingForKillFinalize = true;
            m_pendingExitCode = exitCode;
            m_pendingExitStatus = exitStatus;
            return;
        }
        m_killTimer.stop();
    }
    finalizeProcess(exitCode, exitStatus);
}

void NativeProcessOutputService::finalizeProcess(int exitCode, QProcess::ExitStatus exitStatus) {
    if (m_finalized)
        return;
    drainOutput();
    QString finalText = m_decoder.decode(QByteArray(1, '\0'));
    if (finalText.endsWith(QChar::Null))
        finalText.chop(1);
    if (!finalText.isEmpty() && m_outputCallback)
        m_outputCallback(finalText);
    m_timeoutTimer.stop();
    m_killTimer.stop();
    if (!m_outputFile.flush() && m_error.isEmpty())
        m_error = m_outputFile.errorString();
    m_outputFile.close();
    m_finalized = true;
    if (m_finishedCallback)
        m_finishedCallback(makeResult(exitCode, exitStatus));
}

void NativeProcessOutputService::finishFailedToStart() {
    if (m_finalized)
        return;
    m_timeoutTimer.stop();
    m_killTimer.stop();
    if (m_outputFile.isOpen())
        m_outputFile.close();
    m_finalized = true;
    if (m_finishedCallback)
        m_finishedCallback(makeResult(-1, QProcess::CrashExit));
}

QVariantMap NativeProcessOutputService::makeResult(int exitCode, QProcess::ExitStatus exitStatus) {
    const QVariantMap bounded = boundedFileText(m_outputPath, m_captureLimitBytes);
    const int returnCode = exitStatus == QProcess::CrashExit && exitCode > 0
        ? -exitCode : exitCode;
    QVariantMap result{
        {QStringLiteral("args"), QStringList{m_program} + m_arguments},
        {QStringLiteral("returncode"), returnCode},
        {QStringLiteral("exit_status"), exitStatusText(exitStatus)},
        {QStringLiteral("stdout"), bounded.value(QStringLiteral("text"))},
        {QStringLiteral("stderr"), QString{}},
        {QStringLiteral("stdout_file"), m_outputPath},
        {QStringLiteral("stdout_truncated"), bounded.value(QStringLiteral("truncated"), false)},
        {QStringLiteral("timed_out"), m_stopReason == StopReason::Timeout},
        {QStringLiteral("cancelled"), m_stopReason == StopReason::Cancelled},
    };
    if (!bounded.value(QStringLiteral("ok")).toBool() && m_error.isEmpty())
        m_error = bounded.value(QStringLiteral("error")).toString();
    if (!m_error.isEmpty())
        result.insert(QStringLiteral("error"), m_error);
    return result;
}
