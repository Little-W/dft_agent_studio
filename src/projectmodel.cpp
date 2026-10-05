#include "projectmodel.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>

namespace {
constexpr auto kFixedModelId = "qwen3.5:4b";

QString normalizedAgentPermissionMode(QString mode) {
    mode = mode.trimmed().toLower();
    if (mode == QLatin1String("full_access") || mode == QLatin1String("full")
        || mode == QLatin1String("danger-full-access"))
        return QStringLiteral("full_access");
    if (mode == QLatin1String("autonomous") || mode == QLatin1String("read_only")
        || mode == QLatin1String("readonly") || mode == QLatin1String("read-only"))
        return QStringLiteral("autonomous");
    return QStringLiteral("approval");
}
}

ProjectModel::ProjectModel(QString storagePath, QObject *parent)
    : QAbstractListModel(parent), m_storagePath(std::move(storagePath)) {
    load();
}

int ProjectModel::rowCount(const QModelIndex &parent) const {
    return parent.isValid() ? 0 : m_projects.size();
}

QVariant ProjectModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= m_projects.size())
        return {};
    const auto &project = m_projects.at(index.row());
    switch (role) {
    case IdRole: return project.id;
    case NameRole: return project.name;
    case KindRole: return project.kind;
    case RootRole: return project.root;
    case RtlRootRole: return project.rtlRoot;
    case TopRole: return project.top;
    case TopModuleRole: return project.top;
    case GoalRole: return project.goal;
    case NotesRole: return project.notes;
    case ManagedRole: return project.managed;
    case ModelRole: return project.modelName;
    case FlowProfileRole: return project.flowProfile;
    case MinimumCoverageRole: return project.minimumCoverage >= 0 ? QVariant(project.minimumCoverage) : QVariant();
    case MaximumDftDrcViolationsRole: return project.maximumDftDrcViolations >= 0 ? QVariant(project.maximumDftDrcViolations) : QVariant();
    case LibraryDirRole: return project.libraryDir;
    case LibraryFileRole: return project.libraryFile;
    case LibraryProfileRole: return project.libraryProfile;
    case StatusRole: return projectStatus(project);
    default: return {};
    }
}

QHash<int, QByteArray> ProjectModel::roleNames() const {
    return {
        {IdRole, "projectId"}, {NameRole, "name"}, {KindRole, "kind"}, {RootRole, "root"},
        {RtlRootRole, "rtlRoot"}, {TopRole, "top"}, {TopModuleRole, "topModule"}, {GoalRole, "goal"}, {NotesRole, "notes"},
        {ManagedRole, "managed"}, {ModelRole, "modelName"}, {FlowProfileRole, "flowProfile"},
        {MinimumCoverageRole, "minimumCoverage"}, {MaximumDftDrcViolationsRole, "maximumDftDrcViolations"},
        {LibraryDirRole, "libraryDir"}, {LibraryFileRole, "libraryFile"}, {LibraryProfileRole, "libraryProfile"},
        {StatusRole, "status"}
    };
}

int ProjectModel::currentIndex() const { return m_currentIndex; }

void ProjectModel::setCurrentIndex(int index) {
    const int next = qBound(0, index, qMax(0, m_projects.size() - 1));
    if (m_currentIndex == next)
        return;
    m_currentIndex = next;
    emit currentIndexChanged();
}

QString ProjectModel::storagePath() const { return m_storagePath; }

QVariantMap ProjectModel::currentProject() const { return projectAt(m_currentIndex); }

QVariantMap ProjectModel::projectAt(int index) const {
    if (index < 0 || index >= m_projects.size())
        return {};
    return toMap(m_projects.at(index));
}

int ProjectModel::indexOfProjectId(const QString &projectId) const {
    const QString normalized = projectId.trimmed();
    if (normalized.isEmpty())
        return -1;
    for (int index = 0; index < m_projects.size(); ++index) {
        if (m_projects.at(index).id == normalized)
            return index;
    }
    return -1;
}

bool ProjectModel::addProject(const QString &folderPath) {
    return addProject(folderPath, QStringLiteral("rtl"));
}

