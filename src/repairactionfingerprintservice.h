#pragma once

#include <QString>
#include <QVariant>
#include <QVariantMap>

// Canonical JSON and action fingerprints compatible with repair_runtime.py.
class RepairActionFingerprintService final {
public:
    static QByteArray canonicalJson(const QVariant &value);
    static QString digest(const QVariant &value);
    static QString actionSignature(const QString &name, const QVariantMap &arguments,
                                   const QString &projectRoot = {});
    static QString canonicalProjectPath(const QString &path, const QString &projectRoot);
};
