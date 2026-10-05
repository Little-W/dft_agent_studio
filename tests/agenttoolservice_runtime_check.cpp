#include "agenttoolservice.h"
#include "sessioncatalog.h"

#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QSet>
#include <QTemporaryDir>
#include <QTimer>

#include <algorithm>
#include <cassert>
#include <cstdio>

namespace {
bool runtimeCheckFailed(int line) {
    std::fprintf(stderr, "Agent tool runtime check failed near line %d.\n", line);
    return false;
}

bool responsesSchemaIsStrict(const QVariantMap &schema) {
    if (schema.value(QStringLiteral("type")).toString() == QStringLiteral("object")) {
        const QVariantMap properties = schema.value(QStringLiteral("properties")).toMap();
        if (schema.value(QStringLiteral("additionalProperties")).toBool()
            || schema.value(QStringLiteral("required")).toList().size() != properties.size())
            return runtimeCheckFailed(__LINE__);
        for (auto it = properties.cbegin(); it != properties.cend(); ++it)
            if (!responsesSchemaIsStrict(it.value().toMap()))
                return runtimeCheckFailed(__LINE__);
    }
    const QVariant items = schema.value(QStringLiteral("items"));
    if (items.metaType().id() == QMetaType::QVariantMap
        && !responsesSchemaIsStrict(items.toMap()))
        return runtimeCheckFailed(__LINE__);
    for (const QVariant &value : schema.value(QStringLiteral("anyOf")).toList())
        if (!responsesSchemaIsStrict(value.toMap()))
            return runtimeCheckFailed(__LINE__);
    return true;
}
}