bool ProjectModel::addProject(const QString &folderPath, const QString &kind) {
    const QFileInfo info(folderPath);
    if (!info.isDir()) {
        emit errorOccurred(tr("项目目录不可用：%1").arg(folderPath));
        return false;
    }
    Project project;
    project.name = info.fileName().isEmpty() ? info.absoluteFilePath() : info.fileName();
    project.id = generatedId(project.name);
    project.kind = kind.trimmed().isEmpty() ? QStringLiteral("rtl") : kind.trimmed();
    project.root = info.absoluteFilePath();
    project.goal = tr("检查 %1 的设计与 DFT 配置，并只报告工具返回的证据。").arg(project.name);
    project.notes = tr("新增项目。请在运行前配置输入、约束、工艺库和流程模块。");
    project.managed = false;
    project.modelName = QString::fromLatin1(kFixedModelId);
    project.flowProfile = "modular";
    for (const auto &existing : m_projects) {
        if (existing.id == project.id)
            project.id += QStringLiteral("_%1").arg(m_projects.size() + 1);
    }
    project.dftExecution = QJsonObject{
        {"workspace_path", ""},
        {"workspace_suffix_enabled", true},
        {"agent_permission_mode", "approval"},
        {"additional_workspace_folders", QJsonArray{}},
        {"source_files", QJsonArray{}},
        {"filelist", ""},
        {"constraint_file", ""},
        {"synthesis_settings", QJsonObject{
            {"constraint_files", QJsonArray{}},
            {"clocks", QJsonArray{}},
            {"generated_clocks", QJsonArray{}},
            {"io_delays", QJsonArray{}},
            {"timing_exceptions", QJsonArray{}},
            {"additional_tcl_commands", QJsonArray{}},
            {"max_cores", 4},
            {"compile_command", "compile"},
            {"incremental", false},
            {"retime", false},
            {"gate_clock", false},
            {"scan_ready", true},
            {"boundary_optimization", true},
            {"auto_ungroup", "none"},
            {"reports", QJsonArray{"qor", "timing", "area", "power", "constraints"}}
        }},
        {"use_constraint_file", false},
        {"language", "sverilog"},
        {"clock_period_ns", 1000},
        {"map_effort", "low"},
        {"area_effort", "low"},
        {"power_effort", "none"},
        {"timeout_seconds", 1800},
        {"atpg_cell_model_files", QJsonArray{}},
        {"atpg_timeout_seconds", 1800},
        {"dft_tool", "testmax"},
        {"tessent_dofile", ""},
        {"iteration_limit", 3},
        {"patch_review_enabled", false},
        {"drc_autofix", QJsonObject{{"mode", "off"}}},
        {"agent_terminal", QJsonObject{{"enabled", true}, {"maximum_command_seconds", 300}}},
        {"synthesis_output_dir", QStringLiteral("/media/6/Projects/DFT_agent_outputs/") + project.id + "/synthesis"},
        {"dft_output_dir", QStringLiteral("/media/6/Projects/DFT_agent_outputs/") + project.id + "/dft"},
    };
    project.flowModules = QJsonObject{
        {"synthesis", true}, {"dft", true}, {"scan", true},
        {"mbist", false}, {"atpg", false}, {"lbist", false},
    };
    beginInsertRows({}, m_projects.size(), m_projects.size());
    m_projects.append(project);
    endInsertRows();
    setCurrentIndex(m_projects.size() - 1);
    return save();
}

bool ProjectModel::removeProject(int index) {
    if (index < 0 || index >= m_projects.size())
        return false;
    if (m_projects.at(index).managed) {
        emit errorOccurred(tr("内置项目不能移除。"));
        return false;
    }
    beginRemoveRows({}, index, index);
    m_projects.removeAt(index);
    endRemoveRows();
    setCurrentIndex(qMin(index, m_projects.size() - 1));
    return save();
}

