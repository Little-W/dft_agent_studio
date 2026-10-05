#pragma once

#include <QString>
#include <QVariantMap>

class StudioToolService final {
public:
    static bool supports(const QString &action);
    static QVariantMap dispatch(const QString &action, const QVariantMap &project,
                                const QVariantMap &arguments, const QString &agentRoot);
    static QVariantMap listModels(bool refresh, const QString &agentRoot);
};
