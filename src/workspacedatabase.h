#pragma once

#include <QString>
#include <QVariantMap>

class WorkspaceDatabase final {
public:
    static bool supports(const QString &action);
    static QVariantMap dispatch(const QString &action, const QString &projectId,
                                const QVariantMap &payload, const QString &agentRoot,
                                const QString &databasePath = {});
};
