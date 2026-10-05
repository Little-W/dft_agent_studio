#include "fanatpgservice.h"

#include "processoutputservice.h"
#include "studiopaths.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMutex>
#include <QMutexLocker>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTimer>
#include <QUuid>

#include <algorithm>

namespace {
const QStringList kProfiles{QStringLiteral("none"), QStringLiteral("static"), QStringLiteral("static_dynamic")};
QMutex &reviewMutex() {
    static QMutex mutex;
    return mutex;
}

QString fanRoot(const QVariantMap &project) {
    QString configured = qEnvironmentVariable("DFT_AGENT_FAN_ROOT").trimmed();
    if (!configured.isEmpty()) {
        const QString path = QFileInfo(configured).canonicalFilePath();
        if (!path.isEmpty())
            return path;
    }
    const QString projectRoot = QFileInfo(project.value(QStringLiteral("root")).toString()).canonicalFilePath();
    if (!projectRoot.isEmpty() && QFileInfo(QDir(projectRoot).filePath(QStringLiteral("mod_netlist"))).isDir())
        return projectRoot;
    return {};
}

QStringList circuits(const QString &root) {
    QStringList result;
    const QDir directory(QDir(root).filePath(QStringLiteral("mod_netlist")));
    for (const QFileInfo &file : directory.entryInfoList({QStringLiteral("*.v")}, QDir::Files, QDir::Name)) {
        const QString stem = file.completeBaseName();
        if (QRegularExpression(QStringLiteral("^[A-Za-z0-9_.-]+$")).match(stem).hasMatch())
            result.append(stem);
    }
    return result;
}

QString circuitFromProject(const QVariantMap &project) {
    const QVariantMap metadata = project.value(QStringLiteral("metadata")).toMap();
    const QVariantMap settings = metadata.value(QStringLiteral("fan_atpg")).toMap();
    const QString selected = settings.value(QStringLiteral("circuit"),
        project.value(QStringLiteral("circuit"), QStringLiteral("s27"))).toString().trimmed();
    return selected.isEmpty() ? QStringLiteral("s27") : selected;
}

QVariantMap errorResult(const QString &message) {
    return {{QStringLiteral("ok"), false}, {QStringLiteral("message"), message}};
}

QStringList profileScript(const QString &circuit, const QString &profile) {
    QStringList lines{
        QStringLiteral("read_lib techlib/mod_nangate45.mdt"),
        QStringLiteral("read_netlist mod_netlist/%1.v").arg(circuit),
        QStringLiteral("report_netlist"), QStringLiteral("build_circuit --frame 1"),
        QStringLiteral("report_circuit"), QStringLiteral("set_fault_type saf"),
        QStringLiteral("add_fault --all")};
    if (profile == QLatin1String("static") || profile == QLatin1String("static_dynamic"))
        lines.append(QStringLiteral("set_static_compression on"));
    if (profile == QLatin1String("static_dynamic"))
        lines.append(QStringLiteral("set_dynamic_compression on"));
    lines << QStringLiteral("set_X-Fill on") << QStringLiteral("run_atpg")
          << QStringLiteral("report_statistics > report.rpt") << QStringLiteral("write_pattern result.pat")
          << QStringLiteral("write_to_STIL result.stil") << QStringLiteral("exit");
    return lines;
}

bool parseReport(const QString &text, const QString &circuit, const QString &profile,
                 QVariantMap *metrics, QString *error) {
    const auto percentage = [&text](const QString &label, double *value) {
        const QRegularExpression expression(label + QStringLiteral(R"(\s+([0-9]+(?:\.[0-9]+)?)%)"),
                                             QRegularExpression::CaseInsensitiveOption);
        const auto match = expression.match(text);
        if (!match.hasMatch())
            return false;
        bool ok = false;
        *value = match.captured(1).toDouble(&ok);
        return ok;
    };
    double testCoverage = 0.0;
    double faultCoverage = 0.0;
    if (!percentage(QStringLiteral("test coverage"), &testCoverage)
        || !percentage(QStringLiteral("fault coverage"), &faultCoverage)) {
        *error = QStringLiteral("FAN report does not contain test coverage or fault coverage.");
        return false;
    }
    const auto patternMatch = QRegularExpression(QStringLiteral(R"(#Patterns\s+([0-9]+))"),
                                                   QRegularExpression::CaseInsensitiveOption).match(text);
    const auto runtimeMatch = QRegularExpression(QStringLiteral(R"(ATPG runtime\s+([0-9]+(?:\.[0-9]+)?(?:[eE][+-]?[0-9]+)?)\s+s)"),
                                                   QRegularExpression::CaseInsensitiveOption).match(text);
    if (!patternMatch.hasMatch() || !runtimeMatch.hasMatch()) {
        *error = QStringLiteral("FAN report is missing pattern count or runtime.");
        return false;
    }
    bool patternsOk = false;
    bool runtimeOk = false;
    const int patterns = patternMatch.captured(1).toInt(&patternsOk);
    const double runtime = runtimeMatch.captured(1).toDouble(&runtimeOk);
    if (!patternsOk || !runtimeOk) {
        *error = QStringLiteral("FAN report contains invalid numeric metrics.");
        return false;
    }
    *metrics = {{QStringLiteral("circuit"), circuit}, {QStringLiteral("profile"), profile},
        {QStringLiteral("test_coverage"), testCoverage}, {QStringLiteral("fault_coverage"), faultCoverage},
        {QStringLiteral("patterns"), patterns}, {QStringLiteral("runtime_seconds"), runtime}};
    return true;
}

bool writeJson(const QString &path, const QVariantMap &record, QString *error) {
    QSaveFile output(path);
    if (!output.open(QIODevice::WriteOnly | QIODevice::Text)) {
        *error = output.errorString();
        return false;
    }
    const QByteArray bytes = QJsonDocument(QJsonObject::fromVariantMap(record)).toJson(QJsonDocument::Indented) + '\n';
    if (output.write(bytes) != bytes.size() || !output.commit()) {
        *error = output.errorString();
        return false;
    }
    return true;
}

void recordReview(const QString &dataRoot, const QString &event, const QVariantMap &payload) {
    const QString path = QDir(dataRoot).filePath(QStringLiteral("review.jsonl"));
    QDir().mkpath(QFileInfo(path).absolutePath());
    QMutexLocker lock(&reviewMutex());
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text))
        return;
    const QJsonObject row{
        {QStringLiteral("timestamp"), QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyy-MM-dd'T'HH:mm:ss+00:00"))},
        {QStringLiteral("event"), event}, {QStringLiteral("payload"), QJsonObject::fromVariantMap(payload)}};
    file.write(QJsonDocument(row).toJson(QJsonDocument::Compact) + '\n');
}

