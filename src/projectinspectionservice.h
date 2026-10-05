#pragma once

#include <QVariantMap>

class ProjectInspectionService final {
public:
    static QVariantMap inspect(const QVariantMap &project);
};
