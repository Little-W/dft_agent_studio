#pragma once

#include <QVariantMap>

class DftEvidenceService final {
public:
    static QVariantMap analyze(const QVariantMap &arguments);
    static QVariantMap diagnose(const QVariantMap &arguments);
};
