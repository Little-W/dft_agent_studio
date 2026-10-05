#include "projectinspectionservice.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>
#include <utility>

namespace {
constexpr qsizetype MaximumFiles = 5'000;
constexpr qint64 MaximumFileBytes = 2'000'000;
constexpr qint64 MaximumTotalBytes = 64'000'000;

QStringList listValue(const QVariant &value) {
    if (value.metaType().id() == QMetaType::QString) {
        const QString text = value.toString().trimmed();
        return text.isEmpty() ? QStringList{} : QStringList{text};
    }
    QStringList values;
    for (const QVariant &item : value.toList()) {
        const QString text = item.toString().trimmed();
        if (!text.isEmpty())
            values.append(text);
    }
    return values;
}

bool isHdl(const QString &path) {
    static const QSet<QString> suffixes{
        QStringLiteral("v"), QStringLiteral("sv"), QStringLiteral("vh"),
        QStringLiteral("svh"), QStringLiteral("h")};
    return suffixes.contains(QFileInfo(path).suffix().toLower());
}

bool excludedDirectory(const QString &path) {
    static const QSet<QString> excluded{
        QStringLiteral(".git"), QStringLiteral("build"), QStringLiteral("obj_dir"),
        QStringLiteral("work"), QStringLiteral("node_modules"), QStringLiteral("__pycache__")};
    for (const QString &part : QDir::fromNativeSeparators(path).split(QLatin1Char('/')))
        if (excluded.contains(part))
            return true;
    return false;
}

QString stripComments(QString text) {
    static const QRegularExpression comments(
        QStringLiteral(R"(/\*[\s\S]*?\*/|//[^\n]*)"));
    text.remove(comments);
    return text;
}

QVariantList candidatePorts(const QHash<QString, QSet<QString>> &portFiles,
                            const QHash<QString, int> &occurrences,
                            const QStringList &hints,
                            const QSet<QString> &configured) {
    QStringList names;
    for (auto it = portFiles.cbegin(); it != portFiles.cend(); ++it) {
        const QString lowered = it.key().toLower();
        const bool hinted = std::any_of(hints.cbegin(), hints.cend(), [&lowered](const QString &hint) {
            return lowered.contains(hint);
        });
        if (!hinted || lowered.contains(QStringLiteral("enable")) || lowered.contains(QStringLiteral("gate"))
            || lowered.contains(QStringLiteral("count")) || lowered.contains(QStringLiteral("period"))
            || lowered.contains(QStringLiteral("freq")))
            continue;
        names.append(it.key());
    }
    std::sort(names.begin(), names.end(), [&occurrences](const QString &left, const QString &right) {
        const int leftCount = occurrences.value(left);
        const int rightCount = occurrences.value(right);
        return leftCount == rightCount ? left < right : leftCount > rightCount;
    });
    QVariantList result;
    for (const QString &name : names.mid(0, 32)) {
        QVariantList files;
        QStringList paths = portFiles.value(name).values();
        std::sort(paths.begin(), paths.end());
        for (const QString &path : paths.mid(0, 8))
            files.append(path);
        result.append(QVariantMap{
            {QStringLiteral("name"), name},
            {QStringLiteral("occurrences"), occurrences.value(name)},
            {QStringLiteral("input_port"), true},
            {QStringLiteral("configured"), configured.contains(name)},
            {QStringLiteral("files"), files}});
    }
    return result;
}

QVariantList unconfiguredCandidates(const QVariantList &candidates) {
    QVariantList result;
    for (const QVariant &value : candidates) {
        const QVariantMap entry = value.toMap();
        if (!entry.value(QStringLiteral("configured")).toBool())
            result.append(entry);
    }
    return result;
}
}

