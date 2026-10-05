#pragma once

#include <QVariantMap>

class RepairIssueService final {
public:
    static QVariantMap toolSpec();
    // Returns {ok: true, result: {<Python tool result>, state: <updated state>}}.
    static QVariantMap updateIssue(const QVariantMap &arguments, QVariantMap &state);
};
