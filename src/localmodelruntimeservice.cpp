#include "localmodelruntimeservice.h"

#include <QDir>
#include <QCoreApplication>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>
#include <QThread>
#include <QUrl>

#include <cmath>

namespace {
void setError(QString *error, const QString &message) {
    if (error)
        *error = message;
}

QString absoluteModelPath(QString path, const QString &agentRoot) {
    path = path.trimmed();
    if (path == QStringLiteral("~"))
        path = QDir::homePath();
    else if (path.startsWith(QStringLiteral("~/")))
        path = QDir::home().filePath(path.mid(2));
    if (!QFileInfo(path).isAbsolute())
        path = QDir(agentRoot).absoluteFilePath(path);
    return QDir::cleanPath(path);
}
}

QString LocalModelRuntimeService::findLlamaServerBinary(const QString &agentRoot,
                                                        const QString &configuredPath,
                                                        QString *error) {
    if (!configuredPath.trimmed().isEmpty()) {
        const QString path = absoluteModelPath(configuredPath, agentRoot);
        if (QFileInfo(path).isFile())
            return path;
        setError(error, QStringLiteral("Configured llama-server binary does not exist: %1").arg(path));
        return {};
    }

    const QString configured = qEnvironmentVariable("DFT_AGENT_LLAMA_SERVER").trimmed();
    if (!configured.isEmpty()) {
        const QString path = absoluteModelPath(configured, agentRoot);
        if (QFileInfo(path).isFile())
            return path;
        setError(error, QStringLiteral("Configured llama-server binary does not exist: %1").arg(path));
        return {};
    }

    const QString applicationSidecar = QDir(QCoreApplication::applicationDirPath()).absoluteFilePath(
        QStringLiteral("../libexec/dft-agent-studio/llama-server"));
    if (QFileInfo(applicationSidecar).isFile())
        return QDir::cleanPath(applicationSidecar);

    const QString checkoutBinary = QDir(agentRoot).absoluteFilePath(
        QStringLiteral("../llama.cpp/build/bin/llama-server"));
    if (QFileInfo(checkoutBinary).isFile())
        return QDir::cleanPath(checkoutBinary);
    const QString systemBinary = QStandardPaths::findExecutable(QStringLiteral("llama-server"));
    if (!systemBinary.isEmpty())
        return systemBinary;
    setError(error, QStringLiteral("llama-server was not found; install the Studio llama.cpp sidecar or set DFT_AGENT_LLAMA_SERVER."));
    return {};
}

int LocalModelRuntimeService::gpuFitTargetMiB(double maximumGpuMemoryGiB, int totalGpuMemoryMiB) {
    if (!std::isfinite(maximumGpuMemoryGiB) || maximumGpuMemoryGiB < 0.0)
        return -1;
    if (maximumGpuMemoryGiB == 0.0 || totalGpuMemoryMiB <= 0)
        return 512;
    const qint64 requested = static_cast<qint64>(std::nearbyint(maximumGpuMemoryGiB * 1024.0));
    if (requested >= totalGpuMemoryMiB)
        return 256;
    return qMax(256, totalGpuMemoryMiB - static_cast<int>(requested));
}