bool ProjectModel::updateProject(int index, const QVariantMap &values) {
    if (index < 0 || index >= m_projects.size())
        return false;
    auto &project = m_projects[index];
    const auto update = [&values](QString &field, const char *key) {
        if (values.contains(QString::fromLatin1(key)))
            field = values.value(QString::fromLatin1(key)).toString().trimmed();
    };
    update(project.name, "name");
    update(project.kind, "kind");
    update(project.root, "root");
    update(project.rtlRoot, "rtlRoot");
    update(project.top, "top");
    update(project.goal, "goal");
    update(project.notes, "notes");
    if (values.contains(QStringLiteral("relatedDocuments"))) {
        project.relatedDocuments.clear();
        for (const auto &value : values.value(QStringLiteral("relatedDocuments")).toStringList()) {
            const QString path = value.trimmed();
            if (!path.isEmpty() && !project.relatedDocuments.contains(path))
                project.relatedDocuments.append(path);
        }
    }
    update(project.flowProfile, "flowProfile");
    update(project.libraryDir, "libraryDir");
    update(project.libraryFile, "libraryFile");
    update(project.libraryProfile, "libraryProfile");
    const auto updateExecutionString = [&values, &project](const char *inputKey, const char *storedKey) {
        const QString key = QString::fromLatin1(inputKey);
        if (values.contains(key))
            project.dftExecution.insert(QString::fromLatin1(storedKey), values.value(key).toString().trimmed());
    };
    updateExecutionString("constraintFile", "constraint_file");
    updateExecutionString("fileList", "filelist");
    updateExecutionString("workspacePath", "workspace_path");
    updateExecutionString("clockName", "clock");
    updateExecutionString("resetName", "reset");
    updateExecutionString("mapEffort", "map_effort");
    updateExecutionString("areaEffort", "area_effort");
    updateExecutionString("powerEffort", "power_effort");
    updateExecutionString("sourceLanguage", "language");
    updateExecutionString("synthesisOutputDir", "synthesis_output_dir");
    updateExecutionString("dftOutputDir", "dft_output_dir");
    updateExecutionString("dftTool", "dft_tool");
    updateExecutionString("tessentDofile", "tessent_dofile");
    updateExecutionString("agentPermissionMode", "agent_permission_mode");
    if (values.contains(QStringLiteral("agentPermissionMode")))
        project.dftExecution.insert(QStringLiteral("agent_permission_mode"),
                                    normalizedAgentPermissionMode(values.value(QStringLiteral("agentPermissionMode")).toString()));
    if (values.contains(QStringLiteral("useConstraintFile")))
        project.dftExecution.insert(QStringLiteral("use_constraint_file"), values.value(QStringLiteral("useConstraintFile")).toBool());
    if (values.contains(QStringLiteral("workspaceSuffixEnabled")))
        project.dftExecution.insert(
            QStringLiteral("workspace_suffix_enabled"),
            values.value(QStringLiteral("workspaceSuffixEnabled")).toBool()
        );
    if (values.contains(QStringLiteral("clockPeriodNs")))
        project.dftExecution.insert(QStringLiteral("clock_period_ns"), values.value(QStringLiteral("clockPeriodNs")).toDouble());
    if (values.contains(QStringLiteral("resetActiveState")))
        project.dftExecution.insert(QStringLiteral("reset_active_state"), values.value(QStringLiteral("resetActiveState")).toInt());
    if (values.contains(QStringLiteral("scanChainCount")))
        project.dftExecution.insert(QStringLiteral("scan_chain_count"), values.value(QStringLiteral("scanChainCount")).toInt());
    if (values.contains(QStringLiteral("maxChainLength")))
        project.dftExecution.insert(QStringLiteral("max_chain_length"), values.value(QStringLiteral("maxChainLength")).toInt());
    if (values.contains(QStringLiteral("timeoutSeconds")))
        project.dftExecution.insert(QStringLiteral("timeout_seconds"), values.value(QStringLiteral("timeoutSeconds")).toInt());
    if (values.contains(QStringLiteral("atpgTimeoutSeconds")))
        project.dftExecution.insert(QStringLiteral("atpg_timeout_seconds"), values.value(QStringLiteral("atpgTimeoutSeconds")).toInt());
    if (values.contains(QStringLiteral("iterationLimit")))
        project.dftExecution.insert(QStringLiteral("iteration_limit"), values.value(QStringLiteral("iterationLimit")).toInt());
    if (values.contains(QStringLiteral("patchReviewEnabled")))
        project.dftExecution.insert(QStringLiteral("patch_review_enabled"), values.value(QStringLiteral("patchReviewEnabled")).toBool());
    if (values.contains(QStringLiteral("agentTerminalEnabled"))
        || values.contains(QStringLiteral("agentTerminalMaximumSeconds"))) {
        QJsonObject terminal = project.dftExecution.value(QStringLiteral("agent_terminal")).toObject();
        if (values.contains(QStringLiteral("agentTerminalEnabled")))
            terminal.insert(QStringLiteral("enabled"), values.value(QStringLiteral("agentTerminalEnabled")).toBool());
        if (values.contains(QStringLiteral("agentTerminalMaximumSeconds")))
            terminal.insert(
                QStringLiteral("maximum_command_seconds"),
                values.value(QStringLiteral("agentTerminalMaximumSeconds")).toInt()
            );
        project.dftExecution.insert(QStringLiteral("agent_terminal"), terminal);
    }
    if (values.contains(QStringLiteral("drcAutofixEnabled")) || values.contains(QStringLiteral("drcAutofixTestModePort"))) {
        QJsonObject autofix = project.dftExecution.value(QStringLiteral("drc_autofix")).toObject();
        const bool enabled = values.contains(QStringLiteral("drcAutofixEnabled"))
            ? values.value(QStringLiteral("drcAutofixEnabled")).toBool()
            : autofix.value(QStringLiteral("mode")).toString() == QStringLiteral("clock_reset_set");
        autofix.insert(QStringLiteral("mode"), enabled ? QStringLiteral("clock_reset_set") : QStringLiteral("off"));
        if (values.contains(QStringLiteral("drcAutofixTestModePort"))) {
            const QString port = values.value(QStringLiteral("drcAutofixTestModePort")).toString().trimmed();
            if (port.isEmpty())
                autofix.remove(QStringLiteral("test_mode_port"));
            else
                autofix.insert(QStringLiteral("test_mode_port"), port);
        }
        project.dftExecution.insert(QStringLiteral("drc_autofix"), autofix);
    }
    if (values.contains(QStringLiteral("sourceFiles"))) {
        QJsonArray sourceFiles;
        for (const auto &item : values.value(QStringLiteral("sourceFiles")).toList()) {
            const QString path = item.toString().trimmed();
            if (!path.isEmpty())
                sourceFiles.append(path);
        }
        project.dftExecution.insert(QStringLiteral("source_files"), sourceFiles);
    }
    if (values.contains(QStringLiteral("additionalWorkspaceFolders"))) {
        QJsonArray workspaceFolders;
        QSet<QString> seenFolders;
        for (const auto &item : values.value(QStringLiteral("additionalWorkspaceFolders")).toList()) {
            const QString path = QDir::cleanPath(item.toString().trimmed());
            if (!path.isEmpty() && path != QStringLiteral(".") && !seenFolders.contains(path)) {
                seenFolders.insert(path);
                workspaceFolders.append(path);
            }
        }
        project.dftExecution.insert(QStringLiteral("additional_workspace_folders"), workspaceFolders);
    }
    if (values.contains(QStringLiteral("atpgCellModelFiles"))) {
        QJsonArray cellModels;
        for (const auto &item : values.value(QStringLiteral("atpgCellModelFiles")).toList()) {
            const QString path = item.toString().trimmed();
            if (!path.isEmpty())
                cellModels.append(path);
        }
        project.dftExecution.insert(QStringLiteral("atpg_cell_model_files"), cellModels);
    }
    if (values.contains(QStringLiteral("flowModules"))) {
        project.flowModules = QJsonObject::fromVariantMap(values.value(QStringLiteral("flowModules")).toMap());
    }
    if (values.contains(QStringLiteral("synthesisSettings"))) {
        project.dftExecution.insert(
            QStringLiteral("synthesis_settings"),
            QJsonObject::fromVariantMap(values.value(QStringLiteral("synthesisSettings")).toMap())
        );
    }
    if (values.contains(QStringLiteral("minimumCoverage")))
        project.minimumCoverage = values.value(QStringLiteral("minimumCoverage")).toDouble();
    if (values.contains(QStringLiteral("maximumDftDrcViolations")))
        project.maximumDftDrcViolations = values.value(QStringLiteral("maximumDftDrcViolations")).toInt();
    emit dataChanged(this->index(index), this->index(index));
    emit currentIndexChanged();
    return save();
}

