#include "trainingdatasetservice.h"

#include <QCoreApplication>
#include <QCommandLineParser>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextStream>

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("dft-training-data"));
    app.setApplicationVersion(QStringLiteral(DFT_AGENT_STUDIO_VERSION));
    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Validate and combine reviewed DFT Agent supervised JSONL data."));
    parser.addHelpOption();
    parser.addVersionOption();
    QCommandLineOption inputOption({QStringLiteral("i"), QStringLiteral("input")},
        QStringLiteral("Input JSONL file (repeat for multiple sources)."), QStringLiteral("path"));
    QCommandLineOption outputOption({QStringLiteral("o"), QStringLiteral("output")},
        QStringLiteral("Output mixed JSONL dataset."), QStringLiteral("path"));
    QCommandLineOption manifestOption({QStringLiteral("m"), QStringLiteral("manifest")},
        QStringLiteral("Output provenance manifest JSON."), QStringLiteral("path"));
    QCommandLineOption augmentOption(QStringLiteral("augment-reviewed"),
        QStringLiteral("Build evidence-derived SFT variants from the latest reviewed episode feedback."));
    QCommandLineOption feedbackOption(QStringLiteral("feedback"),
        QStringLiteral("Reviewed episode feedback JSONL."), QStringLiteral("path"));
    QCommandLineOption policyOption(QStringLiteral("policy"),
        QStringLiteral("Agent runtime policy inserted as the system message."), QStringLiteral("path"));
    parser.addOption(inputOption);
    parser.addOption(outputOption);
    parser.addOption(manifestOption);
    parser.addOption(augmentOption);
    parser.addOption(feedbackOption);
    parser.addOption(policyOption);
    parser.process(app);
    if (!parser.isSet(outputOption) || !parser.isSet(manifestOption)) {
        QTextStream(stderr) << "--output and --manifest are required.\n";
        return 2;
    }
    QVariantMap response;
    if (parser.isSet(augmentOption)) {
        if (!parser.values(inputOption).isEmpty() || !parser.isSet(feedbackOption) || !parser.isSet(policyOption)) {
            QTextStream(stderr) << "--augment-reviewed requires --feedback and --policy and cannot be combined with --input.\n";
            return 2;
        }
        response = TrainingDatasetService::buildEvidenceAugmentedSft(
            parser.value(feedbackOption), parser.value(policyOption), parser.value(outputOption), parser.value(manifestOption));
    } else {
        if (parser.values(inputOption).isEmpty() || parser.isSet(feedbackOption) || parser.isSet(policyOption)) {
            QTextStream(stderr) << "--input is required unless --augment-reviewed is used.\n";
            return 2;
        }
        response = TrainingDatasetService::buildSupervisedMix(
            parser.values(inputOption), parser.value(outputOption), parser.value(manifestOption));
    }
    if (!response.value(QStringLiteral("ok")).toBool()) {
        QTextStream(stderr) << response.value(QStringLiteral("message")).toString() << '\n';
        return 1;
    }
    const QJsonObject result = QJsonObject::fromVariantMap(response.value(QStringLiteral("result")).toMap());
    QTextStream(stdout) << QJsonDocument(result).toJson(QJsonDocument::Indented);
    return 0;
}
