#include "configureddftflowservice.h"
#include "sourcecompatibilityservice.h"

#include "dftevidenceservice.h"
#include "dftreportevidenceservice.h"
#include "processoutputservice.h"
#include "studiopaths.h"

#include <QDateTime>
#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcessEnvironment>
#include <QProcess>
#include <QRegularExpression>
#include <QSet>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTimer>
#include <QUuid>
#include <yaml-cpp/yaml.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <functional>
#include <tuple>
#include <utility>

namespace {

constexpr auto kReadiness = "configured_dft_readiness";
constexpr auto kStage = "configured_dft_stage";
constexpr auto kRun = "configured_dft_run";
const QStringList kReadinessAliases{QString::fromLatin1(kReadiness), QStringLiteral("check_dft_readiness"),
                                   QStringLiteral("project_dft_execution_readiness")};
const QStringList kRunAliases{QString::fromLatin1(kRun), QStringLiteral("run_dft_iteration"),
                              QStringLiteral("run_dft_flow"), QStringLiteral("run_and_verify_project_dft_flow")};

QVariantMap failure(const QString &message)
{
    return {{QStringLiteral("ok"), false}, {QStringLiteral("message"), message}};
}

QVariantMap success(const QVariantMap &result)
{
    return {{QStringLiteral("ok"), true}, {QStringLiteral("result"), result}};
}

QString canonicalDirectory(const QString &value, const QString &agentRoot)
{
    if (value.trimmed().isEmpty())
        return {};
    QString unresolved = QDir::cleanPath(studioAbsolutePath(value, agentRoot));
    QStringList suffix;
    while (!QFileInfo(unresolved).exists()) {
        const QFileInfo info(unresolved);
        if (info.fileName().isEmpty() || info.absolutePath() == unresolved)
            break;
        suffix.prepend(info.fileName());
        unresolved = info.absolutePath();
    }
    const QString canonical = QFileInfo(unresolved).canonicalFilePath();
    QString resolved = canonical.isEmpty() ? unresolved : canonical;
    for (const QString &part : suffix)
        resolved = QDir(resolved).filePath(part);
    return QDir::cleanPath(resolved);
}

bool within(const QString &path, const QString &root)
{
    const QString cleanPath = QDir::cleanPath(path);
    const QString cleanRoot = QDir::cleanPath(root);
    return cleanPath == cleanRoot || cleanPath.startsWith(cleanRoot + QDir::separator());
}

QString fixedWorkspaceOwner(const QString &workspace)
{
    for (const QString &name : {QStringLiteral(".dft_agent_workspace.json"), QStringLiteral("stage.json")}) {
        const QString path = QDir(workspace).filePath(name);
        const QFileInfo info(path);
        if (!info.isFile() || info.isSymLink() || info.size() > 1024 * 1024)
            continue;
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly))
            continue;
        const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
        if (!document.isObject())
            continue;
        const QJsonObject object = document.object();
        if (name == QLatin1String(".dft_agent_workspace.json"))
            return object.value(QStringLiteral("project_id")).toString().trimmed();
        return object.value(QStringLiteral("source")).toObject()
            .value(QStringLiteral("project")).toString().trimmed();
    }
    return {};
}

bool containsOnlyOwnedRunDirectories(const QString &workspace, const QString &projectId)
{
    const QFileInfoList entries = QDir(workspace).entryInfoList(
        QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System);
    if (entries.isEmpty())
        return false;
    for (const QFileInfo &entry : entries) {
        if (!entry.isDir() || entry.isSymLink() || fixedWorkspaceOwner(entry.absoluteFilePath()) != projectId)
            return false;
    }
    return true;
}

QString fixedWorkspaceError(const QString &workspaceRoot, const QString &projectRoot,
                            const QString &projectId)
{
    const QString workspace = QDir(workspaceRoot).filePath(projectId);
    if (within(workspace, projectRoot) || within(projectRoot, workspace))
        return QStringLiteral("固定工作区不能与项目目录相同、包含项目目录或位于项目目录中。");
    const QFileInfo info(workspace);
    if (info.isSymLink())
        return QStringLiteral("固定工作区不能是符号链接：%1").arg(workspace);
    if (info.exists() && !info.isDir())
        return QStringLiteral("固定工作区路径不是目录：%1").arg(workspace);
    if (!info.exists())
        return {};
    const QFileInfoList entries = QDir(workspace).entryInfoList(
        QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System);
    if (entries.isEmpty())
        return {};
    const QString owner = fixedWorkspaceOwner(workspace);
    if (owner != projectId && !containsOnlyOwnedRunDirectories(workspace, projectId))
        return QStringLiteral("固定工作区不是空目录，也不是当前项目以前使用的工作区。");
    return {};
}

bool removeGeneratedWorkspacePath(const QString &path)
{
    const QFileInfo info(path);
    if (!info.exists() && !info.isSymLink())
        return true;
    if (info.isSymLink() || info.isFile())
        return QFile::remove(path);
    if (info.isDir())
        return QDir(path).removeRecursively();
    return false;
}

bool prepareFixedWorkspace(const QString &workspaceRoot, const QString &projectRoot,
                           const QString &projectId, const QString &runId,
                           QString *workspacePath, QString *error)
{
    const QString validationError = fixedWorkspaceError(workspaceRoot, projectRoot, projectId);
    if (!validationError.isEmpty()) {
        *error = validationError;
        return false;
    }
    const QString workspace = QDir(workspaceRoot).filePath(projectId);
    if (!QDir().mkpath(workspace)) {
        *error = QStringLiteral("无法创建固定工作区：%1").arg(workspace);
        return false;
    }
    const QStringList generatedDirectories{
        QStringLiteral("flow/input"), QStringLiteral("flow/rtl"), QStringLiteral("flow/logs"),
        QStringLiteral("flow/reports"), QStringLiteral("flow/synthesis"), QStringLiteral("flow/mapped_scan"),
        QStringLiteral("flow/mapped_sim"), QStringLiteral("flow/progress"), QStringLiteral("flow/mbist_sim")};
    for (const QString &relative : generatedDirectories) {
        if (!removeGeneratedWorkspacePath(QDir(workspace).filePath(relative))) {
            *error = QStringLiteral("无法清理固定工作区中的旧流程目录：%1").arg(relative);
            return false;
        }
    }
    for (const QString &name : {QStringLiteral("stage.json"), QStringLiteral("skill_result.json"),
                                QStringLiteral("cross_validation.json"), QStringLiteral("agent_summary.json")}) {
        if (!removeGeneratedWorkspacePath(QDir(workspace).filePath(name))) {
            *error = QStringLiteral("无法清理固定工作区中的旧证据文件：%1").arg(name);
            return false;
        }
    }
    const QJsonObject marker{{QStringLiteral("project_id"), projectId},
                             {QStringLiteral("workspace_suffix_enabled"), false},
                             {QStringLiteral("last_run_id"), runId}};
    QSaveFile markerFile(QDir(workspace).filePath(QStringLiteral(".dft_agent_workspace.json")));
    if (!markerFile.open(QIODevice::WriteOnly)
        || markerFile.write(QJsonDocument(marker).toJson(QJsonDocument::Indented)) < 0
        || !markerFile.commit()) {
        *error = QStringLiteral("无法写入固定工作区所有权标记。");
        return false;
    }
    *workspacePath = workspace;
    return true;
}

bool truthy(const QVariant &value, bool fallback)
{
    if (!value.isValid() || value.isNull())
        return fallback;
    if (value.metaType().id() == QMetaType::Bool)
        return value.toBool();
    if (value.metaType().id() == QMetaType::QString) {
        const QString lower = value.toString().trimmed().toLower();
        if (lower == QLatin1String("false") || lower == QLatin1String("0") || lower == QLatin1String("off"))
            return false;
        if (lower == QLatin1String("true") || lower == QLatin1String("1") || lower == QLatin1String("on"))
            return true;
    }
    return value.toBool();
}

QString sourceInsideRtl(const QString &raw, const QString &projectRoot, const QString &rtlRoot)
{
    if (raw.trimmed().isEmpty())
        return {};
    QString candidate;
    if (QFileInfo(raw).isAbsolute()) {
        candidate = QFileInfo(raw).canonicalFilePath();
    } else {
        const QString rtlCandidate = QFileInfo(QDir(rtlRoot).filePath(raw)).canonicalFilePath();
        const QString projectCandidate = QFileInfo(QDir(projectRoot).filePath(raw)).canonicalFilePath();
        candidate = !rtlCandidate.isEmpty() ? rtlCandidate : projectCandidate;
    }
    if (candidate.isEmpty() || !within(candidate, rtlRoot) || !QFileInfo(candidate).isFile())
        return {};
    return QDir(rtlRoot).relativeFilePath(candidate);
}

struct DftTestModelInput {
    QString file;
    QString format;
    QString design;
};

struct SourceReplacementInput {
    QString target;
    QString file;
    QString sha256;
    QString reason;
};

struct TessentScanChain {
    QString name;
    QString scanIn;
    QString scanOut;
    QString scanEnable;
    QString clock;
    int length = 0;
};

struct ProjectInputs {
    QString projectId;
    QString projectName;
    QString projectRoot;
    QString rtlRoot;
    QString top;
    QString filelist;
    QString constraintFile;
    QString libraryDir;
    QString libraryFile;
    QString dcShell;
    QString testmax;
    QString tessent;
    QString dftTool = QStringLiteral("testmax");
    QString tessentDofile;
    QString workspaceRoot;
    QString clock;
    QString reset;
    int resetActiveState = 0;
    QString resetKind = QStringLiteral("asynchronous");
    QString compileStrategy = QStringLiteral("single_pass");
    int chainCount = 1;
    int maxChainLength = 100;
    int timeoutSeconds = 1800;
    int atpgTimeoutSeconds = 1800;
    int atpgAbortLimit = 10;
    QString atpgDiagnosticMode = QStringLiteral("off");
    int flowTimeoutMultiplier = 1;
    int atpgTimeoutMultiplier = 1;
    int maximumDftDrcViolations = 0;
    QString autofixRequestedMode = QStringLiteral("off");
    QString autofixMode = QStringLiteral("off");
    QString autofixTestModePort;
    QString autofixResetPort;
    QString autofixAdjustment;
    double clockPeriodNs = 1000.0;
    QVariant minimumAtpgCoverage;
    bool useConstraintFile = false;
    bool workspaceSuffixEnabled = true;
    bool synthesis = true;
    bool dft = true;
    bool scan = true;
    bool atpg = false;
    bool mbist = false;
    bool lbist = false;
    QStringList mbistCompileFiles;
    QStringList mbistIncludeDirs;
    QStringList mbistExpectedOutput;
    QStringList mbistRequiredPassMarkers;
    QStringList mbistForbiddenOutput;
    QString mbistTestTop;
    int mbistTimeoutSeconds = 120;
    QString mbistIncludeMode = QStringLiteral("declared");
    QString mbistDiagnosticMode = QStringLiteral("off");
    int mbistTimeoutMultiplier = 1;
    QStringList sourceFiles;
    QStringList manifestSourceFiles;
    QStringList supportFiles;
    QString manifestTarget = QStringLiteral("default");
    QList<SourceReplacementInput> sourceReplacements;
    QString sourceAnnotationMode = QStringLiteral("off");
    QVariantList sourceAnnotationTransforms;
    QStringList includeDirs;
    QStringList sourceDefines;
    QStringList additionalScanClocks;
    QList<QPair<QString, int>> additionalResets;
    QStringList preprocessSources;
    QStringList preprocessDefinitions;
    QStringList preprocessIncludeDirs;
    QString preprocessMacroFile;
    QString preprocessExecutable;
    int preprocessTimeoutSeconds = 120;
    bool preprocessEnabled = false;
    QMap<QString, QStringList> synthesisScriptFiles;
    QStringList additionalSynthesisCommands;
    QStringList synthesisReports;
    QStringList atpgCellModelFiles;
    QStringList tessentCellLibraryFiles;
    QStringList macroLibraryFiles;
    QList<DftTestModelInput> testModels;
    QStringList configurationErrors;
    QStringList missingFiles;
    QStringList unsupported;
    QVariantList sourceCompatibilityExclusions;
    QVariantList sourceAdditions;
};

QString yamlScalar(const YAML::Node &node)
{
    if (!node || !node.IsScalar())
        return {};
    return QString::fromStdString(node.Scalar());
}

QStringList yamlStrings(const YAML::Node &node)
{
    if (!node)
        return {};
    if (node.IsScalar())
        return {yamlScalar(node)};
    QStringList values;
    if (node.IsSequence()) {
        for (const YAML::Node &item : node) {
            const QString value = yamlScalar(item).trimmed();
            if (!value.isEmpty())
                values.append(value);
        }
    }
    return values;
}

QString fusesocNameWithoutVersion(QString value)
{
    static const QRegularExpression versionPrefix(QStringLiteral("^(?:>=|<=|==|>|<|\\^|~)\\s*"));
    value = value.trimmed();
    value.remove(versionPrefix);
    static const QRegularExpression versionSuffix(QStringLiteral("\\s*(?:>=|<=|==|>|<|\\^|~)\\s*.*$"));
    value.remove(versionSuffix);
    const QStringList parts = value.split(QLatin1Char(':'));
    return parts.size() >= 4 ? parts.mid(0, 3).join(QLatin1Char(':')) : value;
}

QStringList manifestRoots(const ProjectInputs &inputs)
{
    QStringList roots{inputs.projectRoot, inputs.rtlRoot};
    roots.removeDuplicates();
    return roots;
}

QString resolveManifestPath(const QString &rawPath, const QString &baseDirectory,
                            const ProjectInputs &inputs, bool requireDirectory = false)
{
    const QString candidate = QFileInfo(rawPath).isAbsolute()
        ? rawPath : QDir(baseDirectory).filePath(rawPath);
    const QString canonical = QFileInfo(candidate).canonicalFilePath();
    const QStringList roots = manifestRoots(inputs);
    if (canonical.isEmpty() || !std::any_of(roots.cbegin(), roots.cend(),
            [&](const QString &root) { return within(canonical, root); }))
        return {};
    const QFileInfo info(canonical);
    if (requireDirectory ? !info.isDir() : !info.isFile())
        return {};
    return canonical;
}

void addManifestHeaderDirectory(ProjectInputs *inputs, const QString &directory)
{
    const QString relative = QDir(inputs->rtlRoot).relativeFilePath(directory);
    if (!inputs->includeDirs.contains(relative))
        inputs->includeDirs.append(relative);
    QDirIterator iterator(directory, QStringList{QStringLiteral("*.h"), QStringLiteral("*.vh"),
                                                  QStringLiteral("*.svh")},
                          QDir::Files | QDir::NoSymLinks, QDirIterator::Subdirectories);
    while (iterator.hasNext()) {
        const QString header = QFileInfo(iterator.next()).canonicalFilePath();
        if (header.isEmpty() || !within(header, inputs->rtlRoot))
            continue;
        const QString headerRelative = QDir(inputs->rtlRoot).relativeFilePath(header);
        if (!inputs->supportFiles.contains(headerRelative))
            inputs->supportFiles.append(headerRelative);
    }
}

void addManifestSource(ProjectInputs *inputs, const QString &manifestDirectory,
                       const QString &rawPath, bool includeFile = false,
                       const QString &includePath = {})
{
    const QString canonical = resolveManifestPath(rawPath, manifestDirectory, *inputs);
    const QStringList roots = manifestRoots(*inputs);
    if (canonical.isEmpty() || !std::any_of(roots.cbegin(), roots.cend(),
            [&](const QString &root) { return within(canonical, root); })) {
        inputs->missingFiles.append(rawPath);
        return;
    }
    const QString suffix = QFileInfo(canonical).suffix().toLower();
    const QString relative = sourceInsideRtl(canonical, inputs->projectRoot, inputs->rtlRoot);
    if (relative.isEmpty()) {
        inputs->missingFiles.append(rawPath);
        return;
    }
    if (includeFile || suffix == QLatin1String("h") || suffix == QLatin1String("vh")
        || suffix == QLatin1String("svh")) {
        const QString requestedInclude = includePath.isEmpty()
            ? QFileInfo(canonical).absolutePath()
            : resolveManifestPath(includePath, manifestDirectory, *inputs, true);
        if (requestedInclude.isEmpty() || !within(requestedInclude, inputs->rtlRoot)) {
            if (!includePath.isEmpty())
                inputs->missingFiles.append(includePath);
            addManifestHeaderDirectory(inputs, QFileInfo(canonical).absolutePath());
        } else {
            addManifestHeaderDirectory(inputs, requestedInclude);
        }
        if (!inputs->supportFiles.contains(relative))
            inputs->supportFiles.append(relative);
        return;
    }
    if (suffix == QLatin1String("v") || suffix == QLatin1String("sv")) {
        if (!inputs->manifestSourceFiles.contains(relative))
            inputs->manifestSourceFiles.append(relative);
        if (!inputs->sourceFiles.contains(relative))
            inputs->sourceFiles.append(relative);
    }
}

void addNativeFilelistSource(ProjectInputs *inputs, const QString &rawPath,
                             const QString &manifestDirectory)
{
    const QString path = QDir(manifestDirectory).filePath(rawPath);
    const QString relative = sourceInsideRtl(path, inputs->projectRoot, inputs->rtlRoot);
    if (relative.isEmpty()) {
        inputs->missingFiles.append(rawPath);
        return;
    }
    if (!inputs->manifestSourceFiles.contains(relative))
        inputs->manifestSourceFiles.append(relative);
    if (!inputs->sourceFiles.contains(relative))
        inputs->sourceFiles.append(relative);
}

bool benderTargetMatches(const QString &condition, const QSet<QString> &targets)
{
    const QString expression = condition.trimmed();
    if (expression.isEmpty())
        return true;
    if (targets.isEmpty())
        return false;
    for (const QString &op : {QStringLiteral("not"), QStringLiteral("any"), QStringLiteral("all")}) {
        const QString prefix = op + QLatin1Char('(');
        if (!expression.startsWith(prefix) || !expression.endsWith(QLatin1Char(')')))
            continue;
        const QStringList values = expression.mid(prefix.size(), expression.size() - prefix.size() - 1)
                                       .split(QLatin1Char(','), Qt::SkipEmptyParts);
        int matches = 0;
        for (const QString &value : values) {
            if (targets.contains(value.trimmed()))
                ++matches;
        }
        if (op == QLatin1String("not"))
            return matches == 0;
        if (op == QLatin1String("any"))
            return matches > 0;
        return matches == values.size();
    }
    return targets.contains(expression);
}

