#pragma once

#include <QVariantMap>
#include <atomic>
#include <memory>

class ConfiguredDftFlowService final {
public:
    static bool supports(const QString &action);
    static QVariantMap dispatch(const QString &action, const QVariantMap &project,
                                const QVariantMap &arguments, const QString &agentRoot);

    static QVariantMap readiness(const QVariantMap &project, const QVariantMap &arguments,
                                 const QString &agentRoot);
    static QVariantMap stage(const QVariantMap &project, const QString &workspaceRoot,
                             const QVariantMap &arguments, const QString &agentRoot);
    static QVariantMap run(const QVariantMap &project, const QString &workspaceRoot,
                           const QVariantMap &arguments, const QString &agentRoot,
                           const std::shared_ptr<std::atomic_bool> &cancelToken = {});
};
