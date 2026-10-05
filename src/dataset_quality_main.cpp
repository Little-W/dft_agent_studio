#include "datasetqualityservice.h"

#include <QCoreApplication>
#include <QCommandLineParser>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextStream>

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("dft-dataset-quality"));
    app.setApplicationVersion(QStringLiteral(DFT_AGENT_STUDIO_VERSION));
    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Audit reviewed training trajectories for duplicates and structural integrity."));
    parser.addHelpOption();
    parser.addVersionOption();
    QCommandLineOption datasetOption({QStringLiteral("i"), QStringLiteral("input")},
        QStringLiteral("Input conversation JSONL file."), QStringLiteral("path"));
    QCommandLineOption examplesOption(QStringLiteral("maximum-example-groups"),
        QStringLiteral("Maximum duplicate examples to include."), QStringLiteral("count"), QStringLiteral("12"));
    parser.addOption(datasetOption);
    parser.addOption(examplesOption);
    parser.process(app);
    bool maximumOk = false;
    const int maximum = parser.value(examplesOption).toInt(&maximumOk);
    if (!parser.isSet(datasetOption) || !maximumOk || maximum < 0 || maximum > 10'000) {
        QTextStream(stderr) << "--input is required and --maximum-example-groups must be 0..10000.\n";
        return 2;
    }
    const QVariantMap response = DatasetQualityService::auditJsonl(parser.value(datasetOption), maximum);
    if (!response.value(QStringLiteral("ok")).toBool()) {
        QTextStream(stderr) << response.value(QStringLiteral("message")).toString() << '\n';
        return 1;
    }
    const QJsonObject report = QJsonObject::fromVariantMap(response.value(QStringLiteral("report")).toMap());
    QTextStream(stdout) << QJsonDocument(report).toJson(QJsonDocument::Indented);
    return report.value(QStringLiteral("passed")).toBool() ? 0 : 2;
}
