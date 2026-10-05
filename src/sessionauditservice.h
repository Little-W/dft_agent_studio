#pragma once

#include <QStringList>
#include <QVariantMap>

class SessionAuditService final {
public:
    static QVariantMap audit(const QStringList &paths);
};
