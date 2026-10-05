#include "trainingshardingservice.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextStream>

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("dft-training-shards"));
    app.setApplicationVersion(QStringLiteral(DFT_AGENT_STUDIO_VERSION));
    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral(
        "按 llama.cpp chat template 与 tokenizer 检查、切分并验证训练样本长度。"));
    parser.addHelpOption();
    parser.addVersionOption();
    QCommandLineOption datasetOption(QStringLiteral("dataset"), QStringLiteral("Input supervised JSONL."), QStringLiteral("path"));
    QCommandLineOption outputOption(QStringLiteral("output-dir"), QStringLiteral("Output directory."), QStringLiteral("path"));
    QCommandLineOption serverOption(QStringLiteral("llama-server"),
        QStringLiteral("Running llama.cpp server base URL (default: http://127.0.0.1:8080)."),
        QStringLiteral("url"), QStringLiteral("http://127.0.0.1:8080"));
    QCommandLineOption limitsOption(QStringLiteral("limits"),
        QStringLiteral("Comma-separated increasing token limits (default: 768,2048,3000)."),
        QStringLiteral("limits"), QStringLiteral("768,2048,3000"));
    QCommandLineOption reportOnlyOption(QStringLiteral("report-only"),
        QStringLiteral("Analyze and write the report without stage JSONL files."));
    parser.addOption(datasetOption);
    parser.addOption(outputOption);
    parser.addOption(serverOption);
    parser.addOption(limitsOption);
    parser.addOption(reportOnlyOption);
    parser.process(app);
    if (!parser.isSet(datasetOption) || !parser.isSet(outputOption)) {
        QTextStream(stderr) << "--dataset and --output-dir are required.\n";
        return 2;
    }
    QList<int> limits;
    for (const QString &part : parser.value(limitsOption).split(QLatin1Char(','), Qt::SkipEmptyParts)) {
        bool valid = false;
        const int limit = part.trimmed().toInt(&valid);
        if (!valid || limit <= 0 || (!limits.isEmpty() && limit <= limits.constLast())) {
            QTextStream(stderr) << "--limits must be comma-separated increasing positive integers.\n";
            return 2;
        }
        limits.append(limit);
    }
    const QVariantMap response = TrainingShardingService::prepare(
        parser.value(datasetOption), parser.value(outputOption), parser.value(serverOption), limits,
        !parser.isSet(reportOnlyOption));
    if (!response.value(QStringLiteral("ok")).toBool()) {
        QTextStream(stderr) << response.value(QStringLiteral("message")).toString() << '\n';
        return 1;
    }
    const QVariantMap result = response.value(QStringLiteral("result")).toMap();
    QTextStream(stdout) << QJsonDocument(QJsonObject::fromVariantMap(result.value(QStringLiteral("report")).toMap()))
                               .toJson(QJsonDocument::Indented);
    return result.value(QStringLiteral("anomaly_count")).toInt() == 0 ? 0 : 2;
}
