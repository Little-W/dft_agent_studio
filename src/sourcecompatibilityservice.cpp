#include "sourcecompatibilityservice.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>

namespace {
constexpr qint64 MaximumSourceBytes = 4 * 1024 * 1024;

QStringList unsupportedConstructs(const QString &text)
{
    static const QList<QPair<QString, QRegularExpression>> patterns{
        {QStringLiteral("rpmos"), QRegularExpression(QStringLiteral("\\brpmos\\b"), QRegularExpression::CaseInsensitiveOption)},
        {QStringLiteral("rnmos"), QRegularExpression(QStringLiteral("\\brnmos\\b"), QRegularExpression::CaseInsensitiveOption)},
        {QStringLiteral("tranif"), QRegularExpression(QStringLiteral("\\btranif[01]?\\b"), QRegularExpression::CaseInsensitiveOption)},
        {QStringLiteral("rtranif"), QRegularExpression(QStringLiteral("\\brtranif[01]?\\b"), QRegularExpression::CaseInsensitiveOption)},
        {QStringLiteral("cmos"), QRegularExpression(QStringLiteral("\\bcmos\\b"), QRegularExpression::CaseInsensitiveOption)},
        {QStringLiteral("rcmos"), QRegularExpression(QStringLiteral("\\brcmos\\b"), QRegularExpression::CaseInsensitiveOption)},
    };
    QStringList result;
    for (const auto &[name, pattern] : patterns) {
        if (pattern.match(text).hasMatch())
            result.append(name);
    }
    return result;
}

QString withoutComments(QString text)
{
    static const QRegularExpression block(QStringLiteral("/\\*[\\s\\S]*?\\*/"));
    static const QRegularExpression line(QStringLiteral("//[^\\n]*"));
    text.replace(block, QStringLiteral(" "));
    text.replace(line, QStringLiteral(" "));
    return text;
}

QStringList declaredModules(const QString &text)
{
    static const QRegularExpression module(QStringLiteral("(?:^|\\n)\\s*module\\s+(?:automatic\\s+)?([A-Za-z_][A-Za-z0-9_$]*)\\b"));
    QStringList names;
    auto matches = module.globalMatch(withoutComments(text));
    while (matches.hasNext()) {
        const QString name = matches.next().captured(1);
        if (!names.contains(name))
            names.append(name);
    }
    return names;
}

QString normalizedRelativePath(const QString &value)
{
    QString path = value.trimmed();
    path.replace(QLatin1Char('\\'), QLatin1Char('/'));
    if (path.isEmpty() || QFileInfo(path).isAbsolute())
        return {};
    const QString normalized = QDir::cleanPath(path);
    if (normalized == QLatin1String(".") || normalized == QLatin1String("..")
        || normalized.startsWith(QStringLiteral("../"))
        || path.split(QLatin1Char('/')).contains(QStringLiteral("..")))
        return {};
    return normalized;
}

QVariantList variantStrings(const QStringList &values)
{
    QVariantList result;
    for (const QString &value : values)
        result.append(value);
    return result;
}
}

QStringList SourceCompatibilityService::moduleDeclarations(const QString &sourcePath)
{
    QFile source(sourcePath);
    if (!source.open(QIODevice::ReadOnly) || source.size() > MaximumSourceBytes)
        return {};
    return declaredModules(QString::fromUtf8(source.readAll()));
}

