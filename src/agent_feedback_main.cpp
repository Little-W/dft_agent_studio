#include "agentepisodeservice.h"
#include "studiopaths.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextStream>

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("dft-agent-feedback"));
    app.setApplicationVersion(QStringLiteral(DFT_AGENT_STUDIO_VERSION));
    QStringList arguments = app.arguments();
    const QString action = arguments.size() > 1 ? arguments.takeAt(1) : QString{};
    if (action == QLatin1String("--version") || action == QLatin1String("-v")) {
        QTextStream(stdout) << app.applicationName() << ' ' << app.applicationVersion() << '\n';
        return 0;
    }
    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addVersionOption();
    QCommandLineOption rootOption(QStringLiteral("root"), QStringLiteral("Studio checkout root."), QStringLiteral("path"));
    QCommandLineOption episodeOption(QStringLiteral("episode"), QStringLiteral("Episode ID to review."), QStringLiteral("id"));
    QCommandLineOption verdictOption(QStringLiteral("verdict"), QStringLiteral("approved, corrected or rejected."), QStringLiteral("verdict"));
    QCommandLineOption noteOption(QStringLiteral("note"), QStringLiteral("Supervisor review note."), QStringLiteral("text"));
    QCommandLineOption correctedOption(QStringLiteral("corrected-response"), QStringLiteral("Corrected assistant response."), QStringLiteral("text"));
    QCommandLineOption feedbackOption(QStringLiteral("feedback"), QStringLiteral("Feedback JSONL path (defaults to Studio data)."), QStringLiteral("path"));
    QCommandLineOption policyOption(QStringLiteral("policy"), QStringLiteral("System policy used to build ChatML rows."), QStringLiteral("path"));
    QCommandLineOption outputOption(QStringLiteral("output"), QStringLiteral("Output SFT JSONL path."), QStringLiteral("path"));
    for (const auto &option : {rootOption, episodeOption, verdictOption, noteOption, correctedOption,
                               feedbackOption, policyOption, outputOption})
        parser.addOption(option);
    parser.process(arguments);

    QVariantMap response;
    const QString root = parser.value(rootOption).trimmed().isEmpty()
        ? studioFindAgentRoot() : QDir(parser.value(rootOption)).absolutePath();
    const QString dataRoot = studioDataRoot(root);
    if (action == QLatin1String("record")) {
        if (!parser.isSet(episodeOption) || !parser.isSet(verdictOption)) {
            QTextStream(stderr) << "record requires --episode and --verdict.\n";
            return 2;
        }
        response = AgentEpisodeService::recordFeedback(parser.value(episodeOption), parser.value(verdictOption),
            parser.value(noteOption), parser.value(correctedOption), dataRoot);
    } else if (action == QLatin1String("export-sft")) {
        if (!parser.isSet(policyOption) || !parser.isSet(outputOption)) {
            QTextStream(stderr) << "export-sft requires --policy and --output.\n";
            return 2;
        }
        const QString feedback = parser.value(feedbackOption).trimmed().isEmpty()
            ? QDir(dataRoot).filePath(QStringLiteral("feedback.jsonl")) : parser.value(feedbackOption);
        response = AgentEpisodeService::exportSft(feedback, parser.value(policyOption), parser.value(outputOption));
    } else {
        QTextStream(stderr) << "Usage: dft-agent-feedback record --episode ID --verdict approved|corrected|rejected\n"
                               "       dft-agent-feedback export-sft --policy FILE --output FILE [--feedback FILE]\n";
        return 2;
    }
    if (!response.value(QStringLiteral("ok")).toBool()) {
        QTextStream(stderr) << response.value(QStringLiteral("message")).toString() << '\n';
        return 1;
    }
    QTextStream(stdout) << QJsonDocument(QJsonObject::fromVariantMap(
        response.value(QStringLiteral("result")).toMap())).toJson(QJsonDocument::Indented);
    return 0;
}