QVariantMap runProfile(const QString &root, const QString &dataRoot, const QString &binary,
                       const QString &circuit, const QString &profile,
                       const std::shared_ptr<std::atomic_bool> &cancelToken) {
    const QString runId = QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMdd'T'HHmmss'Z'"))
        + QStringLiteral("_%1_%2_%3").arg(circuit, profile,
            QString::fromLatin1(QUuid::createUuid().toRfc4122().toHex().left(8)));
    const QString runsRoot = QDir(dataRoot).filePath(QStringLiteral("runs"));
    const QString runDirectory = QDir(runsRoot).filePath(runId);
    if (!QDir().mkpath(runDirectory))
        return {{QStringLiteral("run_id"), runId}, {QStringLiteral("run_dir"), runDirectory},
            {QStringLiteral("return_code"), -1}, {QStringLiteral("success"), false},
            {QStringLiteral("error"), QStringLiteral("Unable to create isolated FAN run directory.")}};
    for (const QString &entry : {QStringLiteral("techlib"), QStringLiteral("mod_netlist")}) {
        const QString source = QDir(root).filePath(entry);
        const QString destination = QDir(runDirectory).filePath(entry);
        if (!QFileInfo(source).isDir() || !QFile::link(source, destination))
            return {{QStringLiteral("run_id"), runId}, {QStringLiteral("run_dir"), runDirectory},
                {QStringLiteral("return_code"), -1}, {QStringLiteral("success"), false},
                {QStringLiteral("error"), QStringLiteral("Unable to stage FAN input tree: %1").arg(entry)}};
    }
    const QString scriptPath = QDir(runDirectory).filePath(QStringLiteral("atpg.script"));
    QFile script(scriptPath);
    const QByteArray scriptBytes = profileScript(circuit, profile).join(QLatin1Char('\n')).toUtf8() + '\n';
    if (!script.open(QIODevice::WriteOnly | QIODevice::Text)
        || script.write(scriptBytes) != scriptBytes.size() || !script.flush())
        return {{QStringLiteral("run_id"), runId}, {QStringLiteral("run_dir"), runDirectory},
            {QStringLiteral("return_code"), -1}, {QStringLiteral("success"), false},
            {QStringLiteral("error"), QStringLiteral("Unable to write FAN ATPG script.")}};
    script.close();
    const QString stdoutPath = QDir(runDirectory).filePath(QStringLiteral("fan.stdout.log"));
    const QString reportPath = QDir(runDirectory).filePath(QStringLiteral("report.rpt"));

    NativeProcessOutputService process;
    QEventLoop loop;
    QVariantMap processResult;
    process.setFinishedCallback([&](const QVariantMap &result) { processResult = result; loop.quit(); });
    const bool started = process.start(binary, {QStringLiteral("-f"), QStringLiteral("atpg.script")}, runDirectory,
                                       stdoutPath, 900'000, 64 * 1024 * 1024);
    if (!started) {
        processResult = {{QStringLiteral("started"), false}, {QStringLiteral("error"), process.errorString()}};
    } else {
        QTimer cancellationPoll;
        if (cancelToken) {
            cancellationPoll.setInterval(100);
            QObject::connect(&cancellationPoll, &QTimer::timeout, &loop, [&] {
                if (cancelToken->load(std::memory_order_relaxed)) {
                    process.cancel();
                    cancellationPoll.stop();
                }
            });
            cancellationPoll.start();
        }
        loop.exec();
    }
    QVariantMap metrics;
    QString error;
    const int returnCode = processResult.value(QStringLiteral("returncode"), -1).toInt();
    bool success = false;
    if (!started)
        error = processResult.value(QStringLiteral("error")).toString();
    else if (!processResult.value(QStringLiteral("error")).toString().isEmpty())
        error = processResult.value(QStringLiteral("error")).toString();
    else if (processResult.value(QStringLiteral("timed_out")).toBool())
        error = QStringLiteral("FAN exceeded 900 seconds");
    else if (processResult.value(QStringLiteral("cancelled")).toBool())
        error = QStringLiteral("FAN execution was cancelled");
    else if (returnCode != 0)
        error = QStringLiteral("FAN exited with status %1").arg(returnCode);
    else if (!QFileInfo(reportPath).isFile())
        error = QStringLiteral("FAN completed but did not produce report.rpt");
    else {
        QFile report(reportPath);
        if (!report.open(QIODevice::ReadOnly))
            error = QStringLiteral("Unable to read FAN report: %1").arg(report.errorString());
        else
            success = parseReport(QString::fromUtf8(report.readAll()), circuit, profile, &metrics, &error);
    }
    const QString stderrLogPath = QDir(runDirectory).filePath(QStringLiteral("fan.stderr.log"));
    QFile stderrLog(stderrLogPath);
    if (!stderrLog.exists()) {
        if (stderrLog.open(QIODevice::WriteOnly | QIODevice::Text)) {
            const QString diagnostic = processResult.value(QStringLiteral("error")).toString();
            stderrLog.write(diagnostic.toUtf8());
        }
    }
    QVariantMap result{
        {QStringLiteral("run_id"), runId}, {QStringLiteral("run_dir"), runDirectory},
        {QStringLiteral("return_code"), returnCode}, {QStringLiteral("success"), success},
        {QStringLiteral("metrics"), metrics}, {QStringLiteral("stdout_file"), stdoutPath},
        {QStringLiteral("stderr_file"), stderrLogPath}, {QStringLiteral("report_file"), reportPath},
    };
    if (!error.isEmpty())
        result.insert(QStringLiteral("error"), error);
    QString writeError;
    if (!writeJson(QDir(runDirectory).filePath(QStringLiteral("result.json")), result, &writeError))
        result.insert(QStringLiteral("error"), QStringLiteral("%1; failed to persist result: %2").arg(error, writeError));
    recordReview(dataRoot, QStringLiteral("run_atpg"), {{QStringLiteral("circuit"), circuit},
        {QStringLiteral("profile"), profile}, {QStringLiteral("result"), result}});
    return result;
}

bool meetsGoal(const QVariantMap &metrics, double minimumCoverage, const QVariant &maximumPatterns) {
    return metrics.value(QStringLiteral("fault_coverage")).toDouble() >= minimumCoverage
        && (!maximumPatterns.isValid() || maximumPatterns.isNull()
            || maximumPatterns.toInt() <= 0 || metrics.value(QStringLiteral("patterns")).toInt() <= maximumPatterns.toInt());
}

QVariantList listValue(const QStringList &values) {
    QVariantList result;
    for (const QString &value : values)
        result.append(value);
    return result;
}
}

