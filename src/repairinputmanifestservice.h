#pragma once

#include <QVariantMap>

class RepairInputManifestService final {
public:
    static QVariantMap snapshot(const QVariantMap &project, const QVariantMap &toolArguments,
                                const QString &agentRoot);
};