bool ProjectModel::setProjectGoal(int index, const QString &goal) {
    return updateProject(index, {{QStringLiteral("goal"), goal}});
}

void ProjectModel::reload() {
    beginResetModel();
    load();
    endResetModel();
    emit currentIndexChanged();
}

bool ProjectModel::load() {
    QFile file(m_storagePath);
    if (!file.open(QIODevice::ReadOnly)) {
        emit errorOccurred(tr("无法读取项目注册表：%1").arg(m_storagePath));
        return false;
    }
    const auto document = QJsonDocument::fromJson(file.readAll());
    if (!document.isObject()) {
        emit errorOccurred(tr("项目注册表 JSON 格式无效。"));
        return false;
    }
    m_projects.clear();
    bool migratedPermissionMode = false;
    for (const auto value : document.object().value("projects").toArray()) {
        if (!value.isObject())
            continue;
        const QJsonObject object = value.toObject();
        const QString savedMode = object.value(QStringLiteral("metadata")).toObject()
                                      .value(QStringLiteral("dft_execution")).toObject()
                                      .value(QStringLiteral("agent_permission_mode")).toString();
        const QString normalizedMode = normalizedAgentPermissionMode(savedMode);
        if (!savedMode.isEmpty() && savedMode != normalizedMode)
            migratedPermissionMode = true;
        m_projects.append(projectFromJson(object));
    }
    m_currentIndex = qBound(0, m_currentIndex, qMax(0, m_projects.size() - 1));
    if (migratedPermissionMode)
        save();
    return true;
}

