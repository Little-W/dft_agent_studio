#include "../src/processoutputservice.h"

#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QTemporaryDir>
#include <QTimer>

#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <cstdio>
#include <functional>

namespace {
QVariantMap run(const QString &program, const QStringList &arguments, const QString &root,
                int timeoutMs, qint64 limitBytes,
                std::function<void(NativeProcessOutputService &)> schedule = {},
                QString *incremental = nullptr) {
    NativeProcessOutputService service;
    QVariantMap result;
    QEventLoop loop;
    service.setOutputCallback([incremental](const QString &text) {
        if (incremental)
            *incremental += text;
    });
    service.setFinishedCallback([&](const QVariantMap &value) {
        result = value;
        loop.quit();
    });
    if (!service.start(program, arguments, root, QStringLiteral("captured.log"), timeoutMs, limitBytes))
        return {{QStringLiteral("start_failed"), true}, {QStringLiteral("error"), service.errorString()}};
    if (schedule)
        schedule(service);
    QTimer::singleShot(timeoutMs + 5000, &loop, [&] {
        service.cancel();
        loop.quit();
    });
    loop.exec();
    return result;
}

bool processExists(qint64 pid) {
#ifdef Q_OS_UNIX
    if (::kill(static_cast<pid_t>(pid), 0) == 0)
        return true;
    return errno == EPERM;
#else
    Q_UNUSED(pid);
    return false;
#endif
}
} // namespace

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    QTemporaryDir temporary;
    if (!temporary.isValid())
        return EXIT_FAILURE;
    const QString root = temporary.path();

    const QString boundedPath = QDir(root).filePath(QStringLiteral("bounded.bin"));
    QFile boundedFile(boundedPath);
    if (!boundedFile.open(QIODevice::WriteOnly))
        return EXIT_FAILURE;
    boundedFile.write(QByteArray(600, 'h'));
    boundedFile.write("TAIL");
    boundedFile.close();
    const QVariantMap bounded = NativeProcessOutputService::boundedFileText(boundedPath, 256);
    if (!bounded.value(QStringLiteral("ok")).toBool()
        || !bounded.value(QStringLiteral("truncated")).toBool()
        || !bounded.value(QStringLiteral("text")).toString().startsWith(QString(128, QLatin1Char('h')))
        || !bounded.value(QStringLiteral("text")).toString().contains(QStringLiteral("TAIL"))
        || NativeProcessOutputService::boundedFileText(boundedPath, 255)
               .value(QStringLiteral("ok")).toBool())
        { std::fprintf(stderr, "bounded read check failed\n"); return EXIT_FAILURE; }

    QString incremental;
    const QString outputScript = QStringLiteral(
        "printf 'HEAD'; i=0; while [ $i -lt 1200 ]; do printf 'line-%04d\\n' $i; i=$((i+1)); done; printf 'TAIL\\377'");
    const QVariantMap output = run(QStringLiteral("/bin/sh"), {QStringLiteral("-c"), outputScript},
                                   root, 5000, 512, {}, &incremental);
    QFile fullLog(QDir(root).filePath(QStringLiteral("captured.log")));
    if (!output.contains(QStringLiteral("returncode"))
        || output.value(QStringLiteral("returncode")).toInt() != 0
        || output.value(QStringLiteral("timed_out")).toBool()
        || output.value(QStringLiteral("cancelled")).toBool()
        || !output.value(QStringLiteral("stdout_truncated")).toBool()
        || !output.value(QStringLiteral("stdout")).toString().startsWith(QStringLiteral("HEAD"))
        || !output.value(QStringLiteral("stdout")).toString().contains(QStringLiteral("log bytes omitted"))
        || !output.value(QStringLiteral("stdout")).toString().endsWith(QString::fromUtf8("TAIL�"))
        || !incremental.startsWith(QStringLiteral("HEAD"))
        || !incremental.endsWith(QString::fromUtf8("TAIL�"))
        || !fullLog.open(QIODevice::ReadOnly) || fullLog.size() < 8000
        || !fullLog.readAll().endsWith(QByteArray("TAIL\xff", 5)))
        { std::fprintf(stderr, "spooled output check failed: start_failed=%d error=%s return=%d status=%s truncated=%d stdout=%s inc=%s logsize=%lld\n",
            output.value(QStringLiteral("start_failed")).toBool(), qPrintable(output.value(QStringLiteral("error")).toString()),
            output.value(QStringLiteral("returncode")).toInt(), qPrintable(output.value(QStringLiteral("exit_status")).toString()),
            output.value(QStringLiteral("stdout_truncated")).toBool(), qPrintable(output.value(QStringLiteral("stdout")).toString()),
            qPrintable(incremental), static_cast<long long>(fullLog.size())); return EXIT_FAILURE; }

    QString splitUtf8Incremental;
    const QVariantMap splitUtf8 = run(QStringLiteral("/bin/sh"),
        {QStringLiteral("-c"), QStringLiteral("printf '\\344'; sleep 0.05; printf '\\270\\255'")},
        root, 3000, 256, {}, &splitUtf8Incremental);
    if (splitUtf8.value(QStringLiteral("stdout")).toString() != QStringLiteral("中")
        || splitUtf8Incremental != QStringLiteral("中")) {
        std::fprintf(stderr, "split UTF-8 sequence did not decode incrementally\n");
        return EXIT_FAILURE;
    }

    const QString childPidPath = QDir(root).filePath(QStringLiteral("child.pid"));
    const QString childScript = QStringLiteral("trap '' TERM; sleep 30 & echo $! > '%1'; wait").arg(childPidPath);
    const QVariantMap timeout = run(QStringLiteral("/bin/sh"), {QStringLiteral("-c"), childScript},
                                    root, 250, 256);
    QFile childPidFile(childPidPath);
    if (!childPidFile.open(QIODevice::ReadOnly)) {
        std::fprintf(stderr, "child pid file was not written\n");
        return EXIT_FAILURE;
    }
    bool pidOk = false;
    const qint64 childPid = childPidFile.readAll().trimmed().toLongLong(&pidOk);
    if (!timeout.value(QStringLiteral("timed_out")).toBool()
        || timeout.value(QStringLiteral("cancelled")).toBool() || !pidOk || childPid <= 0) {
        std::fprintf(stderr, "timeout classification or child PID invalid: timeout=%d cancelled=%d pidok=%d pid=%lld\n",
            timeout.value(QStringLiteral("timed_out")).toBool(),
            timeout.value(QStringLiteral("cancelled")).toBool(), pidOk,
            static_cast<long long>(childPid));
        return EXIT_FAILURE;
    }
    QEventLoop childWait;
    QTimer childPoll;
    childPoll.setInterval(25);
    QObject::connect(&childPoll, &QTimer::timeout, &childWait, [&] {
        if (!processExists(childPid))
            childWait.quit();
    });
    QTimer::singleShot(1500, &childWait, &QEventLoop::quit);
    childPoll.start();
    childWait.exec();
    if (processExists(childPid)) {
        std::fprintf(stderr, "timed-out child remains: %lld\n", static_cast<long long>(childPid));
        return EXIT_FAILURE;
    }

    const QVariantMap cancelled = run(QStringLiteral("/bin/sh"),
        {QStringLiteral("-c"), QStringLiteral("sleep 30")}, root, 5000, 256,
        [](NativeProcessOutputService &service) {
            QTimer::singleShot(150, &service, &NativeProcessOutputService::cancel);
        });
    if (!cancelled.value(QStringLiteral("cancelled")).toBool()
        || cancelled.value(QStringLiteral("timed_out")).toBool())
        { std::fprintf(stderr, "cancel classification failed\n"); return EXIT_FAILURE; }

    return EXIT_SUCCESS;
}