bool resolveBenderManifest(ProjectInputs *inputs, const QString &manifestPath,
                           const QString &target)
{
    YAML::Node payload;
    try {
        payload = YAML::LoadFile(manifestPath.toStdString());
    } catch (const YAML::Exception &error) {
        inputs->configurationErrors.append(QStringLiteral("无法解析 Bender manifest %1：%2")
                                               .arg(manifestPath, QString::fromUtf8(error.what())));
        return false;
    }
    const QString base = QFileInfo(manifestPath).absolutePath();
    QSet<QString> targets;
    for (const QString &name : target.split(QLatin1Char(','), Qt::SkipEmptyParts))
        targets.insert(name.trimmed());
    YAML::Node sources = payload["sources"];
    YAML::Node sourceEntries = sources;
    if (sources.IsMap()) {
        for (const QString &include : yamlStrings(sources["include_dirs"])) {
            const QString path = resolveManifestPath(include, base, *inputs, true);
            if (path.isEmpty() || !within(path, inputs->rtlRoot))
                inputs->missingFiles.append(include);
            else
                addManifestHeaderDirectory(inputs, path);
        }
        sourceEntries = sources["files"];
    }
    if (sourceEntries.IsScalar()) {
        addManifestSource(inputs, base, yamlScalar(sourceEntries));
    } else if (sourceEntries.IsSequence()) {
        for (const YAML::Node &entry : sourceEntries) {
            if (entry.IsScalar()) {
                addManifestSource(inputs, base, yamlScalar(entry));
            } else if (entry.IsMap()) {
                if (!benderTargetMatches(yamlScalar(entry["target"]), targets))
                    continue;
                for (const QString &file : yamlStrings(entry["files"]))
                    addManifestSource(inputs, base, file);
            }
            if (inputs->sourceFiles.size() >= 10'000)
                break;
        }
    }
    for (const QString &include : yamlStrings(payload["export_include_dirs"])) {
        const QString path = resolveManifestPath(include, base, *inputs, true);
        if (path.isEmpty() || !within(path, inputs->rtlRoot))
            inputs->missingFiles.append(include);
        else
            addManifestHeaderDirectory(inputs, path);
    }
    return true;
}

bool resolveFuseSoCManifest(ProjectInputs *inputs, const QString &manifestPath,
                            const QString &requestedTarget)
{
    QHash<QString, QString> index;
    QStringList corePaths;
    QSet<QString> corePathSet;
    for (const QString &root : manifestRoots(*inputs)) {
        QDirIterator iterator(root, QStringList{QStringLiteral("*.core")}, QDir::Files | QDir::NoSymLinks,
                              QDirIterator::Subdirectories);
        while (iterator.hasNext() && corePaths.size() < 10'000) {
            const QString path = QFileInfo(iterator.next()).canonicalFilePath();
            if (path.isEmpty() || path.contains(QDir::separator() + QStringLiteral(".git") + QDir::separator()))
                continue;
            if (!corePathSet.contains(path)) {
                corePathSet.insert(path);
                corePaths.append(path);
            }
        }
    }
    std::sort(corePaths.begin(), corePaths.end());
    for (const QString &path : std::as_const(corePaths)) {
        try {
            YAML::Node payload = YAML::LoadFile(path.toStdString());
            const QString name = yamlScalar(payload["name"]).trimmed();
            if (!name.isEmpty()) {
                if (!index.contains(name))
                    index.insert(name, path);
                const QString unversioned = fusesocNameWithoutVersion(name);
                if (!index.contains(unversioned))
                    index.insert(unversioned, path);
            }
        } catch (const YAML::Exception &) {
            // Invalid unrelated cores are reported if and when selected.
        }
    }

    QSet<QString> visiting;
    QSet<QString> completed;
    QStringList missing;
    QStringList unresolved;
    std::function<void(const QString &, const QString &, int)> visit;
    visit = [&](const QString &path, const QString &target, int depth) {
        const QString canonical = QFileInfo(path).canonicalFilePath();
        if (canonical.isEmpty() || depth > 64) {
            unresolved.append(QStringLiteral("dependency_depth_or_path:%1").arg(path));
            return;
        }
        if (completed.contains(canonical))
            return;
        if (visiting.contains(canonical)) {
            unresolved.append(QStringLiteral("dependency_cycle:%1").arg(QDir(inputs->projectRoot).relativeFilePath(canonical)));
            return;
        }
        visiting.insert(canonical);
        YAML::Node payload;
        try {
            payload = YAML::LoadFile(canonical.toStdString());
        } catch (const YAML::Exception &error) {
            unresolved.append(QStringLiteral("invalid_core:%1:%2").arg(canonical, QString::fromUtf8(error.what())));
            visiting.remove(canonical);
            return;
        }
        YAML::Node targets = payload["targets"];
        QString selectedName;
        for (const QString &candidate : {target, QStringLiteral("default"), QStringLiteral("rtl")}) {
            if (targets[candidate.toStdString()].IsMap()) {
                selectedName = candidate;
                break;
            }
        }
        if (selectedName.isEmpty() && targets.IsMap()) {
            for (const auto &entry : targets) {
                if (entry.second.IsMap()) {
                    selectedName = yamlScalar(entry.first);
                    break;
                }
            }
        }
        YAML::Node selected = selectedName.isEmpty() ? YAML::Node{} : targets[selectedName.toStdString()];
        YAML::Node filesets = payload["filesets"];
        if (!selected.IsMap() || !filesets.IsMap()) {
            unresolved.append(QStringLiteral("target_missing:%1:%2").arg(canonical, target));
            visiting.remove(canonical);
            return;
        }
        const QString base = QFileInfo(canonical).absolutePath();
        for (const QString &filesetName : yamlStrings(selected["filesets"])) {
            if (filesetName.contains(QLatin1Char('?')))
                continue;
            YAML::Node fileset = filesets[filesetName.toStdString()];
            if (!fileset.IsMap()) {
                missing.append(QStringLiteral("fileset:%1").arg(filesetName));
                continue;
            }
            for (const QString &dependency : yamlStrings(fileset["depend"])) {
                const QString key = dependency.trimmed();
                const QString dependencyPath = index.value(key, index.value(fusesocNameWithoutVersion(key)));
                if (dependencyPath.isEmpty()) {
                    missing.append(QStringLiteral("dependency:%1").arg(key));
                    continue;
                }
                visit(dependencyPath, QStringLiteral("default"), depth + 1);
            }
            YAML::Node entries = fileset["files"];
            if (!entries.IsSequence())
                continue;
            for (const YAML::Node &entry : entries) {
                QString rawPath;
                YAML::Node options;
                if (entry.IsScalar()) {
                    rawPath = yamlScalar(entry);
                } else if (entry.IsMap() && entry.size() > 0) {
                    const auto first = *entry.begin();
                    rawPath = yamlScalar(first.first);
                    options = first.second;
                }
                if (rawPath.isEmpty())
                    continue;
                const bool includeFile = options.IsMap()
                    && options["is_include_file"].as<bool>(false);
                const QString includePath = options.IsMap() ? yamlScalar(options["include_path"]) : QString{};
                addManifestSource(inputs, base, rawPath, includeFile, includePath);
                if (inputs->sourceFiles.size() >= 10'000)
                    break;
            }
        }
        visiting.remove(canonical);
        completed.insert(canonical);
    };
    visit(manifestPath, requestedTarget, 0);
    inputs->missingFiles.append(missing);
    inputs->unsupported.append(unresolved);
    return true;
}

QStringList drcAutofixCommands(const ProjectInputs &inputs)
{
    const QString mode = inputs.autofixMode;
    const QString testMode = inputs.autofixTestModePort;
    const QString reset = inputs.autofixResetPort;
    if (mode == QLatin1String("off"))
        return {};

    QStringList commands;
    if (mode == QLatin1String("clock_only")) {
        commands = {
            QStringLiteral("set_dft_configuration -fix_clock enable -fix_reset disable -fix_set disable"),
            QStringLiteral("set_dft_signal -view spec -type TestData -port {%1}").arg(inputs.clock),
        };
        if (!reset.isEmpty())
            commands.append(QStringLiteral("set_dft_signal -view spec -type TestData -port {%1}").arg(reset));
        if (!testMode.isEmpty())
            commands.insert(1, QStringLiteral("set_dft_signal -view spec -type TestMode -active_state 1 -port {%1}").arg(testMode));
        const QString control = testMode.isEmpty() ? QString{} : QStringLiteral(" -control_signal %1").arg(testMode);
        commands.append(QStringLiteral("set_autofix_configuration -type clock -method mux%1 -test_data %2 -fix_data enable")
                            .arg(control, inputs.clock));
        return commands;
    }

    if (mode == QLatin1String("reset_set")) {
        commands.append(QStringLiteral("set_dft_configuration -fix_clock disable -fix_reset enable -fix_set enable"));
        if (!testMode.isEmpty())
            commands.append(QStringLiteral("set_dft_signal -view existing_dft -type TestMode -active_state 1 -port {%1}").arg(testMode));
        commands.append(QStringLiteral("set_dft_signal -view spec -type TestData -port {%1}").arg(reset));
        commands.append(QStringLiteral("set_autofix_configuration -type reset -method mux -test_data %1 -fix_data enable -fix_latch enable").arg(reset));
        commands.append(QStringLiteral("set_autofix_configuration -type set -method mux -test_data %1 -fix_data enable -fix_latch enable").arg(reset));
        return commands;
    }

    commands = {
        QStringLiteral("set_dft_configuration -fix_clock enable -fix_reset enable -fix_set enable"),
        QStringLiteral("set_dft_signal -view spec -type TestData -port {%1}").arg(inputs.clock),
    };
    if (!reset.isEmpty())
        commands.append(QStringLiteral("set_dft_signal -view spec -type TestData -port {%1}").arg(reset));
    if (!testMode.isEmpty()) {
        commands.insert(1, QStringLiteral("set_dft_signal -view existing_dft -type TestMode -active_state 1 -port {%1}").arg(testMode));
        commands.insert(2, QStringLiteral("set_dft_signal -view spec -type TestMode -active_state 1 -port {%1}").arg(testMode));
    }
    const QString control = testMode.isEmpty() ? QString{} : QStringLiteral(" -control_signal %1").arg(testMode);
    commands.append(QStringLiteral("set_autofix_configuration -type clock -method mux%1 -test_data %2 -fix_data enable")
                        .arg(control, inputs.clock));
    if (!reset.isEmpty()) {
        commands.append(QStringLiteral("set_autofix_configuration -type reset -method mux%1 -test_data %2 -fix_data enable -fix_latch enable")
                            .arg(control, reset));
        commands.append(QStringLiteral("set_autofix_configuration -type set -method mux%1 -test_data %2 -fix_data enable -fix_latch enable")
                            .arg(control, reset));
    }
    return commands;
}

QVariantList stringListVariant(const QStringList &values);

bool validateAdditionalSynthesisCommand(const QString &raw, const QString &label,
                                        QString *error)
{
    const QString command = raw.trimmed();
    if (command.isEmpty() || command.contains(QLatin1Char('\n'))
        || command.contains(QLatin1Char('\r')) || command.contains(QLatin1Char(';'))
        || command.contains(QLatin1Char('$'))) {
        *error = QStringLiteral("%1 must be a single Tcl command without variables.").arg(label);
        return false;
    }
    static const QSet<QString> allowedCommands{
        QStringLiteral("set_ideal_network"), QStringLiteral("set_max_area"),
        QStringLiteral("set_fix_multiple_port_nets"), QStringLiteral("set_cost_priority"),
        QStringLiteral("set_case_analysis"), QStringLiteral("set_disable_timing"),
        QStringLiteral("set_dont_use"), QStringLiteral("set_clock_gating_style"),
        QStringLiteral("set_svf")};
    const QString commandName = command.section(QRegularExpression(QStringLiteral("\\s+")), 0, 0).toLower();
    if (!allowedCommands.contains(commandName)) {
        *error = QStringLiteral("%1 command '%2' is not allowed.").arg(label, commandName);
        return false;
    }
    static const QSet<QString> allowedCollections{
        QStringLiteral("all_inputs"), QStringLiteral("all_outputs"),
        QStringLiteral("current_design"), QStringLiteral("get_cells"),
        QStringLiteral("get_clocks"), QStringLiteral("get_designs"),
        QStringLiteral("get_nets"), QStringLiteral("get_pins"), QStringLiteral("get_ports")};
    static const QRegularExpression nestedCommand(
        QStringLiteral("\\[\\s*([A-Za-z_][A-Za-z0-9_]*)"));
    auto matches = nestedCommand.globalMatch(command);
    while (matches.hasNext()) {
        const QString nested = matches.next().captured(1).toLower();
        if (!allowedCollections.contains(nested)) {
            *error = QStringLiteral("%1 contains an unsupported Tcl subcommand: %2")
                         .arg(label, nested);
            return false;
        }
    }
    return true;
}

QString synthesisCompileCommand(const QVariantMap &synthesis, const QVariantMap &execution,
                                const ProjectInputs &inputs)
{
    const QString name = synthesis.value(QStringLiteral("compile_command"), QStringLiteral("compile")).toString();
    QStringList options;
    const QString autoUngroup = synthesis.value(QStringLiteral("auto_ungroup"), QStringLiteral("none")).toString();
    if (name == QLatin1String("compile_ultra")) {
        if (truthy(synthesis.value(QStringLiteral("incremental")), false))
            options.append(QStringLiteral("-incremental"));
        if (truthy(synthesis.value(QStringLiteral("retime")), false))
            options.append(QStringLiteral("-retime"));
        if (!truthy(synthesis.value(QStringLiteral("boundary_optimization")), true))
            options.append(QStringLiteral("-no_boundary_optimization"));
        if (autoUngroup == QLatin1String("none"))
            options.append(QStringLiteral("-no_autoungroup"));
    } else {
        const auto appendEffort = [&options, &execution](const QString &key) {
            if (!execution.contains(key))
                return;
            const QString value = execution.value(key).toString().trimmed().toLower();
            static const QSet<QString> supported{QStringLiteral("none"), QStringLiteral("low"),
                                                  QStringLiteral("medium"), QStringLiteral("high")};
            if (supported.contains(value) && value != QLatin1String("none"))
                options.append(QStringLiteral("-%1 %2").arg(key, value));
        };
        appendEffort(QStringLiteral("map_effort"));
        appendEffort(QStringLiteral("area_effort"));
        appendEffort(QStringLiteral("power_effort"));
        if (truthy(synthesis.value(QStringLiteral("incremental")), false))
            options.append(QStringLiteral("-incremental_mapping"));
        if (autoUngroup == QLatin1String("area") || autoUngroup == QLatin1String("delay"))
            options.append(QStringLiteral("-auto_ungroup %1").arg(autoUngroup));
        else if (autoUngroup == QLatin1String("all"))
            options.append(QStringLiteral("-ungroup_all"));
        if (truthy(synthesis.value(QStringLiteral("boundary_optimization")), true))
            options.append(QStringLiteral("-boundary_optimization"));
    }
    if (truthy(synthesis.value(QStringLiteral("gate_clock")), false))
        options.append(QStringLiteral("-gate_clock"));
    if (truthy(synthesis.value(QStringLiteral("scan_ready")), inputs.scan))
        options.append(QStringLiteral("-scan"));
    return (QStringList{name} + options).join(QLatin1Char(' '));
}

void parseNativeFilelist(const QString &manifestPath, ProjectInputs *inputs,
                         QSet<QString> *active, QSet<QString> *visited, int depth)
{
    if (depth > 16) {
        inputs->unsupported.append(QStringLiteral("nested filelist depth exceeds the native limit of 16."));
        return;
    }
    const QString canonicalManifest = QFileInfo(manifestPath).canonicalFilePath();
    if (canonicalManifest.isEmpty()
        || (!within(canonicalManifest, inputs->projectRoot) && !within(canonicalManifest, inputs->rtlRoot))) {
        inputs->configurationErrors.append(QStringLiteral("nested filelist is missing or outside the project: %1")
                                               .arg(manifestPath));
        return;
    }
    if (active->contains(canonicalManifest)) {
        inputs->configurationErrors.append(QStringLiteral("nested filelist cycle detected at %1")
                                               .arg(canonicalManifest));
        return;
    }
    if (visited->contains(canonicalManifest))
        return;
    QFile file(canonicalManifest);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        inputs->configurationErrors.append(QStringLiteral("cannot read source filelist: %1")
                                               .arg(canonicalManifest));
        return;
    }
    active->insert(canonicalManifest);
    const QString manifestDirectory = QFileInfo(canonicalManifest).absolutePath();
    const QStringList lines = QString::fromUtf8(file.readAll()).split(QLatin1Char('\n'));
    for (const QString &rawLine : lines) {
        const QString line = rawLine.trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#')))
            continue;
        const QStringList tokens = QProcess::splitCommand(line);
        for (qsizetype index = 0; index < tokens.size(); ++index) {
            const QString token = tokens.at(index).trimmed();
            if (token.isEmpty() || token.startsWith(QLatin1Char('#')))
                break;
            if (token == QLatin1String("-f") || token == QLatin1String("-F")
                || token == QLatin1String("F")) {
                if (index + 1 >= tokens.size()) {
                    inputs->configurationErrors.append(QStringLiteral("filelist option %1 has no path: %2")
                                                           .arg(token, canonicalManifest));
                    break;
                }
                const QString nested = tokens.at(++index);
                const QString base = token == QLatin1String("-F")
                    ? manifestDirectory : inputs->projectRoot;
                const QString nestedPath = QFileInfo(nested).isAbsolute()
                    ? nested : QDir(base).filePath(nested);
                parseNativeFilelist(nestedPath, inputs, active, visited, depth + 1);
                continue;
            }
            if (token.startsWith(QStringLiteral("-f")) || token.startsWith(QStringLiteral("-F"))) {
                const bool relativeToManifest = token.startsWith(QStringLiteral("-F"));
                const QString nested = token.mid(2);
                const QString base = relativeToManifest ? manifestDirectory : inputs->projectRoot;
                const QString nestedPath = QFileInfo(nested).isAbsolute()
                    ? nested : QDir(base).filePath(nested);
                parseNativeFilelist(nestedPath, inputs, active, visited, depth + 1);
                continue;
            }
            if (token.startsWith(QStringLiteral("+incdir+"))) {
                const QStringList paths = token.mid(8).split(QLatin1Char('+'), Qt::SkipEmptyParts);
                for (const QString &path : paths) {
                    const QString candidate = QFileInfo(path).isAbsolute()
                        ? path : QDir(manifestDirectory).filePath(path);
                    const QString canonical = QFileInfo(candidate).canonicalFilePath();
                    if (canonical.isEmpty() || !within(canonical, inputs->rtlRoot)
                        || !QFileInfo(canonical).isDir()) {
                        inputs->configurationErrors.append(QStringLiteral("filelist include directory is missing or outside RTL: %1")
                                                               .arg(path));
                    } else {
                        const QString relative = QDir(inputs->rtlRoot).relativeFilePath(canonical);
                        if (!inputs->includeDirs.contains(relative))
                            inputs->includeDirs.append(relative);
                    }
                }
                continue;
            }
            if (token.startsWith(QStringLiteral("+define+"))) {
                const QStringList definitions = token.mid(8).split(QLatin1Char('+'), Qt::SkipEmptyParts);
                static const QRegularExpression validDefine(
                    QStringLiteral("^[A-Za-z_][A-Za-z0-9_$]*(?:=[A-Za-z0-9_.$-]+)?$"));
                for (const QString &definition : definitions) {
                    if (!validDefine.match(definition).hasMatch())
                        inputs->configurationErrors.append(QStringLiteral("invalid filelist macro definition: %1")
                                                               .arg(definition));
                    else if (!inputs->sourceDefines.contains(definition))
                        inputs->sourceDefines.append(definition);
                }
                continue;
            }
            if (token.startsWith(QStringLiteral("-D")) && token.size() > 2) {
                const QString definition = token.mid(2);
                static const QRegularExpression validDefine(
                    QStringLiteral("^[A-Za-z_][A-Za-z0-9_$]*(?:=[A-Za-z0-9_.$-]+)?$"));
                if (!validDefine.match(definition).hasMatch())
                    inputs->configurationErrors.append(QStringLiteral("invalid filelist macro definition: %1").arg(definition));
                else if (!inputs->sourceDefines.contains(definition))
                    inputs->sourceDefines.append(definition);
                continue;
            }
            if (token.startsWith(QStringLiteral("-I")) && token.size() > 2) {
                const QString includePath = token.mid(2);
                const QString canonical = QFileInfo(QDir(manifestDirectory).filePath(includePath)).canonicalFilePath();
                if (canonical.isEmpty() || !within(canonical, inputs->rtlRoot) || !QFileInfo(canonical).isDir())
                    inputs->configurationErrors.append(QStringLiteral("filelist include directory is missing or outside RTL: %1")
                                                           .arg(includePath));
                else {
                    const QString relative = QDir(inputs->rtlRoot).relativeFilePath(canonical);
                    if (!inputs->includeDirs.contains(relative))
                        inputs->includeDirs.append(relative);
                }
                continue;
            }
            if (token == QLatin1String("-D") || token == QLatin1String("-I")) {
                if (index + 1 >= tokens.size()) {
                    inputs->configurationErrors.append(QStringLiteral("filelist option %1 has no value.").arg(token));
                    break;
                }
                const QString value = tokens.at(++index);
                if (token == QLatin1String("-D")) {
                    static const QRegularExpression validDefine(
                        QStringLiteral("^[A-Za-z_][A-Za-z0-9_$]*(?:=[A-Za-z0-9_.$-]+)?$"));
                    if (!validDefine.match(value).hasMatch())
                        inputs->configurationErrors.append(QStringLiteral("invalid filelist macro definition: %1").arg(value));
                    else if (!inputs->sourceDefines.contains(value))
                        inputs->sourceDefines.append(value);
                } else {
                    const QString canonical = QFileInfo(QDir(manifestDirectory).filePath(value)).canonicalFilePath();
                    if (canonical.isEmpty() || !within(canonical, inputs->rtlRoot) || !QFileInfo(canonical).isDir())
                        inputs->configurationErrors.append(QStringLiteral("filelist include directory is missing or outside RTL: %1").arg(value));
                    else {
                        const QString relative = QDir(inputs->rtlRoot).relativeFilePath(canonical);
                        if (!inputs->includeDirs.contains(relative))
                            inputs->includeDirs.append(relative);
                    }
                }
                continue;
            }
            if (token == QLatin1String("-v")) {
                if (index + 1 >= tokens.size()) {
                    inputs->configurationErrors.append(QStringLiteral("filelist option -v has no source path."));
                    break;
                }
                addNativeFilelistSource(inputs, tokens.at(++index), manifestDirectory);
                continue;
            }
            if (token.startsWith(QLatin1Char('-')) || token.startsWith(QLatin1Char('+'))) {
                inputs->unsupported.append(QStringLiteral("filelist option is not supported by native staging: %1")
                                               .arg(token));
                if ((token == QLatin1String("-y") || token == QLatin1String("-I")
                     || token == QLatin1String("-D")) && index + 1 < tokens.size())
                    ++index;
                continue;
            }
            addNativeFilelistSource(inputs, token, manifestDirectory);
        }
    }
    active->remove(canonicalManifest);
    visited->insert(canonicalManifest);
}

QByteArray protectVerilogForCpp(const QByteArray &source, bool *containsSentinel)
{
    static const QList<QByteArray> sentinels{
        QByteArrayLiteral("_DFT_AGENT_CPP_QUOTE_"),
        QByteArrayLiteral("_DFT_AGENT_CPP_DOUBLE_HASH_"),
        QByteArrayLiteral("_DFT_AGENT_CPP_HASH_"),
        QByteArrayLiteral("_DFT_AGENT_CPP_COMMENT_"),
    };
    for (const QByteArray &sentinel : sentinels) {
        if (source.contains(sentinel)) {
            *containsSentinel = true;
            return {};
        }
    }
    *containsSentinel = false;
    static const QRegularExpression directive(
        QStringLiteral("^\\s*#\\s*(?:define|undef|if|ifdef|ifndef|elif|else|endif|error|pragma)\\b"));
    QByteArray protectedSource;
    protectedSource.reserve(source.size());
    for (QByteArray line : source.split('\n')) {
        const QByteArray ending = source.contains('\n') ? QByteArrayLiteral("\n") : QByteArray{};
        line.replace('\'', QByteArrayLiteral("_DFT_AGENT_CPP_QUOTE_"));
        if (line.trimmed().startsWith("##"))
            line.replace("##", QByteArrayLiteral("_DFT_AGENT_CPP_DOUBLE_HASH_"));
        const QString lineText = QString::fromUtf8(line);
        if (lineText.trimmed().startsWith(QStringLiteral("//")))
            line.replace("//", QByteArrayLiteral("_DFT_AGENT_CPP_COMMENT_"));
        if (!directive.match(lineText).hasMatch())
            line.replace('#', QByteArrayLiteral("_DFT_AGENT_CPP_HASH_"));
        protectedSource += line;
        if (ending == "\n")
            protectedSource += ending;
    }
    return protectedSource;
}

QByteArray restoreVerilogAfterCpp(QByteArray text)
{
    text.replace("_DFT_AGENT_CPP_QUOTE_", "'");
    text.replace("_DFT_AGENT_CPP_DOUBLE_HASH_", "##");
    text.replace("_DFT_AGENT_CPP_HASH_", "#");
    text.replace("_DFT_AGENT_CPP_COMMENT_", "//");
    return text;
}

bool preprocessStagedSource(const QString &path, const ProjectInputs &inputs,
                             QVariantMap *record, QString *error)
{
    QFile source(path);
    if (!source.open(QIODevice::ReadOnly)) {
        *error = QStringLiteral("Cannot read staged RTL for C preprocessing: %1").arg(path);
        return false;
    }
    const QByteArray original = source.readAll();
    source.close();
    bool containsSentinel = false;
    const QByteArray protectedSource = protectVerilogForCpp(original, &containsSentinel);
    if (containsSentinel) {
        *error = QStringLiteral("RTL source contains a reserved C-preprocessor sentinel: %1").arg(path);
        return false;
    }

    const QString inputPath = QFileInfo(path).absolutePath() + QStringLiteral("/.")
        + QFileInfo(path).fileName() + QStringLiteral(".dft_agent_cpp_input");
    const QString outputPath = QFileInfo(path).absolutePath() + QStringLiteral("/.")
        + QFileInfo(path).fileName() + QStringLiteral(".dft_agent_cpp_output");
    auto cleanup = [&] { QFile::remove(inputPath); QFile::remove(outputPath); };
    QFile protectedFile(inputPath);
    if (!protectedFile.open(QIODevice::WriteOnly | QIODevice::Truncate)
        || protectedFile.write(protectedSource) != protectedSource.size()) {
        cleanup();
        *error = QStringLiteral("Cannot write temporary preprocessor input: %1").arg(inputPath);
        return false;
    }
    protectedFile.close();

    QStringList arguments{QStringLiteral("-undef"), QStringLiteral("-nostdinc"),
                          QStringLiteral("-P"), QStringLiteral("-C")};
    if (!inputs.preprocessMacroFile.isEmpty())
        arguments << QStringLiteral("-imacros") << inputs.preprocessMacroFile;
    for (const QString &definition : inputs.preprocessDefinitions)
        arguments << QStringLiteral("-D") << definition;
    for (const QString &includeDirectory : inputs.preprocessIncludeDirs)
        arguments << QStringLiteral("-I") << includeDirectory;
    arguments << QStringLiteral("-o") << outputPath << inputPath;

    QProcess process;
    process.setProgram(inputs.preprocessExecutable);
    process.setArguments(arguments);
    process.setWorkingDirectory(QFileInfo(path).absolutePath());
    process.setProcessChannelMode(QProcess::SeparateChannels);
    process.start();
    if (!process.waitForStarted(5'000)) {
        cleanup();
        *error = QStringLiteral("Unable to start C preprocessor %1: %2")
            .arg(inputs.preprocessExecutable, process.errorString());
        return false;
    }
    if (!process.waitForFinished(inputs.preprocessTimeoutSeconds * 1'000)) {
        process.kill();
        process.waitForFinished(2'000);
        const QString detail = QString::fromUtf8(process.readAllStandardError()).trimmed();
        cleanup();
        *error = QStringLiteral("C preprocessing timed out after %1 seconds: %2")
            .arg(inputs.preprocessTimeoutSeconds).arg(detail.left(2'000));
        return false;
    }
    const int exitCode = process.exitCode();
    const QString standardError = QString::fromUtf8(process.readAllStandardError()).trimmed();
    QFile generated(outputPath);
    if (process.exitStatus() != QProcess::NormalExit || exitCode != 0 || !generated.open(QIODevice::ReadOnly)) {
        cleanup();
        *error = QStringLiteral("C preprocessing failed (exit %1): %2")
            .arg(exitCode).arg(standardError.left(2'000));
        return false;
    }
    QByteArray restored = restoreVerilogAfterCpp(generated.readAll());
    generated.close();
    cleanup();
    static const QRegularExpression remainingDirective(
        QStringLiteral("^\\s*#\\s*(?:define|undef|if|ifdef|ifndef|elif|else|endif|error|pragma)\\b"));
    const QStringList lines = QString::fromUtf8(restored).split(QLatin1Char('\n'));
    QStringList remaining;
    for (qsizetype index = 0; index < lines.size(); ++index) {
        if (remainingDirective.match(lines.at(index)).hasMatch())
            remaining.append(QStringLiteral("%1:%2").arg(index + 1).arg(lines.at(index).trimmed()));
    }
    if (!remaining.isEmpty()) {
        *error = QStringLiteral("C preprocessing left unresolved directives: %1")
            .arg(remaining.mid(0, 8).join(QStringLiteral("; ")));
        return false;
    }
    QSaveFile destination(path);
    if (!destination.open(QIODevice::WriteOnly) || destination.write(restored) != restored.size()
        || !destination.commit()) {
        *error = QStringLiteral("Cannot commit preprocessed staged RTL: %1").arg(path);
        return false;
    }
    QStringList command{inputs.preprocessExecutable};
    command.append(arguments);
    *record = {{QStringLiteral("source"), path},
               {QStringLiteral("macro_file"), inputs.preprocessMacroFile},
               {QStringLiteral("definitions"), stringListVariant(inputs.preprocessDefinitions)},
               {QStringLiteral("include_dirs"), stringListVariant(inputs.preprocessIncludeDirs)},
               {QStringLiteral("command"), stringListVariant(command)},
               {QStringLiteral("before_sha256"), QString::fromLatin1(QCryptographicHash::hash(original, QCryptographicHash::Sha256).toHex())},
               {QStringLiteral("after_sha256"), QString::fromLatin1(QCryptographicHash::hash(restored, QCryptographicHash::Sha256).toHex())},
               {QStringLiteral("original_project_modified"), false}};
    return true;
}

QVariantMap parseProject(const QVariantMap &project, const QVariantMap &arguments,
                         const QString &agentRoot, ProjectInputs *out)
{
    const QVariantMap metadata = project.value(QStringLiteral("metadata")).toMap();
    const QVariantMap execution = metadata.value(QStringLiteral("dft_execution")).toMap();
    const QVariantMap modules = metadata.value(QStringLiteral("flow_modules")).toMap();
    out->projectId = project.value(QStringLiteral("id")).toString().trimmed();
    out->projectName = project.value(QStringLiteral("name")).toString().trimmed();
    out->projectRoot = canonicalDirectory(project.value(QStringLiteral("root")).toString(), agentRoot);
    const QString rtlValue = project.value(QStringLiteral("rtl_root"), project.value(QStringLiteral("rtlRoot"))).toString();
    out->rtlRoot = canonicalDirectory(rtlValue.isEmpty() ? project.value(QStringLiteral("root")).toString() : rtlValue,
                                      agentRoot);
    out->top = project.value(QStringLiteral("top")).toString().trimmed();
    out->filelist = execution.value(QStringLiteral("filelist")).toString().trimmed();
    out->manifestTarget = execution.value(QStringLiteral("manifest_target"),
                                           QStringLiteral("default")).toString().trimmed();
    if (out->manifestTarget.isEmpty())
        out->manifestTarget = QStringLiteral("default");
    out->constraintFile = execution.value(QStringLiteral("constraint_file"), execution.value(QStringLiteral("sdc"))).toString().trimmed();
    out->libraryDir = canonicalDirectory(project.value(QStringLiteral("library_dir"), execution.value(QStringLiteral("library_dir"))).toString(), agentRoot);
    out->libraryFile = project.value(QStringLiteral("library_file"), execution.value(QStringLiteral("library_file"))).toString().trimmed();
    out->dcShell = arguments.value(QStringLiteral("dc_shell")).toString().trimmed();
    if (out->dcShell.isEmpty())
    out->dcShell = qEnvironmentVariable("DC_SHELL");
    if (out->dcShell.isEmpty())
        out->dcShell = QStandardPaths::findExecutable(QStringLiteral("dc_shell"));
    out->testmax = arguments.value(QStringLiteral("testmax")).toString().trimmed();
    if (out->testmax.isEmpty())
        out->testmax = qEnvironmentVariable("TESTMAX");
    if (out->testmax.isEmpty())
        out->testmax = QStandardPaths::findExecutable(QStringLiteral("testmax"));
    out->tessent = arguments.value(QStringLiteral("tessent")).toString().trimmed();
    if (out->tessent.isEmpty())
        out->tessent = qEnvironmentVariable("TESSENT");
    if (out->tessent.isEmpty())
        out->tessent = QStandardPaths::findExecutable(QStringLiteral("tessent"));
    out->workspaceRoot = arguments.value(QStringLiteral("workspace_root")).toString().trimmed();
    if (out->workspaceRoot.isEmpty())
        out->workspaceRoot = execution.value(QStringLiteral("workspace_path")).toString().trimmed();
    out->workspaceSuffixEnabled = truthy(execution.value(QStringLiteral("workspace_suffix_enabled")), true);
    if (!out->workspaceSuffixEnabled && out->workspaceRoot.isEmpty())
        out->configurationErrors.append(QStringLiteral(
            "关闭工作区后缀时必须显式设置 dft_execution.workspace_path。"));
    if (out->workspaceRoot.isEmpty())
        out->workspaceRoot = QStringLiteral("/media/6/Projects/DFT_agent_project_workspaces");
    out->workspaceRoot = studioAbsolutePath(out->workspaceRoot, agentRoot);
    out->clock = execution.value(QStringLiteral("clock")).toString().trimmed();
    out->reset = execution.value(QStringLiteral("reset")).toString().trimmed();
    out->resetActiveState = execution.value(QStringLiteral("reset_active_state"), 0).toInt();
    out->resetKind = execution.value(QStringLiteral("reset_kind"), QStringLiteral("asynchronous")).toString();
    out->compileStrategy = arguments.value(QStringLiteral("compile_strategy"),
        execution.value(QStringLiteral("compile_strategy"), QStringLiteral("single_pass"))).toString().trimmed().toLower();
    if (out->compileStrategy != QLatin1String("single_pass")
        && out->compileStrategy != QLatin1String("two_stage_mapping"))
        out->configurationErrors.append(QStringLiteral(
            "compile_strategy must be single_pass or two_stage_mapping."));
    out->chainCount = execution.value(QStringLiteral("scan_chain_count"), 1).toInt();
    out->maxChainLength = execution.value(QStringLiteral("max_chain_length"), 100).toInt();
    const int configuredFlowTimeout = execution.value(QStringLiteral("timeout_seconds"), 1800).toInt();
    if (configuredFlowTimeout < 1 || configuredFlowTimeout > 86400)
        out->configurationErrors.append(QStringLiteral("timeout_seconds 超出允许范围。"));
    out->flowTimeoutMultiplier = arguments.value(QStringLiteral("flow_timeout_multiplier"), 1).toInt();
    if (out->flowTimeoutMultiplier != 1 && out->flowTimeoutMultiplier != 2
        && out->flowTimeoutMultiplier != 4)
        out->configurationErrors.append(QStringLiteral("flow_timeout_multiplier must be 1, 2, or 4."));
    out->timeoutSeconds = int(qMin<qint64>(86400,
        qint64(configuredFlowTimeout) * out->flowTimeoutMultiplier));
    out->clockPeriodNs = execution.value(QStringLiteral("clock_period_ns"), 1000).toDouble();
    out->useConstraintFile = truthy(execution.value(QStringLiteral("use_constraint_file")), false);
    out->synthesis = truthy(modules.value(QStringLiteral("synthesis")), true);
    out->dft = truthy(modules.value(QStringLiteral("dft")), true);
    out->scan = out->dft && truthy(modules.value(QStringLiteral("scan")), true);
    out->atpg = out->dft && truthy(modules.value(QStringLiteral("atpg")), false);
    out->mbist = out->dft && truthy(modules.value(QStringLiteral("mbist")), false);
    out->lbist = out->dft && truthy(modules.value(QStringLiteral("lbist")), false);

    if (out->projectId.isEmpty())
        out->configurationErrors.append(QStringLiteral("项目 ID 不能为空。"));
    if (!QRegularExpression(QStringLiteral("^[A-Za-z0-9_.-]+$")).match(out->projectId).hasMatch())
        out->configurationErrors.append(QStringLiteral("项目 ID 不是安全的目录名称。"));
    if (out->projectRoot.isEmpty() || !QFileInfo(out->projectRoot).isDir())
        out->configurationErrors.append(QStringLiteral("项目目录不可用：%1").arg(out->projectRoot));
    if (out->rtlRoot.isEmpty() || !QFileInfo(out->rtlRoot).isDir())
        out->configurationErrors.append(QStringLiteral("RTL 目录不可用：%1").arg(out->rtlRoot));
    if (out->top.isEmpty() || !QRegularExpression(QStringLiteral("^[A-Za-z_][A-Za-z0-9_$]*$")).match(out->top).hasMatch())
        out->configurationErrors.append(QStringLiteral("顶层模块名不是有效 Verilog 标识符。"));
    if (!out->synthesis)
        out->configurationErrors.append(QStringLiteral("当前 RTL 输入要求启用综合阶段。"));
    if (out->dft && !out->scan && !out->mbist && !out->lbist)
        out->configurationErrors.append(QStringLiteral("启用 DFT 时至少选择一个 DFT 模块。"));
    if (out->atpg && !out->scan)
        out->configurationErrors.append(QStringLiteral("当前 ATPG 流程要求同时启用 Scan。"));
    out->dftTool = execution.value(QStringLiteral("dft_tool"), QStringLiteral("testmax")).toString().trimmed().toLower();
    if (out->dftTool != QLatin1String("testmax") && out->dftTool != QLatin1String("tessent"))
        out->configurationErrors.append(QStringLiteral("dft_execution.dft_tool must be testmax or tessent."));
    out->tessentDofile = execution.value(QStringLiteral("tessent_dofile")).toString().trimmed();
    if (!out->tessentDofile.isEmpty()) {
        QString dofile = QFileInfo(out->tessentDofile).isAbsolute()
            ? out->tessentDofile : QDir(out->projectRoot).filePath(out->tessentDofile);
        QString canonical = QFileInfo(dofile).canonicalFilePath();
        if (canonical.isEmpty() && !QFileInfo(out->tessentDofile).isAbsolute())
            canonical = QFileInfo(QDir(out->rtlRoot).filePath(out->tessentDofile)).canonicalFilePath();
        if (canonical.isEmpty() || !QFileInfo(canonical).isFile()
            || (!within(canonical, out->projectRoot) && !within(canonical, out->rtlRoot)))
            out->configurationErrors.append(QStringLiteral("Tessent dofile is missing or outside configured project/RTL roots."));
        else
            out->tessentDofile = canonical;
    }
    if (out->lbist)
        out->unsupported.append(QStringLiteral("LBIST execution has not been migrated to the native C++ flow yet."));
    if (out->mbist) {
        out->mbistIncludeMode = arguments.value(QStringLiteral("mbist_include_mode"),
            QStringLiteral("declared")).toString().trimmed().toLower();
        if (out->mbistIncludeMode != QLatin1String("declared")
            && out->mbistIncludeMode != QLatin1String("compile_parents"))
            out->configurationErrors.append(QStringLiteral("mbist_include_mode must be declared or compile_parents."));
        out->mbistDiagnosticMode = arguments.value(QStringLiteral("mbist_diagnostic_mode"),
            QStringLiteral("off")).toString().trimmed().toLower();
        if (out->mbistDiagnosticMode == QLatin1String("none"))
            out->mbistDiagnosticMode = QStringLiteral("off");
        if (out->mbistDiagnosticMode != QLatin1String("off")
            && out->mbistDiagnosticMode != QLatin1String("verbose"))
            out->configurationErrors.append(QStringLiteral("mbist_diagnostic_mode must be off or verbose."));
        out->mbistTimeoutMultiplier = arguments.value(QStringLiteral("mbist_timeout_multiplier"), 1).toInt();
        if (out->mbistTimeoutMultiplier != 1 && out->mbistTimeoutMultiplier != 2
            && out->mbistTimeoutMultiplier != 4)
            out->configurationErrors.append(QStringLiteral("mbist_timeout_multiplier must be 1, 2, or 4."));
        const QVariant mbistValue = execution.value(QStringLiteral("mbist_simulation"));
        const QVariantMap mbist = mbistValue.toMap();
        const auto readStringList = [&](const QString &key, bool required, QStringList *destination) {
            const QVariant value = mbist.value(key, QVariantList{});
            if (value.metaType().id() != QMetaType::QVariantList) {
                out->configurationErrors.append(QStringLiteral("mbist_simulation.%1 must be a string list.").arg(key));
                return;
            }
            for (const QVariant &entry : value.toList()) {
                if (entry.metaType().id() != QMetaType::QString || entry.toString().trimmed().isEmpty()) {
                    out->configurationErrors.append(QStringLiteral("mbist_simulation.%1 entries must be nonempty strings.").arg(key));
                    continue;
                }
                destination->append(entry.toString().trimmed());
            }
            if (required && destination->isEmpty())
                out->configurationErrors.append(QStringLiteral("mbist_simulation.%1 must be a nonempty string list.").arg(key));
            if (destination->size() != QSet<QString>(destination->cbegin(), destination->cend()).size())
                out->configurationErrors.append(QStringLiteral("mbist_simulation.%1 cannot contain duplicates.").arg(key));
        };
        if (mbistValue.metaType().id() != QMetaType::QVariantMap) {
            out->configurationErrors.append(QStringLiteral("启用 MBIST 时必须设置 dft_execution.mbist_simulation。"));
        } else {
            readStringList(QStringLiteral("compile_files"), true, &out->mbistCompileFiles);
            readStringList(QStringLiteral("include_dirs"), false, &out->mbistIncludeDirs);
            readStringList(QStringLiteral("expected_output"), true, &out->mbistExpectedOutput);
            readStringList(QStringLiteral("forbidden_output"), false, &out->mbistForbiddenOutput);
            const QVariant passValue = mbist.value(QStringLiteral("required_pass_markers"));
            if (!passValue.isValid() || passValue.isNull()) {
                for (const QString &marker : std::as_const(out->mbistExpectedOutput)) {
                    if (marker.contains(QStringLiteral("_PASS")))
                        out->mbistRequiredPassMarkers.append(marker);
                }
                if (out->mbistRequiredPassMarkers.isEmpty())
                    out->mbistRequiredPassMarkers = out->mbistExpectedOutput;
            } else {
                readStringList(QStringLiteral("required_pass_markers"), true, &out->mbistRequiredPassMarkers);
            }
            const QString testTop = mbist.value(QStringLiteral("test_top")).toString().trimmed();
            if (!testTop.isEmpty()
                && !QRegularExpression(QStringLiteral("^[A-Za-z_][A-Za-z0-9_$]*$")).match(testTop).hasMatch())
                out->configurationErrors.append(QStringLiteral("mbist_simulation.test_top must be a Verilog identifier."));
            else
                out->mbistTestTop = testTop;
            out->mbistTimeoutSeconds = mbist.value(QStringLiteral("timeout_seconds"), 120).toInt();
            if (out->mbistTimeoutSeconds < 1 || out->mbistTimeoutSeconds > 3600)
                out->configurationErrors.append(QStringLiteral("mbist_simulation.timeout_seconds must be from 1 to 3600."));

            const auto safeRelative = [](const QString &relative) {
                return !relative.isEmpty() && !QFileInfo(relative).isAbsolute()
                    && !relative.split(QLatin1Char('/')).contains(QStringLiteral(".."));
            };
            for (const QString &relative : std::as_const(out->mbistCompileFiles)) {
                const QString path = QFileInfo(QDir(out->projectRoot).filePath(relative)).canonicalFilePath();
                if (!safeRelative(relative) || path.isEmpty() || !within(path, out->projectRoot)
                    || !QFileInfo(path).isFile())
                    out->configurationErrors.append(QStringLiteral("MBIST 仿真文件不可用：%1").arg(relative));
            }
            for (const QString &relative : std::as_const(out->mbistIncludeDirs)) {
                const QString path = QFileInfo(QDir(out->projectRoot).filePath(relative)).canonicalFilePath();
                if (!safeRelative(relative) || path.isEmpty() || !within(path, out->projectRoot)
                    || !QFileInfo(path).isDir())
                    out->configurationErrors.append(QStringLiteral("MBIST 仿真包含目录不可用：%1").arg(relative));
            }
            for (const QString &marker : std::as_const(out->mbistRequiredPassMarkers)) {
                if (!out->mbistExpectedOutput.contains(marker))
                    out->configurationErrors.append(QStringLiteral("required_pass_markers must be listed in expected_output: %1").arg(marker));
            }
            for (const QString &marker : std::as_const(out->mbistForbiddenOutput)) {
                if (out->mbistExpectedOutput.contains(marker))
                    out->configurationErrors.append(QStringLiteral("MBIST expected text cannot also be forbidden: %1").arg(marker));
            }
        }
        if (QStandardPaths::findExecutable(QStringLiteral("iverilog")).isEmpty())
            out->configurationErrors.append(QStringLiteral("MBIST functional simulation requires iverilog."));
        if (QStandardPaths::findExecutable(QStringLiteral("vvp")).isEmpty())
            out->configurationErrors.append(QStringLiteral("MBIST functional simulation requires vvp."));
    }
    if (out->clock.isEmpty() && out->scan)
        out->configurationErrors.append(QStringLiteral("Scan 流程必须配置 dft_execution.clock。"));
    if (out->reset.isEmpty() && out->scan)
        out->configurationErrors.append(QStringLiteral("Scan 流程必须配置 dft_execution.reset。"));
    const QRegularExpression portIdentifier(QStringLiteral("^[A-Za-z_][A-Za-z0-9_$]*$"));
    if (out->scan && !out->clock.isEmpty() && !portIdentifier.match(out->clock).hasMatch())
        out->configurationErrors.append(QStringLiteral("dft_execution.clock is not a valid Verilog identifier."));
    if (out->scan && !out->reset.isEmpty() && !portIdentifier.match(out->reset).hasMatch())
        out->configurationErrors.append(QStringLiteral("dft_execution.reset is not a valid Verilog identifier."));
    const QVariant additionalClocksValue = execution.value(QStringLiteral("additional_scan_clocks"), QVariantList{});
    if (out->scan && additionalClocksValue.metaType().id() != QMetaType::QVariantList) {
        out->configurationErrors.append(QStringLiteral("dft_execution.additional_scan_clocks must be a string list."));
    } else if (out->scan) {
        const QVariantList additionalClocks = additionalClocksValue.toList();
        QSet<QString> seenClocks;
        for (const QVariant &value : additionalClocks) {
            const QString clock = value.toString().trimmed();
            if (value.metaType().id() != QMetaType::QString || !portIdentifier.match(clock).hasMatch()) {
                out->configurationErrors.append(QStringLiteral("dft_execution.additional_scan_clocks contains an invalid port."));
            } else if (clock == out->clock || seenClocks.contains(clock)) {
                out->configurationErrors.append(QStringLiteral("additional_scan_clocks cannot repeat a clock port."));
            } else {
                seenClocks.insert(clock);
                out->additionalScanClocks.append(clock);
            }
        }
    }
    const QVariant additionalResetsValue = execution.value(QStringLiteral("additional_resets"), QVariantList{});
    if (out->scan && additionalResetsValue.metaType().id() != QMetaType::QVariantList) {
        out->configurationErrors.append(QStringLiteral("dft_execution.additional_resets must be an object list."));
    } else if (out->scan) {
        const QVariantList additionalResets = additionalResetsValue.toList();
        QSet<QString> seenResets;
        for (qsizetype index = 0; index < additionalResets.size(); ++index) {
            const QVariantMap item = additionalResets.at(index).toMap();
            const QString resetPort = item.value(QStringLiteral("port")).toString().trimmed();
            bool activeStateOk = false;
            const int activeState = item.value(QStringLiteral("active_state"), 0).toInt(&activeStateOk);
            const QString label = QStringLiteral("dft_execution.additional_resets[%1]").arg(index);
            if (additionalResets.at(index).metaType().id() != QMetaType::QVariantMap
                || !portIdentifier.match(resetPort).hasMatch()) {
                out->configurationErrors.append(QStringLiteral("%1.port must be a valid Verilog identifier.").arg(label));
            } else if (!activeStateOk || (activeState != 0 && activeState != 1)) {
                out->configurationErrors.append(QStringLiteral("%1.active_state must be 0 or 1.").arg(label));
            } else if (resetPort == out->reset || seenResets.contains(resetPort)) {
                out->configurationErrors.append(QStringLiteral("additional_resets cannot repeat a reset port."));
            } else {
                seenResets.insert(resetPort);
                out->additionalResets.append(qMakePair(resetPort, activeState));
            }
        }
    }
    if (out->resetActiveState != 0 && out->resetActiveState != 1)
        out->configurationErrors.append(QStringLiteral("reset_active_state must be 0 or 1."));
    if (out->resetKind != QLatin1String("asynchronous") && out->resetKind != QLatin1String("synchronous"))
        out->configurationErrors.append(QStringLiteral("reset_kind must be asynchronous or synchronous."));
    if (out->chainCount < 1 || out->maxChainLength < 1)
        out->configurationErrors.append(QStringLiteral("scan_chain_count 和 max_chain_length 必须为正整数。"));
    if (out->timeoutSeconds < 1 || out->timeoutSeconds > 86400)
        out->configurationErrors.append(QStringLiteral("timeout_seconds 超出允许范围。"));
    if (!std::isfinite(out->clockPeriodNs) || out->clockPeriodNs <= 0)
        out->configurationErrors.append(QStringLiteral("clock_period_ns must be a positive finite number."));
    if (!QFileInfo(out->dcShell).isExecutable())
        out->configurationErrors.append(QStringLiteral("dc_shell 启动器不可用：%1").arg(out->dcShell));
    if (out->libraryDir.isEmpty() || out->libraryFile.isEmpty())
        out->configurationErrors.append(QStringLiteral("必须配置 library_dir 和 library_file。"));
    else if (!QFileInfo(QDir(out->libraryDir).filePath(out->libraryFile)).isFile())
        out->configurationErrors.append(QStringLiteral("工艺库文件不可用：%1").arg(QDir(out->libraryDir).filePath(out->libraryFile)));
    if (QFileInfo(out->libraryFile).fileName() != out->libraryFile)
        out->configurationErrors.append(QStringLiteral("library_file must be a filename within library_dir."));
    if (out->useConstraintFile && out->constraintFile.isEmpty())
        out->configurationErrors.append(QStringLiteral("启用约束文件时必须配置 constraint_file。"));
    if (!out->constraintFile.isEmpty()) {
        QString constraintPath = QFileInfo(out->constraintFile).isAbsolute() ? out->constraintFile
            : QDir(out->projectRoot).filePath(out->constraintFile);
        QString canonicalConstraint = QFileInfo(constraintPath).canonicalFilePath();
        if (canonicalConstraint.isEmpty() && !QFileInfo(out->constraintFile).isAbsolute()) {
            constraintPath = QDir(out->rtlRoot).filePath(out->constraintFile);
            canonicalConstraint = QFileInfo(constraintPath).canonicalFilePath();
        }
        if (canonicalConstraint.isEmpty()
            || (!within(canonicalConstraint, out->projectRoot) && !within(canonicalConstraint, out->rtlRoot)))
            out->configurationErrors.append(QStringLiteral("constraint_file is missing or outside the project and RTL roots."));
        else
            out->constraintFile = canonicalConstraint;
    }
    if (!out->workspaceSuffixEnabled) {
        const QString workspaceError = fixedWorkspaceError(out->workspaceRoot, out->projectRoot, out->projectId);
        if (!workspaceError.isEmpty())
            out->configurationErrors.append(workspaceError);
    }
    const QVariantList cellModels = execution.value(QStringLiteral("atpg_cell_model_files")).toList();
    for (const QVariant &entry : cellModels) {
        const QString raw = entry.toString().trimmed();
        QString path = QFileInfo(raw).isAbsolute() ? raw : QDir(out->libraryDir).filePath(raw);
        path = QFileInfo(path).canonicalFilePath();
        if (raw.isEmpty() || path.isEmpty() || !QFileInfo(path).isFile()) {
            if (out->atpg)
                out->configurationErrors.append(QStringLiteral("ATPG cell model is missing or unreadable: %1").arg(raw));
            continue;
        }
        if (out->dftTool == QLatin1String("testmax")
            && !QStringList{QStringLiteral("v"), QStringLiteral("vg"), QStringLiteral("sv")}
                 .contains(QFileInfo(path).suffix().toLower())) {
            if (out->atpg) {
                out->configurationErrors.append(QStringLiteral(
                    "TestMAX atpg_cell_model_files must be gate-level Verilog (.v/.vg/.sv) read_netlist inputs: %1")
                                                     .arg(path));
                continue;
            }
        }
        if (out->atpgCellModelFiles.contains(path))
            out->configurationErrors.append(QStringLiteral("Duplicate ATPG cell model path: %1").arg(path));
        else
            out->atpgCellModelFiles.append(path);
    }
    if (out->atpg && out->dftTool == QLatin1String("testmax") && out->atpgCellModelFiles.isEmpty())
        out->configurationErrors.append(QStringLiteral("ATPG requires dft_execution.atpg_cell_model_files."));
    if (out->atpg && out->dftTool == QLatin1String("testmax")) {
        if (!QFileInfo(out->testmax).isExecutable())
            out->configurationErrors.append(QStringLiteral("TestMAX launcher is unavailable: %1").arg(out->testmax));
    }
    if (out->atpg && out->dftTool == QLatin1String("tessent")) {
        if (!QFileInfo(out->tessent).isExecutable())
            out->configurationErrors.append(QStringLiteral("Tessent launcher is unavailable: %1").arg(out->tessent));
        const QVariant tessentModelsValue = execution.value(QStringLiteral("tessent_cell_library_files"), QVariantList{});
        if (tessentModelsValue.metaType().id() != QMetaType::QVariantList) {
            out->configurationErrors.append(QStringLiteral("dft_execution.tessent_cell_library_files must be a string list."));
        } else {
            QSet<QString> seenModels;
            for (const QVariant &value : tessentModelsValue.toList()) {
                const QString raw = value.toString().trimmed();
                const QString candidate = QFileInfo(raw).isAbsolute() ? raw : QDir(out->libraryDir).filePath(raw);
                const QString canonical = QFileInfo(candidate).canonicalFilePath();
                if (raw.isEmpty() || canonical.isEmpty() || !QFileInfo(canonical).isFile()) {
                    out->configurationErrors.append(QStringLiteral("Tessent cell library is missing or unreadable: %1").arg(raw));
                    continue;
                }
                if (seenModels.contains(canonical))
                    out->configurationErrors.append(QStringLiteral("Duplicate Tessent cell library: %1").arg(canonical));
                else {
                    seenModels.insert(canonical);
                    out->tessentCellLibraryFiles.append(canonical);
                }
            }
        }
        if (out->tessentCellLibraryFiles.isEmpty()) {
            const QSet<QString> nativeSuffixes{QStringLiteral("atpg"), QStringLiteral("atpglib"),
                QStringLiteral("celllib"), QStringLiteral("tcelllib")};
            for (const QString &model : std::as_const(out->atpgCellModelFiles)) {
                const QFileInfo modelInfo(model);
                if (nativeSuffixes.contains(modelInfo.suffix().toLower())) {
                    out->tessentCellLibraryFiles.append(model);
                    continue;
                }
                if (modelInfo.dir().dirName().compare(QStringLiteral("verilog"), Qt::CaseInsensitive) != 0)
                    continue;
                const QDir libraryRoot = modelInfo.dir().absoluteFilePath(QStringLiteral(".."));
                const QString compiledLibrary = libraryRoot.filePath(QStringLiteral("tessent/libcomp.atpglib"));
                if (QFileInfo(compiledLibrary).isFile())
                    out->tessentCellLibraryFiles.append(QFileInfo(compiledLibrary).canonicalFilePath());
                QDir fastscan(libraryRoot.filePath(QStringLiteral("fastscan")));
                for (const QString &path : fastscan.entryList({QStringLiteral("*.atpg")}, QDir::Files, QDir::Name))
                    out->tessentCellLibraryFiles.append(QFileInfo(fastscan.filePath(path)).canonicalFilePath());
            }
            out->tessentCellLibraryFiles.removeDuplicates();
        }
        if (out->tessentDofile.isEmpty() && out->tessentCellLibraryFiles.isEmpty())
            out->configurationErrors.append(QStringLiteral("Tessent ATPG requires tessent_dofile or tessent_cell_library_files."));
    }
    const QVariant minimumCoverage = execution.value(QStringLiteral("atpg_minimum_coverage"),
                                                       project.value(QStringLiteral("minimum_coverage")));
    if (minimumCoverage.isValid() && !minimumCoverage.isNull() && !minimumCoverage.toString().isEmpty()) {
        bool coverageOk = false;
        const double coverage = minimumCoverage.toDouble(&coverageOk);
        if (!coverageOk || !std::isfinite(coverage) || coverage < 0.0 || coverage > 100.0)
            out->configurationErrors.append(QStringLiteral("atpg_minimum_coverage must be a percentage from 0 to 100."));
        else
            out->minimumAtpgCoverage = coverage;
    }
    const int configuredAtpgTimeout = execution.value(QStringLiteral("atpg_timeout_seconds"),
                                                       out->timeoutSeconds).toInt();
    if (configuredAtpgTimeout < 1 || configuredAtpgTimeout > 86400)
        out->configurationErrors.append(QStringLiteral("atpg_timeout_seconds is outside the allowed range."));
    out->atpgTimeoutMultiplier = arguments.value(QStringLiteral("atpg_timeout_multiplier"), 1).toInt();
    if (out->atpgTimeoutMultiplier != 1 && out->atpgTimeoutMultiplier != 2
        && out->atpgTimeoutMultiplier != 4)
        out->configurationErrors.append(QStringLiteral("atpg_timeout_multiplier must be 1, 2, or 4."));
    out->atpgTimeoutSeconds = int(qMin<qint64>(86400,
        qint64(configuredAtpgTimeout) * out->atpgTimeoutMultiplier));
    out->atpgAbortLimit = arguments.value(QStringLiteral("atpg_abort_limit"),
        execution.value(QStringLiteral("atpg_abort_limit"), 10)).toInt();
    out->atpgDiagnosticMode = arguments.value(QStringLiteral("atpg_diagnostic_mode"),
        QStringLiteral("off")).toString().trimmed().toLower();
    if (out->atpgDiagnosticMode == QLatin1String("none"))
        out->atpgDiagnosticMode = QStringLiteral("off");
    else if (out->atpgDiagnosticMode == QLatin1String("full"))
        out->atpgDiagnosticMode = QStringLiteral("fault_classes");
    if (out->atpgDiagnosticMode != QLatin1String("off")
        && out->atpgDiagnosticMode != QLatin1String("fault_classes"))
        out->configurationErrors.append(QStringLiteral("atpg_diagnostic_mode must be off or fault_classes."));
    if (out->atpgDiagnosticMode != QLatin1String("off") && !out->atpg)
        out->configurationErrors.append(QStringLiteral("ATPG fault-class diagnostics require ATPG to be enabled."));
    out->maximumDftDrcViolations = project.value(QStringLiteral("maximum_dft_drc_violations"), 0).toInt();
    if (out->atpgTimeoutSeconds < 1 || out->atpgTimeoutSeconds > 86400)
        out->configurationErrors.append(QStringLiteral("atpg_timeout_seconds is outside the allowed range."));
    if (out->atpgAbortLimit < 1 || out->atpgAbortLimit > 1000)
        out->configurationErrors.append(QStringLiteral("atpg_abort_limit must be from 1 to 1000."));
    if (out->maximumDftDrcViolations < 0)
        out->configurationErrors.append(QStringLiteral("maximum_dft_drc_violations cannot be negative."));

    const QVariantList sources = execution.value(QStringLiteral("source_files")).toList();
    const QVariantList supports = execution.value(QStringLiteral("source_support_files")).toList();
    const QVariantList globs = execution.value(QStringLiteral("source_globs")).toList();
    const QVariantList supportGlobs = execution.value(QStringLiteral("source_support_globs")).toList();
    const QVariantList excludes = execution.value(QStringLiteral("source_exclude_globs")).toList();
    const QVariant manifestExclusionsValue = execution.value(QStringLiteral("manifest_exclude_files"), QVariantList{});
    const QVariantList manifestExclusions = manifestExclusionsValue.toList();
    if (manifestExclusionsValue.metaType().id() != QMetaType::QVariantList)
        out->configurationErrors.append(QStringLiteral("dft_execution.manifest_exclude_files must be a string list."));
    const QVariantList includes = execution.value(QStringLiteral("source_include_dirs")).toList();
    QString configuredManifestPath = QFileInfo(out->filelist).isAbsolute() ? out->filelist
        : QDir(out->projectRoot).filePath(out->filelist);
    if (!QFileInfo(configuredManifestPath).isFile())
        configuredManifestPath = QDir(out->rtlRoot).filePath(out->filelist);
    const QString canonicalConfiguredManifest = out->filelist.isEmpty()
        ? QString{} : QFileInfo(configuredManifestPath).canonicalFilePath();
    auto addNamedFiles = [&](const QVariantList &values, QStringList *target) {
        for (const QVariant &value : values) {
            QString candidate = value.toString();
            if (!QFileInfo(candidate).isAbsolute())
                candidate = QDir(out->projectRoot).filePath(candidate);
            QString canonicalCandidate = QFileInfo(candidate).canonicalFilePath();
            if (canonicalCandidate.isEmpty() && !QFileInfo(value.toString()).isAbsolute())
                canonicalCandidate = QFileInfo(QDir(out->rtlRoot).filePath(value.toString())).canonicalFilePath();
            if (!canonicalConfiguredManifest.isEmpty() && canonicalCandidate == canonicalConfiguredManifest)
                continue;
            const QString relative = sourceInsideRtl(value.toString(), out->projectRoot, out->rtlRoot);
            if (relative.isEmpty())
                out->missingFiles.append(value.toString());
            else if (!target->contains(relative))
                target->append(relative);
        }
    };
    addNamedFiles(sources, &out->sourceFiles);
    addNamedFiles(supports, &out->supportFiles);
    auto addGlobs = [&](const QVariantList &patterns, QStringList *target) {
        QStringList excludePatterns;
        for (const QVariant &exclude : excludes)
            excludePatterns.append(exclude.toString());
        for (const QVariant &item : patterns) {
            const QString pattern = item.toString().trimmed();
            if (pattern.isEmpty() || pattern.startsWith(QLatin1Char('/')) || pattern.contains(QStringLiteral(".."))) {
                out->configurationErrors.append(QStringLiteral("source glob 必须是 RTL root 内的相对模式：%1").arg(pattern));
                continue;
            }
            QDirIterator iterator(out->rtlRoot, QDir::Files | QDir::NoSymLinks, QDirIterator::Subdirectories);
            while (iterator.hasNext()) {
                const QString file = iterator.next();
                const QString relative = QDir(out->rtlRoot).relativeFilePath(file);
                const bool excluded = std::any_of(excludePatterns.cbegin(), excludePatterns.cend(),
                    [&](const QString &exclude) { return QDir::match(exclude, relative); });
                if (!excluded && QDir::match(pattern, relative) && !target->contains(relative))
                    target->append(relative);
            }
        }
    };
    addGlobs(globs, &out->sourceFiles);
    addGlobs(supportGlobs, &out->supportFiles);
    for (const QString &source : std::as_const(out->sourceFiles)) {
        if (out->supportFiles.contains(source))
            out->configurationErrors.append(QStringLiteral("source_files and source_support_files overlap: %1").arg(source));
    }
    for (const QVariant &value : includes) {
        const QString raw = value.toString();
        QString path = QFileInfo(raw).isAbsolute() ? QFileInfo(raw).canonicalFilePath()
                                                  : QFileInfo(QDir(out->rtlRoot).filePath(raw)).canonicalFilePath();
        if (path.isEmpty() || !within(path, out->rtlRoot) || !QFileInfo(path).isDir())
            out->configurationErrors.append(QStringLiteral("source include directory 不可用或越界：%1").arg(raw));
        else
            out->includeDirs.append(QDir(out->rtlRoot).relativeFilePath(path));
    }

    const QStringList sourceFilesBeforeManifest = out->sourceFiles;
    if (!out->filelist.isEmpty()) {
        QString manifestPath = QFileInfo(out->filelist).isAbsolute() ? out->filelist
            : QDir(out->projectRoot).filePath(out->filelist);
        if (!QFileInfo(manifestPath).isFile())
            manifestPath = QDir(out->rtlRoot).filePath(out->filelist);
        const QString canonicalManifest = QFileInfo(manifestPath).canonicalFilePath();
        if (canonicalManifest.isEmpty() || (!within(canonicalManifest, out->projectRoot) && !within(canonicalManifest, out->rtlRoot))) {
            out->configurationErrors.append(QStringLiteral("filelist 不存在或不在允许目录内：%1").arg(out->filelist));
        } else {
            const QString suffix = QFileInfo(canonicalManifest).suffix().toLower();
            if (suffix == QLatin1String("core")) {
                resolveFuseSoCManifest(out, canonicalManifest, out->manifestTarget);
            } else if (QFileInfo(canonicalManifest).fileName().compare(QStringLiteral("bender.yml"), Qt::CaseInsensitive) == 0) {
                resolveBenderManifest(out, canonicalManifest, out->manifestTarget);
            } else if (suffix == QLatin1String("yml") || suffix == QLatin1String("yaml")
                       || suffix == QLatin1String("lock")) {
                out->unsupported.append(QStringLiteral("Only bender.yml and FuseSoC .core source manifests are supported."));
            } else {
                QSet<QString> activeManifests;
                QSet<QString> visitedManifests;
                parseNativeFilelist(canonicalManifest, out, &activeManifests, &visitedManifests, 0);
            }
        }
    }
    if (!manifestExclusions.isEmpty() && out->filelist.isEmpty())
        out->configurationErrors.append(QStringLiteral("manifest_exclude_files requires a configured filelist."));
    QSet<QString> seenManifestExclusions;
    for (const QVariant &value : manifestExclusions) {
        const QString raw = value.toString().trimmed();
        const QString normalized = QDir::cleanPath(raw);
        if (value.metaType().id() != QMetaType::QString || raw.isEmpty()
            || QFileInfo(raw).isAbsolute() || normalized == QLatin1String("..")
            || normalized.startsWith(QStringLiteral("../"))
            || raw.split(QLatin1Char('/')).contains(QStringLiteral(".."))) {
            out->configurationErrors.append(QStringLiteral("manifest_exclude_files entries must be project-relative RTL paths: %1").arg(raw));
            continue;
        }
        if (seenManifestExclusions.contains(normalized))
            continue;
        seenManifestExclusions.insert(normalized);
        if (!out->manifestSourceFiles.contains(normalized)) {
            out->configurationErrors.append(QStringLiteral("manifest_exclude_files contains an entry not present in the filelist: %1").arg(normalized));
            continue;
        }
        if (!sourceFilesBeforeManifest.contains(normalized))
            out->sourceFiles.removeAll(normalized);
    }
    const QVariant sourceExclusionsValue = arguments.value(QStringLiteral("source_exclude_files"));
    if (sourceExclusionsValue.isValid()
        && sourceExclusionsValue.metaType().id() != QMetaType::QVariantList
        && sourceExclusionsValue.metaType().id() != QMetaType::QStringList) {
        out->configurationErrors.append(QStringLiteral("source_exclude_files must be a list of relative RTL paths."));
    } else {
        QVariantList sourceExclusions = sourceExclusionsValue.toList();
        if (sourceExclusions.isEmpty()) {
            for (const QString &value : sourceExclusionsValue.toStringList())
                sourceExclusions.append(value);
        }
        if (sourceExclusions.size() > 4)
            out->configurationErrors.append(QStringLiteral("At most four unsupported source files may be excluded per iteration."));
        QSet<QString> seenExclusions;
        for (const QVariant &value : sourceExclusions) {
            const QString raw = value.toString().trimmed();
            QString normalized = raw;
            normalized.replace(QLatin1Char('\\'), QLatin1Char('/'));
            normalized = QDir::cleanPath(normalized);
            if (value.metaType().id() != QMetaType::QString || raw.isEmpty()
                || QFileInfo(raw).isAbsolute() || normalized == QLatin1String("..")
                || normalized.startsWith(QStringLiteral("../"))
                || raw.split(QLatin1Char('/')).contains(QStringLiteral(".."))) {
                out->configurationErrors.append(QStringLiteral("source_exclude_files entries must be safe relative RTL paths: %1").arg(raw));
                continue;
            }
            if (seenExclusions.contains(normalized))
                continue;
            seenExclusions.insert(normalized);
            const QVariantMap diagnosis = SourceCompatibilityService::inspectExcludableSource(
                out->rtlRoot, out->sourceFiles, normalized);
            if (!diagnosis.value(QStringLiteral("safe_exclusion")).toBool()) {
                out->configurationErrors.append(QStringLiteral("Source compatibility exclusion is not proven safe for %1: %2")
                    .arg(normalized, diagnosis.value(QStringLiteral("reason")).toString()));
                continue;
            }
            out->sourceFiles.removeAll(normalized);
            out->sourceCompatibilityExclusions.append(diagnosis);
        }
    }
    const QVariant sourceAdditionsValue = arguments.value(QStringLiteral("source_extra_files"));
    if (sourceAdditionsValue.isValid()
        && sourceAdditionsValue.metaType().id() != QMetaType::QVariantList
        && sourceAdditionsValue.metaType().id() != QMetaType::QStringList) {
        out->configurationErrors.append(QStringLiteral("source_extra_files must be a list of project-relative HDL paths or globs."));
    } else {
        QVariantList requestedAdditions = sourceAdditionsValue.toList();
        if (requestedAdditions.isEmpty()) {
            for (const QString &value : sourceAdditionsValue.toStringList())
                requestedAdditions.append(value);
        }
        if (requestedAdditions.size() > 24)
            out->configurationErrors.append(QStringLiteral("At most 24 source files may be added per iteration."));
        QStringList expandedAdditions;
        for (const QVariant &value : requestedAdditions) {
            const QString raw = value.toString().trimmed();
            QString normalized = raw;
            normalized.replace(QLatin1Char('\\'), QLatin1Char('/'));
            if (value.metaType().id() != QMetaType::QString || normalized.isEmpty()
                || QFileInfo(normalized).isAbsolute()
                || normalized.split(QLatin1Char('/')).contains(QStringLiteral(".."))) {
                out->configurationErrors.append(QStringLiteral("source_extra_files entries must be safe relative RTL paths: %1").arg(raw));
                continue;
            }
            normalized = QDir::cleanPath(normalized);
            const bool hasGlob = normalized.contains(QLatin1Char('*')) || normalized.contains(QLatin1Char('?'))
                || normalized.contains(QLatin1Char('['));
            if (!hasGlob) {
                expandedAdditions.append(normalized);
                continue;
            }
            const QRegularExpression pattern(QRegularExpression::wildcardToRegularExpression(normalized));
            QDirIterator iterator(out->rtlRoot, QDir::Files | QDir::NoSymLinks, QDirIterator::Subdirectories);
            qsizetype matchCount = 0;
            while (iterator.hasNext()) {
                const QString candidate = iterator.next();
                const QString relative = QDir(out->rtlRoot).relativeFilePath(candidate);
                if (!pattern.match(relative).hasMatch())
                    continue;
                expandedAdditions.append(relative);
                if (++matchCount > 24)
                    break;
            }
            if (matchCount == 0)
                out->configurationErrors.append(QStringLiteral("source_extra_files glob matched no project files: %1").arg(raw));
        }
        expandedAdditions.removeDuplicates();
        if (expandedAdditions.size() > 24)
            out->configurationErrors.append(QStringLiteral("Expanded source_extra_files exceeds the 24-file iteration limit."));
        QVariantList additions;
        for (const QString &relative : std::as_const(expandedAdditions)) {
            if (out->sourceFiles.contains(relative))
                continue;
            const QString canonical = QFileInfo(QDir(out->rtlRoot).filePath(relative)).canonicalFilePath();
            const QString suffix = QFileInfo(relative).suffix().toLower();
            if (canonical.isEmpty() || !within(canonical, out->rtlRoot) || !QFileInfo(canonical).isFile()
                || (suffix != QLatin1String("v") && suffix != QLatin1String("sv"))) {
                out->configurationErrors.append(QStringLiteral("Supplemental source must be an available project-local .v/.sv file: %1").arg(relative));
                continue;
            }
            const QStringList modules = SourceCompatibilityService::moduleDeclarations(canonical);
            if (modules.isEmpty()) {
                out->configurationErrors.append(QStringLiteral("Supplemental source contains no module declaration or exceeds the inspection limit: %1").arg(relative));
                continue;
            }
            QFile file(canonical);
            if (!file.open(QIODevice::ReadOnly)) {
                out->configurationErrors.append(QStringLiteral("Supplemental source cannot be read: %1").arg(relative));
                continue;
            }
            const QByteArray bytes = file.readAll();
            out->sourceFiles.append(relative);
            out->supportFiles.removeAll(relative);
            additions.append(QVariantMap{
                {QStringLiteral("file"), relative},
                {QStringLiteral("module_names"), stringListVariant(modules.mid(0, 16))},
                {QStringLiteral("sha256"), QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex())}});
        }
        if (!requestedAdditions.isEmpty() && additions.isEmpty())
            out->configurationErrors.append(QStringLiteral("source_extra_files did not add any new HDL source."));
        out->sourceAdditions = additions;
    }
    for (const QString &source : std::as_const(out->sourceFiles)) {
        if (out->supportFiles.contains(source))
            out->configurationErrors.append(QStringLiteral("source_files and source_support_files overlap: %1").arg(source));
    }
    if (out->sourceFiles.isEmpty())
        out->configurationErrors.append(QStringLiteral("source_files/source_globs/filelist 必须解析到 RTL 编译文件。"));
    if (out->sourceFiles.size() + out->supportFiles.size() > 256)
        out->configurationErrors.append(QStringLiteral("源文件数量超过 native slice 上限 256。"));

    const QVariant replacementsValue = execution.value(QStringLiteral("source_replacements"), QVariantList{});
    if (replacementsValue.metaType().id() != QMetaType::QVariantList) {
        out->configurationErrors.append(QStringLiteral("dft_execution.source_replacements must be a list of objects."));
    } else {
        QSet<QString> replacementTargets;
        const QVariantList replacements = replacementsValue.toList();
        for (qsizetype index = 0; index < replacements.size(); ++index) {
            const QVariantMap replacement = replacements.at(index).toMap();
            const QString label = QStringLiteral("dft_execution.source_replacements[%1]").arg(index);
            if (replacements.at(index).metaType().id() != QMetaType::QVariantMap) {
                out->configurationErrors.append(QStringLiteral("%1 must be an object.").arg(label));
                continue;
            }
            const QSet<QString> allowedFields{QStringLiteral("target"), QStringLiteral("file"),
                                                QStringLiteral("sha256"), QStringLiteral("reason")};
            for (auto it = replacement.cbegin(); it != replacement.cend(); ++it) {
                if (!allowedFields.contains(it.key()))
                    out->configurationErrors.append(QStringLiteral("%1 has an unsupported field: %2").arg(label, it.key()));
            }
            const QString target = replacement.value(QStringLiteral("target")).toString().trimmed();
            const QString normalizedTarget = QDir::cleanPath(target);
            if (target.isEmpty() || QFileInfo(target).isAbsolute() || normalizedTarget == QLatin1String("..")
                || normalizedTarget.startsWith(QStringLiteral("../")) || target.split(QLatin1Char('/')).contains(QStringLiteral(".."))) {
                out->configurationErrors.append(QStringLiteral("%1.target must be a relative RTL file.").arg(label));
                continue;
            }
            if (!out->sourceFiles.contains(normalizedTarget))
                out->configurationErrors.append(QStringLiteral("%1.target must be a declared RTL compile source: %2").arg(label, normalizedTarget));
            if (replacementTargets.contains(normalizedTarget))
                out->configurationErrors.append(QStringLiteral("dft_execution.source_replacements has duplicate target: %1").arg(normalizedTarget));
            replacementTargets.insert(normalizedTarget);

            QString adapterPath = replacement.value(QStringLiteral("file")).toString().trimmed();
            if (adapterPath.isEmpty()) {
                out->configurationErrors.append(QStringLiteral("%1.file must not be empty.").arg(label));
                continue;
            }
            adapterPath = studioAbsolutePath(adapterPath, out->projectRoot);
            const QString adapter = QFileInfo(adapterPath).canonicalFilePath();
            if (adapter.isEmpty() || !QFileInfo(adapter).isFile()) {
                out->configurationErrors.append(QStringLiteral("%1.file is unavailable: %2").arg(label, adapterPath));
                continue;
            }
            const QString expectedHash = replacement.value(QStringLiteral("sha256")).toString().trimmed().toLower();
            static const QRegularExpression sha256Pattern(QStringLiteral("^[0-9a-f]{64}$"));
            if (!sha256Pattern.match(expectedHash).hasMatch()) {
                out->configurationErrors.append(QStringLiteral("%1.sha256 must be 64 hexadecimal characters.").arg(label));
                continue;
            }
            QFile adapterFile(adapter);
            if (!adapterFile.open(QIODevice::ReadOnly)) {
                out->configurationErrors.append(QStringLiteral("%1.file cannot be read: %2").arg(label, adapter));
                continue;
            }
            const QString actualHash = QString::fromLatin1(QCryptographicHash::hash(
                adapterFile.readAll(), QCryptographicHash::Sha256).toHex());
            if (actualHash != expectedHash) {
                out->configurationErrors.append(QStringLiteral("%1.sha256 does not match the adapter file.").arg(label));
                continue;
            }
            const QString reason = replacement.value(QStringLiteral("reason")).toString().trimmed();
            if (reason.isEmpty()) {
                out->configurationErrors.append(QStringLiteral("%1.reason must not be empty.").arg(label));
                continue;
            }
            out->sourceReplacements.append({normalizedTarget, adapter, expectedHash, reason});
        }
    }

    QVariantMap preprocessor = execution.value(QStringLiteral("source_preprocessor")).toMap();
    if (arguments.contains(QStringLiteral("source_annotation_mode"))) {
        QString mode = arguments.value(QStringLiteral("source_annotation_mode")).toString().trimmed().toLower();
        if (mode == QLatin1String("none"))
            mode = QStringLiteral("off");
        if (mode != QLatin1String("off") && mode != QLatin1String("strip_unsupported_state_encoding_hints"))
            out->configurationErrors.append(QStringLiteral("source_annotation_mode must be off or strip_unsupported_state_encoding_hints."));
        else
            out->sourceAnnotationMode = mode;
    }
    if (arguments.contains(QStringLiteral("source_preprocess_mode"))) {
        QString requestedMode = arguments.value(QStringLiteral("source_preprocess_mode")).toString().trimmed().toLower();
        if (requestedMode == QLatin1String("none"))
            requestedMode = QStringLiteral("off");
        if (requestedMode != QLatin1String("off") && requestedMode != QLatin1String("cpp"))
            out->configurationErrors.append(QStringLiteral("source_preprocess_mode must be off or cpp."));
        else
            preprocessor.insert(QStringLiteral("mode"), requestedMode);
    }
    if (!preprocessor.isEmpty()) {
        const QSet<QString> allowedPreprocessorFields{
            QStringLiteral("mode"), QStringLiteral("macro_file"), QStringLiteral("source_files"),
            QStringLiteral("definitions"), QStringLiteral("include_dirs"), QStringLiteral("timeout_seconds"),
        };
        for (auto it = preprocessor.cbegin(); it != preprocessor.cend(); ++it) {
            if (!allowedPreprocessorFields.contains(it.key()))
                out->configurationErrors.append(QStringLiteral("source_preprocessor contains an unsupported field: %1").arg(it.key()));
        }
        const QString mode = preprocessor.value(QStringLiteral("mode"), QStringLiteral("cpp")).toString().trimmed().toLower();
        if (mode != QLatin1String("off") && mode != QLatin1String("cpp")) {
            out->configurationErrors.append(QStringLiteral("source_preprocessor.mode must be off or cpp."));
        } else if (mode == QLatin1String("cpp")) {
            out->preprocessEnabled = true;
            const QVariant selectedSourcesValue = preprocessor.value(QStringLiteral("source_files"));
            QVariantList selectedSources = selectedSourcesValue.toList();
            if (selectedSourcesValue.isValid()
                && selectedSourcesValue.metaType().id() != QMetaType::QVariantList)
                out->configurationErrors.append(QStringLiteral("source_preprocessor.source_files must be a list."));
            if (selectedSources.isEmpty()) {
                for (const QString &source : std::as_const(out->sourceFiles))
                    selectedSources.append(source);
            }
            if (selectedSources.isEmpty())
                out->configurationErrors.append(QStringLiteral("source_preprocessor could not resolve any declared RTL source files."));
            for (const QVariant &value : selectedSources) {
                if (value.metaType().id() != QMetaType::QString) {
                    out->configurationErrors.append(QStringLiteral("source_preprocessor.source_files entries must be strings."));
                    continue;
                }
                const QString relative = value.toString().trimmed();
                if (relative.isEmpty() || QFileInfo(relative).isAbsolute() || relative.contains(QStringLiteral(".."))) {
                    out->configurationErrors.append(QStringLiteral("source_preprocessor.source_files must be relative RTL files: %1").arg(relative));
                } else if (!out->sourceFiles.contains(relative)) {
                    out->configurationErrors.append(QStringLiteral("source_preprocessor.source_files must be declared RTL inputs: %1").arg(relative));
                } else if (!out->preprocessSources.contains(relative)) {
                    out->preprocessSources.append(relative);
                }
            }
            const QString macroFile = preprocessor.value(QStringLiteral("macro_file")).toString().trimmed();
            if (!macroFile.isEmpty()) {
                const QStringList segments = macroFile.split(QLatin1Char('/'));
                const QString path = QFileInfo(QDir(out->projectRoot).filePath(macroFile)).canonicalFilePath();
                if (QFileInfo(macroFile).isAbsolute() || segments.contains(QStringLiteral(".."))
                    || path.isEmpty() || !within(path, out->projectRoot) || !QFileInfo(path).isFile())
                    out->configurationErrors.append(QStringLiteral("source_preprocessor.macro_file is unavailable or outside the project: %1").arg(macroFile));
                else
                    out->preprocessMacroFile = path;
            }
            static const QRegularExpression definitionPattern(
                QStringLiteral("^[A-Za-z_][A-Za-z0-9_]*(?:=[A-Za-z0-9_]+)?$"));
            const QVariant definitionsValue = preprocessor.value(QStringLiteral("definitions"), QVariantList{});
            const QVariantList definitions = definitionsValue.toList();
            if (definitionsValue.metaType().id() != QMetaType::QVariantList)
                out->configurationErrors.append(QStringLiteral("source_preprocessor.definitions must be a list."));
            for (const QVariant &value : definitions) {
                if (value.metaType().id() != QMetaType::QString) {
                    out->configurationErrors.append(QStringLiteral("source_preprocessor.definitions entries must be strings."));
                    continue;
                }
                const QString definition = value.toString().trimmed();
                if (!definitionPattern.match(definition).hasMatch())
                    out->configurationErrors.append(QStringLiteral("source_preprocessor has an invalid macro definition: %1").arg(definition));
                else
                    out->preprocessDefinitions.append(definition);
            }
            const QVariant includesValue = preprocessor.value(QStringLiteral("include_dirs"), QVariantList{});
            const QVariantList preprocessorIncludes = includesValue.toList();
            if (includesValue.metaType().id() != QMetaType::QVariantList)
                out->configurationErrors.append(QStringLiteral("source_preprocessor.include_dirs must be a list."));
            for (const QVariant &value : preprocessorIncludes) {
                if (value.metaType().id() != QMetaType::QString) {
                    out->configurationErrors.append(QStringLiteral("source_preprocessor.include_dirs entries must be strings."));
                    continue;
                }
                const QString relative = value.toString().trimmed();
                const QStringList segments = relative.split(QLatin1Char('/'));
                const QString path = QFileInfo(QDir(out->projectRoot).filePath(relative)).canonicalFilePath();
                if (relative.isEmpty() || QFileInfo(relative).isAbsolute() || segments.contains(QStringLiteral(".."))
                    || path.isEmpty() || !within(path, out->projectRoot) || !QFileInfo(path).isDir())
                    out->configurationErrors.append(QStringLiteral("source_preprocessor.include_dirs contains an unavailable or unsafe path: %1").arg(relative));
                else
                    out->preprocessIncludeDirs.append(path);
            }
            out->preprocessTimeoutSeconds = preprocessor.value(QStringLiteral("timeout_seconds"), 120).toInt();
            if (out->preprocessTimeoutSeconds < 1 || out->preprocessTimeoutSeconds > 600)
                out->configurationErrors.append(QStringLiteral("source_preprocessor.timeout_seconds must be from 1 to 600."));
            out->preprocessExecutable = QStandardPaths::findExecutable(QStringLiteral("cpp"));
            if (out->preprocessExecutable.isEmpty() && QFileInfo(QStringLiteral("/usr/bin/cpp")).isExecutable())
                out->preprocessExecutable = QStringLiteral("/usr/bin/cpp");
            if (out->preprocessExecutable.isEmpty())
                out->configurationErrors.append(QStringLiteral("No executable C preprocessor (cpp) is available."));
        }
    }

    const QVariantMap synthesis = execution.value(QStringLiteral("synthesis_settings")).toMap();
    const QVariant reports = synthesis.value(QStringLiteral("reports"), QVariantList{});
    if (reports.metaType().id() != QMetaType::QVariantList) {
        out->configurationErrors.append(QStringLiteral("synthesis_settings.reports must be a string list."));
    } else {
        const QVariantList values = reports.toList();
        for (const QVariant &value : values) {
            if (value.metaType().id() != QMetaType::QString)
                out->configurationErrors.append(QStringLiteral("synthesis_settings.reports entries must be strings."));
            else
                out->synthesisReports.append(value.toString());
        }
    }
    for (const QString &key : {QStringLiteral("pre_scripts"), QStringLiteral("post_scripts"),
                                QStringLiteral("constraint_files")}) {
        const QVariant configured = synthesis.value(key, QVariantList{});
        if (configured.metaType().id() != QMetaType::QVariantList) {
            out->configurationErrors.append(QStringLiteral("synthesis_settings.%1 must be a string list.").arg(key));
            continue;
        }
        const QVariantList values = configured.toList();
        for (const QVariant &value : values) {
            if (value.metaType().id() != QMetaType::QString) {
                out->configurationErrors.append(QStringLiteral("synthesis_settings.%1 entries must be strings.").arg(key));
                continue;
            }
            const QString raw = value.toString().trimmed();
            const QString candidate = QFileInfo(raw).isAbsolute() ? raw : QDir(out->projectRoot).filePath(raw);
            const QString canonical = QFileInfo(candidate).canonicalFilePath();
            const QString suffix = QFileInfo(canonical).suffix().toLower();
            if (raw.isEmpty() || canonical.isEmpty() || !QFileInfo(canonical).isFile()
                || (suffix != QLatin1String("tcl") && suffix != QLatin1String("sdc"))) {
                out->configurationErrors.append(QStringLiteral("synthesis_settings.%1 file is unavailable or not Tcl/SDC: %2")
                                                    .arg(key, raw));
                continue;
            }
            out->synthesisScriptFiles[key].append(canonical);
        }
    }
    const QVariant additionalCommands = synthesis.value(QStringLiteral("additional_tcl_commands"), QVariantList{});
    if (additionalCommands.metaType().id() != QMetaType::QVariantList) {
        out->configurationErrors.append(QStringLiteral("synthesis_settings.additional_tcl_commands must be a string list."));
    } else {
        const QVariantList values = additionalCommands.toList();
        for (qsizetype index = 0; index < values.size(); ++index) {
            if (values.at(index).metaType().id() != QMetaType::QString) {
                out->configurationErrors.append(QStringLiteral("synthesis_settings.additional_tcl_commands entries must be strings."));
                continue;
            }
            QString error;
            const QString label = QStringLiteral("synthesis_settings.additional_tcl_commands[%1]").arg(index);
            const QString command = values.at(index).toString().trimmed();
            if (!validateAdditionalSynthesisCommand(command, label, &error))
                out->configurationErrors.append(error);
            else
                out->additionalSynthesisCommands.append(command);
        }
    }
    const QString compileCommand = synthesis.value(QStringLiteral("compile_command"), QStringLiteral("compile")).toString();
    if (compileCommand != QLatin1String("compile") && compileCommand != QLatin1String("compile_ultra"))
        out->configurationErrors.append(QStringLiteral("synthesis_settings.compile_command must be compile or compile_ultra."));
    const QString autoUngroup = synthesis.value(QStringLiteral("auto_ungroup"), QStringLiteral("none")).toString();
    if (!QStringList{QStringLiteral("none"), QStringLiteral("area"), QStringLiteral("delay"), QStringLiteral("all")}
             .contains(autoUngroup))
        out->configurationErrors.append(QStringLiteral("synthesis_settings.auto_ungroup has an unsupported value."));
    const auto validateEffort = [&execution, out](const QString &key, const QStringList &allowed) {
        if (!execution.contains(key))
            return;
        const QString value = execution.value(key).toString().trimmed().toLower();
        if (!allowed.contains(value))
            out->configurationErrors.append(QStringLiteral("dft_execution.%1 has an unsupported value: %2")
                                                .arg(key, value));
    };
    validateEffort(QStringLiteral("map_effort"),
                   {QStringLiteral("low"), QStringLiteral("medium"), QStringLiteral("high")});
    validateEffort(QStringLiteral("area_effort"),
                   {QStringLiteral("none"), QStringLiteral("low"), QStringLiteral("medium"), QStringLiteral("high")});
    validateEffort(QStringLiteral("power_effort"),
                   {QStringLiteral("none"), QStringLiteral("low"), QStringLiteral("medium"), QStringLiteral("high")});
    const QVariantMap autofix = execution.value(QStringLiteral("drc_autofix")).toMap();
    QString autofixMode = autofix.value(QStringLiteral("mode"), QStringLiteral("off")).toString();
    if (arguments.contains(QStringLiteral("drc_repair_mode")))
        autofixMode = arguments.value(QStringLiteral("drc_repair_mode")).toString().trimmed().toLower();
    out->autofixRequestedMode = autofixMode;
    out->autofixMode = autofixMode;
    if (!QStringList{QStringLiteral("off"), QStringLiteral("clock_only"), QStringLiteral("clock_reset_set"),
                     QStringLiteral("reset_set")}.contains(autofixMode))
        out->configurationErrors.append(QStringLiteral("drc_autofix.mode has an unsupported value."));
    if (out->resetKind == QLatin1String("asynchronous"))
        out->autofixResetPort = out->reset;
    else if (!out->additionalResets.isEmpty())
        out->autofixResetPort = out->additionalResets.first().first;
    if (out->autofixResetPort.isEmpty() && autofixMode == QLatin1String("clock_reset_set")) {
        out->autofixMode = QStringLiteral("clock_only");
        out->autofixAdjustment = QStringLiteral(
            "The configured primary reset is synchronous; reset/set AutoFix requires an asynchronous reset port, so only clock AutoFix is enabled.");
    } else if (out->autofixResetPort.isEmpty() && autofixMode == QLatin1String("reset_set")) {
        out->configurationErrors.append(QStringLiteral(
            "reset_set AutoFix requires a configured asynchronous reset port; the primary reset is synchronous."));
    }
    const QString testMode = autofix.value(QStringLiteral("test_mode_port")).toString();
    out->autofixTestModePort = testMode;
    if (!testMode.isEmpty() && !portIdentifier.match(testMode).hasMatch())
        out->configurationErrors.append(QStringLiteral("drc_autofix.test_mode_port is not a valid identifier."));
    const QVariant macroFilesValue = execution.value(QStringLiteral("macro_library_files"), QVariantList{});
    if (macroFilesValue.metaType().id() != QMetaType::QVariantList) {
        out->configurationErrors.append(QStringLiteral("dft_execution.macro_library_files must be a string list."));
    } else {
        QSet<QString> seenMacroFiles;
        for (const QVariant &value : macroFilesValue.toList()) {
            if (value.metaType().id() != QMetaType::QString) {
                out->configurationErrors.append(QStringLiteral("dft_execution.macro_library_files entries must be strings."));
                continue;
            }
            const QString raw = value.toString().trimmed();
            const QString candidate = QFileInfo(raw).isAbsolute() ? raw : QDir(out->libraryDir).filePath(raw);
            const QString canonical = QFileInfo(candidate).canonicalFilePath();
            if (raw.isEmpty() || canonical.isEmpty() || !QFileInfo(canonical).isFile()) {
                out->configurationErrors.append(QStringLiteral("SRAM macro library is unavailable: %1").arg(raw));
            } else if (seenMacroFiles.contains(canonical)) {
                out->configurationErrors.append(QStringLiteral("dft_execution.macro_library_files contains duplicates."));
            } else {
                seenMacroFiles.insert(canonical);
                out->macroLibraryFiles.append(canonical);
            }
        }
    }
    const QVariant testModelsValue = execution.value(QStringLiteral("test_models"), QVariantList{});
    if (testModelsValue.isValid() && !testModelsValue.isNull()
        && testModelsValue.metaType().id() != QMetaType::QVariantList) {
        out->configurationErrors.append(QStringLiteral("dft_execution.test_models must be a list."));
    } else {
        QSet<QString> seenModels;
        const QVariantList models = testModelsValue.toList();
        for (qsizetype index = 0; index < models.size(); ++index) {
            const QString label = QStringLiteral("dft_execution.test_models[%1]").arg(index);
            if (models.at(index).metaType().id() != QMetaType::QVariantMap) {
                out->configurationErrors.append(QStringLiteral("%1 must be an object.").arg(label));
                continue;
            }
            const QVariantMap model = models.at(index).toMap();
            const QString format = model.value(QStringLiteral("format")).toString().trimmed().toLower();
            const QString raw = model.value(QStringLiteral("file")).toString().trimmed();
            const QString design = model.value(QStringLiteral("design")).toString().trimmed();
            const QString candidate = QFileInfo(raw).isAbsolute() ? raw : QDir(out->projectRoot).filePath(raw);
            const QString canonical = QFileInfo(candidate).canonicalFilePath();
            if (format != QLatin1String("ctl") && format != QLatin1String("ddc")) {
                out->configurationErrors.append(QStringLiteral("%1.format must be ctl or ddc.").arg(label));
                continue;
            }
            if (raw.isEmpty() || canonical.isEmpty() || !QFileInfo(canonical).isFile()) {
                out->configurationErrors.append(QStringLiteral("%1.file is unavailable: %2").arg(label, raw));
                continue;
            }
            if (format == QLatin1String("ctl") && design.isEmpty()) {
                out->configurationErrors.append(QStringLiteral("CTL test model requires a design name."));
                continue;
            }
            if (!design.isEmpty() && !portIdentifier.match(design).hasMatch()) {
                out->configurationErrors.append(QStringLiteral("%1.design must be a valid identifier.").arg(label));
                continue;
            }
            if (format == QLatin1String("ddc") && !design.isEmpty()) {
                out->configurationErrors.append(QStringLiteral("DDC test models must not specify design."));
                continue;
            }
            const QString identity = canonical + QLatin1Char('|') + format + QLatin1Char('|') + design;
            if (seenModels.contains(identity)) {
                out->configurationErrors.append(QStringLiteral("dft_execution.test_models contains duplicates."));
                continue;
            }
            seenModels.insert(identity);
            out->testModels.append({canonical, format, design});
        }
    }
    return {};
}

QVariantList stringListVariant(const QStringList &values)
{
    QVariantList list;
    for (const QString &value : values)
        list.append(value);
    return list;
}

QString safeTclAtom(const QString &value, QString *error)
{
    if (value.contains(QRegularExpression(QStringLiteral("[{}\\[\\]$;\\\\\\n\\r]")))) {
        if (error)
            *error = QStringLiteral("Unsafe Tcl atom: %1").arg(value);
        return {};
    }
    return QLatin1Char('{') + value + QLatin1Char('}');
}

QString tclText(const QVariant &value, const QString &label, QString *error)
{
    const QString text = value.toString().trimmed();
    if (text.isEmpty() || text.contains(QRegularExpression(QStringLiteral("[{}$;\\\\\n\r]")))) {
        if (error)
            *error = QStringLiteral("%1 must be nonempty safe Tcl text.").arg(label);
        return {};
    }
    return QLatin1Char('{') + text + QLatin1Char('}');
}

bool tclNumber(const QVariant &value, bool positive, const QString &label,
               double *number, QString *error)
{
    bool ok = false;
    const double parsed = value.toDouble(&ok);
    if (!ok || !std::isfinite(parsed) || (positive ? parsed <= 0 : parsed < 0)) {
        if (error)
            *error = QStringLiteral("%1 must be a %2 finite number.")
                .arg(label, positive ? QStringLiteral("positive") : QStringLiteral("nonnegative"));
        return false;
    }
    *number = parsed;
    return true;
}

QString tclCollection(const QVariant &value, const QString &label, QString *error)
{
    const QString text = value.toString().trimmed();
    static const QRegularExpression valid(
        QStringLiteral("^\\[\\s*(?:get_ports|get_pins|get_nets|get_cells|get_clocks|all_inputs|all_outputs)\\b[^\\]\\r\\n]*\\]$"));
    if (!valid.match(text).hasMatch() || text.contains(QLatin1Char('$')) || text.contains(QLatin1Char(';'))) {
        if (error)
            *error = QStringLiteral("%1 must be a supported Tcl collection selector.").arg(label);
        return {};
    }
    return text;
}

QString clockGroupCommand(const QString &command, const QString &label, QString *error)
{
    static const QRegularExpression nested(QStringLiteral("\\[\\s*([A-Za-z_][A-Za-z0-9_]*)"));
    if (!command.trimmed().startsWith(QStringLiteral("set_clock_groups "))
        || command.contains(QLatin1Char('$')) || command.contains(QLatin1Char(';'))
        || command.contains(QLatin1Char('\n')) || command.contains(QLatin1Char('\r'))) {
        if (error)
            *error = QStringLiteral("%1 must be a safe set_clock_groups command.").arg(label);
        return {};
    }
    auto matches = nested.globalMatch(command);
    while (matches.hasNext()) {
        if (matches.next().captured(1) != QLatin1String("get_clocks")) {
            if (error)
                *error = QStringLiteral("%1 only allows get_clocks selectors.").arg(label);
            return {};
        }
    }
    return command.trimmed();
}

QVariantList configuredSynthesisAcceptance(const ProjectInputs &inputs)
{
    const auto check = [](const QString &path, const QString &contains = QString{},
                          bool noErrors = false) {
        QVariantMap item{{QStringLiteral("path"), path},
                         {QStringLiteral("blocking"), true}};
        if (!contains.isEmpty())
            item.insert(QStringLiteral("contains"), contains);
        if (noErrors)
            item.insert(QStringLiteral("no_errors"), true);
        return item;
    };
    return {
        check(QStringLiteral("flow/reports/analyze.log"), QString{}, true),
        check(QStringLiteral("flow/reports/read_link.rpt"), inputs.top, true),
        check(QStringLiteral("flow/reports/synthesis_qor.rpt"), QStringLiteral("Design"), true),
        check(QStringLiteral("flow/reports/synthesis_area.rpt"), QStringLiteral("area"), true),
        check(QStringLiteral("flow/reports/synthesis_timing.rpt"),
              (inputs.scan || inputs.useConstraintFile) ? QStringLiteral("slack")
                                                        : QStringLiteral("Report : timing"), true),
        check(QStringLiteral("flow/synthesis/%1_synth.ddc").arg(inputs.projectId)),
        check(QStringLiteral("flow/synthesis/%1_synth.v").arg(inputs.projectId))};
}

QVariantList configuredFlowAcceptance(const ProjectInputs &inputs)
{
    const auto check = [](const QString &path, const QString &contains = QString{},
                          bool noErrors = false) {
        QVariantMap item{{QStringLiteral("path"), path},
                         {QStringLiteral("blocking"), true}};
        if (!contains.isEmpty())
            item.insert(QStringLiteral("contains"), contains);
        if (noErrors)
            item.insert(QStringLiteral("no_errors"), true);
        return item;
    };
    QVariantList acceptance = configuredSynthesisAcceptance(inputs);
    if (inputs.mbist) {
        for (const QString &marker : std::as_const(inputs.mbistExpectedOutput))
            acceptance.append(check(QStringLiteral("flow/reports/agent_mbist_simulation.log"), marker, true));
        if (!inputs.mbistForbiddenOutput.isEmpty()) {
            QVariantMap forbidden{{QStringLiteral("path"), QStringLiteral("flow/reports/agent_mbist_simulation.log")},
                {QStringLiteral("not_contains"), stringListVariant(inputs.mbistForbiddenOutput)},
                {QStringLiteral("no_errors"), true}, {QStringLiteral("blocking"), true}};
            acceptance.append(forbidden);
        }
        QVariantMap summary{{QStringLiteral("path"), QStringLiteral("flow/reports/mbist_summary.json")},
            {QStringLiteral("minimum_mbist_case_coverage"), 100.0}, {QStringLiteral("blocking"), true}};
        acceptance.append(summary);
        acceptance.append(check(QStringLiteral("flow/mapped_sim/%1_mbist.vvp").arg(inputs.projectId)));
    }
    if (!inputs.scan)
        return acceptance;
    QVariantMap postDrc = check(QStringLiteral("flow/reports/post_dft_drc.rpt"),
                                QStringLiteral("Total violations:"), true);
    postDrc.insert(QStringLiteral("maximum_dft_drc_violations"), inputs.maximumDftDrcViolations);
    acceptance.append(check(QStringLiteral("flow/reports/pre_dft_drc.rpt"), QStringLiteral("DRC")));
    acceptance.append(check(QStringLiteral("flow/reports/preview_dft.rpt"), QStringLiteral("Number of chains"), true));
    acceptance.append(postDrc);
    acceptance.append(check(QStringLiteral("flow/reports/scan_path.rpt"), QStringLiteral("Scan_path"), true));
    acceptance.append(check(QStringLiteral("flow/reports/dft_signal.rpt"), QStringLiteral("ScanEnable"), true));
    acceptance.append(check(QStringLiteral("flow/mapped_scan/%1_scan.ddc").arg(inputs.projectId)));
    acceptance.append(check(QStringLiteral("flow/mapped_scan/%1_scan.v").arg(inputs.projectId)));
    acceptance.append(check(QStringLiteral("flow/mapped_scan/%1_scan.spf").arg(inputs.projectId)));
    if (!inputs.testModels.isEmpty())
        acceptance.append(check(QStringLiteral("flow/reports/post_dft_drc.rpt"),
                                QStringLiteral("Using DFT model for cell(s):")));
    if (inputs.autofixMode != QLatin1String("off"))
        acceptance.append(check(QStringLiteral("flow/reports/autofix_configuration.rpt"),
                                QStringLiteral("Autofix"), true));
    return acceptance;
}

QString basicDriver(const QVariantMap &project, const ProjectInputs &inputs,
                    const QVariantMap &arguments, const QHash<QString, QString> &stagedScripts,
                    QString *error)
{
    const QString projectId = inputs.projectId;
    const QVariantMap metadata = project.value(QStringLiteral("metadata")).toMap();
    const QVariantMap execution = metadata.value(QStringLiteral("dft_execution")).toMap();
    const QVariantMap synthesis = execution.value(QStringLiteral("synthesis_settings")).toMap();
    QString linkLibrary = QStringLiteral("set link_library [concat * $target_library");
    for (const QString &library : inputs.macroLibraryFiles)
        linkLibrary += QLatin1Char(' ') + safeTclAtom(library, error);
    linkLibrary += QStringLiteral(" dw_foundation.sldb]");
    QStringList commands{
        "file mkdir reports synthesis mapped_scan progress",
        "proc dft_agent_mark_phase {name} { set fp [open [file join progress ${name}.done] w]; puts $fp $name; close $fp }",
        "set search_path [list . " + safeTclAtom(inputs.libraryDir, error) + "]",
        "set target_library [list " + safeTclAtom(inputs.libraryFile, error) + "]",
        linkLibrary,
        "define_design_lib work -path ./WORK",
        "redirect -tee reports/analyze.log {analyze -vcs \"+define+SYNTHESIS +incdir+./rtl -f input/rtl.f\" -format "
            + safeTclAtom(execution.value(QStringLiteral("language"), QStringLiteral("sverilog")).toString(), error) + "}",
        "set dft_agent_analyze_fp [open reports/analyze.log r]",
        "set dft_agent_analyze_text [read $dft_agent_analyze_fp]",
        "close $dft_agent_analyze_fp",
        "if {[regexp -nocase {(^|\\n)[[:space:]]*(error|fatal):|compilation terminated with [1-9][0-9]* errors} $dft_agent_analyze_text]} {",
        "  echo \"Error: HDL analysis failed; see reports/analyze.log for the first compiler diagnostics.\"",
        "  quit",
        "}",
        "elab " + safeTclAtom(inputs.top, error),
        "current_design " + safeTclAtom(inputs.top, error),
        "uniquify -force",
        "link"
    };
    if (!error->isEmpty())
        return {};
    auto scriptSourceCommands = [&](const QString &key) {
        QStringList result;
        for (const QString &source : inputs.synthesisScriptFiles.value(key)) {
            const QString staged = stagedScripts.value(source);
            if (staged.isEmpty()) {
                *error = QStringLiteral("Project synthesis script was not staged: %1").arg(source);
                return QStringList{};
            }
            result.append(QStringLiteral("source %1").arg(safeTclAtom(staged, error)));
        }
        return result;
    };
    const QStringList preScriptCommands = scriptSourceCommands(QStringLiteral("pre_scripts"));
    if (!error->isEmpty())
        return {};
    for (const DftTestModelInput &model : std::as_const(inputs.testModels)) {
        QString command = QStringLiteral("read_test_model -format %1").arg(model.format);
        if (!model.design.isEmpty())
            command += QStringLiteral(" -design %1").arg(model.design);
        command += QLatin1Char(' ') + safeTclAtom(model.file, error);
        if (!error->isEmpty())
            return {};
        commands.append(command);
    }
    commands.append("redirect -tee reports/read_link.rpt {report_design}");
    commands.append("set dft_agent_read_link_fp [open reports/read_link.rpt r]");
    commands.append("set dft_agent_read_link_text [read $dft_agent_read_link_fp]");
    commands.append("close $dft_agent_read_link_fp");
    commands.append("if {[regexp -nocase {unresolved references|black box \\(unknown\\) components|(^|\\n)[[:space:]]*(error|fatal):} $dft_agent_read_link_text]} {");
    commands.append("  echo \"Error: read/link report contains unresolved references, black-box components, or Error/Fatal diagnostics; stopping before constraints and compile.\"");
    commands.append("  quit");
    commands.append("}");
    commands.append("dft_agent_mark_phase read_link");
    const qsizetype constraintStartIndex = commands.size();
    const QVariantList configuredClocks = synthesis.value(QStringLiteral("clocks")).toList();
    if (inputs.useConstraintFile && !inputs.constraintFile.isEmpty())
        commands.append("source input/" + QFileInfo(inputs.constraintFile).fileName());
    else if (!configuredClocks.isEmpty()) {
        for (qsizetype index = 0; index < configuredClocks.size(); ++index) {
            const QVariantMap clock = configuredClocks.at(index).toMap();
            const QString label = QStringLiteral("synthesis_settings.clocks[%1]").arg(index);
            const QString name = tclText(clock.value(QStringLiteral("name")), label + QStringLiteral(".name"), error);
            const QString source = tclText(clock.value(QStringLiteral("source")), label + QStringLiteral(".source"), error);
            double period = 0.0, rise = 0.0, fall = 0.0;
            if (!tclNumber(clock.value(QStringLiteral("period")), true, label + QStringLiteral(".period"), &period, error)
                || !tclNumber(clock.value(QStringLiteral("rise"), 0), false, label + QStringLiteral(".rise"), &rise, error)
                || !tclNumber(clock.value(QStringLiteral("fall"), period / 2), false, label + QStringLiteral(".fall"), &fall, error))
                return {};
            commands.append(QStringLiteral("create_clock -name %1 -period %2 -waveform {%3 %4} [get_ports %5]")
                .arg(name).arg(period, 0, 'g', 12).arg(rise, 0, 'g', 12).arg(fall, 0, 'g', 12).arg(source));
            for (const auto &setting : {
                     std::tuple<QString, QString, QString>{QStringLiteral("setup_uncertainty"), QStringLiteral("set_clock_uncertainty"), QStringLiteral("-setup")},
                     {QStringLiteral("hold_uncertainty"), QStringLiteral("set_clock_uncertainty"), QStringLiteral("-hold")},
                     {QStringLiteral("transition"), QStringLiteral("set_clock_transition"), QString{}},
                     {QStringLiteral("source_latency"), QStringLiteral("set_clock_latency"), QStringLiteral("-source")},
                     {QStringLiteral("network_latency"), QStringLiteral("set_clock_latency"), QString{}}}) {
                double number = 0.0;
                if (!tclNumber(clock.value(std::get<0>(setting), 0), false,
                               label + QLatin1Char('.') + std::get<0>(setting), &number, error))
                    return {};
                if (number > 0) {
                    const QString option = std::get<2>(setting).isEmpty() ? QString{} : std::get<2>(setting) + QLatin1Char(' ');
                    commands.append(std::get<1>(setting) + QLatin1Char(' ') + option
                        + QString::number(number, 'g', 12) + QStringLiteral(" [get_clocks ") + name + QLatin1Char(']'));
                }
            }
        }
    }
    else if (inputs.scan) {
        QStringList clocks{inputs.clock};
        clocks.append(inputs.additionalScanClocks);
        for (qsizetype index = 0; index < clocks.size(); ++index) {
            const QString name = index == 0 ? QStringLiteral("DFT_SMOKE_CLK")
                : QStringLiteral("DFT_AUX_CLK_%1").arg(index);
            commands.append(QStringLiteral("create_clock -name %1 -period %2 [get_ports {%3}]")
                                .arg(name).arg(inputs.clockPeriodNs, 0, 'g', 12).arg(clocks.at(index)));
            commands.append(QStringLiteral("set_ideal_network [get_ports {%1}]").arg(clocks.at(index)));
        }
        QStringList resets{inputs.reset};
        for (const auto &reset : std::as_const(inputs.additionalResets))
            resets.append(reset.first);
        for (const QString &reset : std::as_const(resets))
            commands.append(QStringLiteral("set_false_path -from [get_ports {%1}]").arg(reset));
    }
    commands.append(scriptSourceCommands(QStringLiteral("constraint_files")));
    if (!error->isEmpty())
        return {};
    for (qsizetype index = 0; index < synthesis.value(QStringLiteral("generated_clocks")).toList().size(); ++index) {
        const QVariantMap clock = synthesis.value(QStringLiteral("generated_clocks")).toList().at(index).toMap();
        const QString label = QStringLiteral("synthesis_settings.generated_clocks[%1]").arg(index);
        const QString name = tclText(clock.value(QStringLiteral("name")), label + QStringLiteral(".name"), error);
        const QString target = tclText(clock.value(QStringLiteral("target")), label + QStringLiteral(".target"), error);
        const QString source = tclText(clock.value(QStringLiteral("source")), label + QStringLiteral(".source"), error);
        if (!error->isEmpty())
            return {};
        QString command = QStringLiteral("create_generated_clock -name %1 -source [get_pins %2]").arg(name, source);
        const QString master = clock.value(QStringLiteral("master")).toString().trimmed();
        if (!master.isEmpty())
            command += QStringLiteral(" -master_clock %1").arg(tclText(master, label + QStringLiteral(".master"), error));
        double divideBy = 1.0, multiplyBy = 1.0;
        if (!tclNumber(clock.value(QStringLiteral("divide_by"), 1), true, label + QStringLiteral(".divide_by"), &divideBy, error)
            || !tclNumber(clock.value(QStringLiteral("multiply_by"), 1), true, label + QStringLiteral(".multiply_by"), &multiplyBy, error))
            return {};
        if (divideBy != 1.0)
            command += QStringLiteral(" -divide_by %1").arg(divideBy, 0, 'g', 12);
        if (multiplyBy != 1.0)
            command += QStringLiteral(" -multiply_by %1").arg(multiplyBy, 0, 'g', 12);
        if (clock.value(QStringLiteral("invert")).toBool())
            command += QStringLiteral(" -invert");
        command += QStringLiteral(" [get_pins %1]").arg(target);
        commands.append(command);
    }
    for (qsizetype index = 0; index < synthesis.value(QStringLiteral("io_delays")).toList().size(); ++index) {
        const QVariantMap delay = synthesis.value(QStringLiteral("io_delays")).toList().at(index).toMap();
        const QString label = QStringLiteral("synthesis_settings.io_delays[%1]").arg(index);
        const QString direction = delay.value(QStringLiteral("direction"), QStringLiteral("input")).toString().trimmed();
        if (direction != QLatin1String("input") && direction != QLatin1String("output")) {
            *error = QStringLiteral("%1.direction must be input or output.").arg(label);
            return {};
        }
        const QString ports = tclText(delay.value(QStringLiteral("ports")), label + QStringLiteral(".ports"), error);
        const QString clock = tclText(delay.value(QStringLiteral("clock")), label + QStringLiteral(".clock"), error);
        if (!error->isEmpty())
            return {};
        const QString command = direction == QLatin1String("input") ? QStringLiteral("set_input_delay") : QStringLiteral("set_output_delay");
        for (const QString &limit : {QStringLiteral("max"), QStringLiteral("min")}) {
            double number = 0.0;
            if (!tclNumber(delay.value(limit, 0), false, label + QLatin1Char('.') + limit, &number, error))
                return {};
            commands.append(QStringLiteral("%1 %2 -%3 -clock [get_clocks %4] [get_ports %5]")
                .arg(command).arg(number, 0, 'g', 12).arg(limit).arg(clock).arg(ports));
        }
    }
    for (qsizetype index = 0; index < synthesis.value(QStringLiteral("timing_exceptions")).toList().size(); ++index) {
        const QVariantMap exception = synthesis.value(QStringLiteral("timing_exceptions")).toList().at(index).toMap();
        const QString label = QStringLiteral("synthesis_settings.timing_exceptions[%1]").arg(index);
        const QString type = exception.value(QStringLiteral("type"), QStringLiteral("false_path")).toString().trimmed();
        const QMap<QString, QString> commandNames{{QStringLiteral("false_path"), QStringLiteral("set_false_path")},
            {QStringLiteral("multicycle"), QStringLiteral("set_multicycle_path")},
            {QStringLiteral("max_delay"), QStringLiteral("set_max_delay")},
            {QStringLiteral("min_delay"), QStringLiteral("set_min_delay")}};
        if (!commandNames.contains(type)) {
            *error = QStringLiteral("%1.type is invalid.").arg(label);
            return {};
        }
        QString command = commandNames.value(type);
        if (type != QLatin1String("false_path")) {
            double number = 0.0;
            if (!tclNumber(exception.value(QStringLiteral("value"), 1), false, label + QStringLiteral(".value"), &number, error))
                return {};
            command += QLatin1Char(' ') + QString::number(number, 'g', 12);
        }
        if (type == QLatin1String("multicycle")) {
            const QString check = exception.value(QStringLiteral("check"), QStringLiteral("setup")).toString().trimmed();
            if (check != QLatin1String("setup") && check != QLatin1String("hold")) {
                *error = QStringLiteral("%1.check must be setup or hold.").arg(label);
                return {};
            }
            command += QStringLiteral(" -") + check;
        }
        int selectors = 0;
        for (const QString &option : {QStringLiteral("from"), QStringLiteral("through"), QStringLiteral("to")}) {
            const QString selector = exception.value(option).toString().trimmed();
            if (selector.isEmpty())
                continue;
            const QString collection = tclCollection(selector, label + QLatin1Char('.') + option, error);
            if (!error->isEmpty())
                return {};
            command += QStringLiteral(" -%1 %2").arg(option, collection);
            ++selectors;
        }
        if (selectors == 0) {
            *error = QStringLiteral("%1 requires at least one from/through/to selector.").arg(label);
            return {};
        }
        commands.append(command);
    }
    const QString clockGroups = synthesis.value(QStringLiteral("clock_groups_tcl")).toString().trimmed();
    if (!clockGroups.isEmpty()) {
        const QStringList lines = clockGroups.split(QLatin1Char('\n'));
        for (qsizetype index = 0; index < lines.size(); ++index) {
            if (lines.at(index).trimmed().isEmpty())
                continue;
            const QString command = clockGroupCommand(lines.at(index),
                QStringLiteral("synthesis_settings.clock_groups_tcl[%1]").arg(index), error);
            if (!error->isEmpty())
                return {};
            commands.append(command);
        }
    }
    const QString operatingCondition = synthesis.value(QStringLiteral("operating_condition")).toString().trimmed();
    if (!operatingCondition.isEmpty())
        commands.append(QStringLiteral("set_operating_conditions %1")
            .arg(tclText(operatingCondition, QStringLiteral("synthesis_settings.operating_condition"), error)));
    const QString minimumLibrary = synthesis.value(QStringLiteral("min_library")).toString().trimmed();
    if (!minimumLibrary.isEmpty())
        commands.append(QStringLiteral("set_min_library %1 -min_version %2")
            .arg(safeTclAtom(inputs.libraryFile, error),
                 tclText(minimumLibrary, QStringLiteral("synthesis_settings.min_library"), error)));
    if (!error->isEmpty())
        return {};
    commands.append(inputs.additionalSynthesisCommands);
    for (const auto &setting : {
             qMakePair(QStringLiteral("max_transition"), QStringLiteral("set_max_transition")),
             qMakePair(QStringLiteral("max_fanout"), QStringLiteral("set_max_fanout")),
             qMakePair(QStringLiteral("max_capacitance"), QStringLiteral("set_max_capacitance"))}) {
        const QVariant value = synthesis.value(setting.first);
        if (!value.isValid() || value.isNull() || value.toString().trimmed().isEmpty())
            continue;
        double number = 0.0;
        if (!tclNumber(value, false, QStringLiteral("synthesis_settings.%1").arg(setting.first), &number, error))
            return {};
        commands.append(QStringLiteral("%1 %2 [current_design]")
            .arg(setting.second).arg(number, 0, 'g', 12));
    }
    const QString drivingCell = synthesis.value(QStringLiteral("driving_cell")).toString().trimmed();
    if (!drivingCell.isEmpty()) {
        const QString safeDrivingCell = tclText(drivingCell, QStringLiteral("synthesis_settings.driving_cell"), error);
        if (!error->isEmpty())
            return {};
        commands.append(QStringLiteral("set_driving_cell -lib_cell %1 [all_inputs]").arg(safeDrivingCell));
    }
    const QVariant outputLoad = synthesis.value(QStringLiteral("output_load"));
    if (outputLoad.isValid() && !outputLoad.isNull() && !outputLoad.toString().trimmed().isEmpty()) {
        double number = 0.0;
        if (!tclNumber(outputLoad, false, QStringLiteral("synthesis_settings.output_load"), &number, error))
            return {};
        commands.append(QStringLiteral("set_load %1 [all_outputs]").arg(number, 0, 'g', 12));
    }
    const QStringList constraintCommands = commands.mid(constraintStartIndex);
    commands.erase(commands.begin() + constraintStartIndex, commands.end());
    commands.append(QStringLiteral("set_host_options -max_cores %1").arg(synthesis.value(QStringLiteral("max_cores"), 1).toInt()));
    const QString hostOptionsCommand = commands.takeLast();
    commands.append(hostOptionsCommand);
    commands.append(preScriptCommands);
    commands.append(constraintCommands);
    if (inputs.scan) {
        QStringList clocks{inputs.clock};
        clocks.append(inputs.additionalScanClocks);
        for (const QString &clock : std::as_const(clocks))
            commands.append(QStringLiteral("set_dft_signal -view existing_dft -type ScanClock -timing {45 55} -port {%1}").arg(clock));
        if (inputs.resetKind == QLatin1String("asynchronous"))
            commands.append(QStringLiteral("set_dft_signal -view existing_dft -type Reset -active_state %1 -port {%2}")
                                .arg(inputs.resetActiveState).arg(inputs.reset));
        for (const auto &reset : std::as_const(inputs.additionalResets))
            commands.append(QStringLiteral("set_dft_signal -view existing_dft -type Reset -active_state %1 -port {%2}")
                                .arg(reset.second).arg(reset.first));
        commands.append(QStringLiteral("set_dft_insertion_configuration -preserve_design_name true"));
        commands.append(QStringLiteral("set_scan_configuration -chain_count %1 -max_length %2")
                            .arg(inputs.chainCount).arg(inputs.maxChainLength));
        commands.append(drcAutofixCommands(inputs));
        if (inputs.resetKind == QLatin1String("synchronous"))
            commands.append(QStringLiteral("set_dft_signal -view spec -type TestData -port {%1}").arg(inputs.reset));
    }
    commands.append(QStringLiteral("dft_agent_mark_phase compile_started"));
    if (inputs.compileStrategy == QLatin1String("two_stage_mapping"))
        commands.append(QStringLiteral("compile -no_map"));
    commands.append(synthesisCompileCommand(synthesis, execution, inputs));
    commands.append(scriptSourceCommands(QStringLiteral("post_scripts")));
    if (!error->isEmpty())
        return {};
    const QStringList reportKinds = inputs.synthesisReports.isEmpty()
        ? QStringList{QStringLiteral("qor"), QStringLiteral("timing"), QStringLiteral("area")}
        : inputs.synthesisReports;
    if (reportKinds.contains(QStringLiteral("qor")))
        commands.append("redirect -tee reports/synthesis_qor.rpt {report_qor}");
    if (reportKinds.contains(QStringLiteral("area")))
        commands.append("redirect -tee reports/synthesis_area.rpt {report_area}");
    if (reportKinds.contains(QStringLiteral("timing")))
        commands.append("redirect -tee reports/synthesis_timing.rpt {report_timing -max_paths 5}");
    if (reportKinds.contains(QStringLiteral("power")))
        commands.append("redirect -tee reports/synthesis_power.rpt {report_power}");
    if (reportKinds.contains(QStringLiteral("constraints")))
        commands.append("redirect -tee reports/synthesis_constraints.rpt {report_constraint -all_violators}");
    if (reportKinds.contains(QStringLiteral("resources")))
        commands.append("redirect -tee reports/synthesis_resources.rpt {report_resources}");
    commands.append(QStringLiteral("write -format ddc -hierarchy -output synthesis/%1_synth.ddc").arg(projectId));
    commands.append(QStringLiteral("write -format verilog -hierarchy -output synthesis/%1_synth.v").arg(projectId));
    commands.append("dft_agent_mark_phase synthesis");
    if (inputs.scan) {
        commands.append("create_test_protocol");
        commands.append("dft_agent_mark_phase scan_config");
        const QVariantMap autofix = execution.value(QStringLiteral("drc_autofix")).toMap();
        if (inputs.autofixMode != QLatin1String("off"))
            commands.append("redirect -tee reports/autofix_configuration.rpt {report_autofix_configuration}");
        commands.append("redirect -tee reports/pre_dft_drc.rpt {dft_drc -pre_dft}");
        commands.append("dft_agent_mark_phase pre_dft_drc");
        commands.append("redirect -tee reports/preview_dft.rpt {preview_dft -show scan_summary}");
        commands.append("insert_dft");
        commands.append("dft_agent_mark_phase scan_inserted");
        commands.append("redirect -tee reports/post_dft_drc.rpt {dft_drc}");
        commands.append("redirect -tee reports/scan_path.rpt {report_scan_path -view existing_dft}");
        commands.append("redirect -tee reports/dft_signal.rpt {report_dft_signal -view existing_dft}");
        commands.append("change_names -rules verilog -hierarchy");
        commands.append(QStringLiteral("write -format ddc -hierarchy -output mapped_scan/%1_scan.ddc").arg(projectId));
        commands.append(QStringLiteral("write -format verilog -hierarchy -output mapped_scan/%1_scan.v").arg(projectId));
        commands.append(QStringLiteral("write_test_protocol -output mapped_scan/%1_scan.spf").arg(projectId));
        commands.append("dft_agent_mark_phase scan");
    }
    commands.append("quit");
    return commands.join(QLatin1Char('\n')) + QLatin1Char('\n');
}

QString testmaxAtpgDriver(const ProjectInputs &inputs, QString *error)
{
    QStringList commands{
        "set_command noabort",
        "set_messages -log logs/agent_atpg.log -replace -level expert"
    };
    for (const QString &model : inputs.atpgCellModelFiles)
        commands.append(QStringLiteral("read_netlist %1").arg(safeTclAtom(model, error)));
    commands.append(QStringLiteral("read_netlist mapped_scan/%1_scan.v").arg(inputs.projectId));
    commands.append("set_rule b5 warning");
    commands.append("run_build");
    commands.append("set_drc -nodisturb");
    commands.append(QStringLiteral("run_drc mapped_scan/%1_scan.spf").arg(inputs.projectId));
    commands.append("add_faults -all");
    commands.append(QStringLiteral("set_atpg -abort_limit %1").arg(inputs.atpgAbortLimit));
    commands.append("run_atpg -auto");
    if (inputs.atpgDiagnosticMode == QLatin1String("fault_classes")) {
        commands.append("report_faults -class AU -summary");
        commands.append("report_faults -class UD -summary");
        commands.append("report_faults -class ND -summary");
    }
    commands.append("quit -force");
    if (!error->isEmpty())
        return {};
    return commands.join(QLatin1Char('\n')) + QLatin1Char('\n');
}

QList<TessentScanChain> tessentScanChains(const QString &spfPath, QString *error)
{
    QFile spf(spfPath);
    if (!spf.open(QIODevice::ReadOnly | QIODevice::Text)) {
        *error = QStringLiteral("Tessent ATPG cannot read the staged scan protocol: %1").arg(spfPath);
        return {};
    }
    const QString text = QString::fromUtf8(spf.readAll());
    const QRegularExpression chainExpression(
        QStringLiteral("ScanChain\\s+\"([^\"]+)\"\\s*\\{([^{}]*)\\}"),
        QRegularExpression::DotMatchesEverythingOption);
    const QRegularExpression fieldExpression(
        QStringLiteral("\\b(ScanIn|ScanOut|ScanEnable|ScanMasterClock)\\s+\"([^\"]+)\"\\s*;"));
    const QRegularExpression lengthExpression(QStringLiteral("\\bScanLength\\s+(\\d+)\\s*;"));
    QList<TessentScanChain> chains;
    auto chainMatch = chainExpression.globalMatch(text);
    while (chainMatch.hasNext()) {
        const auto match = chainMatch.next();
        const QString body = match.captured(2);
        QHash<QString, QString> fields;
        auto fieldMatch = fieldExpression.globalMatch(body);
        while (fieldMatch.hasNext()) {
            const auto field = fieldMatch.next();
            fields.insert(field.captured(1), field.captured(2));
        }
        const auto length = lengthExpression.match(body);
        const QStringList required{QStringLiteral("ScanIn"), QStringLiteral("ScanOut"),
                                   QStringLiteral("ScanEnable"), QStringLiteral("ScanMasterClock")};
        QStringList missing;
        for (const QString &key : required) {
            if (!fields.contains(key))
                missing.append(key);
        }
        if (!length.hasMatch())
            missing.prepend(QStringLiteral("ScanLength"));
        if (!missing.isEmpty()) {
            *error = QStringLiteral("Tessent scan chain %1 lacks required SPF fields: %2")
                .arg(match.captured(1), missing.join(QStringLiteral(", ")));
            return {};
        }
        bool lengthOk = false;
        const int scanLength = length.captured(1).toInt(&lengthOk);
        if (!lengthOk || scanLength < 1) {
            *error = QStringLiteral("Tessent scan chain %1 has an invalid ScanLength.").arg(match.captured(1));
            return {};
        }
        chains.append({match.captured(1), fields.value(QStringLiteral("ScanIn")),
                       fields.value(QStringLiteral("ScanOut")), fields.value(QStringLiteral("ScanEnable")),
                       fields.value(QStringLiteral("ScanMasterClock")), scanLength});
    }
    if (chains.isEmpty())
        *error = QStringLiteral("Tessent ATPG found no ScanChain declarations in %1.").arg(spfPath);
    return chains;
}

QString tessentSafeText(const QString &value, const QString &label, QString *error)
{
    if (value.trimmed().isEmpty() || value.contains(QRegularExpression(QStringLiteral("[{}$;\\\\\\n\\r]")))) {
        *error = QStringLiteral("%1 contains text unsafe for generated Tessent syntax.").arg(label);
        return {};
    }
    return value;
}

QString tessentTestProcedure(const ProjectInputs &inputs, const QList<TessentScanChain> &chains,
                             QString *error)
{
    QStringList clocks;
    QStringList scanEnables;
    for (const TessentScanChain &chain : chains) {
        const QString clock = tessentSafeText(chain.clock, QStringLiteral("ScanMasterClock"), error);
        const QString enable = tessentSafeText(chain.scanEnable, QStringLiteral("ScanEnable"), error);
        if (!error->isEmpty())
            return {};
        if (!clocks.contains(clock)) clocks.append(clock);
        if (!scanEnables.contains(enable)) scanEnables.append(enable);
    }
    QStringList resetForces;
    resetForces.append(QStringLiteral("        force %1 %2 ;").arg(inputs.reset).arg(1 - inputs.resetActiveState));
    for (const auto &reset : std::as_const(inputs.additionalResets))
        resetForces.append(QStringLiteral("        force %1 %2 ;").arg(reset.first).arg(1 - reset.second));
    for (const QString &clock : std::as_const(clocks))
        resetForces.prepend(QStringLiteral("        force %1 0 ;").arg(clock));
    if (!inputs.autofixTestModePort.isEmpty())
        resetForces.append(QStringLiteral("        force %1 1 ;").arg(inputs.autofixTestModePort));
    for (const QString &enable : std::as_const(scanEnables))
        resetForces.append(QStringLiteral("        force %1 1 ;").arg(enable));
    QStringList commands{
        "set time scale 1.000000 ns ;", "timeplate dft_agent_tp =", "    force_pi 0 ;", "    measure_po 10 ;"};
    for (const QString &clock : std::as_const(clocks))
        commands.append(QStringLiteral("    pulse %1 40 10 ;").arg(clock));
    commands.append("    period 100 ;\nend;\n");
    commands.append("procedure shift =\n    scan_group dft_agent_scan_group ;\n    timeplate dft_agent_tp ;\n    cycle =\n        force_sci ;\n        measure_sco ;");
    for (const QString &clock : std::as_const(clocks))
        commands.append(QStringLiteral("        pulse %1 ;").arg(clock));
    commands.append("    end;\nend;\n");
    commands.append("procedure load_unload =\n    scan_group dft_agent_scan_group ;\n    timeplate dft_agent_tp ;\n    cycle =");
    commands.append(resetForces);
    commands.append("    end ;\n    apply shift 2 ;\nend ;\n");
    return commands.join(QLatin1Char('\n'));
}

QString tessentAtpgDriver(const ProjectInputs &inputs, const QString &flow, QString *error)
{
    const QString stem = inputs.projectId + QStringLiteral("_scan");
    const QString spf = QDir(flow).filePath(QStringLiteral("mapped_scan/%1.spf").arg(stem));
    const QList<TessentScanChain> chains = tessentScanChains(spf, error);
    if (!error->isEmpty())
        return {};
    const QString procedurePath = QStringLiteral("agent_tessent_scan.testproc");
    const QString procedure = tessentTestProcedure(inputs, chains, error);
    if (!error->isEmpty())
        return {};
    QFile procedureFile(QDir(flow).filePath(procedurePath));
    if (!procedureFile.open(QIODevice::WriteOnly | QIODevice::Text)
        || procedureFile.write(procedure.toUtf8()) != procedure.toUtf8().size()) {
        *error = QStringLiteral("Unable to write Tessent test procedure.");
        return {};
    }
    procedureFile.close();

    QStringList commands{"set_context patterns -scan"};
    for (const QString &library : std::as_const(inputs.tessentCellLibraryFiles))
        commands.append(QStringLiteral("read_cell_library %1").arg(safeTclAtom(library, error)));
    commands.append(QStringLiteral("read_verilog mapped_scan/%1.v").arg(stem));
    commands.append(QStringLiteral("set_current_design %1").arg(tclText(inputs.top, QStringLiteral("top module"), error)));
    commands.append("add_black_boxes -auto");
    QStringList clocks;
    for (const TessentScanChain &chain : chains) {
        const QString clock = tessentSafeText(chain.clock, QStringLiteral("ScanMasterClock"), error);
        if (!clocks.contains(clock)) clocks.append(clock);
    }
    for (const QString &clock : std::as_const(clocks))
        commands.append(QStringLiteral("add_clocks 0 %1").arg(tclText(clock, QStringLiteral("Tessent scan clock"), error)));
    commands.append(QStringLiteral("add_input_constraints %1 -C%2")
                        .arg(tclText(inputs.reset, QStringLiteral("Tessent reset input"), error))
                        .arg(1 - inputs.resetActiveState));
    for (const auto &reset : std::as_const(inputs.additionalResets))
        commands.append(QStringLiteral("add_input_constraints %1 -C%2")
                            .arg(tclText(reset.first, QStringLiteral("Tessent reset input"), error))
                            .arg(1 - reset.second));
    if (!inputs.autofixTestModePort.isEmpty())
        commands.append(QStringLiteral("add_input_constraints %1 -C1")
                            .arg(tclText(inputs.autofixTestModePort, QStringLiteral("Tessent TestMode input"), error)));
    commands.append(QStringLiteral("add_scan_groups dft_agent_scan_group %1").arg(procedurePath));
    for (const TessentScanChain &chain : chains) {
        const QString name = tessentSafeText(QStringLiteral("chain_") + chain.name, QStringLiteral("Tessent scan chain name"), error);
        const QString scanIn = tessentSafeText(chain.scanIn, QStringLiteral("Tessent scan input"), error);
        const QString scanOut = tessentSafeText(chain.scanOut, QStringLiteral("Tessent scan output"), error);
        commands.append(QStringLiteral("add_scan_chains %1 dft_agent_scan_group %2 %3")
                            .arg(tclText(name, QStringLiteral("Tessent scan chain name"), error),
                                 tclText(scanIn, QStringLiteral("Tessent scan input"), error),
                                 tclText(scanOut, QStringLiteral("Tessent scan output"), error)));
    }
    commands.append("set_system_mode analysis");
    commands.append("report_scan_chains > reports/tessent_scan_chains.rpt");
    commands.append("set_fault_type Stuck");
    commands.append(QStringLiteral("set_abort_limit %1").arg(inputs.atpgAbortLimit));
    commands.append("create_patterns -coverage_effort high");
    commands.append("report_statistics > reports/tessent_atpg_statistics.rpt");
    commands.append(QStringLiteral("write_patterns mapped_scan/%1_tessent_stuck_at.stil -STIL1999 -Replace").arg(stem));
    commands.append("exit");
    if (!error->isEmpty())
        return {};
    return commands.join(QLatin1Char('\n')) + QStringLiteral("\n");
}

QVariantMap executeProcess(const QString &program, const QStringList &arguments,
                           const QString &workingDirectory, const QString &outputPath,
                           int timeoutSeconds,
                           const std::shared_ptr<std::atomic_bool> &cancelToken = {})
{
    NativeProcessOutputService process;
    QEventLoop loop;
    QVariantMap result;
    process.setFinishedCallback([&](const QVariantMap &finished) {
        result = finished;
        loop.quit();
    });
    const int timeoutMs = qBound(1000, timeoutSeconds * 1000, 86400000);
    if (!process.start(program, arguments, workingDirectory, outputPath, timeoutMs, 8 * 1024 * 1024))
        return {{QStringLiteral("started"), false},
                {QStringLiteral("error"), process.errorString().isEmpty()
                     ? QStringLiteral("Unable to start %1.").arg(program) : process.errorString()}};
    QTimer guard;
    guard.setSingleShot(true);
    QObject::connect(&guard, &QTimer::timeout, &loop, [&] { process.cancel(); });
    guard.start(timeoutMs + 5000);
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
    if (result.isEmpty())
        return {{QStringLiteral("started"), true},
                {QStringLiteral("error"), QStringLiteral("Process did not return a final result.")}};
    result.insert(QStringLiteral("started"), true);
    return result;
}

bool writeJsonAtomically(const QString &path, const QVariantMap &value, QString *error)
{
    QSaveFile output(path);
    if (!output.open(QIODevice::WriteOnly)) {
        *error = output.errorString();
        return false;
    }
    output.write(QJsonDocument(QJsonObject::fromVariantMap(value)).toJson(QJsonDocument::Indented));
    output.write("\n");
    if (!output.commit()) {
        *error = output.errorString();
        return false;
    }
    return true;
}

QVariantMap runMbistSimulation(const ProjectInputs &inputs, const QString &flow,
                              const std::shared_ptr<std::atomic_bool> &cancelToken)
{
    const QString iverilog = QStandardPaths::findExecutable(QStringLiteral("iverilog"));
    const QString vvp = QStandardPaths::findExecutable(QStringLiteral("vvp"));
    const QString simulationRoot = QDir(flow).filePath(QStringLiteral("mbist_sim"));
    const QString artifact = QDir(flow).filePath(QStringLiteral("mapped_sim/%1_mbist.vvp").arg(inputs.projectId));
    const QString report = QDir(flow).filePath(QStringLiteral("reports/agent_mbist_simulation.log"));
    const QString compileStdout = QDir(flow).filePath(QStringLiteral("logs/agent_mbist_compile.stdout.log"));
    const QString runStdout = QDir(flow).filePath(QStringLiteral("logs/agent_mbist_run.stdout.log"));
    const int timeout = qMin(3600, inputs.mbistTimeoutSeconds * inputs.mbistTimeoutMultiplier);
    QStringList compileArguments{QStringLiteral("-g2012")};
    if (inputs.mbistDiagnosticMode == QLatin1String("verbose"))
        compileArguments << QStringLiteral("-Wall") << QStringLiteral("-Wimplicit");
    if (!inputs.mbistTestTop.isEmpty())
        compileArguments << QStringLiteral("-s") << inputs.mbistTestTop;
    QStringList includeDirs = inputs.mbistIncludeDirs;
    if (inputs.mbistIncludeMode == QLatin1String("compile_parents")) {
        for (const QString &relative : std::as_const(inputs.mbistCompileFiles)) {
            const QString parent = QFileInfo(relative).path();
            if (parent != QLatin1String(".") && !includeDirs.contains(parent))
                includeDirs.append(parent);
        }
    }
    for (const QString &relative : std::as_const(includeDirs))
        compileArguments << QStringLiteral("-I") << QDir(simulationRoot).filePath(relative);
    compileArguments << QStringLiteral("-o") << artifact;
    for (const QString &relative : std::as_const(inputs.mbistCompileFiles))
        compileArguments << QDir(simulationRoot).filePath(relative);

    const QVariantMap compile = executeProcess(iverilog, compileArguments, flow, compileStdout, timeout, cancelToken);
    QVariantMap run;
    QStringList runArguments;
    if (inputs.mbistDiagnosticMode == QLatin1String("verbose"))
        runArguments.append(QStringLiteral("-v"));
    runArguments.append(artifact);
    const bool compileClean = compile.value(QStringLiteral("started")).toBool()
        && compile.value(QStringLiteral("returncode")).toInt() == 0
        && !compile.value(QStringLiteral("timed_out")).toBool()
        && !compile.value(QStringLiteral("cancelled")).toBool();
    if (compileClean)
        run = executeProcess(vvp, runArguments, flow, runStdout, timeout, cancelToken);

    const QString compileOutput = compile.value(QStringLiteral("stdout")).toString();
    const QString runOutput = run.value(QStringLiteral("stdout")).toString();
    const QString combined = QStringLiteral("Icarus compile command: %1\n%2\nIcarus run command: %3\n%4")
        .arg(([&] { QStringList values{iverilog}; values.append(compileArguments); return values.join(QLatin1Char(' ')); }()),
             compileOutput,
             ([&] { QStringList values{vvp}; values.append(runArguments); return values.join(QLatin1Char(' ')); }()),
             compileClean ? runOutput : QStringLiteral("MBIST simulation was not started because Icarus compilation failed.\n"));
    QSaveFile reportFile(report);
    if (!reportFile.open(QIODevice::WriteOnly | QIODevice::Text)
        || reportFile.write(combined.toUtf8()) != combined.toUtf8().size()
        || !reportFile.commit()) {
        return {{QStringLiteral("completed_cleanly"), false},
                {QStringLiteral("errors"), QStringList{QStringLiteral("Unable to persist MBIST simulation log.")}},
                {QStringLiteral("log"), report}};
    }

    QStringList errors;
    const auto appendProcessErrors = [&](const QVariantMap &process, const QString &name) {
        if (!process.value(QStringLiteral("started")).toBool()) {
            errors.append(process.value(QStringLiteral("error")).toString());
        } else if (process.value(QStringLiteral("timed_out")).toBool()) {
            errors.append(QStringLiteral("%1 timed out after %2 seconds.").arg(name).arg(timeout));
        } else if (process.value(QStringLiteral("cancelled")).toBool()) {
            errors.append(QStringLiteral("%1 was interrupted.").arg(name));
        } else if (process.value(QStringLiteral("returncode")).toInt() != 0) {
            errors.append(QStringLiteral("%1 returned %2.").arg(name).arg(process.value(QStringLiteral("returncode")).toInt()));
        }
    };
    appendProcessErrors(compile, QStringLiteral("Icarus compilation"));
    if (compileClean)
        appendProcessErrors(run, QStringLiteral("MBIST simulation"));
    for (const QString &marker : std::as_const(inputs.mbistExpectedOutput)) {
        if (!combined.contains(marker))
            errors.append(QStringLiteral("MBIST simulation output is missing expected text: %1").arg(marker));
    }
    QStringList observedPassMarkers;
    for (const QString &marker : std::as_const(inputs.mbistRequiredPassMarkers)) {
        if (combined.contains(marker))
            observedPassMarkers.append(marker);
    }
    QStringList observedForbidden;
    for (const QString &marker : std::as_const(inputs.mbistForbiddenOutput)) {
        if (combined.contains(marker)) {
            observedForbidden.append(marker);
            errors.append(QStringLiteral("MBIST simulation output contains forbidden text: %1").arg(marker));
        }
    }
    const double coverage = inputs.mbistRequiredPassMarkers.isEmpty() ? 0.0
        : 100.0 * observedPassMarkers.size() / inputs.mbistRequiredPassMarkers.size();
    const QVariantMap summary{{QStringLiteral("test_top"), inputs.mbistTestTop},
        {QStringLiteral("expected_output"), stringListVariant(inputs.mbistExpectedOutput)},
        {QStringLiteral("required_pass_markers"), stringListVariant(inputs.mbistRequiredPassMarkers)},
        {QStringLiteral("observed_pass_markers"), stringListVariant(observedPassMarkers)},
        {QStringLiteral("missing_pass_markers"), [&] {
            QStringList missing;
            for (const QString &marker : std::as_const(inputs.mbistRequiredPassMarkers))
                if (!observedPassMarkers.contains(marker)) missing.append(marker);
            return stringListVariant(missing);
        }()},
        {QStringLiteral("forbidden_output"), stringListVariant(inputs.mbistForbiddenOutput)},
        {QStringLiteral("observed_forbidden_output"), stringListVariant(observedForbidden)},
        {QStringLiteral("declared_case_coverage_percent"), qRound(coverage * 100.0) / 100.0}};
    const QString summaryFile = QDir(flow).filePath(QStringLiteral("reports/mbist_summary.json"));
    QString summaryError;
    if (!writeJsonAtomically(summaryFile, summary, &summaryError))
        errors.append(QStringLiteral("Unable to persist MBIST summary: %1").arg(summaryError));
    const bool runClean = compileClean && run.value(QStringLiteral("started")).toBool()
        && run.value(QStringLiteral("returncode")).toInt() == 0
        && !run.value(QStringLiteral("timed_out")).toBool()
        && !run.value(QStringLiteral("cancelled")).toBool();
    return {{QStringLiteral("compile_command"), QStringList{iverilog} + compileArguments},
        {QStringLiteral("include_mode"), inputs.mbistIncludeMode},
        {QStringLiteral("diagnostic_mode"), inputs.mbistDiagnosticMode},
        {QStringLiteral("include_dirs"), stringListVariant(includeDirs)},
        {QStringLiteral("run_command"), QStringList{vvp} + runArguments},
        {QStringLiteral("log"), report}, {QStringLiteral("compiled"), compileClean},
        {QStringLiteral("returncode"), run.value(QStringLiteral("returncode"))},
        {QStringLiteral("timed_out"), compile.value(QStringLiteral("timed_out")).toBool()
            || run.value(QStringLiteral("timed_out")).toBool()},
        {QStringLiteral("cancelled"), compile.value(QStringLiteral("cancelled")).toBool()
            || run.value(QStringLiteral("cancelled")).toBool()},
        {QStringLiteral("errors"), stringListVariant(errors)}, {QStringLiteral("summary"), summary},
        {QStringLiteral("summary_file"), summaryFile},
        {QStringLiteral("completed_cleanly"), runClean && errors.isEmpty()}};
}

QVariantMap stageConfigured(const QVariantMap &project, const QString &workspaceRoot,
                            const QVariantMap &arguments, const QString &agentRoot)
{
    const QVariantMap metadata = project.value(QStringLiteral("metadata")).toMap();
    const QVariantMap execution = metadata.value(QStringLiteral("dft_execution")).toMap();
    ProjectInputs inputs;
    parseProject(project, arguments, agentRoot, &inputs);
    if (!inputs.unsupported.isEmpty() || !inputs.configurationErrors.isEmpty() || !inputs.missingFiles.isEmpty())
        return failure(QStringLiteral("Configured project cannot be staged by the native flow slice; see readiness diagnostics."));
    const QString root = canonicalDirectory(inputs.projectRoot, agentRoot);
    const QString stagingRoot = canonicalDirectory(workspaceRoot.isEmpty() ? inputs.workspaceRoot : workspaceRoot, agentRoot);
    if (stagingRoot.isEmpty())
        return failure(QStringLiteral("Configured workspace path is invalid."));
    if (within(stagingRoot, root) || within(root, stagingRoot)
        || within(stagingRoot, inputs.rtlRoot) || within(inputs.rtlRoot, stagingRoot))
        return failure(QStringLiteral("Isolated workspace may not overlap the project or RTL source tree."));
    const QString runId = QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMdd'T'HHmmss'Z'"))
        + QLatin1Char('_') + inputs.projectId + QLatin1Char('_') + QUuid::createUuid().toString(QUuid::WithoutBraces).left(8);
    QString workspace;
    if (inputs.workspaceSuffixEnabled) {
        workspace = QDir(stagingRoot).filePath(runId);
    } else {
        QString prepareError;
        if (!prepareFixedWorkspace(stagingRoot, root, inputs.projectId, runId, &workspace, &prepareError))
            return failure(prepareError);
    }
    const QString flow = QDir(workspace).filePath(QStringLiteral("flow"));
    const QString rtl = QDir(flow).filePath(QStringLiteral("rtl"));
    const QString input = QDir(flow).filePath(QStringLiteral("input"));
    const QString mbistSimulationRoot = QDir(flow).filePath(QStringLiteral("mbist_sim"));
    for (const QString &path : {rtl, input, QDir(flow).filePath("logs"), QDir(flow).filePath("reports"),
                                 QDir(flow).filePath("synthesis"), QDir(flow).filePath("mapped_scan"),
                                 QDir(flow).filePath("mapped_sim"), QDir(flow).filePath("progress"),
                                 mbistSimulationRoot}) {
        if (!QDir().mkpath(path))
            return failure(QStringLiteral("Unable to create isolated flow directory: %1").arg(path));
    }
    QVariantList stagedSources;
    QStringList generatedFileList;
    QStringList includeDirectories{QStringLiteral(".")};
    includeDirectories.append(inputs.includeDirs);
    for (const QString &relative : inputs.supportFiles) {
        const QString parent = QFileInfo(relative).path();
        if (parent != QLatin1String("."))
            includeDirectories.append(parent);
    }
    includeDirectories.removeDuplicates();
    QStringList filesToStage = inputs.sourceFiles + inputs.supportFiles;
    QSet<QString> stagedFileSet(filesToStage.cbegin(), filesToStage.cend());
    const QSet<QString> includeSuffixes{QStringLiteral("v"), QStringLiteral("vg"), QStringLiteral("sv"),
                                        QStringLiteral("vh"), QStringLiteral("svh"), QStringLiteral("h"),
                                        QStringLiteral("inc")};
    for (const QString &include : inputs.includeDirs) {
        const QString includeRoot = QDir(inputs.rtlRoot).filePath(include);
        QDirIterator iterator(includeRoot, QDir::Files | QDir::NoSymLinks, QDirIterator::Subdirectories);
        while (iterator.hasNext()) {
            const QString path = iterator.next();
            if (!includeSuffixes.contains(QFileInfo(path).suffix().toLower()))
                continue;
            const QString canonical = QFileInfo(path).canonicalFilePath();
            if (canonical.isEmpty() || !within(canonical, inputs.rtlRoot))
                continue;
            const QString relative = QDir(inputs.rtlRoot).relativeFilePath(canonical);
            if (!stagedFileSet.contains(relative)) {
                stagedFileSet.insert(relative);
                filesToStage.append(relative);
            }
            if (filesToStage.size() > 1024)
                return failure(QStringLiteral("Native RTL staging exceeds the 1024-file source/include limit."));
        }
    }
    for (const QString &include : includeDirectories)
        generatedFileList.append(include == QLatin1String(".")
            ? QStringLiteral("+incdir+./rtl") : QStringLiteral("+incdir+./rtl/%1").arg(include));
    if (!inputs.sourceDefines.isEmpty())
        generatedFileList.append(QStringLiteral("+define+") + inputs.sourceDefines.join(QLatin1Char('+')));
    QHash<QString, SourceReplacementInput> replacementsByTarget;
    for (const SourceReplacementInput &replacement : std::as_const(inputs.sourceReplacements))
        replacementsByTarget.insert(replacement.target, replacement);
    QVariantList stagedReplacements;
    for (const QString &relative : std::as_const(filesToStage)) {
        const QString source = QDir(inputs.rtlRoot).filePath(relative);
        const QString destination = QDir(rtl).filePath(relative);
        QFile original(source);
        if (!original.open(QIODevice::ReadOnly))
            return failure(QStringLiteral("Unable to read original RTL source: %1").arg(source));
        const QByteArray originalBytes = original.readAll();
        const QString originalHash = QString::fromLatin1(QCryptographicHash::hash(originalBytes, QCryptographicHash::Sha256).toHex());
        QString stagedFrom = source;
        QVariantMap replacementRecord;
        const auto replacement = replacementsByTarget.constFind(relative);
        if (replacement != replacementsByTarget.cend()) {
            QFile adapter(replacement->file);
            if (!adapter.open(QIODevice::ReadOnly))
                return failure(QStringLiteral("Unable to read hash-pinned source replacement: %1").arg(replacement->file));
            const QByteArray adapterBytes = adapter.readAll();
            const QString adapterHash = QString::fromLatin1(QCryptographicHash::hash(adapterBytes, QCryptographicHash::Sha256).toHex());
            if (adapterHash != replacement->sha256)
                return failure(QStringLiteral("Source replacement changed after readiness validation: %1").arg(replacement->file));
            stagedFrom = replacement->file;
            replacementRecord = {
                {QStringLiteral("target"), relative},
                {QStringLiteral("reason"), replacement->reason},
                {QStringLiteral("original"), QVariantMap{{QStringLiteral("file"), source},
                                                         {QStringLiteral("sha256"), originalHash}}},
                {QStringLiteral("replacement"), QVariantMap{{QStringLiteral("file"), replacement->file},
                                                            {QStringLiteral("sha256"), adapterHash}}},
                {QStringLiteral("staged"), QVariantMap{{QStringLiteral("file"), destination},
                                                       {QStringLiteral("sha256"), adapterHash}}},
            };
        }
        if (!QDir().mkpath(QFileInfo(destination).absolutePath()) || !QFile::copy(stagedFrom, destination))
            return failure(QStringLiteral("Unable to stage declared RTL source: %1").arg(source));
        if (!replacementRecord.isEmpty()) {
            QFile stagedFile(destination);
            if (!stagedFile.open(QIODevice::ReadOnly))
                return failure(QStringLiteral("Unable to verify staged source replacement: %1").arg(destination));
            const QString stagedHash = QString::fromLatin1(QCryptographicHash::hash(
                stagedFile.readAll(), QCryptographicHash::Sha256).toHex());
            if (stagedHash != replacement->sha256)
                return failure(QStringLiteral("Staged source replacement hash changed during copy: %1").arg(destination));
            stagedReplacements.append(replacementRecord);
        }
        if (inputs.sourceAnnotationMode == QLatin1String("strip_unsupported_state_encoding_hints")
            && inputs.sourceFiles.contains(relative)) {
            QFile stagedFile(destination);
            if (!stagedFile.open(QIODevice::ReadOnly))
                return failure(QStringLiteral("Unable to read staged RTL for annotation compatibility: %1").arg(destination));
            const QByteArray before = stagedFile.readAll();
            QString text = QString::fromUtf8(before);
            static const QRegularExpression stateHint(
                QStringLiteral(R"((?://\s*(?:synopsys|synthesis)\s+enum_state\b[^\r\n]*))"),
                QRegularExpression::CaseInsensitiveOption);
            QVariantList lineNumbers;
            auto match = stateHint.globalMatch(text);
            while (match.hasNext()) {
                const auto found = match.next();
                lineNumbers.append(text.left(found.capturedStart()).count(QLatin1Char('\n')) + 1);
            }
            if (!lineNumbers.isEmpty()) {
                const QByteArray after = text.replace(stateHint, QString{}).toUtf8();
                stagedFile.close();
                QSaveFile normalized(destination);
                if (!normalized.open(QIODevice::WriteOnly) || normalized.write(after) != after.size()
                    || !normalized.commit())
                    return failure(QStringLiteral("Unable to write staged RTL annotation adapter: %1").arg(destination));
                const QString stagedHash = QString::fromLatin1(QCryptographicHash::hash(after, QCryptographicHash::Sha256).toHex());
                inputs.sourceAnnotationTransforms.append(QVariantMap{
                    {QStringLiteral("source"), source},
                    {QStringLiteral("staged"), destination},
                    {QStringLiteral("mode"), inputs.sourceAnnotationMode},
                    {QStringLiteral("reason"), QStringLiteral("Removed only unsupported enum_state synthesis comment hints from the staged copy; original RTL is unchanged.")},
                    {QStringLiteral("original_sha256"), originalHash},
                    {QStringLiteral("staged_sha256"), stagedHash},
                    {QStringLiteral("transformed_lines"), lineNumbers},
                });
            }
        }
        if (inputs.sourceFiles.contains(relative))
            generatedFileList.append(QStringLiteral("./rtl/%1").arg(relative));
        QVariantMap stagedSource{
            {QStringLiteral("source"), source},
            {QStringLiteral("staged"), destination},
            {QStringLiteral("compile"), inputs.sourceFiles.contains(relative)}
        };
        if (!replacementRecord.isEmpty())
            stagedSource.insert(QStringLiteral("source_replacement"), replacementRecord);
        stagedSources.append(stagedSource);
    }
    if (inputs.sourceAnnotationMode == QLatin1String("strip_unsupported_state_encoding_hints")
        && inputs.sourceAnnotationTransforms.isEmpty())
        return failure(QStringLiteral("source_annotation_mode matched no declared RTL source; no compatibility transformation was applied."));
    QVariantList preprocessedFiles;
    if (inputs.preprocessEnabled) {
        for (const QString &relative : std::as_const(inputs.preprocessSources)) {
            QVariantMap record;
            QString preprocessError;
            const QString stagedPath = QDir(rtl).filePath(relative);
            if (!preprocessStagedSource(stagedPath, inputs, &record, &preprocessError))
                return failure(preprocessError);
            record.insert(QStringLiteral("source_file"), relative);
            preprocessedFiles.append(record);
        }
    }
    const QString filelistPath = QDir(input).filePath(QStringLiteral("rtl.f"));
    QFile filelist(filelistPath);
    if (!filelist.open(QIODevice::WriteOnly | QIODevice::Text))
        return failure(QStringLiteral("Unable to write staged source filelist: %1").arg(filelist.errorString()));
    filelist.write((generatedFileList.join(QLatin1Char('\n')) + QLatin1Char('\n')).toUtf8());
    filelist.close();

    QVariantMap mbistSimulation;
    if (inputs.mbist) {
        QHash<QString, QVariantMap> stagedMbistByPath;
        QStringList mbistIncludeDirs = inputs.mbistIncludeDirs;
        if (inputs.mbistIncludeMode == QLatin1String("compile_parents")) {
            for (const QString &relative : std::as_const(inputs.mbistCompileFiles)) {
                const QString parent = QFileInfo(relative).path();
                if (parent != QLatin1String(".") && !mbistIncludeDirs.contains(parent))
                    mbistIncludeDirs.append(parent);
            }
        }
        const auto stageMbistFile = [&](const QString &relative) -> bool {
            if (stagedMbistByPath.contains(relative))
                return true;
            const QString source = QFileInfo(QDir(root).filePath(relative)).canonicalFilePath();
            if (source.isEmpty() || !within(source, root) || !QFileInfo(source).isFile())
                return false;
            const QString destination = QDir(mbistSimulationRoot).filePath(relative);
            if (!QDir().mkpath(QFileInfo(destination).absolutePath()) || !QFile::copy(source, destination))
                return false;
            QFile stagedFile(destination);
            if (!stagedFile.open(QIODevice::ReadOnly))
                return false;
            const QString hash = QString::fromLatin1(QCryptographicHash::hash(
                stagedFile.readAll(), QCryptographicHash::Sha256).toHex());
            stagedMbistByPath.insert(relative, {{QStringLiteral("source"), source},
                {QStringLiteral("staged"), destination}, {QStringLiteral("sha256"), hash}});
            return true;
        };
        for (const QString &relative : std::as_const(inputs.mbistCompileFiles)) {
            if (!stageMbistFile(relative))
                return failure(QStringLiteral("Unable to stage MBIST simulation input: %1").arg(relative));
        }
        if (inputs.mbistIncludeMode == QLatin1String("compile_parents")) {
            for (const QString &compileFile : std::as_const(inputs.mbistCompileFiles)) {
                const QString parent = QFileInfo(compileFile).path();
                if (parent == QLatin1String("."))
                    continue;
                const QString includeRoot = QDir(root).filePath(parent);
                QDirIterator iterator(includeRoot, QDir::Files | QDir::NoSymLinks, QDirIterator::Subdirectories);
                while (iterator.hasNext()) {
                    const QString candidate = iterator.next();
                    const QString canonical = QFileInfo(candidate).canonicalFilePath();
                    const QString suffix = QFileInfo(canonical).suffix().toLower();
                    if (canonical.isEmpty() || !within(canonical, root)
                        || (suffix != QLatin1String("vh") && suffix != QLatin1String("svh")))
                        continue;
                    const QString relative = QDir(root).relativeFilePath(canonical);
                    if (!stageMbistFile(relative))
                        return failure(QStringLiteral("Unable to stage MBIST compile-parent include: %1").arg(relative));
                }
            }
        }
        for (const QString &include : std::as_const(inputs.mbistIncludeDirs)) {
            const QString includeRoot = QDir(root).filePath(include);
            QDirIterator iterator(includeRoot, QDir::Files | QDir::NoSymLinks, QDirIterator::Subdirectories);
            while (iterator.hasNext()) {
                const QString source = iterator.next();
                const QString canonical = QFileInfo(source).canonicalFilePath();
                if (canonical.isEmpty() || !within(canonical, root))
                    continue;
                const QString relative = QDir(root).relativeFilePath(canonical);
                if (!stageMbistFile(relative))
                    return failure(QStringLiteral("Unable to stage MBIST include file: %1").arg(relative));
                if (stagedMbistByPath.size() > 2048)
                    return failure(QStringLiteral("MBIST staging exceeds the 2048-file limit."));
            }
        }
        QVariantList stagedCompileFiles;
        QVariantList stagedMbistFiles;
        for (const QString &relative : std::as_const(inputs.mbistCompileFiles))
            stagedCompileFiles.append(stagedMbistByPath.value(relative));
        for (auto it = stagedMbistByPath.cbegin(); it != stagedMbistByPath.cend(); ++it)
            stagedMbistFiles.append(it.value());
        mbistSimulation = {{QStringLiteral("compile_files"), stagedCompileFiles},
            {QStringLiteral("include_dirs"), stringListVariant(mbistIncludeDirs)},
            {QStringLiteral("expected_output"), stringListVariant(inputs.mbistExpectedOutput)},
            {QStringLiteral("required_pass_markers"), stringListVariant(inputs.mbistRequiredPassMarkers)},
            {QStringLiteral("forbidden_output"), stringListVariant(inputs.mbistForbiddenOutput)},
            {QStringLiteral("test_top"), inputs.mbistTestTop},
            {QStringLiteral("timeout_seconds"), qMin(3600, inputs.mbistTimeoutSeconds * inputs.mbistTimeoutMultiplier)},
            {QStringLiteral("include_mode"), inputs.mbistIncludeMode},
            {QStringLiteral("diagnostic_mode"), inputs.mbistDiagnosticMode},
            {QStringLiteral("files"), stagedMbistFiles}};
    }

    QVariantMap constraint;
    if (!inputs.constraintFile.isEmpty()) {
        const QString source = QFileInfo(inputs.constraintFile).canonicalFilePath();
        if (source.isEmpty()
            || (!within(source, inputs.projectRoot) && !within(source, inputs.rtlRoot)))
            return failure(QStringLiteral("Constraint file is missing or outside the configured project/RTL roots."));
        const QString target = QDir(input).filePath(QFileInfo(source).fileName());
        if (!QFile::copy(source, target))
            return failure(QStringLiteral("Unable to stage constraint file: %1").arg(source));
        constraint = {{QStringLiteral("source"), source}, {QStringLiteral("staged"), target},
                      {QStringLiteral("loaded"), inputs.useConstraintFile}};
    } else {
        constraint = {{QStringLiteral("loaded"), false}};
    }

    QHash<QString, QString> stagedScripts;
    QVariantList stagedScriptRecords;
    for (const QString &key : {QStringLiteral("pre_scripts"), QStringLiteral("constraint_files"),
                               QStringLiteral("post_scripts")}) {
        for (const QString &source : inputs.synthesisScriptFiles.value(key)) {
            QString relative;
            if (within(source, inputs.projectRoot)) {
                relative = QStringLiteral("scripts/project/%1").arg(QDir(inputs.projectRoot).relativeFilePath(source));
            } else {
                const QString digest = QString::fromLatin1(QCryptographicHash::hash(
                    source.toUtf8(), QCryptographicHash::Sha256).toHex().left(12));
                relative = QStringLiteral("scripts/external/%1_%2").arg(digest, QFileInfo(source).fileName());
            }
            const QString destination = QDir(flow).filePath(relative);
            if (!stagedScripts.contains(source)) {
                if (!QDir().mkpath(QFileInfo(destination).absolutePath()) || !QFile::copy(source, destination))
                    return failure(QStringLiteral("Unable to stage configured synthesis script: %1").arg(source));
                QFile content(destination);
                if (!content.open(QIODevice::ReadOnly))
                    return failure(QStringLiteral("Unable to verify staged synthesis script: %1").arg(destination));
                const QString hash = QString::fromLatin1(QCryptographicHash::hash(
                    content.readAll(), QCryptographicHash::Sha256).toHex());
                stagedScripts.insert(source, relative);
                stagedScriptRecords.append(QVariantMap{
                    {QStringLiteral("source"), source},
                    {QStringLiteral("staged"), destination},
                    {QStringLiteral("destination"), relative},
                    {QStringLiteral("sha256"), hash},
                    {QStringLiteral("settings_key"), key},
                });
            }
        }
    }

    QString driverError;
    const QString driver = basicDriver(project, inputs, arguments, stagedScripts, &driverError);
    if (!driverError.isEmpty())
        return failure(driverError);
    const QString driverPath = QDir(flow).filePath(QStringLiteral("agent_synthesis_dft.tcl"));
    QFile driverFile(driverPath);
    if (!driverFile.open(QIODevice::WriteOnly | QIODevice::Text))
        return failure(QStringLiteral("Unable to write generated DFT driver: %1").arg(driverFile.errorString()));
    driverFile.write(driver.toUtf8());
    driverFile.close();
    QString atpgDriverPath;
    if (inputs.atpg && inputs.dftTool == QLatin1String("testmax")) {
        QString atpgDriverError;
        const QString atpgDriver = testmaxAtpgDriver(inputs, &atpgDriverError);
        if (!atpgDriverError.isEmpty())
            return failure(atpgDriverError);
        atpgDriverPath = QDir(flow).filePath(QStringLiteral("agent_atpg.tcl"));
        QFile atpgDriverFile(atpgDriverPath);
        if (!atpgDriverFile.open(QIODevice::WriteOnly | QIODevice::Text))
            return failure(QStringLiteral("Unable to write generated TestMAX driver: %1").arg(atpgDriverFile.errorString()));
        atpgDriverFile.write(atpgDriver.toUtf8());
        atpgDriverFile.close();
    } else if (inputs.atpg && inputs.dftTool == QLatin1String("tessent") && !inputs.tessentDofile.isEmpty()) {
        atpgDriverPath = QDir(flow).filePath(QStringLiteral("agent_atpg.tcl"));
        QFile customDofile(inputs.tessentDofile);
        QFile stagedDofile(atpgDriverPath);
        if (!customDofile.open(QIODevice::ReadOnly) || !stagedDofile.open(QIODevice::WriteOnly | QIODevice::Text))
            return failure(QStringLiteral("Unable to stage the configured Tessent dofile."));
        const QByteArray contents = customDofile.readAll();
        if (stagedDofile.write(contents) != contents.size())
            return failure(QStringLiteral("Unable to copy the configured Tessent dofile into the run workspace."));
        stagedDofile.close();
    }
    const QVariantMap marker{{QStringLiteral("project_id"), inputs.projectId},
                             {QStringLiteral("workspace_suffix_enabled"), inputs.workspaceSuffixEnabled},
                             {QStringLiteral("last_run_id"), runId}};
    QFile markerFile(QDir(workspace).filePath(QStringLiteral(".dft_agent_workspace.json")));
    if (!markerFile.open(QIODevice::WriteOnly))
        return failure(QStringLiteral("Unable to write native workspace ownership marker."));
    markerFile.write(QJsonDocument(QJsonObject::fromVariantMap(marker)).toJson(QJsonDocument::Indented));
    markerFile.close();
    const QVariantMap source{
        {QStringLiteral("project"), inputs.projectId},
        {QStringLiteral("original_project"), inputs.projectRoot},
        {QStringLiteral("synthesis_configuration"), QVariantMap{
            {QStringLiteral("analyze_format"), execution.value(QStringLiteral("language"),
                QStringLiteral("sverilog")).toString()},
            {QStringLiteral("analyze_format_is_derived"), true},
            {QStringLiteral("analyze_format_control_path"), QStringLiteral("metadata.dft_execution.language")},
            {QStringLiteral("compile_strategy"), execution.value(QStringLiteral("compile_strategy"),
                QStringLiteral("single_pass")).toString()}}},
        {QStringLiteral("scan_configuration"), QVariantMap{
            {QStringLiteral("clocks"), [&] {
                QStringList clocks;
                if (inputs.scan) {
                    clocks.append(inputs.clock);
                    clocks.append(inputs.additionalScanClocks);
                }
                return stringListVariant(clocks);
            }()},
            {QStringLiteral("resets"), [&] {
                QVariantList resets;
                if (inputs.scan) {
                    resets.append(QVariantMap{{QStringLiteral("port"), inputs.reset},
                                              {QStringLiteral("active_state"), inputs.resetActiveState}});
                    for (const auto &reset : std::as_const(inputs.additionalResets))
                        resets.append(QVariantMap{{QStringLiteral("port"), reset.first},
                                                  {QStringLiteral("active_state"), reset.second}});
                }
                return resets;
            }()}
        }},
        {QStringLiteral("rtl_compile_files"), stringListVariant(inputs.sourceFiles)},
        {QStringLiteral("rtl_support_files"), stringListVariant(inputs.supportFiles)},
        {QStringLiteral("rtl"), stagedSources},
        {QStringLiteral("source_replacements"), stagedReplacements},
        {QStringLiteral("source_annotation_mode"), inputs.sourceAnnotationMode},
        {QStringLiteral("source_annotation_transforms"), inputs.sourceAnnotationTransforms},
        {QStringLiteral("synthesis_scripts"), stagedScriptRecords},
        {QStringLiteral("source_preprocessor"), inputs.preprocessEnabled
            ? QVariantMap{{QStringLiteral("mode"), QStringLiteral("cpp")},
                          {QStringLiteral("available"), true},
                          {QStringLiteral("files"), preprocessedFiles},
                          {QStringLiteral("original_project_modified"), false}}
            : QVariantMap{{QStringLiteral("mode"), QStringLiteral("off")}}},
        {QStringLiteral("mbist_simulation"), mbistSimulation},
        {QStringLiteral("macro_library_files"), stringListVariant(inputs.macroLibraryFiles)},
        {QStringLiteral("macro_library_limitations"), execution.value(QStringLiteral("macro_library_limitations"))},
        {QStringLiteral("test_models"), [&] {
            QVariantList models;
            for (const DftTestModelInput &model : std::as_const(inputs.testModels))
                models.append(QVariantMap{{QStringLiteral("file"), model.file},
                                          {QStringLiteral("format"), model.format},
                                          {QStringLiteral("design"), model.design}});
            return models;
        }()},
        {QStringLiteral("atpg"), QVariantMap{
            {QStringLiteral("tool"), inputs.dftTool},
            {QStringLiteral("tessent_dofile"), inputs.tessentDofile},
            {QStringLiteral("tessent_cell_library_files"), stringListVariant(inputs.tessentCellLibraryFiles)},
            {QStringLiteral("tessent_dofile_mode"), inputs.tessentDofile.isEmpty()
                 ? QStringLiteral("generated") : QStringLiteral("custom")}}},
        {QStringLiteral("constraint_file"), constraint},
        {QStringLiteral("library"), QDir(inputs.libraryDir).filePath(inputs.libraryFile)},
        {QStringLiteral("clock_period_ns"), inputs.clockPeriodNs}
    };
    QVariantMap stageRecord{
        {QStringLiteral("run_id"), runId},
        {QStringLiteral("workspace"), workspace},
        {QStringLiteral("flow_directory"), flow},
        {QStringLiteral("driver"), driverPath},
        {QStringLiteral("atpg_driver"), atpgDriverPath},
        {QStringLiteral("source"), source}
    };
    if (!inputs.sourceCompatibilityExclusions.isEmpty())
        stageRecord.insert(QStringLiteral("source_compatibility_exclusions"), inputs.sourceCompatibilityExclusions);
    if (!inputs.sourceAdditions.isEmpty())
        stageRecord.insert(QStringLiteral("source_additions"), inputs.sourceAdditions);
    QSaveFile stageFile(QDir(workspace).filePath(QStringLiteral("stage.json")));
    if (!stageFile.open(QIODevice::WriteOnly))
        return failure(QStringLiteral("Unable to persist native stage manifest: %1").arg(stageFile.errorString()));
    stageFile.write(QJsonDocument(QJsonObject::fromVariantMap(stageRecord)).toJson(QJsonDocument::Indented));
    stageFile.write("\n");
    if (!stageFile.commit())
        return failure(QStringLiteral("Unable to commit native stage manifest: %1").arg(stageFile.errorString()));
    return success(stageRecord);
}

} // namespace

bool ConfiguredDftFlowService::supports(const QString &action)
{
    return kReadinessAliases.contains(action) || kRunAliases.contains(action) || action == QLatin1String(kStage);
}

QVariantMap ConfiguredDftFlowService::readiness(const QVariantMap &project, const QVariantMap &arguments,
                                                const QString &agentRoot)
{
    ProjectInputs inputs;
    parseProject(project, arguments, agentRoot, &inputs);
    const bool ready = inputs.configurationErrors.isEmpty() && inputs.missingFiles.isEmpty()
        && inputs.unsupported.isEmpty();
    const QVariantMap metadata = project.value(QStringLiteral("metadata")).toMap();
    const QVariantMap execution = metadata.value(QStringLiteral("dft_execution")).toMap();
    QStringList configuredClocks;
    QVariantList configuredResets;
    if (inputs.scan) {
        configuredClocks.append(inputs.clock);
        configuredClocks.append(inputs.additionalScanClocks);
        configuredResets.append(QVariantMap{{QStringLiteral("port"), inputs.reset},
                                            {QStringLiteral("active_state"), inputs.resetActiveState}});
        for (const auto &reset : std::as_const(inputs.additionalResets))
            configuredResets.append(QVariantMap{{QStringLiteral("port"), reset.first},
                                                {QStringLiteral("active_state"), reset.second}});
    }
    return success({
        {QStringLiteral("project"), inputs.projectId},
        {QStringLiteral("ready"), ready},
        {QStringLiteral("source_project"), inputs.projectRoot},
        {QStringLiteral("source_rtl_root"), inputs.rtlRoot},
        {QStringLiteral("source_filelist"), inputs.filelist},
        {QStringLiteral("synthesis_configuration"), QVariantMap{
            {QStringLiteral("analyze_format"), execution.value(QStringLiteral("language"),
                QStringLiteral("sverilog")).toString()},
            {QStringLiteral("analyze_format_is_derived"), true},
            {QStringLiteral("analyze_format_control_path"), QStringLiteral("metadata.dft_execution.language")},
            {QStringLiteral("compile_strategy"), execution.value(QStringLiteral("compile_strategy"),
                QStringLiteral("single_pass")).toString()}}},
        {QStringLiteral("constraint_file"), inputs.constraintFile},
        {QStringLiteral("use_constraint_file"), inputs.useConstraintFile},
        {QStringLiteral("clock_period_ns"), inputs.clockPeriodNs},
        {QStringLiteral("source_file_count"), inputs.sourceFiles.size() + inputs.supportFiles.size()},
        {QStringLiteral("source_files"), stringListVariant(inputs.sourceFiles)},
        {QStringLiteral("source_support_files"), stringListVariant(inputs.supportFiles)},
        {QStringLiteral("source_include_dirs"), stringListVariant(inputs.includeDirs)},
        {QStringLiteral("source_manifest_missing"), QVariantList{}},
        {QStringLiteral("source_manifest_unresolved"), stringListVariant(inputs.unsupported)},
        {QStringLiteral("library"), inputs.libraryDir.isEmpty() ? QString{} : QDir(inputs.libraryDir).filePath(inputs.libraryFile)},
        {QStringLiteral("workspace_root"), inputs.workspaceRoot},
        {QStringLiteral("effective_workspace_path"), inputs.workspaceSuffixEnabled
             ? inputs.workspaceRoot : QDir(inputs.workspaceRoot).filePath(inputs.projectId)},
        {QStringLiteral("workspace_suffix_enabled"), inputs.workspaceSuffixEnabled},
        {QStringLiteral("scan_configuration"), QVariantMap{
            {QStringLiteral("chain_count"), inputs.chainCount},
            {QStringLiteral("max_chain_length"), inputs.maxChainLength},
            {QStringLiteral("clocks"), stringListVariant(configuredClocks)},
            {QStringLiteral("resets"), configuredResets},
            {QStringLiteral("drc_autofix"), QVariantMap{
                {QStringLiteral("requested_mode"), inputs.autofixRequestedMode},
                {QStringLiteral("effective_mode"), inputs.autofixMode},
                {QStringLiteral("reset_port"), inputs.autofixResetPort},
                {QStringLiteral("adjustment"), inputs.autofixAdjustment}
            }}
        }},
        {QStringLiteral("atpg_configuration"), QVariantMap{
            {QStringLiteral("enabled"), inputs.atpg},
            {QStringLiteral("tool"), inputs.dftTool},
            {QStringLiteral("launcher"), inputs.dftTool == QLatin1String("tessent") ? inputs.tessent : inputs.testmax},
            {QStringLiteral("cell_model_files"), stringListVariant(inputs.atpgCellModelFiles)},
            {QStringLiteral("tessent_dofile"), inputs.tessentDofile},
            {QStringLiteral("tessent_cell_library_files"), stringListVariant(inputs.tessentCellLibraryFiles)},
            {QStringLiteral("minimum_coverage"), inputs.minimumAtpgCoverage},
            {QStringLiteral("timeout_seconds"), inputs.atpgTimeoutSeconds},
            {QStringLiteral("abort_limit"), inputs.atpgAbortLimit}
        }},
        {QStringLiteral("flow_modules"), QVariantMap{
            {QStringLiteral("synthesis"), inputs.synthesis}, {QStringLiteral("dft"), inputs.dft},
            {QStringLiteral("scan"), inputs.scan}, {QStringLiteral("mbist"), inputs.mbist},
            {QStringLiteral("atpg"), inputs.atpg}, {QStringLiteral("lbist"), inputs.lbist}
        }},
        {QStringLiteral("mbist_simulation"), QVariantMap{
            {QStringLiteral("enabled"), inputs.mbist},
            {QStringLiteral("compile_files"), stringListVariant(inputs.mbistCompileFiles)},
            {QStringLiteral("include_dirs"), stringListVariant(inputs.mbistIncludeDirs)},
            {QStringLiteral("expected_output"), stringListVariant(inputs.mbistExpectedOutput)},
            {QStringLiteral("required_pass_markers"), stringListVariant(inputs.mbistRequiredPassMarkers)},
            {QStringLiteral("forbidden_output"), stringListVariant(inputs.mbistForbiddenOutput)},
            {QStringLiteral("test_top"), inputs.mbistTestTop},
            {QStringLiteral("timeout_seconds"), inputs.mbistTimeoutSeconds}
        }},
        {QStringLiteral("unsupported_modules"), stringListVariant(inputs.unsupported)},
        {QStringLiteral("missing_source_files"), stringListVariant(inputs.missingFiles)},
        {QStringLiteral("configuration_errors"), stringListVariant(inputs.configurationErrors)}
    });
}

QVariantMap ConfiguredDftFlowService::stage(const QVariantMap &project, const QString &workspaceRoot,
                                            const QVariantMap &arguments, const QString &agentRoot)
{
    return stageConfigured(project, workspaceRoot, arguments, agentRoot);
}

QVariantMap ConfiguredDftFlowService::run(const QVariantMap &project, const QString &workspaceRoot,
                                          const QVariantMap &arguments, const QString &agentRoot,
                                          const std::shared_ptr<std::atomic_bool> &cancelToken)
{
    const QVariantMap readinessResult = readiness(project, arguments, agentRoot);
    const QVariantMap readinessData = readinessResult.value(QStringLiteral("result")).toMap();
    if (!readinessData.value(QStringLiteral("ready")).toBool()) {
        QStringList reasons;
        for (const QString &key : {QStringLiteral("configuration_errors"), QStringLiteral("missing_source_files"),
                                   QStringLiteral("source_manifest_missing"), QStringLiteral("source_manifest_unresolved")}) {
            for (const QVariant &entry : readinessData.value(key).toList()) {
                const QString reason = entry.toString().trimmed();
                if (!reason.isEmpty() && !reasons.contains(reason))
                    reasons.append(reason);
            }
        }
        const QString detail = reasons.isEmpty()
            ? QStringLiteral("readiness did not provide a specific blocker") : reasons.join(QStringLiteral("; "));
        return failure(QStringLiteral("Configured DFT project is not ready: %1").arg(detail));
    }
    const QVariantMap stagedResponse = stageConfigured(project, workspaceRoot, arguments, agentRoot);
    if (!stagedResponse.value(QStringLiteral("ok")).toBool())
        return stagedResponse;
    const QVariantMap staged = stagedResponse.value(QStringLiteral("result")).toMap();
    const QString flow = staged.value(QStringLiteral("flow_directory")).toString();
    const QString driverPath = staged.value(QStringLiteral("driver")).toString();
    const QString driver = QFileInfo(driverPath).fileName();
    const QString dcLog = QDir(flow).filePath(QStringLiteral("logs/agent_insert_dft.log"));
    ProjectInputs inputs;
    parseProject(project, arguments, agentRoot, &inputs);
    const QStringList dcArguments{QStringLiteral("-no_gui"), QStringLiteral("-f"), driver};
    QVariantMap dcProcess = executeProcess(inputs.dcShell, dcArguments,
        flow, dcLog, inputs.timeoutSeconds, cancelToken);
    dcProcess.insert(QStringLiteral("attempt_count"), 1);
    const QString firstDcStdout = dcProcess.value(QStringLiteral("stdout")).toString();
    if (dcProcess.value(QStringLiteral("started")).toBool()
        && !dcProcess.value(QStringLiteral("timed_out")).toBool()
        && !dcProcess.value(QStringLiteral("cancelled")).toBool()
        && firstDcStdout.contains(QStringLiteral("DCSH-1"))) {
        const QString firstAttemptLog = QDir(flow).filePath(QStringLiteral("logs/agent_insert_dft.attempt1.log"));
        QFile::remove(firstAttemptLog);
        const bool firstAttemptPreserved = QFile::copy(dcLog, firstAttemptLog);
        dcProcess = executeProcess(inputs.dcShell, dcArguments,
            flow, dcLog, inputs.timeoutSeconds, cancelToken);
        dcProcess.insert(QStringLiteral("attempt_count"), 2);
        dcProcess.insert(QStringLiteral("retry_reason"), QStringLiteral("DCSH-1"));
        dcProcess.insert(QStringLiteral("first_attempt_stdout"), firstDcStdout);
        if (firstAttemptPreserved)
            dcProcess.insert(QStringLiteral("first_attempt_log"), firstAttemptLog);
    }
    if (!dcProcess.value(QStringLiteral("started")).toBool()) {
        QVariantMap execution = dcProcess;
        execution.insert(QStringLiteral("workspace"), staged.value(QStringLiteral("workspace")));
        execution.insert(QStringLiteral("flow_directory"), flow);
        execution.insert(QStringLiteral("driver"), driverPath);
        execution.insert(QStringLiteral("log"), dcLog);
        return {{QStringLiteral("ok"), false},
                {QStringLiteral("message"), dcProcess.value(QStringLiteral("error"))},
                {QStringLiteral("result"), QVariantMap{
                    {QStringLiteral("status"), QStringLiteral("execution_incomplete")},
                    {QStringLiteral("workspace"), staged.value(QStringLiteral("workspace"))},
                    {QStringLiteral("flow_directory"), flow},
                    {QStringLiteral("log"), dcLog},
                    {QStringLiteral("execution"), execution}}}};
    }
    QVariantMap atpgProcess;
    QVariantMap atpgSummary;
    QVariantMap atpgAcceptance;
    QVariantMap mbistExecution;
    QVariantList acceptance = configuredFlowAcceptance(inputs);
    QStringList errors;
    QFile analyzeReport(QDir(flow).filePath(QStringLiteral("reports/analyze.log")));
    if (analyzeReport.open(QIODevice::ReadOnly | QIODevice::Text)) {
        static const QRegularExpression compilerDiagnostic(
            QStringLiteral(R"((?:^|\s)(?:Error|Fatal):|compilation terminated with [1-9][0-9]* errors)"),
            QRegularExpression::CaseInsensitiveOption);
        QTextStream stream(&analyzeReport);
        while (!stream.atEnd() && errors.size() < 12) {
            const QString line = stream.readLine().trimmed();
            if (!line.isEmpty() && compilerDiagnostic.match(line).hasMatch())
                errors.append(line.left(800));
        }
    }
    bool atpgAttempted = false;
    const bool dcClean = dcProcess.value(QStringLiteral("returncode")).toInt() == 0
        && !dcProcess.value(QStringLiteral("timed_out")).toBool()
        && !dcProcess.value(QStringLiteral("cancelled")).toBool();
    const bool hdlAnalysisFailed = !errors.isEmpty();
    const QString retryReason = dcProcess.value(QStringLiteral("retry_reason")).toString();
    if (!dcClean && !hdlAnalysisFailed) {
        if (retryReason == QLatin1String("DCSH-1")
            && dcProcess.value(QStringLiteral("attempt_count")).toInt() >= 2) {
            errors.append(QStringLiteral("dc_shell returned DCSH-1 after the automatic one-time retry. HDL analysis did not start; inspect execution.log and first_attempt_log for the startup/feature failure before changing RTL or retrying unchanged inputs."));
        } else {
            errors.append(QStringLiteral("dc_shell did not finish cleanly; inspect the exact execution.log before changing project inputs."));
        }
    }
    if (!dcClean && hdlAnalysisFailed)
        errors.prepend(QStringLiteral("HDL analysis failed; inspect flow/reports/analyze.log before changing project inputs."));
    const QString workspace = staged.value(QStringLiteral("workspace")).toString();
    if (inputs.mbist && !dcProcess.value(QStringLiteral("cancelled")).toBool()) {
        mbistExecution = runMbistSimulation(inputs, flow, cancelToken);
        errors.append(mbistExecution.value(QStringLiteral("errors")).toStringList());
    }
    const QVariantMap baseAcceptance = DftReportEvidenceService::validateAcceptance(workspace, acceptance);
    const bool baseEvidenceAccepted = baseAcceptance.value(QStringLiteral("passed")).toBool();
    const QVariantMap synthesisAcceptance = DftReportEvidenceService::validateAcceptance(
        workspace, configuredSynthesisAcceptance(inputs));
    const bool synthesisEvidenceAccepted = synthesisAcceptance.value(QStringLiteral("passed")).toBool();
    const QString atpgLog = QDir(flow).filePath(QStringLiteral("logs/agent_atpg.log"));
    const QString atpgStdout = QDir(flow).filePath(QStringLiteral("logs/agent_atpg.stdout.log"));
    const QString atpgSummaryPath = QDir(flow).filePath(QStringLiteral("reports/atpg_summary.json"));
    const QString postDrcPath = QDir(flow).filePath(QStringLiteral("reports/post_dft_drc.rpt"));
    QVariantMap atpgResult;
    bool atpgAccepted = false;
    if (inputs.atpg && dcClean && synthesisEvidenceAccepted) {
        // This run has a unique workspace. Remove expected outputs anyway so no preexisting file can count as evidence.
        QFile::remove(atpgLog);
        QFile::remove(atpgStdout);
        QFile::remove(atpgSummaryPath);
        const QDateTime atpgStarted = QDateTime::currentDateTimeUtc();
        QString atpgDriverPath = staged.value(QStringLiteral("atpg_driver")).toString();
        QString atpgLauncher;
        QStringList atpgArguments;
        if (inputs.dftTool == QLatin1String("tessent")) {
            atpgLauncher = inputs.tessent;
            if (atpgDriverPath.isEmpty()) {
                QString driverError;
                const QString generated = tessentAtpgDriver(inputs, flow, &driverError);
                if (!driverError.isEmpty()) {
                    errors.append(driverError);
                } else {
                    atpgDriverPath = QDir(flow).filePath(QStringLiteral("agent_atpg.tcl"));
                    QFile generatedDofile(atpgDriverPath);
                    if (!generatedDofile.open(QIODevice::WriteOnly | QIODevice::Text)
                        || generatedDofile.write(generated.toUtf8()) != generated.toUtf8().size())
                        errors.append(QStringLiteral("Unable to write generated Tessent ATPG dofile."));
                    generatedDofile.close();
                }
            }
            atpgArguments = {QStringLiteral("-shell"), QStringLiteral("-dofile"),
                QFileInfo(atpgDriverPath).fileName(), QStringLiteral("-logfile"),
                QStringLiteral("logs/agent_atpg.log"), QStringLiteral("-replace"),
                QStringLiteral("-ignore_startup_file"), QStringLiteral("-license_wait"), QStringLiteral("none")};
        } else {
            atpgLauncher = inputs.testmax;
            atpgArguments = {QFileInfo(atpgDriverPath).fileName(), QStringLiteral("-shell")};
        }
        if (errors.isEmpty()) {
            atpgProcess = executeProcess(atpgLauncher, atpgArguments, flow, atpgStdout,
                inputs.atpgTimeoutSeconds, cancelToken);
            atpgAttempted = atpgProcess.value(QStringLiteral("started")).toBool();
        }
        if (!atpgProcess.value(QStringLiteral("started")).toBool()) {
            const QString processError = atpgProcess.value(QStringLiteral("error")).toString();
            if (!processError.isEmpty())
                errors.append(processError);
        } else if (atpgProcess.value(QStringLiteral("returncode")).toInt() != 0
                   || atpgProcess.value(QStringLiteral("timed_out")).toBool()
                   || atpgProcess.value(QStringLiteral("cancelled")).toBool()) {
            errors.append(QStringLiteral("%1 did not finish cleanly.").arg(inputs.dftTool));
        }
        const bool freshAtpgLog = QFileInfo(atpgLog).isFile()
            && QFileInfo(atpgLog).lastModified().toUTC() >= atpgStarted.addSecs(-1);
        if (freshAtpgLog) {
            const QVariantMap parsed = DftReportEvidenceService::parseAtpgReport(atpgLog, inputs.dftTool);
            atpgSummary = parsed.value(QStringLiteral("result")).toMap();
            atpgSummary.insert(QStringLiteral("report_file"), atpgLog);
            atpgSummary.insert(QStringLiteral("report_fresh"), true);
            atpgSummary.insert(QStringLiteral("minimum_coverage"), inputs.minimumAtpgCoverage);
            atpgSummary.insert(QStringLiteral("diagnostic_mode"), inputs.atpgDiagnosticMode);
            const QVariant coverage = atpgSummary.value(QStringLiteral("coverage_percent"));
            const QVariantMap drcEnvelope = DftReportEvidenceService::parseDrcReport(postDrcPath);
            const QVariantMap drcReport = drcEnvelope.value(QStringLiteral("result")).toMap();
            const QVariant observedViolations = drcReport.value(QStringLiteral("total_violations"));
            QVariantMap summaryForDisk = atpgSummary;
            summaryForDisk.insert(QStringLiteral("post_dft_drc_violations"), observedViolations);
            summaryForDisk.insert(QStringLiteral("post_dft_drc_report"), postDrcPath);
            QString summaryError;
            if (!writeJsonAtomically(atpgSummaryPath, summaryForDisk, &summaryError))
                errors.append(QStringLiteral("Unable to persist ATPG summary: %1").arg(summaryError));
            atpgSummary = summaryForDisk;

            QVariantMap atpgLogCheck{
                {QStringLiteral("path"), QStringLiteral("flow/logs/agent_atpg.log")},
                {QStringLiteral("no_errors"), true}, {QStringLiteral("blocking"), true}
            };
            if (inputs.dftTool == QLatin1String("testmax")) {
                atpgLogCheck.insert(QStringLiteral("contains"), QStringLiteral("Design rules checking was successful"));
            } else if (inputs.tessentDofile.isEmpty()) {
                atpgLogCheck.insert(QStringLiteral("contains"), QStringLiteral("successfully traced"));
                acceptance.append(QVariantMap{{QStringLiteral("path"), QStringLiteral("flow/reports/tessent_scan_chains.rpt")},
                    {QStringLiteral("contains"), QStringLiteral("chain =")}, {QStringLiteral("no_errors"), true},
                    {QStringLiteral("blocking"), true}});
                acceptance.append(QVariantMap{{QStringLiteral("path"), QStringLiteral("flow/reports/tessent_atpg_statistics.rpt")},
                    {QStringLiteral("contains"), QStringLiteral("fault_coverage")}, {QStringLiteral("no_errors"), true},
                    {QStringLiteral("blocking"), true}});
                acceptance.append(QVariantMap{
                    {QStringLiteral("path"), QStringLiteral("flow/mapped_scan/%1_tessent_stuck_at.stil")
                         .arg(inputs.projectId + QStringLiteral("_scan"))},
                    {QStringLiteral("blocking"), true}});
            }
            if (inputs.minimumAtpgCoverage.isValid())
                atpgLogCheck.insert(QStringLiteral("minimum_percentage"), inputs.minimumAtpgCoverage);
            acceptance.append(atpgLogCheck);
            acceptance.append(QVariantMap{
                {QStringLiteral("path"), QStringLiteral("flow/reports/atpg_summary.json")},
                {QStringLiteral("blocking"), true}
            });
            atpgAcceptance = DftReportEvidenceService::validateAcceptance(workspace, acceptance);
            const bool coveragePresent = coverage.isValid() && !coverage.isNull();
            const bool coverageMeetsTarget = !inputs.minimumAtpgCoverage.isValid()
                || (coveragePresent && coverage.toDouble() >= inputs.minimumAtpgCoverage.toDouble());
            const bool atpgProcessClean = atpgProcess.value(QStringLiteral("started")).toBool()
                && atpgProcess.value(QStringLiteral("returncode")).toInt() == 0
                && !atpgProcess.value(QStringLiteral("timed_out")).toBool()
                && !atpgProcess.value(QStringLiteral("cancelled")).toBool();
            const bool toolDrcAccepted = inputs.dftTool == QLatin1String("tessent")
                || atpgSummary.value(QStringLiteral("drc_completed")).toBool();
            atpgAccepted = baseEvidenceAccepted && atpgProcessClean && freshAtpgLog
                && toolDrcAccepted
                && coveragePresent && coverageMeetsTarget
                && atpgAcceptance.value(QStringLiteral("passed")).toBool();
            atpgResult = {{QStringLiteral("status"), atpgAccepted ? QStringLiteral("evidence_ready")
                                                              : QStringLiteral("blocked")},
                          {QStringLiteral("process"), atpgProcess},
                          {QStringLiteral("summary"), atpgSummary},
                          {QStringLiteral("acceptance"), atpgAcceptance},
                          {QStringLiteral("coverage_present"), coveragePresent},
                          {QStringLiteral("coverage_meets_target"), coverageMeetsTarget},
                          {QStringLiteral("report_fresh"), true},
                          {QStringLiteral("summary_file"), atpgSummaryPath}};
        } else {
            atpgSummary = {{QStringLiteral("report_file"), atpgLog},
                           {QStringLiteral("report_fresh"), false},
                           {QStringLiteral("coverage_percent"), QVariant{}},
                           {QStringLiteral("drc_completed"), false}};
            atpgAcceptance = {{QStringLiteral("passed"), false}, {QStringLiteral("blocking"), true},
                              {QStringLiteral("issues"), QStringList{QStringLiteral("Fresh TestMAX ATPG report was not generated.")}}};
            atpgResult = {{QStringLiteral("status"), QStringLiteral("blocked")},
                          {QStringLiteral("process"), atpgProcess},
                          {QStringLiteral("summary"), atpgSummary},
                          {QStringLiteral("acceptance"), atpgAcceptance},
                          {QStringLiteral("coverage_present"), false},
                          {QStringLiteral("coverage_meets_target"), false},
                          {QStringLiteral("report_fresh"), false}};
        }
    } else if (inputs.atpg && dcClean && !synthesisEvidenceAccepted) {
        const QString reason = QStringLiteral("Synthesis/read-link evidence failed acceptance; ATPG was not launched.");
        errors.append(reason);
        atpgProcess = {{QStringLiteral("started"), false},
                       {QStringLiteral("returncode"), -1},
                       {QStringLiteral("error"), reason}};
        atpgSummary = {{QStringLiteral("report_file"), atpgLog},
                       {QStringLiteral("report_fresh"), false},
                       {QStringLiteral("coverage_percent"), QVariant{}},
                       {QStringLiteral("drc_completed"), false}};
        atpgAcceptance = {{QStringLiteral("passed"), false}, {QStringLiteral("blocking"), true},
                          {QStringLiteral("issues"), QStringList{reason}}};
        atpgResult = {{QStringLiteral("status"), QStringLiteral("blocked")},
                      {QStringLiteral("process"), atpgProcess},
                      {QStringLiteral("summary"), atpgSummary},
                      {QStringLiteral("acceptance"), atpgAcceptance},
                      {QStringLiteral("coverage_present"), false},
                      {QStringLiteral("coverage_meets_target"), false},
                      {QStringLiteral("report_fresh"), false}};
    }
    QVariantMap execution{
        {QStringLiteral("project"), inputs.projectId},
        {QStringLiteral("flow"), QStringLiteral("configured_rtl_synthesis")
            + (inputs.scan ? QStringLiteral("_scan") : QString{})
            + (inputs.mbist ? QStringLiteral("_mbist") : QString{})
            + (inputs.atpg ? QStringLiteral("_atpg") : QString{})},
        {QStringLiteral("workspace"), workspace},
        {QStringLiteral("flow_directory"), flow},
        {QStringLiteral("driver"), driverPath},
        {QStringLiteral("log"), dcLog},
        {QStringLiteral("returncode"), dcProcess.value(QStringLiteral("returncode"))},
        {QStringLiteral("attempt_count"), dcProcess.value(QStringLiteral("attempt_count"), 1)},
        {QStringLiteral("retry_reason"), retryReason},
        {QStringLiteral("first_attempt_log"), dcProcess.value(QStringLiteral("first_attempt_log"))},
        {QStringLiteral("first_attempt_stdout"), dcProcess.value(QStringLiteral("first_attempt_stdout"))},
        {QStringLiteral("failure_category"), dcClean ? QStringLiteral("none")
            : hdlAnalysisFailed ? QStringLiteral("hdl_analysis_failed")
            : retryReason == QLatin1String("DCSH-1") ? QStringLiteral("dc_shell_startup_failure")
            : QStringLiteral("eda_process_failure")},
        {QStringLiteral("next_action"), dcClean ? QString{} : hdlAnalysisFailed
            ? QStringLiteral("Read the first compiler diagnostic in analyze.log and trace it to the staged input.")
            : retryReason == QLatin1String("DCSH-1")
                ? QStringLiteral("The automatic retry is exhausted. Inspect both startup logs and tool/license availability; no HDL diagnosis was produced.")
                : QStringLiteral("Read the exact process log and diagnose the EDA startup/exit failure before editing source.")},
        {QStringLiteral("timed_out"), dcProcess.value(QStringLiteral("timed_out"))},
        {QStringLiteral("cancelled"), dcProcess.value(QStringLiteral("cancelled"))},
        {QStringLiteral("stdout"), dcProcess.value(QStringLiteral("stdout"))},
        {QStringLiteral("log_view_truncated"), dcProcess.value(QStringLiteral("stdout_truncated"))},
        {QStringLiteral("errors"), stringListVariant(errors)},
        {QStringLiteral("completed_cleanly"), dcClean && (!inputs.mbist
             || mbistExecution.value(QStringLiteral("completed_cleanly")).toBool()) && (!inputs.atpg
             || (atpgAttempted && atpgProcess.value(QStringLiteral("returncode")).toInt() == 0
                 && !atpgProcess.value(QStringLiteral("timed_out")).toBool()
                 && !atpgProcess.value(QStringLiteral("cancelled")).toBool()))}
    };
    QVariantList acceptanceList = acceptance;
    QVariantMap payload;
    bool reportsAccepted = false;
    if (inputs.atpg) {
        reportsAccepted = atpgAccepted;
        payload = {
            {QStringLiteral("skill"), QStringLiteral("configured_rtl_dft_insert_scan_atpg_native_slice")},
            {QStringLiteral("status"), execution.value(QStringLiteral("completed_cleanly")).toBool() && reportsAccepted
                 ? QStringLiteral("review_ready") : (errors.isEmpty() ? QStringLiteral("review_blocked")
                                                                       : QStringLiteral("execution_incomplete"))},
            {QStringLiteral("execution"), execution}, {QStringLiteral("readiness"), readinessData},
            {QStringLiteral("source"), staged.value(QStringLiteral("source"))},
            {QStringLiteral("acceptance"), acceptanceList},
            {QStringLiteral("atpg"), atpgResult},
            {QStringLiteral("mbist"), mbistExecution},
            {QStringLiteral("verification_note"), QStringLiteral("Only fresh ATPG reports from the selected backend and applicable post-DFT DRC reports that pass native acceptance and two-round evidence review can be reported verified.")}
        };
    } else {
        payload = {
            {QStringLiteral("skill"), inputs.mbist && !inputs.scan
                 ? QStringLiteral("configured_rtl_synthesis_mbist_native")
                 : QStringLiteral("configured_rtl_dft_insert_dft_native_slice")},
            {QStringLiteral("status"), execution.value(QStringLiteral("completed_cleanly")).toBool()
                 && baseEvidenceAccepted ? QStringLiteral("review_ready") : QStringLiteral("execution_incomplete")},
            {QStringLiteral("execution"), execution}, {QStringLiteral("readiness"), readinessData},
            {QStringLiteral("source"), staged.value(QStringLiteral("source"))},
            {QStringLiteral("acceptance"), acceptanceList},
            {QStringLiteral("mbist"), mbistExecution},
            {QStringLiteral("verification_note"), QStringLiteral("A successful tool exit is not sufficient; fresh reports and every enabled module's staged output artifacts must pass acceptance and two-round evidence review.")}
        };
    }
    payload.insert(QStringLiteral("base_acceptance_result"), baseAcceptance);
    const QVariantMap analysis = DftEvidenceService::analyze({{QStringLiteral("result"), payload}});
    payload.insert(QStringLiteral("dft_analysis"), analysis);
    const QString evidenceFile = QDir(staged.value(QStringLiteral("workspace")).toString()).filePath(QStringLiteral("skill_result.json"));
    QSaveFile evidence(evidenceFile);
    if (!evidence.open(QIODevice::WriteOnly))
        return failure(QStringLiteral("Unable to create flow evidence: %1").arg(evidence.errorString()));
    evidence.write(QJsonDocument(QJsonObject::fromVariantMap(payload)).toJson(QJsonDocument::Indented));
    evidence.write("\n");
    if (!evidence.commit())
        return failure(QStringLiteral("Unable to commit flow evidence: %1").arg(evidence.errorString()));
    payload.insert(QStringLiteral("evidence_file"), evidenceFile);
    if (payload.value(QStringLiteral("status")).toString() == QLatin1String("review_ready")) {
        const QVariantMap cross = DftReportEvidenceService::crossValidateEvidence(evidenceFile, workspace);
        const QVariantMap crossResult = cross.value(QStringLiteral("result")).toMap();
        payload.insert(QStringLiteral("verification"), crossResult);
        const QString verificationStatus = crossResult.value(QStringLiteral("status")).toString();
        payload.insert(QStringLiteral("status"), verificationStatus == QLatin1String("verified")
                           ? QStringLiteral("verified") : verificationStatus);
        if (inputs.atpg) {
            QVariantMap finalAtpg = atpgResult;
            finalAtpg.insert(QStringLiteral("status"), verificationStatus == QLatin1String("verified")
                                 ? QStringLiteral("verified") : QStringLiteral("blocked"));
            payload.insert(QStringLiteral("atpg"), finalAtpg);
        }
    } else if (inputs.atpg) {
        payload.insert(QStringLiteral("verification"), QVariantMap{
            {QStringLiteral("status"), QStringLiteral("blocked")},
            {QStringLiteral("reason"), QStringLiteral("Fresh report acceptance did not pass; no ATPG PASS is asserted.")}
        });
    }
    return success(payload);
}

QVariantMap ConfiguredDftFlowService::dispatch(const QString &action, const QVariantMap &project,
                                              const QVariantMap &arguments, const QString &agentRoot)
{
    if (!supports(action))
        return failure(QStringLiteral("Unsupported configured DFT action."));
    if (kReadinessAliases.contains(action))
        return readiness(project, arguments, agentRoot);
    const QString workspaceRoot = arguments.value(QStringLiteral("workspace_root")).toString();
    if (action == QLatin1String(kStage))
        return stage(project, workspaceRoot, arguments, agentRoot);
    return run(project, workspaceRoot, arguments, agentRoot);
}