bool agentToolServiceRuntimeCheck() {
    QTemporaryDir temporary;
    if (!temporary.isValid())
        return runtimeCheckFailed(__LINE__);
    const QString projectRoot = temporary.path();
    const QString configRoot = QDir(temporary.path()).filePath(QStringLiteral("studio_data/config"));
    if (!QDir().mkpath(configRoot))
        return runtimeCheckFailed(__LINE__);
    const QString modelPath = QDir(projectRoot).filePath(QStringLiteral("cell_model.v"));
    QFile model(modelPath);
    if (!model.open(QIODevice::WriteOnly | QIODevice::Text))
        return runtimeCheckFailed(__LINE__);
    model.write("module cell_model; endmodule\n");
    model.close();
    const QString secondModelPath = QDir(projectRoot).filePath(QStringLiteral("second_cell_model.v"));
    QFile secondModel(secondModelPath);
    if (!secondModel.open(QIODevice::WriteOnly | QIODevice::Text))
        return runtimeCheckFailed(__LINE__);
    secondModel.write("module second_cell_model; endmodule\n");
    secondModel.close();
    const QJsonObject projectRow{{QStringLiteral("id"), QStringLiteral("tool-runtime-project")},
        {QStringLiteral("name"), QStringLiteral("Tool runtime project")},
        {QStringLiteral("root"), projectRoot},
        {QStringLiteral("metadata"), QJsonObject{{QStringLiteral("dft_execution"), QJsonObject{
            {QStringLiteral("dft_tool"), QStringLiteral("testmax")},
            {QStringLiteral("agent_permission_mode"), QStringLiteral("full_access")},
            {QStringLiteral("atpg_cell_model_files"), QJsonArray{QStringLiteral("old_cell_model.tcl")}}}}}}};
    const QJsonObject otherProjectRow{{QStringLiteral("id"), QStringLiteral("other-project")},
        {QStringLiteral("name"), QStringLiteral("Unselected project")},
        {QStringLiteral("root"), projectRoot},
        {QStringLiteral("metadata"), QJsonObject{{QStringLiteral("dft_execution"), QJsonObject{
            {QStringLiteral("reset_kind"), QStringLiteral("asynchronous")}}}}}};
    QFile projectsFile(QDir(configRoot).filePath(QStringLiteral("projects.json")));
    if (!projectsFile.open(QIODevice::WriteOnly | QIODevice::Text))
        return runtimeCheckFailed(__LINE__);
    projectsFile.write(QJsonDocument(QJsonObject{{QStringLiteral("version"), 1},
        {QStringLiteral("projects"), QJsonArray{projectRow, otherProjectRow}}}).toJson(QJsonDocument::Indented));
    projectsFile.close();

    QFile source(QDir(projectRoot).filePath(QStringLiteral("top.v")));
    if (!source.open(QIODevice::WriteOnly | QIODevice::Text))
        return runtimeCheckFailed(__LINE__);
    source.write("module top; endmodule\n");
    source.close();

    QVariantMap project{{QStringLiteral("id"), QStringLiteral("tool-runtime-project")},
                        {QStringLiteral("root"), projectRoot},
                        {QStringLiteral("dftExecution"), QVariantMap{
                            {QStringLiteral("agent_permission_mode"), QStringLiteral("full_access")}}}};
    const QVariantMap runtimeSessionCreated = SessionCatalog::dispatch(QStringLiteral("session_new"), project,
        {{QStringLiteral("name"), QStringLiteral("Native runtime fixture")}}, temporary.path());
    const QString runtimeSessionId = runtimeSessionCreated.value(QStringLiteral("result")).toMap()
        .value(QStringLiteral("session")).toMap().value(QStringLiteral("id")).toString();
    if (!runtimeSessionCreated.value(QStringLiteral("ok")).toBool() || runtimeSessionId.isEmpty())
        return runtimeCheckFailed(__LINE__);
    project.insert(QStringLiteral("session_id"), runtimeSessionId);
    const QVariantList catalog = AgentToolService::nativeToolCatalog(project);
    if (catalog.isEmpty())
        return runtimeCheckFailed(__LINE__);
    QVariantMap mbistDisabledProject = project;
    QVariantMap disabledMetadata;
    disabledMetadata.insert(QStringLiteral("flow_modules"), QVariantMap{{QStringLiteral("mbist"), false}});
    mbistDisabledProject.insert(QStringLiteral("metadata"), disabledMetadata);
    const QVariantList mbistDisabledCatalog = AgentToolService::nativeToolCatalog(mbistDisabledProject);
    bool mbistOverridesHidden = false;
    for (const QVariant &value : mbistDisabledCatalog) {
        const QVariantMap definition = value.toMap();
        if (definition.value(QStringLiteral("name")).toString() != QLatin1String("run_dft_iteration"))
            continue;
        const QVariantMap properties = definition.value(QStringLiteral("parameters")).toMap()
            .value(QStringLiteral("properties")).toMap();
        mbistOverridesHidden = !properties.contains(QStringLiteral("mbist_timeout_multiplier"))
            && !properties.contains(QStringLiteral("mbist_include_mode"))
            && !properties.contains(QStringLiteral("mbist_diagnostic_mode"));
    }
    if (!mbistOverridesHidden)
        return runtimeCheckFailed(__LINE__);
    QSet<QString> catalogNames;
    for (const QVariant &item : catalog) {
        const QVariantMap definition = item.toMap();
        catalogNames.insert(definition.value(QStringLiteral("name")).toString());
        const QVariantMap schema = definition.value(QStringLiteral("parameters")).toMap();
        if (definition.value(QStringLiteral("strict")).toBool() != true
            || !responsesSchemaIsStrict(schema)) {
            return runtimeCheckFailed(__LINE__);
        }
    }
    for (const QString &name : {QStringLiteral("shell"), QStringLiteral("read_file"),
                                QStringLiteral("search_project_text"), QStringLiteral("apply_patch"),
                                QStringLiteral("create_file"), QStringLiteral("run_dft_iteration"),
                                QStringLiteral("run_dft_optimization"), QStringLiteral("skills_list"),
                                QStringLiteral("skills_read"),
                                QStringLiteral("read_conversation_history"),
                                QStringLiteral("memory_remember_short_term"),
                                QStringLiteral("update_studio_project"),
                                QStringLiteral("save_run_report")}) {
        if (!catalogNames.contains(name)) {
            std::fprintf(stderr, "Missing tool in native catalog: %s\n", qPrintable(name));
            return runtimeCheckFailed(__LINE__);
        }
    }
    for (const QVariant &item : catalog) {
        const QVariantMap definition = item.toMap();
        if (definition.value(QStringLiteral("name")).toString() == QLatin1String("update_studio_project")) {
            const QVariantMap changes = definition.value(QStringLiteral("parameters")).toMap()
                .value(QStringLiteral("properties")).toMap().value(QStringLiteral("changes")).toMap();
            const QString description = changes.value(QStringLiteral("items")).toMap()
                .value(QStringLiteral("properties")).toMap().value(QStringLiteral("path")).toMap()
                .value(QStringLiteral("description")).toString();
            if (!definition.value(QStringLiteral("description")).toString().contains(QStringLiteral("dot-separated"))
                || !description.contains(QStringLiteral("metadata.dft_execution.atpg_cell_model_files")))
                return runtimeCheckFailed(__LINE__);
        }
        if (definition.value(QStringLiteral("name")).toString() == QLatin1String("shell")) {
            const QVariantMap commandSchema = definition.value(QStringLiteral("parameters")).toMap()
                .value(QStringLiteral("properties")).toMap().value(QStringLiteral("command")).toMap();
            const QVariantList alternatives = commandSchema.value(QStringLiteral("anyOf")).toList();
            if (alternatives.size() != 2
                || alternatives.at(0).toMap().value(QStringLiteral("type")).toString() != QLatin1String("string")
                || alternatives.at(1).toMap().value(QStringLiteral("type")).toString() != QLatin1String("array"))
                return runtimeCheckFailed(__LINE__);
        }
        if (definition.value(QStringLiteral("name")).toString() == QLatin1String("dft_report_parse_drc")) {
            const QString description = definition.value(QStringLiteral("description")).toString();
            const QVariantMap maximum = definition.value(QStringLiteral("parameters")).toMap()
                .value(QStringLiteral("properties")).toMap().value(QStringLiteral("maximum")).toMap();
            QString maximumDescription = maximum.value(QStringLiteral("description")).toString();
            for (const QVariant &alternative : maximum.value(QStringLiteral("anyOf")).toList())
                maximumDescription += alternative.toMap().value(QStringLiteral("description")).toString();
            if (!description.contains(QStringLiteral("not a DRC acceptance threshold"))
                || !maximumDescription.contains(QStringLiteral("not the allowed violation count")))
                return runtimeCheckFailed(__LINE__);
        }
        if (definition.value(QStringLiteral("name")).toString() == QLatin1String("run_dft_iteration")) {
            const QString iterationDescription = definition.value(QStringLiteral("description")).toString();
            if (!iterationDescription.contains(QStringLiteral("changed inputs"))
                || !iterationDescription.contains(QStringLiteral("null for optional settings"))
                || iterationDescription.contains(QStringLiteral("enum_state"))
                || iterationDescription.contains(QStringLiteral("source_preprocess_mode=cpp")))
                return runtimeCheckFailed(__LINE__);
            const QVariantMap repairMode = definition.value(QStringLiteral("parameters")).toMap()
                .value(QStringLiteral("properties")).toMap()
                .value(QStringLiteral("drc_repair_mode")).toMap();
            QVariantList modes = repairMode.value(QStringLiteral("enum")).toList();
            if (modes.isEmpty()) {
                for (const QVariant &alternative : repairMode.value(QStringLiteral("anyOf")).toList()) {
                    const QVariantMap schema = alternative.toMap();
                    if (schema.value(QStringLiteral("type")).toString() == QLatin1String("string")) {
                        modes = schema.value(QStringLiteral("enum")).toList();
                        break;
                    }
                }
            }
            if (!modes.contains(QStringLiteral("clock_only"))
                || !modes.contains(QStringLiteral("reset_set"))
                || !modes.contains(QStringLiteral("clock_reset_set"))) {
                std::fprintf(stderr, "run_dft_iteration repair mode schema: %s\n",
                    QJsonDocument::fromVariant(repairMode).toJson(QJsonDocument::Compact).constData());
                return runtimeCheckFailed(__LINE__);
            }
        }
        if (definition.value(QStringLiteral("name")).toString() != QLatin1String("analyze_dft_results"))
            continue;
        const QVariantMap scope = definition.value(QStringLiteral("parameters")).toMap()
            .value(QStringLiteral("properties")).toMap().value(QStringLiteral("scope")).toMap();
        const QByteArray scopeSchema = QJsonDocument::fromVariant(scope).toJson(QJsonDocument::Compact);
        if (!scopeSchema.contains("string") || scopeSchema.contains("enum"))
            return runtimeCheckFailed(__LINE__);
    }
    for (const QVariant &item : catalog) {
        const QVariantMap definition = item.toMap();
        if (definition.value(QStringLiteral("name")).toString() != QLatin1String("run_dft_iteration"))
            continue;
        const QVariantMap properties = definition.value(QStringLiteral("parameters")).toMap()
            .value(QStringLiteral("properties")).toMap();
        for (const QString &key : {QStringLiteral("source_annotation_mode"),
                 QStringLiteral("source_preprocess_mode"), QStringLiteral("source_dependency_mode"),
                 QStringLiteral("drc_repair_mode"), QStringLiteral("mbist_include_mode"),
                 QStringLiteral("mbist_diagnostic_mode"), QStringLiteral("atpg_diagnostic_mode"),
                 QStringLiteral("compile_strategy")}) {
            const QVariantMap schema = properties.value(key).toMap();
            QVariantList values = schema.value(QStringLiteral("enum")).toList();
            if (values.isEmpty()) {
                for (const QVariant &alternative : schema.value(QStringLiteral("anyOf")).toList()) {
                    const QVariantMap candidate = alternative.toMap();
                    if (candidate.value(QStringLiteral("type")).toString() == QLatin1String("string"))
                        values = candidate.value(QStringLiteral("enum")).toList();
                }
            }
            const bool hasExplicitEmptyString = std::any_of(values.cbegin(), values.cend(), [](const QVariant &value) {
                if (value.metaType().id() != QMetaType::QString)
                    return false;
                const QString text = value.toString();
                return text.isEmpty() && !text.isNull();
            });
            if (!hasExplicitEmptyString)
                return runtimeCheckFailed(__LINE__);
            if (key == QLatin1String("source_annotation_mode") && !values.contains(QStringLiteral("none")))
                return runtimeCheckFailed(__LINE__);
            if (key == QLatin1String("atpg_diagnostic_mode")
                && (!values.contains(QStringLiteral("full"))
                    || !QJsonDocument::fromVariant(schema).toJson(QJsonDocument::Compact)
                            .contains("ordinary ATPG")))
                return runtimeCheckFailed(__LINE__);
        }
        break;
    }

    QVariantList events;
    const QVariantMap initialized = AgentToolService::initializeToolRuntime(
        QStringLiteral("native-runtime-test"), project, temporary.path(), {},
        [&events](const QVariantMap &event) { events.append(event); });
    if (!initialized.value(QStringLiteral("ok")).toBool()
        || initialized.value(QStringLiteral("native_dispatch_boundary")).toList().size() != 0)
        return runtimeCheckFailed(__LINE__);
    const QVariantList toolDefinitions = AgentToolService::responsesToolDefinitions(
        QStringLiteral("native-runtime-test"));
    if (toolDefinitions.size() != catalog.size())
        return runtimeCheckFailed(__LINE__);
    QEventLoop readLoop;
    QTimer readTimeout;
    readTimeout.setSingleShot(true);
    QObject::connect(&readTimeout, &QTimer::timeout, &readLoop, &QEventLoop::quit);
    QVariantMap readResult;
    AgentToolService::dispatchToolAsync(QStringLiteral("native-runtime-test"), QStringLiteral("read_file"),
        {{QStringLiteral("path"), QStringLiteral("top.v")}}, QStringLiteral("read-call"),
        [&readResult, &readLoop](const QVariantMap &result) {
            readResult = result;
            QMetaObject::invokeMethod(&readLoop, &QEventLoop::quit, Qt::QueuedConnection);
        });
    readTimeout.start(5000);
    readLoop.exec();
    if (!readResult.value(QStringLiteral("ok")).toBool()
        || !readResult.value(QStringLiteral("result")).toMap().value(QStringLiteral("text")).toString()
                .contains(QStringLiteral("module top")))
        return runtimeCheckFailed(__LINE__);

    auto runTool = [](const QString &name, const QVariantMap &arguments, const QString &callId) {
        QEventLoop loop;
        QTimer timeout;
        timeout.setSingleShot(true);
        QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
        QVariantMap result;
        AgentToolService::dispatchToolAsync(QStringLiteral("native-runtime-test"), name, arguments, callId,
            [&result, &loop](const QVariantMap &value) {
                result = value;
                QMetaObject::invokeMethod(&loop, &QEventLoop::quit, Qt::QueuedConnection);
            });
        timeout.start(5000);
        loop.exec();
        return result;
    };
    const QVariantMap analysisWithoutJob = runTool(QStringLiteral("analyze_dft_results"), {},
        QStringLiteral("analysis-without-job"));
    if (analysisWithoutJob.value(QStringLiteral("ok")).toBool()
        || analysisWithoutJob.value(QStringLiteral("result")).toMap()
            .value(QStringLiteral("status")).toString() != QLatin1String("no_completed_result")
        || analysisWithoutJob.value(QStringLiteral("result")).toMap()
            .value(QStringLiteral("evidence_available")).toBool())
        return runtimeCheckFailed(__LINE__);
    const QVariantMap startedDft = runTool(QStringLiteral("run_dft_flow"), {},
        QStringLiteral("latest-dft-analysis-start"));
    const QString dftJobId = startedDft.value(QStringLiteral("result")).toMap()
        .value(QStringLiteral("job_id")).toString();
    if (!startedDft.value(QStringLiteral("ok")).toBool() || dftJobId.isEmpty())
        return runtimeCheckFailed(__LINE__);
    const QVariantMap finishedDft = runTool(QStringLiteral("wait_dft_job"), {
        {QStringLiteral("job_id"), dftJobId}, {QStringLiteral("wait_seconds"), 30}
    }, QStringLiteral("latest-dft-analysis-wait"));
    if (!finishedDft.value(QStringLiteral("ok")).toBool()
        || finishedDft.value(QStringLiteral("result")).toMap().value(QStringLiteral("state")).toString()
            == QLatin1String("running"))
        return runtimeCheckFailed(__LINE__);
    const QVariantMap latestDftAnalysis = runTool(QStringLiteral("analyze_dft_results"), {},
        QStringLiteral("latest-dft-analysis"));
    const QVariantMap latestDftSummary = latestDftAnalysis.value(QStringLiteral("result")).toMap();
    if (latestDftAnalysis.value(QStringLiteral("ok")).toBool()
        || latestDftSummary.value(QStringLiteral("job_id")).toString() != dftJobId
        || !latestDftSummary.value(QStringLiteral("evidence_available")).toBool()
        || latestDftSummary.value(QStringLiteral("errors")).toList().isEmpty()
        || latestDftSummary.value(QStringLiteral("status")).toString() == QStringLiteral("repeated_action_blocked"))
        return runtimeCheckFailed(__LINE__);
    const QVariantMap projectSearch = runTool(QStringLiteral("search_project_text"), {
        {QStringLiteral("query"), QStringLiteral("module top")},
        {QStringLiteral("path_prefix"), projectRoot},
        {QStringLiteral("max_results"), 10}
    }, QStringLiteral("search-project-directory"));
    const QVariantList searchMatches = projectSearch.value(QStringLiteral("result")).toMap()
        .value(QStringLiteral("matches")).toList();
    if (!projectSearch.value(QStringLiteral("ok")).toBool() || searchMatches.isEmpty()
        || !searchMatches.first().toMap().value(QStringLiteral("text")).toString()
            .contains(QStringLiteral("module top")))
        return runtimeCheckFailed(__LINE__);

    const QString skillFilePath = QDir(projectRoot).filePath(QStringLiteral("skills/runtime-skill/SKILL.md"));
    if (!QDir().mkpath(QFileInfo(skillFilePath).absolutePath()))
        return runtimeCheckFailed(__LINE__);
    QFile liveSkillFile(skillFilePath);
    if (!liveSkillFile.open(QIODevice::WriteOnly | QIODevice::Text)
        || liveSkillFile.write("---\nname: runtime-skill\ndescription: Live load test.\nauto_load: true\n---\nfirst version\n") < 0)
        return runtimeCheckFailed(__LINE__);
    liveSkillFile.close();
    const QVariantMap listedSkills = runTool(QStringLiteral("skills_list"), {}, QStringLiteral("skills-list-tool"));
    const QVariantMap listedSkillResult = listedSkills.value(QStringLiteral("result")).toMap()
        .value(QStringLiteral("result")).toMap();
    QString liveSkillId;
    for (const QVariant &value : listedSkillResult.value(QStringLiteral("skills")).toList()) {
        const QVariantMap skill = value.toMap();
        if (skill.value(QStringLiteral("name")).toString() == QStringLiteral("runtime-skill"))
            liveSkillId = skill.value(QStringLiteral("skill_id")).toString();
    }
    const QVariantMap firstSkillRead = runTool(QStringLiteral("skills_read"), {
        {QStringLiteral("skill_id"), liveSkillId}, {QStringLiteral("start_line"), 1},
        {QStringLiteral("max_lines"), 100}, {QStringLiteral("max_characters"), 8'000},
    }, QStringLiteral("skills-read-first"));
    const QVariantMap firstSkillResult = firstSkillRead.value(QStringLiteral("result")).toMap()
        .value(QStringLiteral("result")).toMap();
    if (liveSkillId.isEmpty() || !firstSkillRead.value(QStringLiteral("ok")).toBool()
        || !firstSkillResult.value(QStringLiteral("content")).toString().contains(QStringLiteral("first version"))) {
        std::fprintf(stderr, "Dynamic skill first read: skills=%s result=%s\n",
            QJsonDocument::fromVariant(listedSkills).toJson(QJsonDocument::Compact).constData(),
            QJsonDocument::fromVariant(firstSkillRead).toJson(QJsonDocument::Compact).constData());
        return runtimeCheckFailed(__LINE__);
    }
    if (!liveSkillFile.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)
        || liveSkillFile.write("---\nname: runtime-skill\ndescription: Live load test.\nauto_load: true\n---\nupdated version\n") < 0)
        return runtimeCheckFailed(__LINE__);
    liveSkillFile.close();
    const QVariantMap secondSkillRead = runTool(QStringLiteral("skills_read"), {
        {QStringLiteral("skill_id"), liveSkillId}, {QStringLiteral("start_line"), 1},
        {QStringLiteral("max_lines"), 100}, {QStringLiteral("max_characters"), 8'000},
    }, QStringLiteral("skills-read-second"));
    const QVariantMap secondSkillResult = secondSkillRead.value(QStringLiteral("result")).toMap()
        .value(QStringLiteral("result")).toMap();
    if (!secondSkillRead.value(QStringLiteral("ok")).toBool()
        || !secondSkillResult.value(QStringLiteral("content")).toString().contains(QStringLiteral("updated version"))) {
        std::fprintf(stderr, "Dynamic skill second read: %s\n",
            QJsonDocument::fromVariant(secondSkillRead).toJson(QJsonDocument::Compact).constData());
        return runtimeCheckFailed(__LINE__);
    }

    QJsonObject externalMetadata = projectRow.value(QStringLiteral("metadata")).toObject();
    QJsonObject externalExecution = externalMetadata.value(QStringLiteral("dft_execution")).toObject();
    externalExecution.insert(QStringLiteral("atpg_cell_model_files"), QJsonArray{modelPath});
    externalMetadata.insert(QStringLiteral("dft_execution"), externalExecution);
    QJsonObject externalProject = projectRow;
    externalProject.insert(QStringLiteral("metadata"), externalMetadata);
    if (!projectsFile.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
        return runtimeCheckFailed(__LINE__);
    projectsFile.write(QJsonDocument(QJsonObject{{QStringLiteral("version"), 1},
        {QStringLiteral("projects"), QJsonArray{externalProject}}}).toJson(QJsonDocument::Indented));
    projectsFile.close();
    const QVariantMap externallyUpdatedReadiness = runTool(QStringLiteral("check_dft_readiness"), {},
        QStringLiteral("readiness-after-external-project-update")).value(QStringLiteral("result")).toMap();
    if (externallyUpdatedReadiness.value(QStringLiteral("atpg_configuration")).toMap()
            .value(QStringLiteral("cell_model_files")).toStringList() != QStringList{modelPath})
        return runtimeCheckFailed(__LINE__);
    const QVariantMap slashProjectUpdate = runTool(QStringLiteral("update_studio_project"), {
        {QStringLiteral("project_id"), QStringLiteral("tool-runtime-project")},
        {QStringLiteral("reason"), QStringLiteral("invalid path format regression test")},
        {QStringLiteral("changes"), QVariantList{QVariantMap{
            {QStringLiteral("path"), QStringLiteral("metadata/atpg_cell_model_files")},
            {QStringLiteral("value"), QVariantList{secondModelPath}}}}}
    }, QStringLiteral("update-project-slash-path"));
    const QVariantMap slashProjectUpdateResult = slashProjectUpdate.value(QStringLiteral("result")).toMap();
    if (slashProjectUpdateResult.value(QStringLiteral("status")).toString() != QLatin1String("failed")
        || !slashProjectUpdateResult.value(QStringLiteral("error")).toString()
            .contains(QStringLiteral("metadata.dft_execution.atpg_cell_model_files")))
        return runtimeCheckFailed(__LINE__);

    const QVariantMap projectUpdate = runTool(QStringLiteral("update_studio_project"), {
        {QStringLiteral("project_id"), QStringLiteral("tool-runtime-project")},
        {QStringLiteral("reason"), QStringLiteral("runtime config synchronization regression test")},
        {QStringLiteral("changes"), QVariantList{QVariantMap{
            {QStringLiteral("path"), QStringLiteral("metadata.dft_execution.atpg_cell_model_files")},
            {QStringLiteral("value"), QVariantList{secondModelPath}}}}}
    }, QStringLiteral("update-project-tool-runtime"));
    if (!projectUpdate.value(QStringLiteral("ok")).toBool()) {
        std::fprintf(stderr, "project update failed: %s\n", QJsonDocument::fromVariant(projectUpdate)
            .toJson(QJsonDocument::Compact).constData());
        return runtimeCheckFailed(__LINE__);
    }
    const QVariantMap updatedReadiness = runTool(QStringLiteral("check_dft_readiness"), {},
        QStringLiteral("readiness-after-project-update")).value(QStringLiteral("result")).toMap();
    const QStringList readinessErrors = updatedReadiness.value(QStringLiteral("configuration_errors")).toStringList();
    if (updatedReadiness.value(QStringLiteral("atpg_configuration")).toMap()
            .value(QStringLiteral("cell_model_files")).toStringList() != QStringList{secondModelPath}
        || std::any_of(readinessErrors.cbegin(), readinessErrors.cend(), [](const QString &error) {
            return error.contains(QStringLiteral("old_cell_model.tcl"));
        })) {
        std::fprintf(stderr, "readiness did not observe project update: %s\n",
            QJsonDocument::fromVariant(updatedReadiness).toJson(QJsonDocument::Compact).constData());
        return runtimeCheckFailed(__LINE__);
    }
    const QVariantMap firstInspection = runTool(QStringLiteral("inspect_project"), {}, QStringLiteral("inspect-first"));
    const QVariantMap repeatedInspection = runTool(QStringLiteral("inspect_project"), {}, QStringLiteral("inspect-repeat"));
    if (!firstInspection.value(QStringLiteral("ok")).toBool()
        || repeatedInspection.value(QStringLiteral("result")).toMap().value(QStringLiteral("status")).toString()
            != QStringLiteral("repeated_action_blocked"))
        return runtimeCheckFailed(__LINE__);
    const QString checkpointRoot = QDir(temporary.path()).filePath(QStringLiteral("studio_data/agent_runtime/repair_sessions"));
    if (QDir(checkpointRoot).entryList(QDir::Dirs | QDir::NoDotAndDotDot).isEmpty())
        return runtimeCheckFailed(__LINE__);

    QEventLoop shellLoop;
    QTimer shellTimeout;
    shellTimeout.setSingleShot(true);
    QObject::connect(&shellTimeout, &QTimer::timeout, &shellLoop, &QEventLoop::quit);
    QVariantMap shellResult;
    AgentToolService::dispatchToolAsync(QStringLiteral("native-runtime-test"), QStringLiteral("shell"),
        {{QStringLiteral("command"), QVariantList{QStringLiteral("bash"), QStringLiteral("-lc"),
                                                   QStringLiteral("printf native-shell-ok")}},
         {QStringLiteral("cwd"), QStringLiteral(".")}}, QStringLiteral("shell-call"),
        [&shellResult, &shellLoop](const QVariantMap &result) {
            shellResult = result;
            QMetaObject::invokeMethod(&shellLoop, &QEventLoop::quit, Qt::QueuedConnection);
        });
    shellTimeout.start(5000);
    shellLoop.exec();
    if (!shellResult.value(QStringLiteral("ok")).toBool()
        || !shellResult.value(QStringLiteral("result")).toMap().value(QStringLiteral("stdout")).toString()
                .contains(QStringLiteral("native-shell-ok")))
        return runtimeCheckFailed(__LINE__);
    const QVariantMap shellStringCommand = runTool(QStringLiteral("shell"), {
        {QStringLiteral("command"), QStringLiteral("mkdir -p shell-string-dir && printf shell-string-ok > shell-string-dir/result.txt")},
        {QStringLiteral("cwd"), QStringLiteral(".")},
    }, QStringLiteral("shell-string-command-call"));
    QFile shellStringOutput(QDir(projectRoot).filePath(QStringLiteral("shell-string-dir/result.txt")));
    if (!shellStringCommand.value(QStringLiteral("ok")).toBool()
        || !shellStringOutput.open(QIODevice::ReadOnly)
        || !shellStringOutput.readAll().contains("shell-string-ok"))
        return runtimeCheckFailed(__LINE__);
    const QVariantMap shellSingleItemArray = runTool(QStringLiteral("shell"), {
        {QStringLiteral("command"), QVariantList{QStringLiteral("printf shell-single-array-ok && printf shell-array-two")}},
        {QStringLiteral("cwd"), QStringLiteral(".")},
    }, QStringLiteral("shell-single-item-array-call"));
    const QString shellSingleOutput = shellSingleItemArray.value(QStringLiteral("result")).toMap()
                                          .value(QStringLiteral("stdout")).toString();
    if (!shellSingleItemArray.value(QStringLiteral("ok")).toBool()
        || !shellSingleOutput.contains(QStringLiteral("shell-single-array-ok"))
        || !shellSingleOutput.contains(QStringLiteral("shell-array-two")))
        return runtimeCheckFailed(__LINE__);
    const QString shellLogPath = QDir(projectRoot).filePath(QStringLiteral("shell-from-root.log"));
    const QVariantMap shellFromRoot = runTool(QStringLiteral("shell"), {
        {QStringLiteral("command"), QVariantList{QStringLiteral("bash"), QStringLiteral("-lc"),
                                                  QStringLiteral("printf native-shell-root-ok")}},
        {QStringLiteral("cwd"), QStringLiteral("/")},
        {QStringLiteral("output_path"), shellLogPath},
    }, QStringLiteral("shell-root-cwd-call"));
    QFile shellLog(shellLogPath);
    if (!shellFromRoot.value(QStringLiteral("ok")).toBool() || !shellLog.open(QIODevice::ReadOnly)
        || !shellLog.readAll().contains("native-shell-root-ok"))
        return runtimeCheckFailed(__LINE__);
    const QString externalShellLogPath = QDir(temporary.path()).filePath(QStringLiteral("external-shell.log"));
    const QVariantMap shellExternalOutput = runTool(QStringLiteral("shell"), {
        {QStringLiteral("command"), QStringLiteral("printf native-shell-external-ok")},
        {QStringLiteral("cwd"), QStringLiteral("/")},
        {QStringLiteral("output_path"), externalShellLogPath},
    }, QStringLiteral("shell-external-output-call"));
    QFile externalShellLog(externalShellLogPath);
    if (!shellExternalOutput.value(QStringLiteral("ok")).toBool()
        || !externalShellLog.open(QIODevice::ReadOnly)
        || !externalShellLog.readAll().contains("native-shell-external-ok"))
        return runtimeCheckFailed(__LINE__);
    const QVariantMap shellDeviceOutput = runTool(QStringLiteral("shell"), {
        {QStringLiteral("command"), QStringLiteral("printf native-shell-device-ok")},
        {QStringLiteral("cwd"), QStringLiteral("/")},
        {QStringLiteral("output_path"), QStringLiteral("/dev/null")},
    }, QStringLiteral("shell-device-output-call"));
    const QString shellDeviceError = shellDeviceOutput.value(QStringLiteral("result")).toMap()
        .value(QStringLiteral("error")).toString();
    if (shellDeviceOutput.value(QStringLiteral("ok")).toBool()
        || !shellDeviceError.contains(QStringLiteral("Leave output_path empty")))
        return runtimeCheckFailed(__LINE__);
    const QString underscoreWorkspace = QDir(projectRoot).filePath(QStringLiteral("escaped_path_workspace"));
    if (!QDir().mkpath(underscoreWorkspace))
        return runtimeCheckFailed(__LINE__);
    QString modelEscapedWorkspace = underscoreWorkspace;
    modelEscapedWorkspace.replace(QStringLiteral("_"), QStringLiteral("\\_"));
    const QVariantMap escapedShellPath = runTool(QStringLiteral("shell"), {
        {QStringLiteral("command"), QVariantList{QStringLiteral("bash"), QStringLiteral("-lc"),
                                                  QStringLiteral("printf escaped-shell-path-ok")}},
        {QStringLiteral("cwd"), modelEscapedWorkspace},
    }, QStringLiteral("shell-escaped-path-call"));
    if (!escapedShellPath.value(QStringLiteral("ok")).toBool()
        || !escapedShellPath.value(QStringLiteral("result")).toMap()
                .value(QStringLiteral("stdout")).toString().contains(QStringLiteral("escaped-shell-path-ok")))
        return runtimeCheckFailed(__LINE__);
    bool shellEvidencePersisted = false;
    const QString checkpointSessions = QDir(temporary.path()).filePath(
        QStringLiteral("studio_data/agent_runtime/repair_sessions"));
    const QStringList sessionDirectories = QDir(checkpointSessions).entryList(
        QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &sessionDirectory : sessionDirectories) {
        QFile checkpoint(QDir(checkpointSessions).filePath(sessionDirectory + QStringLiteral("/repair-state.json")));
        if (!checkpoint.open(QIODevice::ReadOnly))
            continue;
        const QJsonObject evidence = QJsonDocument::fromJson(checkpoint.readAll()).object()
                                         .value(QStringLiteral("evidence")).toObject();
        for (const QJsonValue &entry : evidence) {
            if (entry.toObject().value(QStringLiteral("tool")).toString() == QStringLiteral("shell"))
                shellEvidencePersisted = true;
        }
    }
    if (!shellEvidencePersisted)
        return runtimeCheckFailed(__LINE__);

    const QString rtlSkillPath = QDir(projectRoot).filePath(QStringLiteral("skills/dft-rtl-editing/SKILL.md"));
    if (!QDir().mkpath(QFileInfo(rtlSkillPath).absolutePath()))
        return runtimeCheckFailed(__LINE__);
    QFile rtlSkillFile(rtlSkillPath);
    if (!rtlSkillFile.open(QIODevice::WriteOnly | QIODevice::Text)
        || rtlSkillFile.write("---\nname: dft-rtl-editing\ndescription: Evidence-based HDL edits.\nauto_load: true\n---\nConfirm RTL root cause before editing.\n") < 0)
        return runtimeCheckFailed(__LINE__);
    rtlSkillFile.close();
    const QString drcSkillPath = QDir(projectRoot).filePath(QStringLiteral("skills/dft-drc-fix/SKILL.md"));
    if (!QDir().mkpath(QFileInfo(drcSkillPath).absolutePath()))
        return runtimeCheckFailed(__LINE__);
    QFile drcSkillFile(drcSkillPath);
    if (!drcSkillFile.open(QIODevice::WriteOnly | QIODevice::Text)
        || drcSkillFile.write("---\nname: dft-drc-fix\ndescription: Diagnose DRC causes.\nauto_load: true\n---\nCheck report and configuration before editing.\n") < 0)
        return runtimeCheckFailed(__LINE__);
    drcSkillFile.close();
    const QVariantMap rtlSkillList = runTool(QStringLiteral("skills_list"), {}, QStringLiteral("rtl-skills-list"));
    const QVariantList availableSkills = rtlSkillList.value(QStringLiteral("result")).toMap()
        .value(QStringLiteral("result")).toMap().value(QStringLiteral("skills")).toList();
    QString rtlSkillId;
    QString drcSkillId;
    for (const QVariant &value : availableSkills) {
        const QVariantMap skill = value.toMap();
        if (skill.value(QStringLiteral("name")).toString() == QStringLiteral("dft-rtl-editing"))
            rtlSkillId = skill.value(QStringLiteral("skill_id")).toString();
        if (skill.value(QStringLiteral("name")).toString() == QStringLiteral("dft-drc-fix"))
            drcSkillId = skill.value(QStringLiteral("skill_id")).toString();
    }
    if (rtlSkillId.isEmpty() || drcSkillId.isEmpty())
        return runtimeCheckFailed(__LINE__);

    const QVariantMap shortMemoryStored = runTool(QStringLiteral("memory_remember_short_term"), {
        {QStringLiteral("category"), QStringLiteral("verified_repair")},
        {QStringLiteral("content_json"), QStringLiteral("{\"diagnosis\":\"temporary memory fixture\",\"validation\":\"passed\"}")},
        {QStringLiteral("evidence_file"), QStringLiteral("reports/check.rpt")},
        {QStringLiteral("status"), QStringLiteral("verified")},
        {QStringLiteral("valid_for_hours"), 24}}, QStringLiteral("short-memory-write"));
    if (!shortMemoryStored.value(QStringLiteral("ok")).toBool())
        return runtimeCheckFailed(__LINE__);
    const QVariantMap shortMemoryRecalled = runTool(QStringLiteral("memory_recall"), {
        {QStringLiteral("query"), QStringLiteral("temporary memory fixture")},
        {QStringLiteral("limit"), 8}}, QStringLiteral("short-memory-recall"));
    const QVariantList recalledMemories = shortMemoryRecalled.value(QStringLiteral("result")).toMap()
        .value(QStringLiteral("memories")).toList();
    if (!shortMemoryRecalled.value(QStringLiteral("ok")).toBool() || recalledMemories.isEmpty())
        return runtimeCheckFailed(__LINE__);

    const QString reportSkillPath = QDir(projectRoot).filePath(
        QStringLiteral("skills/dft-run-report/SKILL.md"));
    if (!QDir().mkpath(QFileInfo(reportSkillPath).absolutePath()))
        return runtimeCheckFailed(__LINE__);
    QFile reportSkillFile(reportSkillPath);
    if (!reportSkillFile.open(QIODevice::WriteOnly | QIODevice::Text)
        || reportSkillFile.write("---\nname: dft-run-report\ndescription: Write evidence-grounded DFT completion reports.\nauto_load: true\n---\nUse the active language and include tested hypotheses, edit paths, evidence, and measured results.\n") < 0)
        return runtimeCheckFailed(__LINE__);
    reportSkillFile.close();
    const QString generalReportSkillPath = QDir(projectRoot).filePath(
        QStringLiteral("skills/scientific-report-writing/SKILL.md"));
    if (!QDir().mkpath(QFileInfo(generalReportSkillPath).absolutePath()))
        return runtimeCheckFailed(__LINE__);
    QFile generalReportSkillFile(generalReportSkillPath);
    if (!generalReportSkillFile.open(QIODevice::WriteOnly | QIODevice::Text)
        || generalReportSkillFile.write("---\nname: scientific-report-writing\ndescription: General technical report writing.\nauto_load: true\n---\nStructure technical reports clearly.\n") < 0)
        return runtimeCheckFailed(__LINE__);
    generalReportSkillFile.close();
    if (!runTool(QStringLiteral("skills_list"), {}, QStringLiteral("report-skills-refresh"))
             .value(QStringLiteral("ok")).toBool())
        return runtimeCheckFailed(__LINE__);
    const QVariantMap reportArguments{
        {QStringLiteral("title"), QStringLiteral("Runtime report skill check")},
        {QStringLiteral("category"), QStringLiteral("run_report")},
        {QStringLiteral("markdown"), QStringLiteral("# Runtime report\n\nEvidence-backed result.\n")}};
    const QVariantMap reportBeforeSkill = runTool(QStringLiteral("save_run_report"), reportArguments,
        QStringLiteral("report-before-skill"));
    if (reportBeforeSkill.value(QStringLiteral("ok")).toBool()
        || reportBeforeSkill.value(QStringLiteral("result")).toMap().value(QStringLiteral("status"))
            != QStringLiteral("skill_required")
        || reportBeforeSkill.value(QStringLiteral("result")).toMap().value(QStringLiteral("skill_id"))
            != QStringLiteral("dft-run-report")
        || reportBeforeSkill.value(QStringLiteral("result")).toMap().value(QStringLiteral("blocked_tool"))
            != QStringLiteral("save_run_report")
        || reportBeforeSkill.value(QStringLiteral("result")).toMap()
            .value(QStringLiteral("retry_tool_after_skill")) != QStringLiteral("save_run_report")
        || !reportBeforeSkill.value(QStringLiteral("result")).toMap().value(QStringLiteral("message")).toString()
            .contains(QStringLiteral("Report not saved")))
        return runtimeCheckFailed(__LINE__);
    const QVariantMap reportSkillRead = runTool(QStringLiteral("skills_read"), {
        {QStringLiteral("skill_id"), QStringLiteral("dft-run-report")},
        {QStringLiteral("start_line"), 1}, {QStringLiteral("max_lines"), 100},
        {QStringLiteral("max_characters"), 8'000}}, QStringLiteral("report-skill-read"));
    if (!reportSkillRead.value(QStringLiteral("ok")).toBool())
        return runtimeCheckFailed(__LINE__);
    const QVariantMap reportAfterSkill = runTool(QStringLiteral("save_run_report"), reportArguments,
        QStringLiteral("report-after-skill"));
    if (reportAfterSkill.value(QStringLiteral("result")).toMap().value(QStringLiteral("status"))
            == QStringLiteral("skill_required"))
        return runtimeCheckFailed(__LINE__);
    const QVariantMap savedRuntimeReport = reportAfterSkill.value(QStringLiteral("result")).toMap();
    const QVariantMap savedRuntimeEvidence = savedRuntimeReport.value(QStringLiteral("execution_evidence")).toMap();
    QFile savedRuntimeReportFile(savedRuntimeReport.value(QStringLiteral("path")).toString());
    const QString savedRuntimeReportBody = savedRuntimeReportFile.open(QIODevice::ReadOnly)
        ? QString::fromUtf8(savedRuntimeReportFile.readAll()) : QString{};
    if (!reportAfterSkill.value(QStringLiteral("ok")).toBool()
        || savedRuntimeEvidence.value(QStringLiteral("execution_count")).toInt() != 1
        || !savedRuntimeEvidence.value(QStringLiteral("current_turn_run")).toBool()
        || !savedRuntimeReportBody.contains(QStringLiteral("本回合记录到 `1` 次 DFT 执行")))
        return runtimeCheckFailed(__LINE__);

    const QString preflightRuntimeId = QStringLiteral("native-runtime-skill-preflight");
    if (!AgentToolService::initializeToolRuntime(preflightRuntimeId, project, projectRoot)
             .value(QStringLiteral("ok")).toBool())
        return runtimeCheckFailed(__LINE__);
    QEventLoop preflightLoop;
    QTimer preflightTimeout;
    preflightTimeout.setSingleShot(true);
    QObject::connect(&preflightTimeout, &QTimer::timeout, &preflightLoop, &QEventLoop::quit);
    QVariantMap preflightResult;
    AgentToolService::dispatchToolAsync(preflightRuntimeId, QStringLiteral("create_file"),
        {{QStringLiteral("path"), QStringLiteral("preflight.v")},
         {QStringLiteral("content"), QStringLiteral("module preflight; endmodule\n")},
         {QStringLiteral("purpose"), QStringLiteral("runtime initialization skill preflight")}},
        QStringLiteral("skill-preflight-call"), [&preflightResult, &preflightLoop](const QVariantMap &result) {
            preflightResult = result;
            QMetaObject::invokeMethod(&preflightLoop, &QEventLoop::quit, Qt::QueuedConnection);
        });
    preflightTimeout.start(5000);
    preflightLoop.exec();
    if (preflightResult.value(QStringLiteral("ok")).toBool()
        || preflightResult.value(QStringLiteral("result")).toMap().value(QStringLiteral("status")).toString()
            != QStringLiteral("skill_required")
        || QFileInfo::exists(QDir(projectRoot).filePath(QStringLiteral("preflight.v"))))
        return runtimeCheckFailed(__LINE__);

    QVariantMap updateArguments{{QStringLiteral("project_id"), QStringLiteral("tool-runtime-project")},
        {QStringLiteral("reason"), QStringLiteral("runtime skill preflight check")},
        {QStringLiteral("changes"), QVariantList{QVariantMap{
            {QStringLiteral("path"), QStringLiteral("metadata.dft_execution.reset_kind")},
            {QStringLiteral("value"), QStringLiteral("synchronous")}}, QVariantMap{
            {QStringLiteral("path"), QStringLiteral("metadata.dft_execution.language")},
            {QStringLiteral("value"), QStringLiteral("verilog")}}}}};
    QVariantMap crossProjectUpdate = updateArguments;
    crossProjectUpdate.insert(QStringLiteral("project_id"), QStringLiteral("other-project"));
    crossProjectUpdate.insert(QStringLiteral("changes"), QVariantList{QVariantMap{
        {QStringLiteral("path"), QStringLiteral("metadata.dft_execution.scan_chain_count")},
        {QStringLiteral("value"), 2}}});
    const QVariantMap blockedCrossProjectUpdate = runTool(QStringLiteral("update_studio_project"),
        crossProjectUpdate, QStringLiteral("project-update-outside-session"));
    if (blockedCrossProjectUpdate.value(QStringLiteral("ok")).toBool()
        || blockedCrossProjectUpdate.value(QStringLiteral("result")).toMap()
            .value(QStringLiteral("status")).toString() != QStringLiteral("failed")
        || !blockedCrossProjectUpdate.value(QStringLiteral("result")).toMap()
            .value(QStringLiteral("error")).toString().contains(QStringLiteral("only its selected project")))
        return runtimeCheckFailed(__LINE__);
    const QVariantMap updateBeforeSkill = runTool(QStringLiteral("update_studio_project"), updateArguments,
        QStringLiteral("project-update-before-skill"));
    if (updateBeforeSkill.value(QStringLiteral("ok")).toBool()
        || updateBeforeSkill.value(QStringLiteral("result")).toMap().value(QStringLiteral("status")).toString()
            != QStringLiteral("skill_required")) {
        std::fprintf(stderr, "DFT skill gate result: %s\n",
            QJsonDocument::fromVariant(updateBeforeSkill).toJson(QJsonDocument::Compact).constData());
        return runtimeCheckFailed(__LINE__);
    }
    const QVariantMap shellBeforeDiagnosis = runTool(QStringLiteral("shell"), {
        {QStringLiteral("command"), QStringLiteral("sed -i 's/module top/module top/' top.v")},
        {QStringLiteral("cwd"), projectRoot}}, QStringLiteral("shell-rtl-edit-before-drc-skill"));
    if (shellBeforeDiagnosis.value(QStringLiteral("ok")).toBool()
        || shellBeforeDiagnosis.value(QStringLiteral("result")).toMap().value(QStringLiteral("status")).toString()
            != QStringLiteral("skill_required")
        || shellBeforeDiagnosis.value(QStringLiteral("result")).toMap().value(QStringLiteral("skill_id")).toString()
            != QStringLiteral("dft-drc-fix"))
        return runtimeCheckFailed(__LINE__);
    QEventLoop editLoop;
    QTimer editTimeout;
    editTimeout.setSingleShot(true);
    QObject::connect(&editTimeout, &QTimer::timeout, &editLoop, &QEventLoop::quit);
    QVariantMap editResult;
    AgentToolService::dispatchToolAsync(QStringLiteral("native-runtime-test"), QStringLiteral("create_file"),
        {{QStringLiteral("path"), QStringLiteral("generated.v")},
         {QStringLiteral("content"), QStringLiteral("module generated; endmodule\n")},
         {QStringLiteral("purpose"), QStringLiteral("runtime native file edit check")}},
        QStringLiteral("edit-call"), [&editResult, &editLoop](const QVariantMap &result) {
            editResult = result;
            QMetaObject::invokeMethod(&editLoop, &QEventLoop::quit, Qt::QueuedConnection);
        });
    editTimeout.start(5000);
    editLoop.exec();
    if (editResult.value(QStringLiteral("ok")).toBool()
        || editResult.value(QStringLiteral("result")).toMap().value(QStringLiteral("status")).toString()
            != QStringLiteral("skill_required")
        || editResult.value(QStringLiteral("result")).toMap().value(QStringLiteral("skill_id")).toString()
            != QStringLiteral("dft-drc-fix")
        || editResult.value(QStringLiteral("result")).toMap().value(QStringLiteral("ok")).toBool()
        || editResult.value(QStringLiteral("result")).toMap().value(QStringLiteral("applied")).toBool()
        || !editResult.value(QStringLiteral("result")).toMap().value(QStringLiteral("message")).toString()
            .contains(QStringLiteral("Required skill: `dft-drc-fix`"))
        || QFileInfo::exists(QDir(projectRoot).filePath(QStringLiteral("generated.v"))))
        return runtimeCheckFailed(__LINE__);

    const QVariantMap drcSkillRead = runTool(QStringLiteral("skills_read"), {
        {QStringLiteral("skill_id"), drcSkillId}, {QStringLiteral("start_line"), 1},
        {QStringLiteral("max_lines"), 100}, {QStringLiteral("max_characters"), 8'000},
    }, QStringLiteral("drc-skill-read"));
    if (!drcSkillRead.value(QStringLiteral("ok")).toBool()
        || !runTool(QStringLiteral("update_studio_project"), updateArguments,
            QStringLiteral("project-update-after-skill")).value(QStringLiteral("ok")).toBool())
        return runtimeCheckFailed(__LINE__);
    const QVariantMap shellBeforeRtlSkill = runTool(QStringLiteral("shell"), {
        {QStringLiteral("command"), QStringLiteral("sed -i 's/module top/module top/' top.v")},
        {QStringLiteral("cwd"), projectRoot}}, QStringLiteral("shell-rtl-edit-before-rtl-skill"));
    if (shellBeforeRtlSkill.value(QStringLiteral("ok")).toBool()
        || shellBeforeRtlSkill.value(QStringLiteral("result")).toMap().value(QStringLiteral("status")).toString()
            != QStringLiteral("skill_required")
        || shellBeforeRtlSkill.value(QStringLiteral("result")).toMap().value(QStringLiteral("skill_id")).toString()
            != QStringLiteral("dft-rtl-editing"))
        return runtimeCheckFailed(__LINE__);

    const QVariantMap rtlEditAttemptAfterDiagnosis = runTool(QStringLiteral("create_file"), {
        {QStringLiteral("path"), QStringLiteral("generated.v")},
        {QStringLiteral("content"), QStringLiteral("module generated; endmodule\n")},
        {QStringLiteral("purpose"), QStringLiteral("runtime native file edit check")}},
        QStringLiteral("edit-call-after-drc-skill"));
    if (rtlEditAttemptAfterDiagnosis.value(QStringLiteral("ok")).toBool()
        || rtlEditAttemptAfterDiagnosis.value(QStringLiteral("result")).toMap()
            .value(QStringLiteral("status")).toString() != QStringLiteral("skill_required")
        || rtlEditAttemptAfterDiagnosis.value(QStringLiteral("result")).toMap()
            .value(QStringLiteral("skill_id")).toString() != QStringLiteral("dft-rtl-editing"))
        return runtimeCheckFailed(__LINE__);

    const QVariantMap rtlSkillRead = runTool(QStringLiteral("skills_read"), {
        {QStringLiteral("skill_id"), rtlSkillId}, {QStringLiteral("start_line"), 1},
        {QStringLiteral("max_lines"), 100}, {QStringLiteral("max_characters"), 8'000},
    }, QStringLiteral("rtl-skill-read"));
    if (!rtlSkillRead.value(QStringLiteral("ok")).toBool())
        return runtimeCheckFailed(__LINE__);
    const QVariantMap shellAfterRtlSkill = runTool(QStringLiteral("shell"), {
        {QStringLiteral("command"), QStringLiteral("sed -i 's/module top/module top/' top.v")},
        {QStringLiteral("cwd"), projectRoot}}, QStringLiteral("shell-rtl-edit-after-skills"));
    if (!shellAfterRtlSkill.value(QStringLiteral("ok")).toBool()
        || !shellAfterRtlSkill.value(QStringLiteral("result")).toMap().value(QStringLiteral("stdout")).toString().isEmpty())
        return runtimeCheckFailed(__LINE__);

    const QVariantMap existingRtlPatch = runTool(QStringLiteral("apply_patch"), {
        {QStringLiteral("files"), QVariantList{QStringLiteral("top.v")}},
        {QStringLiteral("purpose"), QStringLiteral("Fix an RTL defect confirmed by the current DRC report and source review")},
        {QStringLiteral("patch"), QStringLiteral(
            "--- a/top.v\n+++ b/top.v\n@@ -1 +1 @@\n"
            "-module top; endmodule\n+module top; wire rtl_fix; endmodule\n")},
    }, QStringLiteral("edit-existing-rtl-after-skill"));
    QFile editedRtl(QDir(projectRoot).filePath(QStringLiteral("top.v")));
    if (!existingRtlPatch.value(QStringLiteral("ok")).toBool()
        || !editedRtl.open(QIODevice::ReadOnly)
        || !editedRtl.readAll().contains("wire rtl_fix")) {
        std::fprintf(stderr, "Existing RTL patch result: %s\n",
            QJsonDocument::fromVariant(existingRtlPatch).toJson(QJsonDocument::Compact).constData());
        return runtimeCheckFailed(__LINE__);
    }

    const QVariantMap refreshedRtlRead = runTool(QStringLiteral("read_file"), {
        {QStringLiteral("path"), QStringLiteral("top.v")},
        {QStringLiteral("start_line"), 1}, {QStringLiteral("max_lines"), 20},
        {QStringLiteral("max_characters"), 2'000}}, QStringLiteral("read-rtl-before-stale-patch"));
    if (!refreshedRtlRead.value(QStringLiteral("ok")).toBool())
        return runtimeCheckFailed(__LINE__);
    const QVariantMap staleContextPatch = runTool(QStringLiteral("apply_patch"), {
        {QStringLiteral("files"), QVariantList{QStringLiteral("top.v")}},
        {QStringLiteral("purpose"), QStringLiteral("Exercise stale patch context recovery")},
        {QStringLiteral("patch"), QStringLiteral(
            "--- a/top.v\n+++ b/top.v\n@@ -1 +1 @@\n"
            "-module top; stale_context; endmodule\n+module top; refreshed_context; endmodule\n")}},
        QStringLiteral("stale-rtl-patch-context"));
    const QVariantMap staleContextResult = staleContextPatch.value(QStringLiteral("result")).toMap();
    if (staleContextPatch.value(QStringLiteral("ok")).toBool()
        || staleContextResult.value(QStringLiteral("status")).toString() != QStringLiteral("needs_source_read")
        || QFileInfo(staleContextResult.value(QStringLiteral("path")).toString()).canonicalFilePath()
            != QFileInfo(QDir(projectRoot).filePath(QStringLiteral("top.v"))).canonicalFilePath()
        || staleContextResult.contains(QStringLiteral("next_tool"))
        || staleContextResult.value(QStringLiteral("start_line")).toInt() != 1
        || !staleContextResult.value(QStringLiteral("reason")).toString().contains(QStringLiteral("本次没有修改文件"))) {
        std::fprintf(stderr, "Stale patch result: %s\n",
            QJsonDocument::fromVariant(staleContextPatch).toJson(QJsonDocument::Compact).constData());
        return runtimeCheckFailed(__LINE__);
    }

    editResult.clear();
    QEventLoop secondEditLoop;
    QTimer secondEditTimeout;
    secondEditTimeout.setSingleShot(true);
    QObject::connect(&secondEditTimeout, &QTimer::timeout, &secondEditLoop, &QEventLoop::quit);
    AgentToolService::dispatchToolAsync(QStringLiteral("native-runtime-test"), QStringLiteral("create_file"),
        {{QStringLiteral("path"), QStringLiteral("generated.v")},
         {QStringLiteral("content"), QStringLiteral("module generated; endmodule\n")},
         {QStringLiteral("purpose"), QStringLiteral("runtime native file edit check")}},
        QStringLiteral("edit-call-after-skill"), [&editResult, &secondEditLoop](const QVariantMap &result) {
            editResult = result;
            QMetaObject::invokeMethod(&secondEditLoop, &QEventLoop::quit, Qt::QueuedConnection);
        });
    secondEditTimeout.start(5000);
    secondEditLoop.exec();
    QFile generated(QDir(projectRoot).filePath(QStringLiteral("generated.v")));
    if (!editResult.value(QStringLiteral("ok")).toBool() || !generated.open(QIODevice::ReadOnly)
        || !generated.readAll().contains("module generated"))
        return runtimeCheckFailed(__LINE__);
    if (!AgentToolService::completedToolResults(QStringLiteral("native-runtime-test")).contains(
            readResult.value(QStringLiteral("request_id")).toString()))
        return runtimeCheckFailed(__LINE__);

    QTemporaryDir external;
    if (!external.isValid())
        return runtimeCheckFailed(__LINE__);
    QFile externalFile(QDir(external.path()).filePath(QStringLiteral("outside.txt")));
    if (!externalFile.open(QIODevice::WriteOnly | QIODevice::Text))
        return runtimeCheckFailed(__LINE__);
    externalFile.write("approved external read\n");
    externalFile.close();
    QVariantList readOnlyDefinitions;
    for (const QVariant &definitionValue : catalog) {
        const QVariantMap definition = definitionValue.toMap();
        if (definition.value(QStringLiteral("name")).toString() == QStringLiteral("read_file"))
            readOnlyDefinitions.append(definition);
    }
    QVariantMap restrictedProject = project;
    restrictedProject.insert(QStringLiteral("agentPermissionMode"), QStringLiteral("on_request"));
    restrictedProject.insert(QStringLiteral("session_id"), QStringLiteral("runtime-approval-test"));
    const QVariantMap restrictedInit = AgentToolService::initializeToolRuntime(
        QStringLiteral("native-runtime-path-approval"), restrictedProject, readOnlyDefinitions,
        temporary.path(), [&events](const QVariantMap &event) { events.append(event); });
    if (!restrictedInit.value(QStringLiteral("ok")).toBool())
        return runtimeCheckFailed(__LINE__);
    QEventLoop pathApprovalLoop;
    QTimer pathApprovalTimeout;
    pathApprovalTimeout.setSingleShot(true);
    QObject::connect(&pathApprovalTimeout, &QTimer::timeout, &pathApprovalLoop, &QEventLoop::quit);
    QVariantMap pathReadResult;
    AgentToolService::dispatchToolAsync(QStringLiteral("native-runtime-path-approval"), QStringLiteral("read_file"),
        {{QStringLiteral("path"), externalFile.fileName()}}, QStringLiteral("outside-read-call"),
        [&pathReadResult, &pathApprovalLoop](const QVariantMap &result) {
            pathReadResult = result;
            QMetaObject::invokeMethod(&pathApprovalLoop, &QEventLoop::quit, Qt::QueuedConnection);
        });
    QEventLoop pathPendingLoop;
    QTimer::singleShot(100, &pathPendingLoop, &QEventLoop::quit);
    pathPendingLoop.exec();
    if (!pathReadResult.isEmpty()) {
        return runtimeCheckFailed(__LINE__);
    }
    const QVariantList pathPending = AgentToolService::pendingToolRequests(
        QStringLiteral("native-runtime-path-approval"));
    if (pathPending.size() != 1
        || !pathPending.first().toMap().value(QStringLiteral("reason")).toString().contains(externalFile.fileName()))
        return runtimeCheckFailed(__LINE__);
    const QString pathRequestId = pathPending.first().toMap().value(QStringLiteral("id")).toString();
    const QVariantMap pathAudit = AgentToolService::dispatch(QStringLiteral("path_permission_request_read"),
        restrictedProject, {{QStringLiteral("thread_id"), QStringLiteral("runtime-approval-test")},
                            {QStringLiteral("request_id"), pathRequestId}}, temporary.path());
    if (!pathAudit.value(QStringLiteral("ok")).toBool()
        || !pathAudit.value(QStringLiteral("result")).toMap().value(QStringLiteral("found")).toBool())
        return runtimeCheckFailed(__LINE__);
    if (!AgentToolService::decideTool(QStringLiteral("native-runtime-path-approval"), pathRequestId, true)
             .value(QStringLiteral("ok")).toBool())
        return runtimeCheckFailed(__LINE__);
    pathApprovalTimeout.start(5000);
    pathApprovalLoop.exec();
    if (!pathReadResult.value(QStringLiteral("ok")).toBool()
        || !pathReadResult.value(QStringLiteral("result")).toMap().value(QStringLiteral("text")).toString()
                .contains(QStringLiteral("approved external read")))
        return runtimeCheckFailed(__LINE__);
    const QVariantMap approvedAudit = AgentToolService::dispatch(QStringLiteral("path_permission_request_read"),
        restrictedProject, {{QStringLiteral("thread_id"), QStringLiteral("runtime-approval-test")},
                            {QStringLiteral("request_id"), pathRequestId}}, temporary.path());
    if (approvedAudit.value(QStringLiteral("result")).toMap().value(QStringLiteral("record")).toMap()
            .value(QStringLiteral("decision")).toString() != QStringLiteral("approved"))
        return runtimeCheckFailed(__LINE__);

    QVariantMap reviewProject = project;
    reviewProject.insert(QStringLiteral("metadata"), QVariantMap{
        {QStringLiteral("dft_execution"), QVariantMap{{QStringLiteral("patch_review_enabled"), true}}}});
    if (!AgentToolService::initializeToolRuntime(QStringLiteral("native-runtime-patch-review"), reviewProject,
            temporary.path()).value(QStringLiteral("ok")).toBool())
        return runtimeCheckFailed(__LINE__);
    QEventLoop patchReviewLoop;
    QTimer patchReviewTimeout;
    patchReviewTimeout.setSingleShot(true);
    QObject::connect(&patchReviewTimeout, &QTimer::timeout, &patchReviewLoop, &QEventLoop::quit);
    QVariantMap patchReviewResult;
    AgentToolService::dispatchToolAsync(QStringLiteral("native-runtime-patch-review"), QStringLiteral("create_file"),
        {{QStringLiteral("path"), QStringLiteral("reviewed.txt")},
         {QStringLiteral("content"), QStringLiteral("reviewed\n")},
         {QStringLiteral("purpose"), QStringLiteral("patch review gate check")}},
        QStringLiteral("patch-review-call"), [&patchReviewResult, &patchReviewLoop](const QVariantMap &result) {
            patchReviewResult = result;
            QMetaObject::invokeMethod(&patchReviewLoop, &QEventLoop::quit, Qt::QueuedConnection);
        });
    QEventLoop patchPendingLoop;
    QTimer::singleShot(100, &patchPendingLoop, &QEventLoop::quit);
    patchPendingLoop.exec();
    const QVariantList patchPending = AgentToolService::pendingToolRequests(
        QStringLiteral("native-runtime-patch-review"));
    const QString reviewedPath = QDir(projectRoot).filePath(QStringLiteral("reviewed.txt"));
    if (!patchReviewResult.isEmpty() || patchPending.size() != 1 || QFileInfo::exists(reviewedPath))
        return runtimeCheckFailed(__LINE__);
    const QString patchRequestId = patchPending.first().toMap().value(QStringLiteral("id")).toString();
    if (!AgentToolService::decideTool(QStringLiteral("native-runtime-patch-review"), patchRequestId, true)
             .value(QStringLiteral("ok")).toBool())
        return runtimeCheckFailed(__LINE__);
    patchReviewTimeout.start(5000);
    patchReviewLoop.exec();
    if (!patchReviewResult.value(QStringLiteral("ok")).toBool() || !QFileInfo::exists(reviewedPath))
        return runtimeCheckFailed(__LINE__);

    const QVariantList gatedDefinitions{QVariantMap{
        {QStringLiteral("type"), QStringLiteral("function")},
        {QStringLiteral("name"), QStringLiteral("agent_sleep")},
        {QStringLiteral("description"), QStringLiteral("approval fixture")},
        {QStringLiteral("parameters"), QVariantMap{
            {QStringLiteral("type"), QStringLiteral("object")},
            {QStringLiteral("properties"), QVariantMap{
                {QStringLiteral("seconds"), QVariantMap{{QStringLiteral("type"), QStringLiteral("integer")}}},
                {QStringLiteral("reason"), QVariantMap{{QStringLiteral("type"), QStringLiteral("string")}}}}},
            {QStringLiteral("required"), QVariantList{QStringLiteral("seconds"), QStringLiteral("reason")}},
            {QStringLiteral("additionalProperties"), false}}},
        {QStringLiteral("risk"), QStringLiteral("execute")},
        {QStringLiteral("requires_approval"), true}}};
    const QVariantMap gatedInit = AgentToolService::initializeToolRuntime(
        QStringLiteral("native-runtime-gated"), project, gatedDefinitions, temporary.path(),
        [&events](const QVariantMap &event) { events.append(event); });
    if (!gatedInit.value(QStringLiteral("ok")).toBool())
        return runtimeCheckFailed(__LINE__);

    QEventLoop approvalLoop;
    QTimer approvalTimeout;
    approvalTimeout.setSingleShot(true);
    QObject::connect(&approvalTimeout, &QTimer::timeout, &approvalLoop, &QEventLoop::quit);
    QVariantMap approvedResult;
    AgentToolService::dispatchToolAsync(QStringLiteral("native-runtime-gated"), QStringLiteral("agent_sleep"),
        {{QStringLiteral("seconds"), 1}, {QStringLiteral("reason"), QStringLiteral("approval test")}},
        QStringLiteral("approval-call"), [&approvedResult, &approvalLoop](const QVariantMap &result) {
            approvedResult = result;
            QMetaObject::invokeMethod(&approvalLoop, &QEventLoop::quit, Qt::QueuedConnection);
        });
    QEventLoop pendingLoop;
    QTimer::singleShot(100, &pendingLoop, &QEventLoop::quit);
    pendingLoop.exec();
    if (!approvedResult.isEmpty())
        return runtimeCheckFailed(__LINE__);
    const QVariantList pending = AgentToolService::pendingToolRequests(QStringLiteral("native-runtime-gated"));
    if (pending.size() != 1)
        return runtimeCheckFailed(__LINE__);
    const QString approvalId = pending.first().toMap().value(QStringLiteral("id")).toString();
    if (!AgentToolService::decideTool(QStringLiteral("native-runtime-gated"), approvalId, true)
             .value(QStringLiteral("ok")).toBool())
        return runtimeCheckFailed(__LINE__);
    approvalTimeout.start(5000);
    approvalLoop.exec();
    if (!approvedResult.value(QStringLiteral("ok")).toBool()
        || approvedResult.value(QStringLiteral("result")).toMap().value(QStringLiteral("slept")).toBool() != true)
        return runtimeCheckFailed(__LINE__);

    QEventLoop deniedLoop;
    QTimer deniedTimeout;
    deniedTimeout.setSingleShot(true);
    QObject::connect(&deniedTimeout, &QTimer::timeout, &deniedLoop, &QEventLoop::quit);
    QVariantMap deniedResult;
    AgentToolService::dispatchToolAsync(QStringLiteral("native-runtime-gated"), QStringLiteral("agent_sleep"),
        {{QStringLiteral("seconds"), 1}, {QStringLiteral("reason"), QStringLiteral("deny test")}},
        QStringLiteral("deny-call"), [&deniedResult, &deniedLoop](const QVariantMap &result) {
            deniedResult = result;
            QMetaObject::invokeMethod(&deniedLoop, &QEventLoop::quit, Qt::QueuedConnection);
        });
    QEventLoop deniedPendingLoop;
    QTimer::singleShot(100, &deniedPendingLoop, &QEventLoop::quit);
    deniedPendingLoop.exec();
    const QVariantList deniedPending = AgentToolService::pendingToolRequests(QStringLiteral("native-runtime-gated"));
    if (deniedPending.size() != 1)
        return runtimeCheckFailed(__LINE__);
    const QString deniedId = deniedPending.first().toMap().value(QStringLiteral("id")).toString();
    if (!AgentToolService::decideTool(QStringLiteral("native-runtime-gated"), deniedId, false)
             .value(QStringLiteral("ok")).toBool())
        return runtimeCheckFailed(__LINE__);
    deniedTimeout.start(1000);
    deniedLoop.exec();
    if (deniedResult.value(QStringLiteral("ok")).toBool()
        || deniedResult.value(QStringLiteral("result")).toMap().value(QStringLiteral("status")).toString()
                != QStringLiteral("approval_rejected"))
        return runtimeCheckFailed(__LINE__);

    const QString latestWorkspace = latestDftSummary.value(QStringLiteral("execution")).toMap()
        .value(QStringLiteral("workspace")).toString();
    const QString latestStagePath = latestWorkspace.isEmpty()
        ? QString{} : QDir(latestWorkspace).filePath(QStringLiteral("stage.json"));
    if (!QFileInfo(latestStagePath).isFile()) {
        const QVariantMap permittedRtlPatchWithoutStage = runTool(QStringLiteral("apply_patch"), {
            {QStringLiteral("files"), QVariantList{QStringLiteral("top.v")}},
            {QStringLiteral("purpose"), QStringLiteral("Preflight failure has no staged RTL run evidence")},
            {QStringLiteral("patch"), QStringLiteral(
                "--- a/top.v\n+++ b/top.v\n@@ -1 +1 @@\n"
                "-module top; wire rtl_fix; endmodule\n+module top; wire rtl_fix, evidence_checked; endmodule\n")}},
            QStringLiteral("latest-dft-rtl-edit-after-preflight-failure"));
        if (!permittedRtlPatchWithoutStage.value(QStringLiteral("ok")).toBool())
            return runtimeCheckFailed(__LINE__);
    } else {
    const QVariantMap latestRtlRead = runTool(QStringLiteral("read_file"), {
        {QStringLiteral("path"), QStringLiteral("top.v")},
        {QStringLiteral("start_line"), 1}, {QStringLiteral("max_lines"), 20},
        {QStringLiteral("max_characters"), 2'000}}, QStringLiteral("latest-dft-rtl-source-read"));
    if (!latestRtlRead.value(QStringLiteral("ok")).toBool())
        return runtimeCheckFailed(__LINE__);
    const QVariantMap prematureRtlPatch = runTool(QStringLiteral("apply_patch"), {
        {QStringLiteral("files"), QVariantList{QStringLiteral("top.v")}},
        {QStringLiteral("purpose"), QStringLiteral("Test fresh DFT evidence gate")},
        {QStringLiteral("patch"), QStringLiteral(
            "--- a/top.v\n+++ b/top.v\n@@ -1 +1 @@\n"
            "-module top; wire rtl_fix; endmodule\n+module top; wire rtl_fix, evidence_checked; endmodule\n")}},
        QStringLiteral("latest-dft-rtl-edit-before-config-evidence"));
    const QVariantMap prematurePatchResult = prematureRtlPatch.value(QStringLiteral("result")).toMap();
    if (!latestRtlRead.value(QStringLiteral("ok")).toBool()
        || prematureRtlPatch.value(QStringLiteral("ok")).toBool()
        || prematurePatchResult.value(QStringLiteral("status")).toString() != QStringLiteral("evidence_required")
        || !prematurePatchResult.value(QStringLiteral("path")).toString().endsWith(QStringLiteral("stage.json"))) {
        std::fprintf(stderr, "Latest DFT evidence gate: %s\n",
            QJsonDocument::fromVariant(QVariantMap{{QStringLiteral("latest_read"), latestRtlRead},
                {QStringLiteral("patch"), prematureRtlPatch}}).toJson(QJsonDocument::Compact).constData());
        return runtimeCheckFailed(__LINE__);
    }
    const QVariantMap latestStageRead = runTool(QStringLiteral("read_file"), {
        {QStringLiteral("path"), prematurePatchResult.value(QStringLiteral("path"))},
        {QStringLiteral("start_line"), 1}, {QStringLiteral("max_lines"), 200},
        {QStringLiteral("max_characters"), 8'000}}, QStringLiteral("latest-dft-stage-read"));
    const QVariantMap prematureRtlPatchAfterStage = runTool(QStringLiteral("apply_patch"), {
        {QStringLiteral("files"), QVariantList{QStringLiteral("top.v")}},
        {QStringLiteral("purpose"), QStringLiteral("Test generated script evidence gate")},
        {QStringLiteral("patch"), QStringLiteral(
            "--- a/top.v\n+++ b/top.v\n@@ -1 +1 @@\n"
            "-module top; wire rtl_fix; endmodule\n+module top; wire rtl_fix, evidence_checked; endmodule\n")}},
        QStringLiteral("latest-dft-rtl-edit-before-script-evidence"));
    const QVariantMap scriptGateResult = prematureRtlPatchAfterStage.value(QStringLiteral("result")).toMap();
    if (!latestStageRead.value(QStringLiteral("ok")).toBool()
        || prematureRtlPatchAfterStage.value(QStringLiteral("ok")).toBool()
        || scriptGateResult.value(QStringLiteral("status")).toString() != QStringLiteral("evidence_required")
        || !scriptGateResult.value(QStringLiteral("path")).toString().endsWith(QStringLiteral(".tcl")))
        return runtimeCheckFailed(__LINE__);
    const QVariantMap latestDriverRead = runTool(QStringLiteral("read_file"), {
        {QStringLiteral("path"), scriptGateResult.value(QStringLiteral("path"))},
        {QStringLiteral("start_line"), 1}, {QStringLiteral("max_lines"), 120},
        {QStringLiteral("max_characters"), 8'000}}, QStringLiteral("latest-dft-driver-read"));
    const QVariantMap parserCompatibilityPatch = runTool(QStringLiteral("apply_patch"), {
        {QStringLiteral("files"), QVariantList{QStringLiteral("top.v")}},
        {QStringLiteral("purpose"), QStringLiteral("Test that a diagnosed parser compatibility edit is not hard-coded away")},
        {QStringLiteral("patch"), QStringLiteral(
            "--- a/top.v\n+++ b/top.v\n@@ -1 +1 @@\n"
            "-module top; // synopsys enum_state\n+module top;\n")}},
        QStringLiteral("tool-compatibility-pragma-removal"));
    if (!latestDriverRead.value(QStringLiteral("ok")).toBool()
        || !parserCompatibilityPatch.value(QStringLiteral("ok")).toBool()) {
        std::fprintf(stderr, "Parser-compatibility edit: %s\n",
            QJsonDocument::fromVariant(parserCompatibilityPatch).toJson(QJsonDocument::Compact).constData());
        return runtimeCheckFailed(__LINE__);
    }
    const QVariantMap permittedRtlPatch = runTool(QStringLiteral("apply_patch"), {
        {QStringLiteral("files"), QVariantList{QStringLiteral("top.v")}},
        {QStringLiteral("purpose"), QStringLiteral("Test edit proceeds after current-run evidence review")},
        {QStringLiteral("patch"), QStringLiteral(
            "--- a/top.v\n+++ b/top.v\n@@ -1 +1 @@\n"
            "-module top; wire rtl_fix; endmodule\n+module top; wire rtl_fix, evidence_checked; endmodule\n")}},
        QStringLiteral("latest-dft-rtl-edit-after-config-evidence"));
    if (!latestDriverRead.value(QStringLiteral("ok")).toBool()
        || !permittedRtlPatch.value(QStringLiteral("ok")).toBool())
        return runtimeCheckFailed(__LINE__);
    }

    bool sawApproval = false;
    bool sawDecision = false;
    bool sawResult = false;
    for (const QVariant &eventValue : events) {
        const QString type = eventValue.toMap().value(QStringLiteral("event")).toString();
        sawApproval |= type == QStringLiteral("tool_approval_requested");
        sawDecision |= type == QStringLiteral("tool_approval_decided");
        sawResult |= type == QStringLiteral("tool_result");
    }
    const bool cleanedMain = AgentToolService::closeToolRuntime(QStringLiteral("native-runtime-test"));
    const bool cleanedGated = AgentToolService::closeToolRuntime(QStringLiteral("native-runtime-gated"));
    const bool cleanedPath = AgentToolService::closeToolRuntime(QStringLiteral("native-runtime-path-approval"));
    const bool cleanedReview = AgentToolService::closeToolRuntime(QStringLiteral("native-runtime-patch-review"));
    return cleanedMain && cleanedGated && cleanedPath && cleanedReview && sawApproval && sawDecision && sawResult;
}