QVariantMap ProjectInspectionService::inspect(const QVariantMap &project) {
    const QString root = QFileInfo(project.value(QStringLiteral("root")).toString()).canonicalFilePath();
    if (root.isEmpty() || !QFileInfo(root).isDir())
        return {{QStringLiteral("available"), false},
                {QStringLiteral("error"), QStringLiteral("Project source root is unavailable.")}};

    const QVariantMap metadata = project.value(QStringLiteral("metadata")).toMap();
    const QVariantMap execution = metadata.value(QStringLiteral("dft_execution")).toMap();
    QStringList roots{root};
    const QString rtlRootValue = project.value(QStringLiteral("rtl_root"),
        project.value(QStringLiteral("rtlRoot"))).toString().trimmed();
    if (!rtlRootValue.isEmpty()) {
        const QFileInfo rtlInfo(QFileInfo(rtlRootValue).isAbsolute()
            ? rtlRootValue : QDir(root).filePath(rtlRootValue));
        const QString rtlRoot = rtlInfo.canonicalFilePath();
        if (QFileInfo(rtlRoot).isDir() && !roots.contains(rtlRoot))
            roots.append(rtlRoot);
    }

    QStringList sourcePaths = listValue(execution.value(QStringLiteral("source_files")));
    sourcePaths.append(listValue(execution.value(QStringLiteral("source_support_files"))));
    QStringList files;
    QSet<QString> seen;
    const auto addFile = [&](const QString &rawPath) {
        QFileInfo info(QFileInfo(rawPath).isAbsolute() ? rawPath : QDir(root).filePath(rawPath));
        const QString canonical = info.canonicalFilePath();
        if (!info.isFile() || canonical.isEmpty() || !isHdl(canonical))
            return;
        bool inRoot = false;
        for (const QString &candidateRoot : roots) {
            const QString prefix = candidateRoot.endsWith(QDir::separator())
                ? candidateRoot : candidateRoot + QDir::separator();
            if (canonical == candidateRoot || canonical.startsWith(prefix)) {
                inRoot = true;
                break;
            }
        }
        if (!inRoot || excludedDirectory(canonical) || seen.contains(canonical)
            || files.size() >= MaximumFiles)
            return;
        seen.insert(canonical);
        files.append(canonical);
    };
    for (const QString &path : std::as_const(sourcePaths))
        addFile(path);

    if (files.isEmpty()) {
        for (const QString &scanRoot : std::as_const(roots)) {
            QDirIterator it(scanRoot, QDir::Files | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
            while (it.hasNext() && files.size() < MaximumFiles) {
                const QString path = it.next();
                if (!excludedDirectory(path) && isHdl(path))
                    addFile(path);
            }
            if (files.size() >= MaximumFiles)
                break;
        }
    }
    std::sort(files.begin(), files.end());

    QHash<QString, QSet<QString>> portFiles;
    QHash<QString, int> occurrences;
    QStringList modules;
    QStringList inspectedFiles;
    qint64 totalBytes = 0;
    for (const QString &path : std::as_const(files)) {
        QFile file(path);
        const qint64 size = QFileInfo(path).size();
        if (size < 0 || size > MaximumFileBytes || totalBytes + size > MaximumTotalBytes)
            continue;
        if (!file.open(QIODevice::ReadOnly))
            continue;
        const QString text = stripComments(QString::fromUtf8(file.readAll()));
        totalBytes += size;
        const QString relativePath = QDir(root).relativeFilePath(path);
        inspectedFiles.append(relativePath);
        static const QRegularExpression moduleExpression(
            QStringLiteral(R"(\bmodule\s+(?:automatic\s+)?([A-Za-z_][A-Za-z0-9_$]*)\b)"));
        auto moduleMatch = moduleExpression.globalMatch(text);
        while (moduleMatch.hasNext()) {
            const QString module = moduleMatch.next().captured(1);
            if (!modules.contains(module))
                modules.append(module);
        }

        static const QRegularExpression identifierExpression(QStringLiteral(R"(\b[A-Za-z_][A-Za-z0-9_$]*\b)"));
        auto identifierMatch = identifierExpression.globalMatch(text);
        QSet<QString> identifiers;
        while (identifierMatch.hasNext()) {
            const QString identifier = identifierMatch.next().captured();
            const QString lowered = identifier.toLower();
            if (lowered.contains(QStringLiteral("clk")) || lowered.contains(QStringLiteral("clock"))
                || lowered.contains(QStringLiteral("rst")) || lowered.contains(QStringLiteral("reset"))
                || lowered.contains(QStringLiteral("por")))
                identifiers.insert(identifier);
        }
        for (const QString &identifier : std::as_const(identifiers))
            occurrences[identifier] += text.count(QRegularExpression(
                QStringLiteral("\\b") + QRegularExpression::escape(identifier) + QStringLiteral("\\b")));

        static const QRegularExpression inputExpression(QStringLiteral(R"(\binput\b([^;\n]*))"));
        auto inputMatch = inputExpression.globalMatch(text);
        while (inputMatch.hasNext()) {
            const QString declaration = inputMatch.next().captured(1);
            auto portMatch = identifierExpression.globalMatch(declaration);
            while (portMatch.hasNext()) {
                const QString port = portMatch.next().captured();
                const QString lowered = port.toLower();
                if (lowered.contains(QStringLiteral("clk")) || lowered.contains(QStringLiteral("clock"))
                    || lowered.contains(QStringLiteral("rst")) || lowered.contains(QStringLiteral("reset"))
                    || lowered.contains(QStringLiteral("por")))
                    portFiles[port].insert(relativePath);
            }
        }
    }

    QStringList configuredClocks = listValue(execution.value(QStringLiteral("clock")));
    configuredClocks.append(listValue(execution.value(QStringLiteral("additional_scan_clocks"))));
    QStringList configuredResets = listValue(execution.value(QStringLiteral("reset")));
    const QVariantList additionalResets = execution.value(QStringLiteral("additional_resets")).toList();
    for (const QVariant &entry : additionalResets)
        configuredResets.append(entry.toMap().value(QStringLiteral("port")).toString());
    const QSet<QString> clocks(configuredClocks.cbegin(), configuredClocks.cend());
    const QSet<QString> resets(configuredResets.cbegin(), configuredResets.cend());
    const QString top = project.value(QStringLiteral("top")).toString();
    const QVariantList clockCandidates = candidatePorts(portFiles, occurrences,
        {QStringLiteral("clk"), QStringLiteral("clock")}, clocks);
    const QVariantList resetCandidates = candidatePorts(portFiles, occurrences,
        {QStringLiteral("rst"), QStringLiteral("reset"), QStringLiteral("por")}, resets);
    return {
        {QStringLiteral("available"), true},
        {QStringLiteral("scan_truncated"), files.size() >= MaximumFiles || inspectedFiles.size() < files.size()},
        {QStringLiteral("hdl_file_count"), files.size()},
        {QStringLiteral("inspected_file_count"), inspectedFiles.size()},
        {QStringLiteral("inspected_bytes"), totalBytes},
        {QStringLiteral("source_files"), inspectedFiles},
        {QStringLiteral("module_names"), modules},
        {QStringLiteral("configured_top_found"), modules.contains(top)},
        {QStringLiteral("clock_candidates"), clockCandidates},
        {QStringLiteral("reset_candidates"), resetCandidates},
        {QStringLiteral("unconfigured_clock_candidates"), unconfiguredCandidates(clockCandidates)},
        {QStringLiteral("unconfigured_reset_candidates"), unconfiguredCandidates(resetCandidates)},
        {QStringLiteral("interpretation"), QStringLiteral(
            "Signal names are search hints, not confirmed clock/reset semantics. Read source declarations and always blocks before changing project settings or RTL.")}
    };
}
