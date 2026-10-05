#include "preferencedatasetservice.h"

#include <QCoreApplication>
#include <QCommandLineParser>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextStream>

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("dft-preference-data"));
    app.setApplicationVersion(QStringLiteral(DFT_AGENT_STUDIO_VERSION));
    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Validate reviewed preference pairs and write a provenance manifest."));
    parser.addHelpOption();
    parser.addVersionOption();
    QCommandLineOption datasetOption({QStringLiteral("d"), QStringLiteral("dataset")},
        QStringLiteral("Preference pair JSONL file."), QStringLiteral("path"));
    QCommandLineOption manifestOption({QStringLiteral("m"), QStringLiteral("manifest")},
        QStringLiteral("Output provenance manifest JSON."), QStringLiteral("path"));
    parser.addOption(datasetOption);
    parser.addOption(manifestOption);
    parser.process(app);
    if (!parser.isSet(datasetOption) || !parser.isSet(manifestOption)) {
        QTextStream(stderr) << "--dataset and --manifest are required.\n";
        return 2;
    }
    const QVariantMap response = PreferenceDatasetService::buildManifest(
        parser.value(datasetOption), parser.value(manifestOption));
    if (!response.value(QStringLiteral("ok")).toBool()) {
        QTextStream(stderr) << response.value(QStringLiteral("message")).toString() << '\n';
        return 1;
    }
    QTextStream(stdout) << QJsonDocument(QJsonObject::fromVariantMap(
        response.value(QStringLiteral("result")).toMap())).toJson(QJsonDocument::Indented);
    return 0;
}