bool FanAtpgService::selected(const QVariantMap &project) {
    const QString profile = project.value(QStringLiteral("flow_profile"),
        project.value(QStringLiteral("flowProfile"))).toString().trimmed().toLower();
    return profile == QLatin1String("fan_atpg_compression");
}

QVariantMap FanAtpgService::inspect(const QVariantMap &project) {
    const QString root = fanRoot(project);
    const QString circuit = circuitFromProject(project);
    const QStringList available = circuits(root);
    if (root.isEmpty() || !available.contains(circuit))
        return {{QStringLiteral("backend"), QStringLiteral("fan_atpg")},
            {QStringLiteral("circuit"), circuit}, {QStringLiteral("profiles"), listValue(kProfiles)},
            {QStringLiteral("available_circuits"), listValue(available)},
            {QStringLiteral("reason"), root.isEmpty() ? QStringLiteral("FAN benchmark root is unavailable.")
                : QStringLiteral("FAN circuit is not in the benchmark allowlist.")}};
    const QString netlist = QDir(root).filePath(QStringLiteral("mod_netlist/%1.v").arg(circuit));
    return {{QStringLiteral("backend"), QStringLiteral("fan_atpg")}, {QStringLiteral("circuit"), circuit},
        {QStringLiteral("netlist"), netlist}, {QStringLiteral("netlist_bytes"), QFileInfo(netlist).size()},
        {QStringLiteral("profiles"), listValue(kProfiles)}};
}

