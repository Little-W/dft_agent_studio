#include "evaluationdatasetservice.h"

#include <QCoreApplication>
#include <QCommandLineParser>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextStream>

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("dft-evaluation-data"));
    app.setApplicationVersion(QStringLiteral(DFT_AGENT_STUDIO_VERSION));
    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Validate holdout/development cases and write a provenance manifest."));
    parser.addHelpOption();
    parser.addVersionOption();
    QCommandLineOption holdoutOption(QStringLiteral("holdout"), QStringLiteral("Evaluation JSONL file."), QStringLiteral("path"));
    QCommandLineOption trainingOption(QStringLiteral("training"),
        QStringLiteral("Training JSONL file (repeatable)."), QStringLiteral("path"));
    QCommandLineOption projectOption(QStringLiteral("training-project"),
        QStringLiteral("Project identifier included in training (repeatable)."), QStringLiteral("project"));
    QCommandLineOption kindOption(QStringLiteral("dataset-kind"), QStringLiteral("holdout or development."),
                                  QStringLiteral("kind"), QStringLiteral("holdout"));
    QCommandLineOption manifestOption(QStringLiteral("manifest"), QStringLiteral("Output manifest JSON."), QStringLiteral("path"));
    parser.addOption(holdoutOption);
    parser.addOption(trainingOption);
    parser.addOption(projectOption);
    parser.addOption(kindOption);
    parser.addOption(manifestOption);
    parser.process(app);
    if (!parser.isSet(holdoutOption) || parser.values(trainingOption).isEmpty() || !parser.isSet(manifestOption)) {
        QTextStream(stderr) << "--holdout, at least one --training, and --manifest are required.\n";
        return 2;
    }
    const QVariantMap response = EvaluationDatasetService::buildHoldoutManifest(
        parser.value(holdoutOption), parser.values(trainingOption), parser.value(manifestOption),
        parser.values(projectOption), parser.value(kindOption));
    if (!response.value(QStringLiteral("ok")).toBool()) {
        QTextStream(stderr) << response.value(QStringLiteral("message")).toString() << '\n';
        return 1;
    }
    QTextStream(stdout) << QJsonDocument(QJsonObject::fromVariantMap(
        response.value(QStringLiteral("result")).toMap())).toJson(QJsonDocument::Indented);
    return 0;
}