bool LocalModelRuntimeService::llamaServerCommand(const QVariantMap &model,
                                                   const QString &agentRoot,
                                                   const QString &serverBinary,
                                                   int totalGpuMemoryMiB,
                                                   QStringList *command,
                                                   QString *error) {
    if (!command) {
        setError(error, QStringLiteral("A command destination is required."));
        return false;
    }
    command->clear();
    const QUrl apiBase(model.value(QStringLiteral("apiBase")).toString().trimmed());
    const QString host = apiBase.host().toLower();
    const QString path = apiBase.path().isEmpty() ? QStringLiteral("/") : apiBase.path();
    if (apiBase.scheme() != QStringLiteral("http")
        || (host != QStringLiteral("127.0.0.1") && host != QStringLiteral("localhost")
            && host != QStringLiteral("::1"))
        || (path != QStringLiteral("/") && path != QStringLiteral("/v1"))) {
        setError(error, QStringLiteral("llama.cpp requires a local HTTP API URL with no custom path."));
        return false;
    }
    if (apiBase.port(11503) < 1 || apiBase.port(11503) > 65'535) {
        setError(error, QStringLiteral("llama.cpp API port is invalid."));
        return false;
    }

    const QString binary = serverBinary.trimmed();
    if (binary.isEmpty() || !QFileInfo(binary).isFile()) {
        setError(error, QStringLiteral("llama-server binary is unavailable: %1").arg(binary));
        return false;
    }
    const QString modelPath = absoluteModelPath(model.value(QStringLiteral("baseModelPath")).toString(), agentRoot);
    if (!QFileInfo(modelPath).isFile() || QFileInfo(modelPath).suffix().compare(QStringLiteral("gguf"), Qt::CaseInsensitive) != 0) {
        setError(error, QStringLiteral("GGUF base model is unavailable: %1").arg(modelPath));
        return false;
    }
    const QString adapterValue = model.value(QStringLiteral("adapterPath")).toString().trimmed();
    const QString adapterPath = adapterValue.isEmpty() ? QString{} : absoluteModelPath(adapterValue, agentRoot);
    if (!adapterPath.isEmpty()
        && (!QFileInfo(adapterPath).isFile()
            || QFileInfo(adapterPath).suffix().compare(QStringLiteral("gguf"), Qt::CaseInsensitive) != 0)) {
        setError(error, QStringLiteral("GGUF LoRA adapter is unavailable: %1").arg(adapterPath));
        return false;
    }

    const QString modelId = model.value(QStringLiteral("modelId")).toString().trimmed();
    const int contextWindow = model.value(QStringLiteral("contextWindow"), 65'536).toInt();
    const int maximumNewTokens = model.value(QStringLiteral("maximumNewTokens"), 1'024).toInt();
    const int port = apiBase.port(11'503);
    const QString inferenceMode = model.value(QStringLiteral("inferenceMode"), QStringLiteral("cpu_gpu")).toString();
    if (modelId.isEmpty() || contextWindow < 2'048 || contextWindow > 2'000'000
        || maximumNewTokens < 1 || maximumNewTokens > contextWindow) {
        setError(error, QStringLiteral("Local model identity or token limits are invalid."));
        return false;
    }
    if (inferenceMode != QStringLiteral("gpu") && inferenceMode != QStringLiteral("cpu_gpu")
        && inferenceMode != QStringLiteral("cpu")) {
        setError(error, QStringLiteral("Inference mode must be gpu, cpu_gpu, or cpu."));
        return false;
    }
    const double gpuMemoryGiB = model.value(QStringLiteral("gpuMemoryGiB")).toDouble();
    if (!std::isfinite(gpuMemoryGiB) || gpuMemoryGiB < 0.0) {
        setError(error, QStringLiteral("Maximum GPU memory must be a finite non-negative value."));
        return false;
    }

    const int idealThreads = qMax(1, QThread::idealThreadCount());
    const int threads = qMax(1, idealThreads / 2);
    *command = {binary, QStringLiteral("--model"), modelPath,
                QStringLiteral("--alias"), modelId,
                QStringLiteral("--ctx-size"), QString::number(contextWindow),
                QStringLiteral("--parallel"), QStringLiteral("1"),
                QStringLiteral("--flash-attn"), QStringLiteral("on"),
                QStringLiteral("--cache-type-k"), QStringLiteral("q4_0"),
                QStringLiteral("--cache-type-v"), QStringLiteral("q4_0"),
                QStringLiteral("--batch-size"), QStringLiteral("512"),
                QStringLiteral("--ubatch-size"), QStringLiteral("128"),
                QStringLiteral("--threads"), QString::number(threads),
                QStringLiteral("--threads-batch"), QString::number(idealThreads),
                QStringLiteral("--jinja"), QStringLiteral("--cache-prompt"),
                QStringLiteral("--host"), QStringLiteral("127.0.0.1"),
                QStringLiteral("--port"), QString::number(port),
                QStringLiteral("--timeout"), QStringLiteral("3600"),
                QStringLiteral("--metrics"), QStringLiteral("--n-predict"),
                QString::number(maximumNewTokens)};
    if (!adapterPath.isEmpty())
        *command << QStringLiteral("--lora") << adapterPath;
    if (inferenceMode == QStringLiteral("cpu")) {
        *command << QStringLiteral("--device") << QStringLiteral("none")
                 << QStringLiteral("--n-gpu-layers") << QStringLiteral("0")
                 << QStringLiteral("--no-kv-offload");
    } else if (inferenceMode == QStringLiteral("gpu")) {
        *command << QStringLiteral("--n-gpu-layers") << QStringLiteral("all");
    } else {
        *command << QStringLiteral("--n-gpu-layers") << QStringLiteral("auto")
                 << QStringLiteral("--fit") << QStringLiteral("on")
                 << QStringLiteral("--fit-target")
                 << QString::number(gpuFitTargetMiB(gpuMemoryGiB, totalGpuMemoryMiB));
    }
    return true;
}
