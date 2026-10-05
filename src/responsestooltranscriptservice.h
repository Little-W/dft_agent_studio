#pragma once

#include <QString>
#include <QVariantMap>

class ResponsesToolTranscriptService final {
public:
    static bool supports(const QString &action);
    static QVariantMap dispatch(const QString &action, const QVariantMap &arguments,
                                const QString &agentRoot);
};
