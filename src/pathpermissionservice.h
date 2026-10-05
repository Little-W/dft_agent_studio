#pragma once

#include <QString>
#include <QVariantMap>

// Durable storage for user decisions about a thread's requested filesystem access.
class PathPermissionService final {
public:
    static QString component(const QString &value);
    static QString requestPath(const QString &requestsRoot, const QString &threadId,
                               const QString &requestId);
    static bool writeRequest(const QString &requestsRoot, const QString &threadId,
                             const QString &requestId, const QVariantMap &record,
                             QString *error = nullptr);
    static QVariantMap readRequest(const QString &requestsRoot, const QString &threadId,
                                   const QString &requestId);
    static QVariantMap updateRequest(const QString &requestsRoot, const QString &threadId,
                                     const QString &requestId, const QVariantMap &changes,
                                     QString *error = nullptr);
};