bool ProjectModel::save() {
    QDir().mkpath(QFileInfo(m_storagePath).absolutePath());
    QJsonArray projects;
    for (const auto &project : m_projects)
        projects.append(projectToJson(project));
    QSaveFile file(m_storagePath);
    if (!file.open(QIODevice::WriteOnly)) {
        emit errorOccurred(tr("无法写入项目注册表：%1").arg(m_storagePath));
        return false;
    }
    file.write(QJsonDocument(QJsonObject{{"version", 1}, {"projects", projects}}).toJson(QJsonDocument::Indented));
    if (!file.commit()) {
        emit errorOccurred(tr("保存项目注册表失败：%1").arg(m_storagePath));
        return false;
    }
    emit projectSaved();
    return true;
}

ProjectModel::Project ProjectModel::projectFromJson(const QJsonObject &object) {
    const auto metadata = object.value("metadata").toObject();
    QJsonObject dftExecution = metadata.value("dft_execution").toObject();
    if (dftExecution.contains(QStringLiteral("agent_permission_mode")))
        dftExecution.insert(QStringLiteral("agent_permission_mode"), normalizedAgentPermissionMode(
            dftExecution.value(QStringLiteral("agent_permission_mode")).toString()));
    QString kind = object.value("kind").toString();
    if (kind == QStringLiteral("external-rtl") || kind == QStringLiteral("rtl-dft"))
        kind = QStringLiteral("rtl");
    QJsonObject flowModules = metadata.value("flow_modules").toObject();
    if (flowModules.isEmpty() && (kind == QStringLiteral("rtl") || kind == QStringLiteral("rtl-dft"))) {
        flowModules = QJsonObject{
            {"synthesis", true}, {"dft", true}, {"scan", true},
            {"mbist", false}, {"atpg", false}, {"lbist", false},
        };
    }
    return {
        object.value("id").toString(), object.value("name").toString(), kind,
        object.value("root").toString(), object.value("rtl_root").toString(), object.value("top").toString(),
        object.value("goal").toString(), object.value("notes").toString(),
        [&object] {
            QStringList paths;
            for (const auto &value : object.value("related_documents").toArray()) {
                const QString path = value.toString().trimmed();
                if (!path.isEmpty() && !paths.contains(path))
                    paths.append(path);
            }
            return paths;
        }(),
        object.value("managed").toBool(),
        QString::fromLatin1(kFixedModelId), object.value("flow_profile").toString(),
        object.contains("minimum_coverage") ? object.value("minimum_coverage").toDouble() : -1.0,
        object.contains("maximum_dft_drc_violations") ? object.value("maximum_dft_drc_violations").toInt() : -1,
        object.value("library_dir").toString(), object.value("library_file").toString(), object.value("library_profile").toString(),
        dftExecution, flowModules
    };
}

