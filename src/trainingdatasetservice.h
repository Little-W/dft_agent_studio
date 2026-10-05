#pragma once

#include <QStringList>
#include <QVariantMap>

class TrainingDatasetService final {
public:
    static QVariantMap buildSupervisedMix(const QStringList &inputs,
                                          const QString &output,
                                          const QString &manifest);
    static QVariantMap buildEvidenceAugmentedSft(const QString &feedback,
                                                 const QString &policy,
                                                 const QString &output,
                                                 const QString &manifest);
};
