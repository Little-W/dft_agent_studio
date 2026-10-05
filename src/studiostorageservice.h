#pragma once

#include <QJsonValue>
#include <QString>
#include <QVariantMap>

class StudioStorageService final {
public:
    static QVariantMap resolvePath(const QString &path, const QString &agentRoot);
    static QVariantMap remapRecord(const QVariant &value, const QString &agentRoot);
    static QVariantMap modelRunConfig(const QString &modelId, const QString &agentRoot);
    static QVariantMap diagnose(const QString &agentRoot);
};
