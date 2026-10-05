#include "pathpermissionservice.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>

namespace {
QVariantMap readRequestFile(const QString &path, bool *ok) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (ok)
            *ok = false;
        return {};
    }

    QJsonParseError parseError{};
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        if (ok)
            *ok = false;
        return {};
    }
    if (ok)
        *ok = true;
    return document.object().toVariantMap();
}
} // namespace

QString PathPermissionService::component(const QString &value) {
    const QString normalized = value.trimmed();
    static const QRegularExpression valid(QStringLiteral("^[A-Za-z0-9._-]+$"));
    if (normalized == QStringLiteral(".") || normalized == QStringLiteral("..")
        || !valid.match(normalized).hasMatch())
        return QStringLiteral("invalid");
    return normalized;
}

QString PathPermissionService::requestPath(const QString &requestsRoot, const QString &threadId,
                                            const QString &requestId) {
    const QString threadComponent = component(threadId);
    const QString requestComponent = component(requestId);
    return QDir(requestsRoot).filePath(threadComponent + QLatin1Char('/') + requestComponent
                                       + QStringLiteral(".json"));
}

bool PathPermissionService::writeRequest(const QString &requestsRoot, const QString &threadId,
                                         const QString &requestId, const QVariantMap &record,
                                         QString *error) {
    const QString path = requestPath(requestsRoot, threadId, requestId);
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) {
        if (error)
            *error = QStringLiteral("无法创建权限请求目录。");
        return false;
    }

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error)
            *error = file.errorString();
        return false;
    }
    const QByteArray bytes = QJsonDocument(QJsonObject::fromVariantMap(record))
                                 .toJson(QJsonDocument::Compact);
    if (file.write(bytes) != bytes.size() || !file.commit()) {
        if (error)
            *error = file.errorString();
        return false;
    }
    return true;
}

QVariantMap PathPermissionService::readRequest(const QString &requestsRoot, const QString &threadId,
                                              const QString &requestId) {
    return readRequestFile(requestPath(requestsRoot, threadId, requestId), nullptr);
}

QVariantMap PathPermissionService::updateRequest(const QString &requestsRoot, const QString &threadId,
                                                 const QString &requestId,
                                                 const QVariantMap &changes, QString *error) {
    bool readOk = false;
    QVariantMap current = readRequestFile(requestPath(requestsRoot, threadId, requestId), &readOk);
    if (!readOk)
        return {};

    for (auto it = changes.cbegin(); it != changes.cend(); ++it)
        current.insert(it.key(), it.value());
    current.insert(QStringLiteral("updated_at"), QDateTime::currentMSecsSinceEpoch() / 1000.0);
    if (!writeRequest(requestsRoot, threadId, requestId, current, error))
        return {};
    return current;
}
