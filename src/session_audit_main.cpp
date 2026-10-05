#include "sessionauditservice.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QTextStream>

#ifndef DFT_AGENT_STUDIO_VERSION
#define DFT_AGENT_STUDIO_VERSION "0.1.0"
#endif

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("dft-session-audit"));
    QCoreApplication::setApplicationVersion(QStringLiteral(DFT_AGENT_STUDIO_VERSION));
    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Audit DFT runtime event logs without exposing prompts, source, or secrets."));
    parser.addHelpOption();
    parser.addVersionOption();
    QCommandLineOption outputOption(QStringList{QStringLiteral("output")},
        QStringLiteral("Write the complete JSON report to this file."), QStringLiteral("path"));
    parser.addOption(outputOption);
    parser.addPositionalArgument(QStringLiteral("logs"),
        QStringLiteral("One or more .events.bin, JSONL, or JSON-array event logs."), QStringLiteral("<log>..."));
    parser.process(app);

    const QVariantMap response = SessionAuditService::audit(parser.positionalArguments());
    if (!response.value(QStringLiteral("ok")).toBool()) {
        QTextStream(stderr) << "Audit failed: " << response.value(QStringLiteral("message")).toString() << Qt::endl;
        return 1;
    }
    const QByteArray json = QJsonDocument(QJsonObject::fromVariantMap(
        response.value(QStringLiteral("result")).toMap())).toJson(QJsonDocument::Indented) + '\n';
    const QString outputPath = parser.value(outputOption).trimmed();
    if (outputPath.isEmpty()) {
        QTextStream(stdout) << QString::fromUtf8(json);
        return 0;
    }
    const QFileInfo outputInfo(outputPath);
    if (!QDir().mkpath(outputInfo.absolutePath())) {
        QTextStream(stderr) << "Audit failed: cannot create report directory." << Qt::endl;
        return 1;
    }
    QSaveFile output(outputInfo.absoluteFilePath());
    if (!output.open(QIODevice::WriteOnly) || output.write(json) != json.size() || !output.commit()) {
        QTextStream(stderr) << "Audit failed: cannot atomically write report." << Qt::endl;
        return 1;
    }
    return 0;
}
