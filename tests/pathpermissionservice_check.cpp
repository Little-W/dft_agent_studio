#include "../src/pathpermissionservice.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <cstdlib>

int main() {
    QTemporaryDir temporary;
    if (!temporary.isValid())
        return EXIT_FAILURE;

    const QString root = temporary.path();
    const QString normalPath = PathPermissionService::requestPath(root, "thread-1", "request_2");
    if (normalPath != QDir(root).filePath(QStringLiteral("thread-1/request_2.json")))
        return EXIT_FAILURE;
    if (PathPermissionService::component("../outside") != QStringLiteral("invalid")
        || PathPermissionService::component("..") != QStringLiteral("invalid")
        || PathPermissionService::component("  child.1  ") != QStringLiteral("child.1"))
        return EXIT_FAILURE;

    const QVariantMap original{{QStringLiteral("request_id"), QStringLiteral("request_2")},
                               {QStringLiteral("status"), QStringLiteral("pending")},
                               {QStringLiteral("path"), QStringLiteral("/outside/data")}};
    QString error;
    if (!PathPermissionService::writeRequest(root, "thread-1", "request_2", original, &error))
        return EXIT_FAILURE;
    if (PathPermissionService::readRequest(root, "thread-1", "request_2") != original)
        return EXIT_FAILURE;

    const QVariantMap updated = PathPermissionService::updateRequest(
        root, "thread-1", "request_2",
        {{QStringLiteral("status"), QStringLiteral("approved")}}, &error);
    if (updated.value(QStringLiteral("status")).toString() != QStringLiteral("approved")
        || updated.value(QStringLiteral("path")).toString() != QStringLiteral("/outside/data")
        || updated.value(QStringLiteral("updated_at")).toDouble() <= 0.0
        || PathPermissionService::readRequest(root, "thread-1", "request_2") != updated)
        return EXIT_FAILURE;

    const QString invalidPath = PathPermissionService::requestPath(root, "../escape", "bad/id");
    if (invalidPath != QDir(root).filePath(QStringLiteral("invalid/invalid.json")))
        return EXIT_FAILURE;

    const QString corruptPath = PathPermissionService::requestPath(root, "thread-1", "corrupt");
    QFile corrupt(corruptPath);
    if (!corrupt.open(QIODevice::WriteOnly) || corrupt.write("[]") != 2)
        return EXIT_FAILURE;
    corrupt.close();
    if (!PathPermissionService::readRequest(root, "thread-1", "corrupt").isEmpty()
        || !PathPermissionService::readRequest(root, "missing", "missing").isEmpty()
        || !PathPermissionService::updateRequest(root, "missing", "missing", {}).isEmpty())
        return EXIT_FAILURE;

    if (!PathPermissionService::writeRequest(root, "thread-1", "empty", {}, &error))
        return EXIT_FAILURE;
    const QVariantMap updatedEmpty = PathPermissionService::updateRequest(
        root, "thread-1", "empty", {{QStringLiteral("status"), QStringLiteral("approved")}}, &error);
    if (updatedEmpty.value(QStringLiteral("status")).toString() != QStringLiteral("approved")
        || updatedEmpty.value(QStringLiteral("updated_at")).toDouble() <= 0.0)
        return EXIT_FAILURE;

    return EXIT_SUCCESS;
}
