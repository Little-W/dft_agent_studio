#include "projectconfigimporter.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>

namespace {
constexpr qsizetype MaximumTclLines = 100'000;
constexpr qsizetype MaximumCommands = 320;
constexpr qsizetype MaximumCommandCharacters = 36'000;

QJsonObject typeSchema(const QString &type) {
    return {{QStringLiteral("type"), type}};
}

QJsonObject objectSchema(const QJsonObject &properties, const QJsonArray &required = {}) {
    return {
        {QStringLiteral("type"), QStringLiteral("object")},
        {QStringLiteral("properties"), properties},
        {QStringLiteral("required"), required},
        {QStringLiteral("additionalProperties"), false},
    };
}

QJsonObject arraySchema(const QJsonObject &itemSchema, int maximum = -1, int minimum = -1) {
    QJsonObject schema{
        {QStringLiteral("type"), QStringLiteral("array")},
        {QStringLiteral("items"), itemSchema},
    };
    if (minimum >= 0)
        schema.insert(QStringLiteral("minItems"), minimum);
    if (maximum >= 0)
        schema.insert(QStringLiteral("maxItems"), maximum);
    return schema;
}

QJsonObject enumSchema(const QStringList &values) {
    QJsonArray choices;
    for (const auto &value : values)
        choices.append(value);
    QJsonObject schema = typeSchema(QStringLiteral("string"));
    schema.insert(QStringLiteral("enum"), choices);
    return schema;
}

QJsonObject integerEnumSchema(const QJsonArray &values) {
    return {{QStringLiteral("type"), QStringLiteral("integer")},
            {QStringLiteral("enum"), values}};
}

QJsonObject numberSchema() {
    return typeSchema(QStringLiteral("number"));
}

QJsonObject stringArraySchema(int maximum = 24) {
    return arraySchema(typeSchema(QStringLiteral("string")), maximum);
}

QJsonObject settingsSchema(const QString &stage) {
    const QJsonObject clock = objectSchema({
        {"name", typeSchema("string")}, {"source", typeSchema("string")}, {"period", numberSchema()},
        {"rise", numberSchema()}, {"fall", numberSchema()}, {"setup_uncertainty", numberSchema()},
        {"hold_uncertainty", numberSchema()}, {"transition", numberSchema()},
        {"source_latency", numberSchema()}, {"network_latency", numberSchema()},
    }, {"name", "source", "period", "rise", "fall", "setup_uncertainty", "hold_uncertainty",
        "transition", "source_latency", "network_latency"});
    const QJsonObject generatedClock = objectSchema({
        {"name", typeSchema("string")}, {"target", typeSchema("string")}, {"source", typeSchema("string")},
        {"master", typeSchema("string")}, {"divide_by", numberSchema()}, {"multiply_by", numberSchema()},
        {"duty_cycle", numberSchema()}, {"invert", typeSchema("boolean")},
    }, {"name", "target", "source", "master", "divide_by", "multiply_by", "duty_cycle", "invert"});
    const QJsonObject ioDelay = objectSchema({
        {"direction", enumSchema({"input", "output"})}, {"ports", typeSchema("string")},
        {"clock", typeSchema("string")}, {"max", numberSchema()}, {"min", numberSchema()},
    }, {"direction", "ports", "clock", "max", "min"});
    const QJsonObject exception = objectSchema({
        {"type", enumSchema({"false_path", "multicycle", "max_delay", "min_delay"})},
        {"from", typeSchema("string")}, {"through", typeSchema("string")}, {"to", typeSchema("string")},
        {"value", numberSchema()}, {"check", enumSchema({"setup", "hold"})},
    }, {"type", "from", "through", "to", "value", "check"});

    if (stage == QStringLiteral("dft")) {
        return objectSchema({
            {"dft_enabled", typeSchema("boolean")}, {"scan_enabled", typeSchema("boolean")},
            {"mbist_enabled", typeSchema("boolean")}, {"atpg_enabled", typeSchema("boolean")},
            {"lbist_enabled", typeSchema("boolean")}, {"scan_clock", typeSchema("string")},
            {"reset", typeSchema("string")}, {"reset_active_state", integerEnumSchema({0, 1})},
            {"scan_chain_count", typeSchema("integer")}, {"max_chain_length", typeSchema("integer")},
            {"drc_autofix_enabled", typeSchema("boolean")}, {"drc_autofix_test_mode_port", typeSchema("string")},
            {"atpg_cell_model_files", stringArraySchema(32)}, {"atpg_timeout_seconds", typeSchema("integer")},
        });
    }
    return objectSchema({
        {"library_dir", typeSchema("string")}, {"target_library", typeSchema("string")},
        {"library_profile", typeSchema("string")}, {"constraint_file", typeSchema("string")},
        {"use_constraint_file", typeSchema("boolean")}, {"constraint_files", stringArraySchema()},
        {"pre_scripts", stringArraySchema()}, {"post_scripts", stringArraySchema()},
        {"clocks", arraySchema(clock, 16)}, {"generated_clocks", arraySchema(generatedClock, 16)},
        {"io_delays", arraySchema(ioDelay, 32)}, {"timing_exceptions", arraySchema(exception, 64)},
        {"additional_tcl_commands", stringArraySchema(64)}, {"clock_groups_tcl", typeSchema("string")},
        {"operating_condition", typeSchema("string")}, {"min_library", typeSchema("string")},
        {"max_cores", typeSchema("integer")}, {"compile_command", enumSchema({"compile", "compile_ultra"})},
        {"map_effort", enumSchema({"low", "medium", "high"})},
        {"area_effort", enumSchema({"none", "low", "medium", "high"})},
        {"power_effort", enumSchema({"none", "low", "medium", "high"})},
        {"incremental", typeSchema("boolean")}, {"retime", typeSchema("boolean")},
        {"gate_clock", typeSchema("boolean")}, {"scan_ready", typeSchema("boolean")},
        {"boundary_optimization", typeSchema("boolean")},
        {"auto_ungroup", enumSchema({"none", "area", "delay", "all"})},
        {"max_transition", typeSchema("string")}, {"max_fanout", typeSchema("string")},
        {"max_capacitance", typeSchema("string")}, {"driving_cell", typeSchema("string")},
        {"output_load", typeSchema("string")},
        {"reports", arraySchema(enumSchema({"qor", "timing", "area", "power", "constraints", "resources"}), 6)},
        {"reports", arraySchema(enumSchema({"qor", "timing", "area", "power", "constraints", "resources"}), 6)},
    });
}

QJsonObject argumentSchema(const QString &stage) {
    return objectSchema({
        {"stage", enumSchema({stage})},
        {"settings", settingsSchema(stage)},
        {"confidence", enumSchema({"high", "medium", "low"})},
        {"evidence", arraySchema(typeSchema("string"), 24, 1)},
        {"warnings", arraySchema(typeSchema("string"), 12)},
    }, {"stage", "settings", "confidence", "evidence", "warnings"});
}

bool hasExpression(const QJsonValue &value) {
    if (value.isString()) {
        const QString text = value.toString();
        return text.contains('$') || text.contains('[') || text.contains(']');
    }
    if (value.isArray()) {
        for (const auto &item : value.toArray()) {
            if (hasExpression(item))
                return true;
        }
    }
    if (value.isObject()) {
        const QJsonObject object = value.toObject();
        for (auto it = object.begin(); it != object.end(); ++it) {
            if (hasExpression(it.value()))
                return true;
        }
    }
    return false;
}

QString optionValue(const QString &command, const QString &option) {
    const QRegularExpression expression(QStringLiteral("(?:^|\\s)-%1\\s+([^\\s]+)").arg(QRegularExpression::escape(option)),
                                        QRegularExpression::CaseInsensitiveOption);
    const auto match = expression.match(command);
    return match.hasMatch() ? match.captured(1).trimmed().remove('{').remove('}').remove('"') : QString{};
}

QString firstCollection(const QString &command, const QString &collection) {
    const QRegularExpression expression(QStringLiteral("\\[%1\\s+([^\\]]+)\\]").arg(QRegularExpression::escape(collection)),
                                        QRegularExpression::CaseInsensitiveOption);
    const auto match = expression.match(command);
    return match.hasMatch() ? match.captured(1).trimmed().remove('{').remove('}').remove('"') : QString{};
}

QStringList markersForField(const QString &stage, const QString &field) {
    static const QHash<QString, QStringList> synthesis{
        {"library_dir", {"search_path"}}, {"target_library", {"target_library"}},
        {"constraint_file", {"read_sdc"}}, {"use_constraint_file", {"read_sdc"}},
        {"constraint_files", {"source"}}, {"pre_scripts", {"source"}}, {"post_scripts", {"source"}},
        {"clock_groups_tcl", {"set_clock_groups"}}, {"operating_condition", {"set_operating_conditions"}},
        {"min_library", {"set_min_library"}}, {"max_cores", {"set_host_options"}},
        {"compile_command", {"compile"}}, {"map_effort", {"map_effort"}},
        {"area_effort", {"area_effort"}}, {"power_effort", {"power_effort"}},
        {"incremental", {"-incremental"}}, {"retime", {"-retime"}}, {"gate_clock", {"-gate_clock"}},
        {"scan_ready", {"-scan"}}, {"boundary_optimization", {"boundary_optimization"}},
        {"auto_ungroup", {"ungroup"}}, {"max_transition", {"set_max_transition"}},
        {"max_fanout", {"set_max_fanout"}}, {"max_capacitance", {"set_max_capacitance"}},
        {"driving_cell", {"set_driving_cell"}}, {"output_load", {"set_load"}},
        {"clocks", {"create_clock", "set_clock_"}},
        {"generated_clocks", {"create_generated_clock"}},
        {"io_delays", {"set_input_delay", "set_output_delay"}},
        {"timing_exceptions", {"set_false_path", "set_multicycle_path", "set_max_delay", "set_min_delay"}},
        {"reports", {"report_"}}, {"additional_tcl_commands", {"set_"}},
    };
    static const QHash<QString, QStringList> dft{
        {"dft_enabled", {"dft"}}, {"scan_enabled", {"scan"}}, {"mbist_enabled", {"mbist"}},
        {"atpg_enabled", {"atpg"}}, {"lbist_enabled", {"lbist"}},
        {"scan_clock", {"set_dft_signal"}}, {"reset", {"set_dft_signal"}},
        {"reset_active_state", {"set_dft_signal"}}, {"scan_chain_count", {"set_scan_configuration"}},
        {"max_chain_length", {"set_scan_configuration"}}, {"drc_autofix_enabled", {"autofix"}},
        {"drc_autofix_test_mode_port", {"autofix"}}, {"atpg_cell_model_files", {"read_netlist"}},
        {"atpg_timeout_seconds", {"timeout"}},
    };
    return stage == QStringLiteral("dft") ? dft.value(field) : synthesis.value(field);
}

QStringList selectedCommands(const QString &stage) {
    if (stage == QStringLiteral("dft"))
        return {QStringLiteral("source"), QStringLiteral("read_netlist"), QStringLiteral("set_dft_configuration"),
                QStringLiteral("set_dft_signal"), QStringLiteral("set_autofix_configuration"),
                QStringLiteral("set_scan_configuration"), QStringLiteral("set_scan_compression_configuration"),
                QStringLiteral("set_dft_insertion_configuration"), QStringLiteral("create_test_protocol"),
                QStringLiteral("dft_drc"), QStringLiteral("preview_dft"), QStringLiteral("insert_dft"),
                QStringLiteral("report_dft"), QStringLiteral("report_scan_path"), QStringLiteral("set_atpg"),
                QStringLiteral("set_faults"), QStringLiteral("set_patterns"), QStringLiteral("set_test_mode"),
                QStringLiteral("set_mbist"), QStringLiteral("set_memory"), QStringLiteral("set_jtag"),
                QStringLiteral("add_faults"), QStringLiteral("create_patterns"), QStringLiteral("run_atpg"),
                QStringLiteral("write_patterns")};
    return {QStringLiteral("analyze"), QStringLiteral("elaborate"), QStringLiteral("link"), QStringLiteral("uniquify"),
            QStringLiteral("source"), QStringLiteral("create_clock"), QStringLiteral("create_generated_clock"),
            QStringLiteral("set_clock_uncertainty"), QStringLiteral("set_clock_latency"),
            QStringLiteral("set_clock_transition"), QStringLiteral("set_clock_groups"), QStringLiteral("set_input_delay"),
            QStringLiteral("set_output_delay"), QStringLiteral("set_false_path"), QStringLiteral("set_multicycle_path"),
            QStringLiteral("set_max_delay"), QStringLiteral("set_min_delay"), QStringLiteral("set_max_transition"),
            QStringLiteral("set_max_fanout"), QStringLiteral("set_max_capacitance"), QStringLiteral("set_ideal_network"),
            QStringLiteral("set_max_area"), QStringLiteral("set_fix_multiple_port_nets"), QStringLiteral("set_cost_priority"),
            QStringLiteral("set_case_analysis"), QStringLiteral("set_disable_timing"), QStringLiteral("set_dont_use"),
            QStringLiteral("set_operating_conditions"), QStringLiteral("set_min_library"), QStringLiteral("set_host_options"),
            QStringLiteral("set_driving_cell"), QStringLiteral("set_load"), QStringLiteral("set_clock_gating_style"),
            QStringLiteral("set_svf"), QStringLiteral("set_map_effort"), QStringLiteral("set_area_effort"),
            QStringLiteral("set_power_effort"), QStringLiteral("set_ungroup"), QStringLiteral("compile"),
            QStringLiteral("compile_ultra"), QStringLiteral("compile_exploration"), QStringLiteral("report_qor"),
            QStringLiteral("report_timing"), QStringLiteral("report_area"), QStringLiteral("report_power"),
            QStringLiteral("report_constraint"), QStringLiteral("report_resources"), QStringLiteral("write"),
            QStringLiteral("write_file"), QStringLiteral("target_library"), QStringLiteral("link_library"),
            QStringLiteral("search_path")};
}

QJsonArray readCommands(const QString &path, const QString &stage, QString *error) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        *error = QStringLiteral("无法读取 Tcl/SDC 文件：%1").arg(file.errorString());
        return {};
    }
    const QStringList allowed = selectedCommands(stage);
    const QRegularExpression commandPattern(QStringLiteral("\\b(?:%1)\\b").arg(
        [&allowed] {
            QStringList escaped;
            for (const auto &command : allowed)
                escaped.append(QRegularExpression::escape(command));
            return escaped.join('|');
        }()), QRegularExpression::CaseInsensitiveOption);
    const QString text = QString::fromUtf8(file.readAll());
    const QStringList lines = text.split('\n');
    QJsonArray commands;
    QString pending;
    qsizetype startLine = 0;
    qsizetype totalCharacters = 0;
    for (qsizetype index = 0; index < lines.size() && index < MaximumTclLines; ++index) {
        QString line = lines.at(index).trimmed();
        if (pending.isEmpty() && (line.isEmpty() || line.startsWith('#')))
            continue;
        if (pending.isEmpty())
            startLine = index + 1;
        const bool continued = line.endsWith('\\');
        if (continued)
            line.chop(1);
        if (!pending.isEmpty())
            pending += ' ';
        pending += line.trimmed();
        if (continued)
            continue;
        const QString command = pending.trimmed();
        pending.clear();
        if (command.isEmpty() || !commandPattern.match(command).hasMatch())
            continue;
        if (commands.size() >= MaximumCommands || totalCharacters + command.size() > MaximumCommandCharacters)
            break;
        commands.append(QJsonObject{{QStringLiteral("label"), QStringLiteral("L%1").arg(startLine)},
                                    {QStringLiteral("command"), command}});
        totalCharacters += command.size();
    }
    if (commands.isEmpty())
        *error = QStringLiteral("脚本中没有找到可导入的 %1 指令。")
            .arg(stage == QStringLiteral("dft") ? QStringLiteral("DFT") : QStringLiteral("synthesis"));
    return commands;
}

