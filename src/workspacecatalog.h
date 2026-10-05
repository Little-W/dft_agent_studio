#pragma once

#include <QString>
#include <QVariantMap>

class WorkspaceCatalog final {
public:
    static bool supports(const QString &action);
    static QVariantMap dispatch(const QString &action, const QVariantMap &project,
                                const QVariantMap &payload, const QString &agentRoot);
};
