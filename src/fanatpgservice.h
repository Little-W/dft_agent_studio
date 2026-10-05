#pragma once

#include <QVariantMap>

#include <atomic>
#include <memory>

class FanAtpgService final {
public:
    static bool selected(const QVariantMap &project);
    static QVariantMap inspect(const QVariantMap &project);
    static QVariantMap readiness(const QVariantMap &project);
    static QVariantMap run(const QVariantMap &project, const QVariantMap &arguments,
                           const QString &agentRoot,
                           const std::shared_ptr<std::atomic_bool> &cancelToken = {});
    static QVariantMap runIteration(const QVariantMap &project, const QVariantMap &arguments);
};
