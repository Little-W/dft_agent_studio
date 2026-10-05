#pragma once

#include <QList>
#include <QVariant>
#include <QVariantMap>

class RepairRunEvidenceService final {
public:
    // Finds executed adapter envelopes and associates each with its controlled run.
    static QList<QVariantMap> controlledRunPayloads(const QVariant &value);
};