QJsonObject schemaForModel(const QJsonObject &model) {
    const int context = qMax(2'048, model.value(QStringLiteral("context_window")).toInt(65'536));
    const int history = qMax(1'024, model.value(QStringLiteral("input_context_tokens")).toInt(16'384));
    QJsonObject options{
        {"num_ctx", context}, {"num_predict", model.value("maximum_new_tokens").toInt(4'096)},
        {"history_token_limit", history}, {"temperature", model.value("temperature").toDouble(0.6)},
        {"top_k", model.value("top_k").toInt(40)}, {"top_p", model.value("top_p").toDouble(0.9)},
        {"min_p", model.value("min_p").toDouble(0.05)}, {"repeat_penalty", model.value("repeat_penalty").toDouble(1.1)},
        {"repeat_last_n", model.value("repeat_last_n").toInt(256)},
        {"dry_multiplier", model.value("dry_multiplier").toDouble(0.5)},
        {"presence_penalty", model.value("presence_penalty").toDouble(0.0)},
        {"frequency_penalty", model.value("frequency_penalty").toDouble(0.0)},
    };
    QJsonObject agentOptions{
        {"context_window", context}, {"history_token_limit", history}, {"history_message_count", 0},
        {"think", false}, {"repeat_penalty", model.value("repeat_penalty").toDouble(1.1)},
        {"repeat_last_n", model.value("repeat_last_n").toInt(256)},
        {"temperature", model.value("temperature").toDouble(0.6)}, {"top_k", model.value("top_k").toInt(40)},
        {"top_p", model.value("top_p").toDouble(0.9)}, {"min_p", model.value("min_p").toDouble(0.05)},
        {"dry_multiplier", model.value("dry_multiplier").toDouble(0.5)},
        {"presence_penalty", model.value("presence_penalty").toDouble(0.0)},
        {"frequency_penalty", model.value("frequency_penalty").toDouble(0.0)},
    };
    return {{QStringLiteral("options"), options}, {QStringLiteral("dft_agent"), agentOptions}};
}

QString readApiKey(const QJsonObject &model) {
    const QString inlineKey = model.value(QStringLiteral("api_key")).toString().trimmed();
    if (!inlineKey.isEmpty())
        return inlineKey;
    const QString keyPath = model.value(QStringLiteral("api_key_file")).toString().trimmed();
    if (keyPath.isEmpty())
        return {};
    QFile file(keyPath);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return QString::fromUtf8(file.readAll()).trimmed();
}

QVariantMap toVariantMap(const QJsonObject &object) {
    return object.toVariantMap();
}
}

