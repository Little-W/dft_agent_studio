#pragma once

#include <QString>

bool initializeStudioUserDataRoot(const QString &agentRoot, QString *error = nullptr,
                                  const QString &targetRootOverride = {});
