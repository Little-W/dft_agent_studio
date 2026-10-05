#pragma once

#include <QVariantList>
#include <QVariantMap>
#include <QString>
#include <QStringList>

class SourceCompatibilityService {
public:
    static QStringList moduleDeclarations(const QString &sourcePath);
    static QVariantMap inspectExcludableSource(const QString &sourceRoot,
                                               const QStringList &compileFiles,
                                               const QString &relativePath);
    static QVariantMap diagnoseUnsupportedCompileSource(const QString &workspace,
                                                        const QVariantList &errors);
};
