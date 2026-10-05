#pragma once

#include <QString>
#include <QVariantMap>

class StudioNativeApi final {
public:
    static bool supports(const QString &method);
    static QVariantMap dispatch(const QString &method, const QVariantMap &params,
                                const QString &agentRoot);
};
