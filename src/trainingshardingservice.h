#pragma once

#include <QVariantMap>
#include <QList>

class TrainingShardingService final {
public:
    static QVariantMap prepare(const QString &datasetPath, const QString &outputDirectory,
                               const QString &llamaServerUrl, const QList<int> &limits,
                               bool writeOutputs);
    static QVariantMap preflight(const QString &datasetPath, const QString &outputPath,
                                 const QString &llamaServerUrl, int checkedMaxLength = 0);
};