QJsonObject ProjectModel::projectToJson(const Project &project) {
    QJsonObject metadata{{"model_name", project.modelName}};
    if (!project.dftExecution.isEmpty())
        metadata.insert(QStringLiteral("dft_execution"), project.dftExecution);
    if (!project.flowModules.isEmpty())
        metadata.insert(QStringLiteral("flow_modules"), project.flowModules);
    QJsonObject object{
        {"id", project.id}, {"name", project.name}, {"kind", project.kind}, {"root", project.root},
        {"goal", project.goal}, {"top", project.top}, {"rtl_root", project.rtlRoot}, {"notes", project.notes},
        {"managed", project.managed}, {"metadata", metadata},
        {"flow_profile", project.flowProfile}, {"library_dir", project.libraryDir},
        {"library_file", project.libraryFile}, {"library_profile", project.libraryProfile}
    };
    QJsonArray relatedDocuments;
    for (const auto &path : project.relatedDocuments)
        relatedDocuments.append(path);
    object.insert(QStringLiteral("related_documents"), relatedDocuments);
    if (project.minimumCoverage >= 0)
        object.insert(QStringLiteral("minimum_coverage"), project.minimumCoverage);
    if (project.maximumDftDrcViolations >= 0)
        object.insert(QStringLiteral("maximum_dft_drc_violations"), project.maximumDftDrcViolations);
    return object;
}

QString ProjectModel::generatedId(const QString &name) {
    QString id = name.toLower();
    id.replace(QRegularExpression("[^a-z0-9]+"), "_");
    id.remove(QRegularExpression("^_+|_+$"));
    return id.isEmpty() ? QStringLiteral("dft_project") : id.left(64);
}

QString ProjectModel::projectStatus(const Project &project) {
    return QFileInfo::exists(project.root) ? QStringLiteral("ready") : QStringLiteral("unavailable");
}

QVariantMap ProjectModel::toMap(const Project &project) const {
    return {
        {"id", project.id}, {"name", project.name}, {"kind", project.kind}, {"root", project.root},
        {"rtlRoot", project.rtlRoot}, {"top", project.top}, {"goal", project.goal}, {"notes", project.notes},
        {"managed", project.managed}, {"modelName", project.modelName}, {"flowProfile", project.flowProfile},
        {"minimumCoverage", project.minimumCoverage >= 0 ? QVariant(project.minimumCoverage) : QVariant()},
        {"maximumDftDrcViolations", project.maximumDftDrcViolations >= 0 ? QVariant(project.maximumDftDrcViolations) : QVariant()},
        {"libraryDir", project.libraryDir}, {"libraryFile", project.libraryFile}, {"libraryProfile", project.libraryProfile},
        {"agentPermissionMode", normalizedAgentPermissionMode(
            project.dftExecution.value(QStringLiteral("agent_permission_mode")).toString())},
        {"relatedDocuments", project.relatedDocuments},
        {"dftExecution", project.dftExecution.toVariantMap()},
        {"flowModules", project.flowModules.toVariantMap()},
        {"metadata", QVariantMap{
            {"model_name", project.modelName}, {"dft_execution", project.dftExecution.toVariantMap()},
            {"flow_modules", project.flowModules.toVariantMap()}
        }},
        {"status", projectStatus(project)}
    };
}
