#pragma once

#include <QVariantMap>

class PreferenceDatasetService final {
public:
    static QVariantMap buildManifest(const QString &datasetPath, const QString &manifestPath);
};
