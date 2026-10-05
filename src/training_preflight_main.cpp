#include "trainingshardingservice.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextStream>

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("dft-training-preflight"));
    app.setApplicationVersion(QStringLiteral(DFT_AGENT_STUDIO_VERSION));
    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("使用 llama.cpp tokenizer 审计训练数据的 prompt 与完整序列长度。"));
    parser.addHelpOption();
    parser.addVersionOption();
    const QCommandLineOption datasetOption(QStringLiteral("dataset"),
        QStringLiteral("Input supervised JSONL dataset."), QStringLiteral("path"));
    const QCommandLineOption outputOption(QStringLiteral("output"),
        QStringLiteral("Output JSON report path."), QStringLiteral("path"));
    const QCommandLineOption serverOption(QStringLiteral("llama-server"),
        QStringLiteral("Running llama.cpp server base URL."),
        QStringLiteral("url"), QStringLiteral("http://127.0.0.1:8080"));
    const QCommandLineOption maxLengthOption(QStringLiteral("check-max-length"),
        QStringLiteral("Fail with exit code 2 if any full sequence exceeds this length (minimum 128)."),
        QStringLiteral("tokens"));
    parser.addOption(datasetOption);
    parser.addOption(outputOption);
    parser.addOption(serverOption);
    parser.addOption(maxLengthOption);
    parser.process(app);
    if (!parser.isSet(datasetOption) || !parser.isSet(outputOption)) {
        QTextStream(stderr) << "--dataset and --output are required.\n";
        return 2;
    }
    int maximum = 0;
    if (parser.isSet(maxLengthOption)) {
        bool valid = false;
        maximum = parser.value(maxLengthOption).toInt(&valid);
        if (!valid || maximum < 128) {
            QTextStream(stderr) << "--check-max-length must be an integer >= 128.\n";
            return 2;
        }
    }
    const QVariantMap response = TrainingShardingService::preflight(
        parser.value(datasetOption), parser.value(outputOption), parser.value(serverOption), maximum);
    if (!response.value(QStringLiteral("ok")).toBool()) {
        QTextStream(stderr) << response.value(QStringLiteral("message")).toString() << '\n';
        return 1;
    }
    const QVariantMap result = response.value(QStringLiteral("result")).toMap();
    QTextStream(stdout) << QJsonDocument(QJsonObject::fromVariantMap(result.value(QStringLiteral("report")).toMap()))
                               .toJson(QJsonDocument::Indented);
    return result.value(QStringLiteral("exit_code")).toInt();
}