QVariantMap SourceCompatibilityService::inspectExcludableSource(const QString &sourceRoot,
                                                                 const QStringList &compileFiles,
                                                                 const QString &relativePath)
{
    const QString normalized = normalizedRelativePath(relativePath);
    if (normalized.isEmpty())
        return {{QStringLiteral("file"), relativePath},
                {QStringLiteral("safe_exclusion"), false},
                {QStringLiteral("reason"), QStringLiteral("Source diagnosis requires a safe relative path.")}};

    QStringList files;
    for (const QString &file : compileFiles) {
        const QString item = normalizedRelativePath(file);
        if (!item.isEmpty() && !files.contains(item))
            files.append(item);
    }
    if (!files.contains(normalized))
        return {{QStringLiteral("file"), normalized},
                {QStringLiteral("safe_exclusion"), false},
                {QStringLiteral("reason"), QStringLiteral("The reported source is not in this run's compile list.")},
                {QStringLiteral("module_names"), QVariantList{}},
                {QStringLiteral("external_references"), QVariantList{}},
                {QStringLiteral("unsupported_constructs"), QVariantList{}}};

    const QString canonicalRoot = QFileInfo(sourceRoot).canonicalFilePath();
    const QString sourcePath = QFileInfo(QDir(sourceRoot).filePath(normalized)).canonicalFilePath();
    if (canonicalRoot.isEmpty() || sourcePath.isEmpty() || !QFileInfo(sourcePath).isFile()
        || !(sourcePath == canonicalRoot || sourcePath.startsWith(canonicalRoot + QDir::separator())))
        return {{QStringLiteral("file"), normalized},
                {QStringLiteral("safe_exclusion"), false},
                {QStringLiteral("reason"), QStringLiteral("The source is missing or escapes the staged RTL root.")},
                {QStringLiteral("module_names"), QVariantList{}},
                {QStringLiteral("external_references"), QVariantList{}},
                {QStringLiteral("unsupported_constructs"), QVariantList{}}};

    QFile source(sourcePath);
    if (source.size() > MaximumSourceBytes || !source.open(QIODevice::ReadOnly))
        return {{QStringLiteral("file"), normalized},
                {QStringLiteral("safe_exclusion"), false},
                {QStringLiteral("reason"), QStringLiteral("The source is unreadable or exceeds the 4 MiB inspection limit.")},
                {QStringLiteral("module_names"), QVariantList{}},
                {QStringLiteral("external_references"), QVariantList{}},
                {QStringLiteral("unsupported_constructs"), QVariantList{}}};

    const QByteArray bytes = source.readAll();
    const QString text = QString::fromUtf8(bytes);
    const QStringList constructs = unsupportedConstructs(withoutComments(text));
    const QStringList modules = declaredModules(text);
    QVariantList references;
    const QStringList modulePatterns = modules;
    for (const QString &file : files) {
        if (file == normalized)
            continue;
        const QString candidatePath = QFileInfo(QDir(sourceRoot).filePath(file)).canonicalFilePath();
        if (candidatePath.isEmpty() || !(candidatePath == canonicalRoot
            || candidatePath.startsWith(canonicalRoot + QDir::separator())))
            continue;
        QFile candidate(candidatePath);
        if (!candidate.open(QIODevice::ReadOnly) || candidate.size() > MaximumSourceBytes)
            continue;
        const QString candidateText = withoutComments(QString::fromUtf8(candidate.readAll()));
        for (const QString &module : modulePatterns) {
            const QRegularExpression reference(QStringLiteral("\\b%1\\b").arg(QRegularExpression::escape(module)));
            if (reference.match(candidateText).hasMatch())
                references.append(QVariantMap{{QStringLiteral("module"), module},
                                              {QStringLiteral("file"), file}});
        }
    }
    const bool safe = !constructs.isEmpty() && !modules.isEmpty() && references.isEmpty();
    QString reason;
    if (constructs.isEmpty())
        reason = QStringLiteral("No known unsupported synthesis primitive was found in the source.");
    else if (modules.isEmpty())
        reason = QStringLiteral("The source contains no independently identifiable module declaration.");
    else if (!references.isEmpty())
        reason = QStringLiteral("Another compile source references a module declared by this file.");
    else
        reason = QStringLiteral("The tool-unsupported source declares modules that no other compile source references.");
    return {{QStringLiteral("file"), normalized},
            {QStringLiteral("safe_exclusion"), safe},
            {QStringLiteral("reason"), reason},
            {QStringLiteral("module_names"), variantStrings(modules.mid(0, 16))},
            {QStringLiteral("external_references"), references.mid(0, 16)},
            {QStringLiteral("unsupported_constructs"), variantStrings(constructs)},
            {QStringLiteral("sha256"), QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex())}};
}

QVariantMap SourceCompatibilityService::diagnoseUnsupportedCompileSource(const QString &workspace,
                                                                          const QVariantList &errors)
{
    static const QStringList markers{
        QStringLiteral("switches are not supported"), QStringLiteral("switch primitive is not supported"),
        QStringLiteral("tranif"), QStringLiteral("rpmos"), QStringLiteral("rnmos")};
    static const QRegularExpression pathPattern(QStringLiteral("(?:^|\\s)(?:\\./)?rtl/(.+?):([0-9]+):"),
                                                QRegularExpression::CaseInsensitiveOption);
    QString candidate;
    QStringList relevantErrors;
    for (const QVariant &value : errors) {
        const QString line = value.toString();
        const QString lowered = line.toLower();
        const bool hasMarker = std::any_of(markers.cbegin(), markers.cend(), [&lowered](const QString &marker) {
            return lowered.contains(marker);
        });
        if (!hasMarker)
            continue;
        if (relevantErrors.size() < 4)
            relevantErrors.append(line);
        const auto match = pathPattern.match(line);
        if (candidate.isEmpty() && match.hasMatch())
            candidate = normalizedRelativePath(match.captured(1));
    }
    if (candidate.isEmpty())
        return {};

    QStringList compileFiles;
    QFile filelist(QDir(workspace).filePath(QStringLiteral("flow/input/rtl.f")));
    if (filelist.open(QIODevice::ReadOnly | QIODevice::Text) && filelist.size() <= 2 * 1024 * 1024) {
        const QStringList lines = QString::fromUtf8(filelist.readAll()).split(QLatin1Char('\n'));
        for (QString line : lines) {
            line = line.trimmed();
            if (line.isEmpty() || line.startsWith(QLatin1Char('+')))
                continue;
            if (line.startsWith(QStringLiteral("./rtl/")))
                line.remove(0, 6);
            const QString normalized = normalizedRelativePath(line);
            if (!normalized.isEmpty() && !compileFiles.contains(normalized))
                compileFiles.append(normalized);
        }
    }
    QVariantMap result = inspectExcludableSource(QDir(workspace).filePath(QStringLiteral("flow/rtl")),
                                                  compileFiles, candidate);
    result.insert(QStringLiteral("reported_errors"), variantStrings(relevantErrors));
    return result;
}