ProjectConfigImporter::ProjectConfigImporter(QString workingDirectory, QObject *parent)
    : QObject(parent), m_workingDirectory(std::move(workingDirectory)) {
    connect(&m_toolClient, &AgentToolClient::completed, this, &ProjectConfigImporter::finish);
}

ProjectConfigImporter::~ProjectConfigImporter() {
    m_toolClient.cancel();
}

bool ProjectConfigImporter::running() const { return m_running; }
QString ProjectConfigImporter::stage() const { return m_stage; }
QString ProjectConfigImporter::sourcePath() const { return m_sourcePath; }
QString ProjectConfigImporter::status() const { return m_status; }
QString ProjectConfigImporter::error() const { return m_error; }
QVariantMap ProjectConfigImporter::result() const { return m_result; }

void ProjectConfigImporter::setModelCatalogPath(const QString &value) {
    m_modelCatalogPath = QFileInfo(value).absoluteFilePath();
}

void ProjectConfigImporter::start(const QString &stage, const QString &tclPath, const QString &modelId) {
    if (running())
        return;
    const QString normalizedStage = stage.trimmed().toLower();
    if (normalizedStage != QStringLiteral("synthesis") && normalizedStage != QStringLiteral("dft")) {
        setError(tr("导入类别必须是 synthesis 或 dft。"));
        emit completed(false);
        return;
    }
    const QFileInfo source(tclPath.trimmed());
    if (!source.isFile() || !QStringList{QStringLiteral("tcl"), QStringLiteral("sdc")}.contains(source.suffix().toLower())) {
        setError(tr("请选择可读取的 Tcl 或 SDC 文件。"));
        emit completed(false);
        return;
    }
    QFile catalog(m_modelCatalogPath);
    if (m_modelCatalogPath.isEmpty() || !catalog.open(QIODevice::ReadOnly)) {
        setError(tr("模型配置文件不可用。"));
        emit completed(false);
        return;
    }
    QJsonParseError parseError;
    const QJsonDocument catalogDocument = QJsonDocument::fromJson(catalog.readAll(), &parseError);
    if (!catalogDocument.isObject()) {
        setError(tr("模型配置文件无效：%1").arg(parseError.errorString()));
        emit completed(false);
        return;
    }
    const QJsonObject catalogObject = catalogDocument.object();
    const QString selectedId = modelId.trimmed().isEmpty()
        ? catalogObject.value(QStringLiteral("active_model_id")).toString()
        : modelId.trimmed();
    QJsonObject selectedModel;
    for (const QJsonValue &entry : catalogObject.value(QStringLiteral("models")).toArray()) {
        const QJsonObject candidate = entry.toObject();
        if (candidate.value(QStringLiteral("id")).toString() == selectedId
            && candidate.value(QStringLiteral("enabled")).toBool(true)) {
            selectedModel = candidate;
            break;
        }
    }
    if (selectedModel.isEmpty()) {
        setError(tr("所选模型不可用。"));
        emit completed(false);
        return;
    }
    QString commandError;
    const QJsonArray commands = readCommands(source.absoluteFilePath(), normalizedStage, &commandError);
    if (!commandError.isEmpty()) {
        setError(commandError);
        emit completed(false);
        return;
    }

    m_stage = normalizedStage;
    m_sourcePath = source.absoluteFilePath();
    m_result.clear();
    emit resultChanged();
    setError({});
    setStatus(tr("正在分析 Tcl/SDC 指令…"));

    QJsonArray messages;
    messages.append(QJsonObject{{QStringLiteral("role"), QStringLiteral("system")},
        {QStringLiteral("content"), normalizedStage == QStringLiteral("dft")
            ? QStringLiteral("你是 DFT Tcl 配置导入 Agent。只提取输入中明确出现的设置；不要推测。"
              "变量表达式无法确定时保留表单默认值并说明。evidence 必须引用输入中的 L 行号。调用且只调用给定工具。")
            : QStringLiteral("你是综合 Tcl 配置导入 Agent。只提取输入中明确出现的设置；不要推测。"
              "变量表达式无法确定时保留表单默认值并说明。evidence 必须引用输入中的 L 行号。调用且只调用给定工具.")}});
    const QJsonObject userPayload{
        {QStringLiteral("source"), m_sourcePath},
        {QStringLiteral("stage"), normalizedStage},
        {QStringLiteral("commands"), commands},
    };
    messages.append(QJsonObject{{QStringLiteral("role"), QStringLiteral("user")},
        {QStringLiteral("content"), QString::fromUtf8(QJsonDocument(userPayload).toJson(QJsonDocument::Compact))}});

    AgentToolClient::Request request;
    request.baseUrl = selectedModel.value(QStringLiteral("api_base")).toString();
    request.apiKey = readApiKey(selectedModel);
    request.model = selectedModel.value(QStringLiteral("id")).toString();
    request.toolName = QStringLiteral("import_project_tcl_configuration");
    request.toolDescription = QStringLiteral("从已有 Tcl 指令提取当前项目的综合或 DFT 设置。");
    request.parameters = argumentSchema(normalizedStage);
    request.messages = messages;
    request.maxTokens = qBound(256, selectedModel.value(QStringLiteral("maximum_new_tokens")).toInt(4096), 32'768);
    request.temperature = selectedModel.value(QStringLiteral("temperature")).toDouble(0.6);
    const QJsonObject modelOptions = schemaForModel(selectedModel);
    request.options = modelOptions.value(QStringLiteral("options")).toObject();
    request.agentOptions = modelOptions.value(QStringLiteral("dft_agent")).toObject();
    request.timeoutMs = qBound(10, selectedModel.value(QStringLiteral("timeout_seconds")).toInt(120), 1'800) * 1000;
    setRunning(true);
    m_toolClient.start(request);
}

void ProjectConfigImporter::cancel() {
    if (running())
        m_toolClient.cancel();
}

void ProjectConfigImporter::setRunning(bool value) {
    if (m_running == value)
        return;
    m_running = value;
    emit runningChanged();
}

void ProjectConfigImporter::setStatus(const QString &value) {
    if (m_status == value)
        return;
    m_status = value;
    emit stateChanged();
}

void ProjectConfigImporter::setError(const QString &value) {
    if (m_error == value)
        return;
    m_error = value;
    emit stateChanged();
}

void ProjectConfigImporter::finish(const QJsonObject &arguments, const QString &error) {
    if (!error.isEmpty()) {
        setError(error);
        setStatus(tr("导入失败"));
        setRunning(false);
        emit completed(false);
        return;
    }
    if (arguments.value(QStringLiteral("stage")).toString() != m_stage
        || !arguments.value(QStringLiteral("settings")).isObject()) {
        setError(tr("模型返回了不匹配的配置类别或缺少 settings 对象。"));
        setStatus(tr("导入失败"));
        setRunning(false);
        emit completed(false);
        return;
    }
    const QJsonArray commands = [&] {
        QString ignored;
        return readCommands(m_sourcePath, m_stage, &ignored);
    }();
    QSet<QString> allowedEvidence;
    QString commandText;
    QString commandTextLower;
    for (const QJsonValue &item : commands) {
        const QJsonObject row = item.toObject();
        allowedEvidence.insert(row.value(QStringLiteral("label")).toString());
        commandText += row.value(QStringLiteral("command")).toString() + '\n';
    }
    commandTextLower = commandText.toLower();
    QJsonArray evidence;
    for (const QJsonValue &entry : arguments.value(QStringLiteral("evidence")).toArray()) {
        const QString label = entry.toString();
        if (!allowedEvidence.contains(label)) {
            setError(tr("模型引用了输入中不存在的 Tcl 行号：%1").arg(label));
            setStatus(tr("导入失败"));
            setRunning(false);
            emit completed(false);
            return;
        }
        if (!evidence.contains(label))
            evidence.append(label);
    }
    if (evidence.isEmpty()) {
        setError(tr("模型没有返回可核验的 Tcl 行号。"));
        setStatus(tr("导入失败"));
        setRunning(false);
        emit completed(false);
        return;
    }

    QJsonObject settings = arguments.value(QStringLiteral("settings")).toObject();
    QStringList removed;
    for (auto it = settings.begin(); it != settings.end();) {
        const QStringList markers = markersForField(m_stage, it.key());
        bool supported = !markers.isEmpty();
        for (const QString &marker : markers)
            supported = supported && commandTextLower.contains(marker.toLower());
        if (!supported || hasExpression(it.value())) {
            removed.append(it.key());
            it = settings.erase(it);
            continue;
        }
        if (it.value().isString()) {
            const QString value = it.value().toString().trimmed();
            const bool special = m_stage == QStringLiteral("synthesis")
                && QStringList{QStringLiteral("compile_command"), QStringLiteral("map_effort"),
                    QStringLiteral("area_effort"), QStringLiteral("power_effort"),
                    QStringLiteral("auto_ungroup"), QStringLiteral("clock_groups_tcl")}.contains(it.key());
            if (value.isEmpty() || (!special && !commandText.contains(value))) {
                removed.append(it.key());
                it = settings.erase(it);
                continue;
            }
        }
        ++it;
    }

    QJsonArray warnings;
    for (const QJsonValue &item : arguments.value(QStringLiteral("warnings")).toArray()) {
        const QString warning = item.toString().trimmed();
        if (!warning.isEmpty() && !warnings.contains(warning))
            warnings.append(warning);
    }
    if (!removed.isEmpty()) {
        std::sort(removed.begin(), removed.end());
        removed.removeDuplicates();
        warnings.append(QStringLiteral("已忽略缺少 Tcl 指令支持或仍含变量的字段：%1").arg(removed.join(QStringLiteral("、"))));
    }
    if (m_stage == QStringLiteral("synthesis") && QFileInfo(m_sourcePath).suffix().compare(QStringLiteral("sdc"), Qt::CaseInsensitive) == 0) {
        settings.insert(QStringLiteral("constraint_file"), m_sourcePath);
        settings.insert(QStringLiteral("use_constraint_file"), true);
    }
    if (m_stage == QStringLiteral("dft")
        && (commandTextLower.contains(QStringLiteral("set_dft_"))
            || commandTextLower.contains(QStringLiteral("insert_dft"))
            || commandTextLower.contains(QStringLiteral("dft_drc"))))
        settings.insert(QStringLiteral("dft_enabled"), true);

    QJsonObject normalized = arguments;
    normalized.insert(QStringLiteral("settings"), settings);
    normalized.insert(QStringLiteral("evidence"), evidence);
    normalized.insert(QStringLiteral("warnings"), warnings);
    normalized.insert(QStringLiteral("source_command_count"), commands.size());
    normalized.insert(QStringLiteral("raw_arguments"), QString::fromUtf8(QJsonDocument(arguments).toJson(QJsonDocument::Compact)));
    m_result = normalized.toVariantMap();
    emit resultChanged();
    setError({});
    setStatus(tr("已从 Tcl 提取设置，请检查后保存"));
    setRunning(false);
    emit completed(true);
}