QVariantMap FanAtpgService::readiness(const QVariantMap &project) {
    const QString root = fanRoot(project);
    const QString binary = QDir(root).filePath(QStringLiteral("bin/opt/fan"));
    const QVariantMap inspection = inspect(project);
    const QString reason = root.isEmpty() ? QStringLiteral("FAN benchmark root is unavailable.")
        : !inspection.value(QStringLiteral("reason")).toString().isEmpty()
            ? inspection.value(QStringLiteral("reason")).toString()
            : !QFileInfo(binary).isExecutable() ? QStringLiteral("FAN ATPG executable is unavailable.") : QString{};
    return {{QStringLiteral("ready"), reason.isEmpty()}, {QStringLiteral("backend"), QStringLiteral("fan_atpg")},
        {QStringLiteral("circuit"), inspection.value(QStringLiteral("circuit"))},
        {QStringLiteral("profiles"), listValue(kProfiles)},
        {QStringLiteral("binary"), binary}, {QStringLiteral("reason"), reason}};
}

QVariantMap FanAtpgService::run(const QVariantMap &project, const QVariantMap &arguments,
                                const QString &agentRoot,
                                const std::shared_ptr<std::atomic_bool> &cancelToken) {
    const QVariantMap checked = readiness(project);
    if (!checked.value(QStringLiteral("ready")).toBool())
        return errorResult(QStringLiteral("FAN ATPG prerequisites are not met: %1").arg(checked.value(QStringLiteral("reason")).toString()));
    const QString circuit = circuitFromProject(project);
    const QVariantMap metadata = project.value(QStringLiteral("metadata")).toMap();
    const QVariantMap settings = metadata.value(QStringLiteral("fan_atpg")).toMap();
    bool coverageOk = false;
    const double minimumCoverage = arguments.value(QStringLiteral("min_coverage"),
        project.value(QStringLiteral("minimum_coverage"), project.value(QStringLiteral("minimumCoverage"), 0.0)))
        .toDouble(&coverageOk);
    QVariant maximumPatterns = arguments.value(QStringLiteral("max_patterns"),
        settings.value(QStringLiteral("maximum_patterns"), project.value(QStringLiteral("maximumPatterns"))));
    bool patternsOk = !maximumPatterns.isValid() || maximumPatterns.isNull() || maximumPatterns.toString().isEmpty();
    if (!patternsOk) {
        bool parsed = false;
        const int parsedPatterns = maximumPatterns.toInt(&parsed);
        patternsOk = parsed;
        if (parsed)
            maximumPatterns = parsedPatterns;
    }
    if (!coverageOk || minimumCoverage < 0.0 || minimumCoverage > 100.0)
        return errorResult(QStringLiteral("FAN minimum coverage must be a number between 0 and 100."));
    if (!patternsOk || (maximumPatterns.isValid() && !maximumPatterns.isNull() && maximumPatterns.toInt() < 1))
        return errorResult(QStringLiteral("FAN maximum patterns must be a positive integer when supplied."));

    const QString root = fanRoot(project);
    const QString dataRoot = studioDataRoot(agentRoot);
    const QString binary = checked.value(QStringLiteral("binary")).toString();
    QVariantList candidates;
    for (const QString &profile : kProfiles) {
        if (cancelToken && cancelToken->load(std::memory_order_relaxed))
            break;
        candidates.append(runProfile(root, dataRoot, binary, circuit, profile, cancelToken));
    }
    QVariantList successful;
    for (const QVariant &value : candidates) {
        const QVariantMap record = value.toMap();
        if (record.value(QStringLiteral("success")).toBool())
            successful.append(record);
    }
    if (successful.isEmpty())
        return {{QStringLiteral("ok"), false},
            {QStringLiteral("message"), cancelToken && cancelToken->load(std::memory_order_relaxed)
                ? QStringLiteral("FAN ATPG optimization was cancelled before a report completed.")
                : QStringLiteral("All FAN profiles failed; inspect the individual artifact logs.")},
            {QStringLiteral("candidates"), candidates}};
    const auto rank = [minimumCoverage, &maximumPatterns](const QVariantMap &candidate) {
        const QVariantMap metrics = candidate.value(QStringLiteral("metrics")).toMap();
        const bool satisfied = meetsGoal(metrics, minimumCoverage, maximumPatterns);
        const double coverageShortfall = qMax(0.0, minimumCoverage - metrics.value(QStringLiteral("fault_coverage")).toDouble());
        const int patternLimit = maximumPatterns.isValid() && !maximumPatterns.isNull()
            ? maximumPatterns.toInt() : metrics.value(QStringLiteral("patterns")).toInt();
        const int patternOverrun = qMax(0, metrics.value(QStringLiteral("patterns")).toInt() - patternLimit);
        QVariantList rank;
        if (satisfied) {
            rank << 0 << metrics.value(QStringLiteral("patterns"))
                 << -metrics.value(QStringLiteral("fault_coverage")).toDouble()
                 << metrics.value(QStringLiteral("runtime_seconds"));
        } else {
            rank << 1 << coverageShortfall << patternOverrun
                 << -metrics.value(QStringLiteral("fault_coverage")).toDouble()
                 << metrics.value(QStringLiteral("patterns"));
        }
        return rank;
    };
    std::sort(successful.begin(), successful.end(), [&](const QVariant &left, const QVariant &right) {
        const QVariantList a = rank(left.toMap());
        const QVariantList b = rank(right.toMap());
        const int count = qMin(a.size(), b.size());
        for (int index = 0; index < count; ++index) {
            const double av = a.at(index).toDouble();
            const double bv = b.at(index).toDouble();
            if (!qFuzzyCompare(av + 1.0, bv + 1.0))
                return av < bv;
        }
        return a.size() < b.size();
    });
    const QVariantMap best = successful.first().toMap();
    const bool objectiveMet = meetsGoal(best.value(QStringLiteral("metrics")).toMap(), minimumCoverage, maximumPatterns);
    const QVariantMap optimized{
        {QStringLiteral("goal"), QVariantMap{{QStringLiteral("min_coverage"), minimumCoverage},
            {QStringLiteral("max_patterns"), maximumPatterns}}},
        {QStringLiteral("circuit"), circuit}, {QStringLiteral("selected_run_id"), best.value(QStringLiteral("run_id"))},
        {QStringLiteral("selected_profile"), best.value(QStringLiteral("metrics")).toMap().value(QStringLiteral("profile"))},
        {QStringLiteral("objective_met"), objectiveMet}, {QStringLiteral("candidates"), candidates}};
    recordReview(dataRoot, QStringLiteral("optimize"), optimized);
    const QStringList blockers = objectiveMet ? QStringList{} : QStringList{
        QStringLiteral("FAN 实际结果没有满足项目的覆盖率或向量数量目标。")};
    const QVariantMap payload{
        {QStringLiteral("backend"), QStringLiteral("fan_atpg")}, {QStringLiteral("executed"), true},
        {QStringLiteral("ready"), true}, {QStringLiteral("readiness"), checked},
        {QStringLiteral("result"), optimized},
        {QStringLiteral("cross_validation"), QVariantMap{{QStringLiteral("status"), objectiveMet ? QStringLiteral("verified") : QStringLiteral("blocked")},
            {QStringLiteral("blockers"), blockers}}}};
    return {{QStringLiteral("ok"), true}, {QStringLiteral("result"), payload}};
}

QVariantMap FanAtpgService::runIteration(const QVariantMap &project, const QVariantMap &arguments) {
    Q_UNUSED(project);
    Q_UNUSED(arguments);
    return {{QStringLiteral("ok"), true}, {QStringLiteral("result"), QVariantMap{
        {QStringLiteral("executed"), false}, {QStringLiteral("backend"), QStringLiteral("fan_atpg")},
        {QStringLiteral("reason"), QStringLiteral("FAN baseline has exhausted the approved compression profiles; review actual candidate reports before adding a new configuration.")}}}};
}
