#include "scrolltraceanalyzerservice.h"

#include <QCoreApplication>
#include <QCommandLineParser>
#include <QFile>
#include <QJsonDocument>
#include <QTextStream>

#include <cmath>

#ifndef DFT_AGENT_STUDIO_VERSION
#define DFT_AGENT_STUDIO_VERSION "0.1.0"
#endif

int main(int argc, char *argv[]) {
    QCoreApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("dft-scroll-trace-analyzer"));
    app.setApplicationVersion(QStringLiteral(DFT_AGENT_STUDIO_VERSION));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Analyze a DFT Agent Studio Qt Quick scroll trace."));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument(QStringLiteral("trace"), QStringLiteral("JSON trace emitted by --scroll-trace."));
    const QCommandLineOption targetHzOption(QStringLiteral("target-hz"),
        QStringLiteral("Target display refresh rate in Hz."), QStringLiteral("hz"), QStringLiteral("165"));
    const QCommandLineOption minimumRatioOption(QStringLiteral("min-frame-ratio"),
        QStringLiteral("Minimum accepted fraction of the target frame rate."), QStringLiteral("ratio"), QStringLiteral("1.0"));
    parser.addOption(targetHzOption);
    parser.addOption(minimumRatioOption);
    parser.process(app);

    bool targetOk = false;
    bool ratioOk = false;
    const double targetHz = parser.value(targetHzOption).toDouble(&targetOk);
    const double minimumRatio = parser.value(minimumRatioOption).toDouble(&ratioOk);
    if (parser.positionalArguments().size() != 1 || !targetOk || !ratioOk
        || !std::isfinite(targetHz) || !std::isfinite(minimumRatio) || targetHz <= 0.0 || minimumRatio <= 0.0) {
        QTextStream(stderr) << "FAIL: provide one trace file and positive numeric frame-rate settings.\n";
        return 2;
    }

    const QString path = parser.positionalArguments().first();
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        QTextStream(stderr) << "FAIL: cannot read trace " << path << ": " << file.errorString() << '\n';
        return 2;
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        QTextStream(stderr) << "FAIL: invalid trace JSON: " << parseError.errorString() << '\n';
        return 2;
    }

    const ScrollTraceAnalysis analysis = ScrollTraceAnalyzerService::analyze(
        document.object(), targetHz, minimumRatio);
    if (!analysis.error.isEmpty()) {
        QTextStream(stderr) << "FAIL: " << analysis.error << '\n';
        return 2;
    }
    QTextStream output(stdout);
    for (const QString &check : analysis.checks)
        output << check << '\n';
    output << analysis.metrics << '\n';
    return analysis.passed ? 0 : 1;
}
