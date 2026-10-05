#pragma once

#include <QVariantMap>

class DftReportEvidenceService final {
public:
    static bool supports(const QString &action);
    static QVariantMap dispatch(const QString &action, const QVariantMap &arguments);

    static QVariantMap parseDrcReport(const QString &path, int maximum = 8);
    static QVariantMap parseAtpgReport(const QString &path, const QString &tool = QStringLiteral("testmax"),
                                       const QString &diagnosticMode = {});
    static QVariantMap validateAcceptance(const QString &evidenceRoot, const QVariantList &acceptance);
    static QVariantMap crossValidateEvidence(const QString &evidenceFile, const QString &workspaceRoot);
};
