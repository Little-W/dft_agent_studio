#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QStringList>
#include <QVariantMap>

class DatasetQualityService final {
public:
    static QString normalizeTrainingText(const QString &text);
    static QString conversationFingerprint(const QJsonObject &row);
    static QJsonObject auditStructure(const QJsonArray &rows);
    static QJsonObject auditRows(const QJsonArray &rows, int maximumExampleGroups = 12);
    static QVariantMap selectUniqueRows(const QJsonArray &rows,
                                        const QStringList &existingFingerprints = {},
                                        int maximumRows = -1);
    static QVariantMap auditJsonl(const QString &datasetPath, int maximumExampleGroups = 12);
};
