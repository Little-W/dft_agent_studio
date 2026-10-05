#pragma once

#include <QVariantMap>

class RunReportService final {
public:
    static QVariantMap list(const QVariantMap &project, const QVariantMap &payload,
                            const QString &agentRoot);
    static QVariantMap read(const QVariantMap &project, const QVariantMap &payload,
                            const QString &agentRoot);
    static QVariantMap save(const QVariantMap &project, const QVariantMap &payload,
                            const QString &agentRoot);
};
