#pragma once

#include <QFile>
#include <QObject>
#include <QProcess>
#include <QProcessEnvironment>
#include <QStringDecoder>
#include <QTimer>
#include <QVariantMap>

#include <functional>

class NativeProcessOutputService final : public QObject {
public:
    using OutputCallback = std::function<void(const QString &)>;
    using FinishedCallback = std::function<void(const QVariantMap &)>;

    explicit NativeProcessOutputService(QObject *parent = nullptr);
    ~NativeProcessOutputService() override;

    bool start(const QString &program, const QStringList &arguments,
               const QString &workingDirectory, const QString &outputPath,
               int timeoutMilliseconds, qint64 captureLimitBytes = 4 * 1024 * 1024,
               QProcessEnvironment environment = {});
    void cancel();
    bool isRunning() const;
    QString errorString() const;
    void setOutputCallback(OutputCallback callback);
    void setFinishedCallback(FinishedCallback callback);

    static QVariantMap boundedFileText(const QString &path,
                                       qint64 limitBytes = 4 * 1024 * 1024);

private:
    enum class StopReason { None, Timeout, Cancelled, Failed };

    void drainOutput();
    void stopProcess(StopReason reason);
    void terminateProcessTree(int signalNumber);
    void finishProcess(int exitCode, QProcess::ExitStatus exitStatus);
    void finalizeProcess(int exitCode, QProcess::ExitStatus exitStatus);
    void finishFailedToStart();
    QVariantMap makeResult(int exitCode, QProcess::ExitStatus exitStatus);

    QProcess m_process;
    QFile m_outputFile;
    QTimer m_timeoutTimer;
    QTimer m_killTimer;
    QString m_program;
    QStringList m_arguments;
    QString m_outputPath;
    QString m_error;
    QStringDecoder m_decoder{QStringDecoder::Utf8};
    qint64 m_captureLimitBytes = 4 * 1024 * 1024;
    qint64 m_processPid = 0;
    int m_timeoutMilliseconds = 0;
    QList<qint64> m_descendantPids;
    StopReason m_stopReason = StopReason::None;
    bool m_finalized = true;
    bool m_stopRequestedBeforeStart = false;
    bool m_waitingForKillFinalize = false;
    int m_pendingExitCode = -1;
    QProcess::ExitStatus m_pendingExitStatus = QProcess::CrashExit;
    OutputCallback m_outputCallback;
    FinishedCallback m_finishedCallback;
};
