#pragma once

#include <QStringList>
#include <QVariantMap>

class EvaluationDatasetService final {
public:
    static QVariantMap buildHoldoutManifest(const QString &holdout,
                                            const QStringList &trainingFiles,
                                            const QString &manifest,
                                            const QStringList &trainingProjects = {},
                                            const QString &datasetKind = QStringLiteral("holdout"));
};
