#pragma once

#include <QStringList>
#include <QVariantMap>

class LocalModelRuntimeService final {
public:
    static bool llamaServerCommand(const QVariantMap &model, const QString &agentRoot,
                                   const QString &serverBinary, int totalGpuMemoryMiB,
                                   QStringList *command, QString *error = nullptr);
    static QString findLlamaServerBinary(const QString &agentRoot, const QString &configuredPath = {},
                                         QString *error = nullptr);
    static int gpuFitTargetMiB(double maximumGpuMemoryGiB, int totalGpuMemoryMiB);
};
