#include "trainingshardingservice.h"

#include "trainingpolicies.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSaveFile>
#include <QSet>
#include <QTimer>
#include <QUrl>

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>

namespace {
QVariantMap failure(const QString &message) {
    return {{QStringLiteral("ok"), false}, {QStringLiteral("message"), message}};
}

QString jsonText(const QJsonValue &value) {
    if (value.isString())
        return value.toString();
    if (value.isObject())
        return QString::fromUtf8(QJsonDocument(value.toObject()).toJson(QJsonDocument::Compact));
    if (value.isArray())
        return QString::fromUtf8(QJsonDocument(value.toArray()).toJson(QJsonDocument::Compact));
    return value.toVariant().toString();
}

QByteArray sha256File(const QString &path, QString *error = nullptr) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error)
            *error = QStringLiteral("无法读取 %1：%2").arg(path, file.errorString());
        return {};
    }
    QCryptographicHash hash(QCryptographicHash::Sha256);
    while (!file.atEnd()) {
        const QByteArray chunk = file.read(1024 * 1024);
        if (chunk.isEmpty() && file.error() != QFileDevice::NoError) {
            if (error)
                *error = QStringLiteral("读取 %1 失败：%2").arg(path, file.errorString());
            return {};
        }
        hash.addData(chunk);
    }
    return hash.result().toHex();
}

QJsonValue canonicalJsonValue(const QJsonValue &value) {
    if (value.isObject()) {
        QJsonObject ordered;
        QStringList keys = value.toObject().keys();
        std::sort(keys.begin(), keys.end());
        for (const QString &key : keys)
            ordered.insert(key, canonicalJsonValue(value.toObject().value(key)));
        return ordered;
    }
    if (value.isArray()) {
        QJsonArray items;
        for (const QJsonValue &item : value.toArray())
            items.append(canonicalJsonValue(item));
        return items;
    }
    return value;
}

QByteArray canonicalJson(const QJsonValue &value) {
    const QJsonValue canonical = canonicalJsonValue(value);
    if (canonical.isObject())
        return QJsonDocument(canonical.toObject()).toJson(QJsonDocument::Compact);
    if (canonical.isArray())
        return QJsonDocument(canonical.toArray()).toJson(QJsonDocument::Compact);
    return QJsonDocument(QJsonArray{canonical}).toJson(QJsonDocument::Compact).sliced(1).chopped(1);
}

QString fingerprint(const QJsonObject &message) {
    return QString::fromLatin1(QCryptographicHash::hash(canonicalJson(message), QCryptographicHash::Sha256).toHex());
}

QJsonObject toolCallIds(const QJsonObject &message, bool *valid) {
    QJsonObject ids;
    const QJsonValue callsValue = message.value(QStringLiteral("tool_calls"));
    if (callsValue.isUndefined() || callsValue.isNull()) {
        *valid = true;
        return ids;
    }
    if (!callsValue.isArray()) {
        *valid = false;
        return ids;
    }
    const QJsonArray calls = callsValue.toArray();
    for (qsizetype index = 0; index < calls.size(); ++index) {
        const QJsonObject call = calls.at(index).toObject();
        const QString id = call.value(QStringLiteral("id")).toString();
        if (id.isEmpty() || ids.contains(id)) {
            *valid = false;
            return {};
        }
        ids.insert(id, true);
    }
    *valid = !calls.isEmpty();
    return ids;
}

struct AssistantBlock {
    QJsonArray messages;
    int sourceTurn = 0;
};

struct Integrity {
    int assistantCount = 0;
    int toolCalls = 0;
    int toolResults = 0;
    int selectedToolCalls = 0;
    QList<int> selectedSourceTurns;
    QStringList selectedFingerprints;
};

QString conversationIntegrity(const QJsonObject &row, Integrity *integrity,
                              QJsonArray *prefix, QList<AssistantBlock> *blocks) {
    const QJsonArray messages = row.value(QStringLiteral("messages")).toArray();
    if (messages.size() < 3)
        return QStringLiteral("Each SFT example must contain system, user, and assistant messages");
    if (messages.first().toObject().value(QStringLiteral("role")).toString() != QLatin1String("system"))
        return QStringLiteral("The first SFT message must be the system policy");
    const QJsonValue tools = row.value(QStringLiteral("tools"));
    if (!tools.isUndefined() && (!tools.isArray() || tools.toArray().isEmpty()))
        return QStringLiteral("SFT tools must be a non-empty list when provided");
    const QJsonValue controlValue = row.value(QStringLiteral("training_control"));
    if (!controlValue.isUndefined() && !controlValue.isNull() && !controlValue.isObject())
        return QStringLiteral("Full-conversation training_control must be an object");
    qsizetype firstAssistant = -1;
    for (qsizetype index = 0; index < messages.size(); ++index) {
        if (messages.at(index).toObject().value(QStringLiteral("role")).toString() == QLatin1String("assistant")) {
            firstAssistant = index;
            break;
        }
    }
    if (firstAssistant < 0)
        return QStringLiteral("会话没有 assistant 回合");
    for (qsizetype index = 0; index < firstAssistant; ++index) {
        const QString role = messages.at(index).toObject().value(QStringLiteral("role")).toString();
        if (role != QLatin1String("system") && role != QLatin1String("user"))
            return QStringLiteral("首个 assistant 回合之前出现了不支持的角色");
        prefix->append(messages.at(index));
    }

    int sourceTurn = 0;
    qsizetype index = firstAssistant;
    while (index < messages.size()) {
        const QJsonObject assistant = messages.at(index).toObject();
        if (assistant.value(QStringLiteral("role")).toString() != QLatin1String("assistant"))
            return QStringLiteral("工具交互之间出现了不支持的角色顺序");
        ++sourceTurn;
        ++integrity->assistantCount;
        AssistantBlock block;
        block.sourceTurn = sourceTurn;
        block.messages.append(assistant);
        ++index;
        while (index < messages.size()
               && messages.at(index).toObject().value(QStringLiteral("role")).toString() == QLatin1String("tool")) {
            block.messages.append(messages.at(index));
            ++integrity->toolResults;
            ++index;
        }

        bool callsValid = false;
        const QJsonObject calls = toolCallIds(assistant, &callsValid);
        const bool hasCalls = assistant.contains(QStringLiteral("tool_calls"))
            && !assistant.value(QStringLiteral("tool_calls")).isNull();
        const qsizetype resultsCount = block.messages.size() - 1;
        if (hasCalls) {
            const QJsonArray rawCalls = assistant.value(QStringLiteral("tool_calls")).toArray();
            if (!callsValid)
                return QStringLiteral("assistant 回合 %1 的 tool_calls 无效").arg(sourceTurn);
            if (resultsCount == 0)
                return QStringLiteral("assistant 回合 %1 缺少紧随其后的工具结果").arg(sourceTurn);
            QSet<QString> resultIds;
            for (qsizetype resultIndex = 1; resultIndex < block.messages.size(); ++resultIndex) {
                const QString id = block.messages.at(resultIndex).toObject()
                    .value(QStringLiteral("tool_call_id")).toString();
                if (id.isEmpty() || resultIds.contains(id))
                    return QStringLiteral("assistant 回合 %1 的工具调用编号与结果不一致").arg(sourceTurn);
                resultIds.insert(id);
            }
            if (calls.size() != resultsCount || resultIds.size() != calls.size())
                return QStringLiteral("assistant 回合 %1 的工具调用数与结果数不一致").arg(sourceTurn);
            for (const QString &id : calls.keys()) {
                if (!resultIds.contains(id))
                    return QStringLiteral("assistant 回合 %1 的工具调用编号与结果不一致").arg(sourceTurn);
            }
            integrity->toolCalls += rawCalls.size();
        } else if (resultsCount > 0) {
            return QStringLiteral("assistant 回合 %1 没有工具调用却带有工具结果").arg(sourceTurn);
        }
        blocks->append(block);
    }

    const QString finalRole = messages.last().toObject().value(QStringLiteral("role")).toString();
    if (finalRole != QLatin1String("assistant")
        && !(finalRole == QLatin1String("tool") && controlValue.isObject()))
        return QStringLiteral("The final SFT message must be the reviewed assistant answer");

    QList<QJsonObject> assistants;
    for (const QJsonValue &message : messages) {
        if (message.toObject().value(QStringLiteral("role")).toString() == QLatin1String("assistant"))
            assistants.append(message.toObject());
    }
    const QJsonObject trainingControl = row.value(QStringLiteral("training_control")).toObject();
    QJsonArray selected = trainingControl.value(QStringLiteral("supervised_assistant_turns")).toArray();
    if (controlValue.isObject() && selected.isEmpty())
        return QStringLiteral("supervised_assistant_turns must be sorted unique one-based assistant indexes");
    if (!controlValue.isObject()) {
        for (int turn = 1; turn <= assistants.size(); ++turn)
            selected.append(turn);
    }
    QSet<int> selectedSet;
    int previous = 0;
    for (const QJsonValue &value : selected) {
        if (!value.isDouble() || std::floor(value.toDouble()) != value.toDouble())
            return QStringLiteral("supervised_assistant_turns 必须是递增的正整数列表");
        const int turn = value.toInt();
        if (turn <= previous || turn < 1 || turn > assistants.size())
            return QStringLiteral("supervised_assistant_turns 必须是递增且有效的 assistant 回合索引");
        previous = turn;
        selectedSet.insert(turn);
    }
    QJsonArray sourceTurns = trainingControl.value(QStringLiteral("source_assistant_turns")).toArray();
    if (sourceTurns.isEmpty())
        sourceTurns = selected;
    if (sourceTurns.size() != selected.size())
        return QStringLiteral("source_assistant_turns 与监督回合数不一致");
    for (qsizetype selectedIndex = 0; selectedIndex < selected.size(); ++selectedIndex) {
        integrity->selectedSourceTurns.append(sourceTurns.at(selectedIndex).toInt());
        integrity->selectedFingerprints.append(fingerprint(assistants.at(selected.at(selectedIndex).toInt() - 1)));
    }
    for (int turn = 1; turn <= assistants.size(); ++turn) {
        if (!selectedSet.contains(turn))
            continue;
        const QJsonObject assistant = assistants.at(turn - 1);
        const QJsonArray calls = assistant.value(QStringLiteral("tool_calls")).toArray();
        integrity->selectedToolCalls += calls.size();
    }
    return {};
}

class LlamaTokenizer final {
public:
    explicit LlamaTokenizer(QString baseUrl) : m_baseUrl(std::move(baseUrl)) {
        while (m_baseUrl.endsWith(QLatin1Char('/')))
            m_baseUrl.chop(1);
    }

    bool render(const QJsonObject &row, QString *prompt, QString *error,
                bool addGenerationPrompt = false, bool replaceTrainingPolicy = true) {
        QJsonArray messages = row.value(QStringLiteral("messages")).toArray();
        if (replaceTrainingPolicy && !messages.isEmpty()) {
            const QJsonObject metadata = row.value(QStringLiteral("metadata")).toObject();
            const QString policy = metadata.value(QStringLiteral("training_policy")).toString();
            QJsonObject system = messages.first().toObject();
            if (system.value(QStringLiteral("role")).toString() == QLatin1String("system")) {
                const QString original = system.value(QStringLiteral("content")).toString();
                system.insert(QStringLiteral("content"), trainingSystem(policy, original));
                messages[0] = system;
            }
        }
        QJsonObject templateRequest{{QStringLiteral("messages"), messages},
                                   {QStringLiteral("chat_template_kwargs"), QJsonObject{
                                        {QStringLiteral("enable_thinking"), false}}}};
        if (addGenerationPrompt)
            templateRequest.insert(QStringLiteral("add_generation_prompt"), true);
        const QJsonValue tools = row.value(QStringLiteral("tools"));
        if (tools.isArray() && !tools.toArray().isEmpty())
            templateRequest.insert(QStringLiteral("tools"), tools);
        QJsonObject templateResponse;
        if (!post(QStringLiteral("/apply-template"), templateRequest, &templateResponse, error))
            return false;
        *prompt = templateResponse.value(QStringLiteral("prompt")).toString();
        if (prompt->isEmpty()) {
            *error = QStringLiteral("llama-server /apply-template 返回空 prompt");
            return false;
        }
        return true;
    }

    bool count(const QJsonObject &row, int *count, QString *error) {
        QJsonArray tokens;
        if (!tokenIds(row, &tokens, error))
            return false;
        *count = tokens.size();
        return true;
    }

    bool tokenIds(const QJsonObject &row, QJsonArray *tokens, QString *error,
                  bool addGenerationPrompt = false, bool replaceTrainingPolicy = true) {
        QString prompt;
        if (!render(row, &prompt, error, addGenerationPrompt, replaceTrainingPolicy))
            return false;
        QJsonObject tokenResponse;
        if (!post(QStringLiteral("/tokenize"), QJsonObject{
                {QStringLiteral("content"), prompt}, {QStringLiteral("add_special"), false},
                {QStringLiteral("parse_special"), true}}, &tokenResponse, error))
            return false;
        *tokens = tokenResponse.value(QStringLiteral("tokens")).toArray();
        if (tokens->isEmpty()) {
            *error = QStringLiteral("llama-server /tokenize 返回空 tokens");
            return false;
        }
        return true;
    }

private:
    bool post(const QString &path, const QJsonObject &body, QJsonObject *response, QString *error) {
        QNetworkAccessManager manager;
        QNetworkRequest request(QUrl(m_baseUrl + path));
        request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
        QNetworkReply *reply = manager.post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
        QEventLoop loop;
        QTimer timeout;
        timeout.setSingleShot(true);
        QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
        QObject::connect(&timeout, &QTimer::timeout, &loop, [&] {
            reply->abort();
            loop.quit();
        });
        timeout.start(30'000);
        loop.exec();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray bytes = reply->readAll();
        const QString networkError = reply->errorString();
        reply->deleteLater();
        if (timeout.isActive() && status >= 200 && status < 300) {
            QJsonParseError parseError{};
            const QJsonDocument document = QJsonDocument::fromJson(bytes, &parseError);
            if (parseError.error == QJsonParseError::NoError && document.isObject()) {
                *response = document.object();
                return true;
            }
            *error = QStringLiteral("llama-server %1 返回无效 JSON").arg(path);
            return false;
        }
        const QString detail = QString::fromUtf8(bytes).trimmed();
        *error = QStringLiteral("llama-server %1 失败 (HTTP %2): %3%4")
            .arg(path).arg(status).arg(detail.isEmpty() ? networkError : detail,
                timeout.isActive() ? QString{} : QStringLiteral(" (timeout)"));
        return false;
    }

    static QString trainingSystem(const QString &policy, const QString &fallback) {
        static const QHash<QString, QString> policies{
            {QStringLiteral("iteration_problem_solving_v1"), QStringLiteral(
                "你是 DFT 迭代决策 Agent。只能依据工具证据选择允许动作。"
                "每次只输出一个 JSON 对象，不得输出 Markdown、代码块、Tcl 或 JSON 外的说明。"
                "JSON 必须包含 action、reason 和 evidence；action 必须来自输入的 allowed_actions。"
                "DFT DRC 是可测试性设计规则检查，不是版图几何检查。"
                "C 类规则通常涉及时钟、复位或置位的测试控制；S1 表示扫描路径被非扫描单元阻断。"
                "增加 ATPG 中止上限只会增加单个故障的搜索工作量，不能保证覆盖率提高。"
                "每次参数试验必须引用当前报告，只改变一项，事先写明预期观察项和恢复条件；"
                "结果无改善或变差时恢复上一有效值，再根据 DRC、故障类别或仿真标志检查其他原因。"
                "首次运行和已有受控重试均由通用工具完成；只要 allowed_actions 仍含受控参数调整、诊断或依赖处理，"
                "就不得进入终端修改模式。只有普通脚本没有可用重试动作，或普通迭代用尽后仍未达目标时，"
                "才可在同一个隔离副本修改 Tcl、SDC、filelist 和辅助脚本并重跑。"
                "每次修改保存补丁，不得降低目标或声称未验证的修复已经成功。只有最新 DRC/综合证据明确指向 RTL 设计缺陷时，才修改隔离工作副本中的 RTL；工具、库、路径、约束或脚本故障应修复对应配置，不得用 RTL 修改掩盖。")},
            {QStringLiteral("supervisor_feedback_v1"), QStringLiteral(
                "你是接受监管反馈的 DFT Agent。先引用工具证据，再指出原回答的问题，最后给出修正后的动作。"
                "不能把程序缺陷、工具错误或缺少报告写成设计已完成；只有报告证实 RTL 本身有缺陷时，才修改隔离工作副本中的 RTL。"
                "通用工具失败后，应先检查是否还有登记过的参数调整、诊断或依赖处理；这些步骤用尽后，"
                "再判断能否通过隔离副本内的控制文件修改和亲自重跑继续处理。")},
            {QStringLiteral("role_reinforcement_v1"), QStringLiteral(
                "你是受监管的综合与 DFT Agent。职责是读取项目配置、调用允许工具、依据真实报告迭代并如实汇报。"
                "首次运行和常规迭代优先调用通用工具；只有普通脚本没有可用重试动作，或常规迭代用尽后仍未达目标时，"
                "才可修改隔离副本内的控制文件并运行登记过的 DFT 程序。"
                "原始输入用于追溯并保持不变；隔离工作副本中的 RTL 可编辑。先排除工具、库、约束、路径、filelist 和 Tcl 配置问题，只有新鲜证据确认 RTL 设计缺陷后才修复 RTL，并保留补丁、说明因果并重跑验证。")},
            {QStringLiteral("workflow_execution_v1"), QStringLiteral(
                "你是综合与 DFT 流程执行 Agent。必须按项目设置选择源码清单、工艺库、约束、综合、扫描链、DFT DRC、ATPG 或 MBIST 阶段。"
                "首次运行和已有受控重试使用通用工具；这些步骤无法完成任务后，自主处理才在同一个隔离副本连续读、改、运行并保存补丁，"
                "正式复核使用新的工作目录。")},
            {QStringLiteral("dft_tool_reasoning_v1"), QStringLiteral(
                "你是 DFT 工具使用 Agent。只说明已经批准且与当前阶段匹配的 dc_shell、TestMAX、Tessent、lc_shell 或仿真命令。"
                "必须解释命令输入、预期报告和失败条件；允许在隔离副本亲自运行，但不得编造工具执行结果。")},
            {QStringLiteral("workspace_agent_v1"), QStringLiteral(
                "你是综合与 DFT 自主处理 Agent。通用工具已经先运行，且与当前证据匹配的受控重试步骤已经用尽；"
                "只有普通脚本无法继续，或普通迭代无法达到目标时才进入自主处理。"
                "每次只输出一个 JSON 对象，不得输出 Markdown、代码块、Tcl 或 JSON 外的说明。"
                "JSON 必须包含 action 和 reason；action 只能是 search_text、read_file、terminal、stage_project_source、write_file、"
                "replace_text、run_dft_tool、finish 或 finish_manual_review，并按动作填写所需字段，不得自造 alert 等动作名。"
                "字段必须严格按动作填写：read_file 只用 path、start_line、line_count；search_text 只用 path、pattern、file_glob、"
                "maximum_matches、context_lines；terminal 只用 command，不得填写 path、working_directory 或 timeout_seconds；"
                "stage_project_source 只用 project_path；write_file 只用 path、content、expected_sha256，"
                "新文件不填写 expected_sha256，已有文件必须填写；replace_text 只用 path、"
                "expected_text、replacement、expected_sha256；run_dft_tool 只用 tool_name、script_path、working_directory、"
                "timeout_seconds；finish 和 finish_manual_review 不得增加动作参数。"
                "search_text 必须填写 pattern，并限制匹配数；定位后使用 read_file 的 start_line 和 line_count 读取不超过 200 行。"
                "terminal 动作必须填写非空 command；先用 pwd、test、find、rg、sed、tail、stat、sha256sum 等命令取得事实，"
                "路径含空格时必须使用引号，并限制查找深度和输出行数。"
                "终端从 /work 启动，/project 是用于追溯的原始输入；不得猜测路径，不得把同一条失败命令原样重复执行。"
                "缺失模块必须先用 rg 查找模块声明，再用带结果上限的 find 输出准确的 /project 文件路径；"
                "确认文件用途后调用 stage_project_source 将所需 HDL 纳入隔离工作副本；先修复文件清单或 Tcl 等配置。若新鲜报告和 RTL 代码共同证实设计缺陷，可编辑隔离副本中的 RTL。"
                "系统不会自动补文件，不得把整个 RTL 目录加入编译，也不得只登记源码而不修改编译清单。"
                "宿主控制、联网、删除、Git 写操作和专用 DFT 程序不得从 terminal 调用；专用 DFT 程序只能使用 run_dft_tool。"
                "所有动作在同一个隔离副本完成，可读写 Tcl、SDC、filelist、配置和辅助脚本，并运行登记过的 DFT 程序。"
                "不得完整读取大文件，也不得用 cat、tac、less 或 more 输出全文。修改现有文件前先读取 sha256，"
                "每次变化调用 diff -u 保存补丁，运行后先搜索新报告，再读取命中附近的小片段。"
                "修改参数或脚本前说明问题特征、原值、预期效果和恢复条件；每次只改变一项，"
                "根据新旧 DRC、覆盖率、故障类别、pattern 或 MBIST 标志决定保留或恢复。"
                "保留原始项目和工艺库不变；允许在隔离工作副本修复经证据确认的 RTL 缺陷。工具或配置问题不能通过 RTL 改动掩盖。")},
            {QStringLiteral("tool_trajectory_v1"), QStringLiteral(
                "你是综合与 DFT 工具调用 Agent。每次只根据当前状态、以前的工具调用和最新工具结果选择一个下一工具。"
                "调用 query_dft_manual 时必须遵守用户指定的 maximum_lines；未指定时首次查询最多读取 32 行，"
                "需要更多内容时先依据已返回的小片段确定下一个具体问题。"
                "必须使用工具返回的准确路径、错误、报告数值和文件哈希，不得把计划写成已经执行。"
                "普通工具和受控重试尚可使用时继续调用它们；重复调用、空结果、字段错误或工具失败以后，"
                "必须改变信息来源、路径、字段或处理方法，不能原样重试。"
                "用户指定 JSON 字段或其他结构化格式时，工具返回后必须严格按该格式回答。"
                "最终回答必须逐项说明实际执行的阶段、未达目标项，以及 RTL 修改的报告依据、根因、补丁和验证结果。")},
            {QStringLiteral("workspace_trajectory_v1"), QStringLiteral(
                "你是综合与 DFT 自主处理 Agent。通用工具已经先运行，且与当前证据匹配的受控重试步骤已经用尽；"
                "只有普通脚本无法继续，或普通迭代无法达到目标时才进入自主处理。"
                "每次只输出一个 JSON 对象，不得输出 Markdown、代码块、Tcl 或 JSON 外的说明。"
                "JSON 必须包含 action 和 reason；action 只能是 search_text、read_file、terminal、stage_project_source、write_file、"
                "replace_text、run_dft_tool、finish 或 finish_manual_review，并按动作填写所需字段，不得自造 alert 等动作名。"
                "字段必须严格按动作填写：read_file 只用 path、start_line、line_count；search_text 只用 path、pattern、file_glob、"
                "maximum_matches、context_lines；terminal 只用 command，不得填写 path、working_directory 或 timeout_seconds；"
                "stage_project_source 只用 project_path；write_file 只用 path、content、expected_sha256，"
                "新文件不填写 expected_sha256，已有文件必须填写；replace_text 只用 path、"
                "expected_text、replacement、expected_sha256；run_dft_tool 只用 tool_name、script_path、working_directory、"
                "timeout_seconds；finish 和 finish_manual_review 不得增加动作参数。"
                "search_text 必须填写 pattern，并限制匹配数；定位后使用 read_file 的 start_line 和 line_count 读取不超过 200 行。"
                "terminal 动作必须填写非空 command；先用 pwd、test、find、rg、sed、tail、stat、sha256sum 等命令取得事实，"
                "路径含空格时必须使用引号，并限制查找深度和输出行数。"
                "终端从 /work 启动，/project 是用于追溯的原始输入；不得猜测路径，不得把同一条失败命令原样重复执行。"
                "缺失模块必须先用 rg 查找模块声明，再用带结果上限的 find 输出准确的 /project 文件路径；"
                "确认文件用途后调用 stage_project_source 将所需 HDL 纳入隔离工作副本；先修复文件清单或 Tcl 等配置。若新鲜报告和 RTL 代码共同证实设计缺陷，可编辑隔离副本中的 RTL。"
                "系统不会自动补文件，不得把整个 RTL 目录加入编译，也不得只登记源码而不修改编译清单。"
                "宿主控制、联网、删除、Git 写操作和专用 DFT 程序不得从 terminal 调用；专用 DFT 程序只能使用 run_dft_tool。"
                "所有动作在同一个隔离副本完成，可读写 Tcl、SDC、filelist、配置和辅助脚本，并运行登记过的 DFT 程序。"
                "不得完整读取大文件，也不得用 cat、tac、less 或 more 输出全文。修改现有文件前先读取 sha256，"
                "每次变化调用 diff -u 保存补丁，运行后先搜索新报告，再读取命中附近的小片段。"
                "修改参数或脚本前说明问题特征、原值、预期效果和恢复条件；每次只改变一项，"
                "根据新旧 DRC、覆盖率、故障类别、pattern 或 MBIST 标志决定保留或恢复。"
                "保留原始项目和工艺库不变；允许在隔离工作副本修复经证据确认的 RTL 缺陷。工具或配置问题不能通过 RTL 改动掩盖。"
                "当前输入包含以前的动作和真实工具结果。只根据最新状态选择一个下一动作；"
                "以前失败或被拒绝的动作只用于纠正，不能把它复制成新答案。")},
            {QStringLiteral("tcl_config_import_v1"), QStringLiteral(
                "你是综合与 DFT Tcl 配置导入 Agent。只提取输入中明确出现的设置，不补写未出现的信息。"
                "变量表达式无法确定数值时保留当前表单默认值，并在 warnings 说明。"
                "evidence 只能填写输入中的 L 行号，至少填写一项。路径保持脚本中的原始写法。"
                "不要把注释或推测当成配置。调用且只调用给定工具。")},
        };
        return policies.value(policy, fallback);
    }

    QString m_baseUrl;
};

QJsonObject segmentRow(const QJsonObject &row, const QJsonArray &prefix,
                       const AssistantBlock *context, const QList<AssistantBlock> &targets,
                       const QString &sourceCase, int segmentNumber) {
    QJsonObject segment = row;
    QJsonArray messages = prefix;
    if (context) {
        for (const QJsonValue &message : context->messages)
            messages.append(message);
    }
    for (const AssistantBlock &block : targets) {
        for (const QJsonValue &message : block.messages)
            messages.append(message);
    }
    segment.insert(QStringLiteral("messages"), messages);
    QJsonArray selectedTurns;
    QJsonArray sourceTurns;
    const int firstSupervised = context ? 2 : 1;
    for (int index = 0; index < targets.size(); ++index) {
        selectedTurns.append(firstSupervised + index);
        sourceTurns.append(targets.at(index).sourceTurn);
    }
    QJsonObject control{{QStringLiteral("supervised_assistant_turns"), selectedTurns},
                        {QStringLiteral("source_assistant_turns"), sourceTurns},
                        {QStringLiteral("context_source_assistant_turn"), context
                             ? QJsonValue(context->sourceTurn) : QJsonValue(QJsonValue::Null)}};
    segment.insert(QStringLiteral("training_control"), control);
    QJsonObject metadata = row.value(QStringLiteral("metadata")).toObject();
    metadata.insert(QStringLiteral("case"), QStringLiteral("%1/length-segment-%2").arg(sourceCase).arg(segmentNumber));
    metadata.insert(QStringLiteral("length_stage_source_case"), sourceCase);
    metadata.insert(QStringLiteral("length_segment_number"), segmentNumber);
    segment.insert(QStringLiteral("metadata"), metadata);
    return segment;
}

struct Stage {
    QString name;
    int lower = 1;
    int upper = 0;
};

QList<Stage> lengthStages(const QList<int> &limits, QString *error) {
    if (limits.isEmpty()) {
        *error = QStringLiteral("长度上限必须是递增的正整数");
        return {};
    }
    int previous = 0;
    QList<Stage> stages;
    for (qsizetype index = 0; index < limits.size(); ++index) {
        const int upper = limits.at(index);
        if (upper <= previous) {
            *error = QStringLiteral("长度上限必须是递增的正整数");
            return {};
        }
        const int lower = previous + 1;
        stages.append({QStringLiteral("stage_%1_%2_%3").arg(index + 1, 2, 10, QLatin1Char('0'))
                           .arg(lower).arg(upper), lower, upper});
        previous = upper;
    }
    return stages;
}

int percentile(QList<int> values, double fraction) {
    if (values.isEmpty())
        return 0;
    std::sort(values.begin(), values.end());
    const int index = qBound(0, static_cast<int>(std::ceil(fraction * values.size())) - 1, values.size() - 1);
    return values.at(index);
}

QJsonObject lengthsReport(const QList<int> &lengths) {
    QJsonArray values;
    for (const int value : lengths)
        values.append(value);
    const qint64 total = std::accumulate(lengths.cbegin(), lengths.cend(), qint64(0));
    const int maximum = lengths.isEmpty() ? 0 : *std::max_element(lengths.cbegin(), lengths.cend());
    return {{QStringLiteral("rows"), lengths.size()},
            {QStringLiteral("tokens"), total},
            {QStringLiteral("mean"), lengths.isEmpty() ? 0.0 : static_cast<double>(total) / lengths.size()},
            {QStringLiteral("p50"), percentile(lengths, 0.50)},
            {QStringLiteral("p90"), percentile(lengths, 0.90)},
            {QStringLiteral("p95"), percentile(lengths, 0.95)},
            {QStringLiteral("p99"), percentile(lengths, 0.99)},
            {QStringLiteral("maximum"), maximum},
            {QStringLiteral("over_3100_rows"), std::count_if(lengths.cbegin(), lengths.cend(), [](int n) { return n > 3100; })},
            {QStringLiteral("over_4100_rows"), std::count_if(lengths.cbegin(), lengths.cend(), [](int n) { return n > 4100; })}};
}

bool writeJsonl(const QString &path, const QList<QJsonObject> &rows, QString *error) {
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        *error = QStringLiteral("无法写入 %1：%2").arg(path, file.errorString());
        return false;
    }
    for (const QJsonObject &row : rows) {
        const QByteArray line = QJsonDocument(row).toJson(QJsonDocument::Compact) + '\n';
        if (file.write(line) != line.size()) {
            *error = QStringLiteral("写入 %1 失败：%2").arg(path, file.errorString());
            return false;
        }
    }
    if (!file.commit()) {
        *error = QStringLiteral("提交 %1 失败：%2").arg(path, file.errorString());
        return false;
    }
    return true;
}

bool atomicJson(const QString &path, const QJsonObject &object, QString *error) {
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) {
        *error = QStringLiteral("无法创建报告目录：%1").arg(QFileInfo(path).absolutePath());
        return false;
    }
    QSaveFile file(path);
    const QByteArray bytes = QJsonDocument(object).toJson(QJsonDocument::Indented) + '\n';
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
        *error = QStringLiteral("无法保存 %1：%2").arg(path, file.errorString());
        return false;
    }
    return true;
}

QString preflightSystem(const QString &policy) {
    static const QHash<QString, QString> systems{
        {QStringLiteral("iteration_problem_solving_v1"), QStringLiteral(
            "你是 DFT 迭代决策 Agent。只能依据工具证据选择允许动作。"
            "每次只输出一个 JSON 对象，不得输出 Markdown、代码块、Tcl 或 JSON 外的说明。"
            "JSON 必须包含 action、reason 和 evidence；action 必须来自输入的 allowed_actions。"
            "DFT DRC 是可测试性设计规则检查，不是版图几何检查。"
            "C 类规则通常涉及时钟、复位或置位的测试控制；S1 表示扫描路径被非扫描单元阻断。"
            "增加 ATPG 中止上限只会增加单个故障的搜索工作量，不能保证覆盖率提高。"
            "每次参数试验必须引用当前报告，只改变一项，事先写明预期观察项和恢复条件；"
            "结果无改善或变差时恢复上一有效值，再根据 DRC、故障类别或仿真标志检查其他原因。"
            "首次运行和已有受控重试均由通用工具完成；只要 allowed_actions 仍含受控参数调整、诊断或依赖处理，"
            "就不得进入终端修改模式。只有普通脚本没有可用重试动作，或普通迭代用尽后仍未达目标时，"
            "才可在同一个隔离副本修改 Tcl、SDC、filelist 和辅助脚本并重跑。"
            "每次修改保存补丁，不得降低目标或声称未验证的修复已经成功。只有最新 DRC/综合证据明确指向 RTL 设计缺陷时，才修改隔离工作副本中的 RTL；工具、库、路径、约束或脚本故障应修复对应配置，不得用 RTL 修改掩盖。")},
        {QStringLiteral("supervisor_feedback_v1"), QStringLiteral(
            "你是接受监管反馈的 DFT Agent。先引用工具证据，再指出原回答的问题，最后给出修正后的动作。"
            "不能把程序缺陷、工具错误或缺少报告写成设计已完成；只有报告证实 RTL 本身有缺陷时，才修改隔离工作副本中的 RTL。"
            "通用工具失败后，应先检查是否还有登记过的参数调整、诊断或依赖处理；这些步骤用尽后，"
            "再判断能否通过隔离副本内的控制文件修改和亲自重跑继续处理。")},
        {QStringLiteral("role_reinforcement_v1"), QStringLiteral(
            "你是受监管的综合与 DFT Agent。职责是读取项目配置、调用允许工具、依据真实报告迭代并如实汇报。"
            "首次运行和常规迭代优先调用通用工具；只有普通脚本没有可用重试动作，或常规迭代用尽后，"
            "才可修改隔离副本内的控制文件并运行登记过的 DFT 程序。"
            "原始输入用于追溯并保持不变；隔离工作副本中的 RTL 可编辑。先排除工具、库、约束、路径、filelist 和 Tcl 配置问题，只有新鲜证据确认 RTL 设计缺陷后才修复 RTL，并保留补丁、说明因果并重跑验证。")},
        {QStringLiteral("workflow_execution_v1"), QStringLiteral(
            "你是综合与 DFT 流程执行 Agent。必须按项目设置选择源码清单、工艺库、约束、综合、扫描链、DFT DRC、ATPG 或 MBIST 阶段。"
            "首次运行和已有受控重试使用通用工具；这些步骤无法完成任务后，自主处理才在同一个隔离副本连续读、改、运行并保存补丁，"
            "正式复核使用新的工作目录。")},
        {QStringLiteral("dft_tool_reasoning_v1"), QStringLiteral(
            "你是 DFT 工具使用 Agent。只说明已经批准且与当前阶段匹配的 dc_shell、TestMAX、Tessent、lc_shell 或仿真命令。"
            "必须解释命令输入、预期报告和失败条件；允许在隔离副本亲自运行，但不得编造工具执行结果。")},
        {QStringLiteral("workspace_agent_v1"), QStringLiteral(
            "你是综合与 DFT 自主处理 Agent。通用工具已经先运行，且与当前证据匹配的受控重试步骤已经用尽；"
            "只有普通脚本无法继续，或普通迭代无法达到目标时才进入自主处理。"
            "每次只输出一个 JSON 对象，不得输出 Markdown、代码块、Tcl 或 JSON 外的说明。"
            "JSON 必须包含 action 和 reason；action 只能是 search_text、read_file、terminal、stage_project_source、write_file、"
            "replace_text、run_dft_tool、finish 或 finish_manual_review，并按动作填写所需字段，不得自造 alert 等动作名。"
            "字段必须严格按动作填写：read_file 只用 path、start_line、line_count；search_text 只用 path、pattern、file_glob、"
            "maximum_matches、context_lines；terminal 只用 command，不得填写 path、working_directory 或 timeout_seconds；"
            "stage_project_source 只用 project_path；write_file 只用 path、content、expected_sha256，"
            "新文件不填写 expected_sha256，已有文件必须填写；replace_text 只用 path、"
            "expected_text、replacement、expected_sha256；run_dft_tool 只用 tool_name、script_path、working_directory、"
            "timeout_seconds；finish 和 finish_manual_review 不得增加动作参数。"
            "search_text 必须填写 pattern，并限制匹配数；定位后使用 read_file 的 start_line 和 line_count 读取不超过 200 行。"
            "terminal 动作必须填写非空 command；先用 pwd、test、find、rg、sed、tail、stat、sha256sum 等命令取得事实，"
            "路径含空格时必须使用引号，并限制查找深度和输出行数。"
            "终端从 /work 启动，/project 是用于追溯的原始输入；不得猜测路径，不得把同一条失败命令原样重复执行。"
            "缺失模块必须先用 rg 查找模块声明，再用带结果上限的 find 输出准确的 /project 文件路径；"
            "确认文件用途后调用 stage_project_source 将所需 HDL 纳入隔离工作副本；先修复文件清单或 Tcl 等配置。若新鲜报告和 RTL 代码共同证实设计缺陷，可编辑隔离副本中的 RTL。"
            "系统不会自动补文件，不得把整个 RTL 目录加入编译，也不得只登记源码而不修改编译清单。"
            "宿主控制、联网、删除、Git 写操作和专用 DFT 程序不得从 terminal 调用；专用 DFT 程序只能使用 run_dft_tool。"
            "所有动作在同一个隔离副本完成，可读写 Tcl、SDC、filelist、配置和辅助脚本，并运行登记过的 DFT 程序。"
            "不得完整读取大文件，也不得用 cat、tac、less 或 more 输出全文。修改现有文件前先读取 sha256，"
            "每次变化调用 diff -u 保存补丁，运行后先搜索新报告，再读取命中附近的小片段。"
            "修改参数或脚本前说明问题特征、原值、预期效果和恢复条件；每次只改变一项，"
            "根据新旧 DRC、覆盖率、故障类别、pattern 或 MBIST 标志决定保留或恢复。"
            "保留原始项目和工艺库不变；允许在隔离工作副本修复经证据确认的 RTL 缺陷。工具或配置问题不能通过 RTL 改动掩盖。")},
        {QStringLiteral("tool_trajectory_v1"), QStringLiteral(
            "你是综合与 DFT 工具调用 Agent。每次只根据当前状态、以前的工具调用和最新工具结果选择一个下一工具。"
            "调用 query_dft_manual 时必须遵守用户指定的 maximum_lines；未指定时首次查询最多读取 32 行，"
            "需要更多内容时先依据已返回的小片段确定下一个具体问题。"
            "必须使用工具返回的准确路径、错误、报告数值和文件哈希，不得把计划写成已经执行。"
            "普通工具和受控重试尚可使用时继续调用它们；重复调用、空结果、字段错误或工具失败以后，"
            "必须改变信息来源、路径、字段或处理方法，不能原样重试。"
            "用户指定 JSON 字段或其他结构化格式时，工具返回后必须严格按该格式回答。"
            "最终回答必须逐项说明实际执行的阶段、未达目标项，以及 RTL 修改的报告依据、根因、补丁和验证结果。")},
        {QStringLiteral("workspace_trajectory_v1"), QStringLiteral(
            "你是综合与 DFT 自主处理 Agent。通用工具已经先运行，且与当前证据匹配的受控重试步骤已经用尽；"
            "只有普通脚本无法继续，或普通迭代无法达到目标时才进入自主处理。"
            "每次只输出一个 JSON 对象，不得输出 Markdown、代码块、Tcl 或 JSON 外的说明。"
            "JSON 必须包含 action 和 reason；action 只能是 search_text、read_file、terminal、stage_project_source、write_file、"
            "replace_text、run_dft_tool、finish 或 finish_manual_review，并按动作填写所需字段，不得自造 alert 等动作名。"
            "字段必须严格按动作填写：read_file 只用 path、start_line、line_count；search_text 只用 path、pattern、file_glob、"
            "maximum_matches、context_lines；terminal 只用 command，不得填写 path、working_directory 或 timeout_seconds；"
            "stage_project_source 只用 project_path；write_file 只用 path、content、expected_sha256，"
            "新文件不填写 expected_sha256，已有文件必须填写；replace_text 只用 path、"
            "expected_text、replacement、expected_sha256；run_dft_tool 只用 tool_name、script_path、working_directory、"
            "timeout_seconds；finish 和 finish_manual_review 不得增加动作参数。"
            "search_text 必须填写 pattern，并限制匹配数；定位后使用 read_file 的 start_line 和 line_count 读取不超过 200 行。"
            "terminal 动作必须填写非空 command；先用 pwd、test、find、rg、sed、tail、stat、sha256sum 等命令取得事实，"
            "路径含空格时必须使用引号，并限制查找深度和输出行数。"
            "终端从 /work 启动，/project 是用于追溯的原始输入；不得猜测路径，不得把同一条失败命令原样重复执行。"
            "缺失模块必须先用 rg 查找模块声明，再用带结果上限的 find 输出准确的 /project 文件路径；"
            "确认文件用途后调用 stage_project_source 将所需 HDL 纳入隔离工作副本；先修复文件清单或 Tcl 等配置。若新鲜报告和 RTL 代码共同证实设计缺陷，可编辑隔离副本中的 RTL。"
            "系统不会自动补文件，不得把整个 RTL 目录加入编译，也不得只登记源码而不修改编译清单。"
            "宿主控制、联网、删除、Git 写操作和专用 DFT 程序不得从 terminal 调用；专用 DFT 程序只能使用 run_dft_tool。"
            "所有动作在同一个隔离副本完成，可读写 Tcl、SDC、filelist、配置和辅助脚本，并运行登记过的 DFT 程序。"
            "不得完整读取大文件，也不得用 cat、tac、less 或 more 输出全文。修改现有文件前先读取 sha256，"
            "每次变化调用 diff -u 保存补丁，运行后先搜索新报告，再读取命中附近的小片段。"
            "修改参数或脚本前说明问题特征、原值、预期效果和恢复条件；每次只改变一项，"
            "根据新旧 DRC、覆盖率、故障类别、pattern 或 MBIST 标志决定保留或恢复。"
            "保留原始项目和工艺库不变；允许在隔离工作副本修复经证据确认的 RTL 缺陷。工具或配置问题不能通过 RTL 改动掩盖。"
            "当前输入包含以前的动作和真实工具结果。只根据最新状态选择一个下一动作；"
            "以前失败或被拒绝的动作只用于纠正，不能把它复制成新答案。")},
        {QStringLiteral("tcl_config_import_v1"), QStringLiteral(
            "你是综合与 DFT Tcl 配置导入 Agent。只提取输入中明确出现的设置，不补写未出现的信息。"
            "变量表达式无法确定数值时保留当前表单默认值，并在 warnings 说明。"
            "evidence 只能填写输入中的 L 行号，至少填写一项。路径保持脚本中的原始写法。"
            "不要把注释或推测当成配置。调用且只调用给定工具。")},
    };
    return systems.value(policy, QStringLiteral(
        "你是受控 DFT 执行助手。只能依据工具返回的证据陈述状态、数值和路径；"
        "仅 cross_validation.status=verified 可称证据已验证。"
        "出现错误、阻塞或缺少工具调用时必须明确未完成并说明限制；不得编造执行、修复或工程复核结论。"));
}

QJsonObject tokenLengthSummary(const QList<int> &values, const QList<int> &thresholds) {
    QList<int> ordered = values;
    std::sort(ordered.begin(), ordered.end());
    const auto percentileValue = [&ordered](double fraction) {
        const double position = (ordered.size() - 1) * fraction;
        const int lower = static_cast<int>(std::floor(position));
        const int upper = static_cast<int>(std::ceil(position));
        const double weight = position - lower;
        return std::nearbyint((ordered.at(lower) * (1.0 - weight) + ordered.at(upper) * weight) * 1000.0) / 1000.0;
    };
    QJsonObject above;
    for (const int threshold : thresholds) {
        above.insert(QString::number(threshold), std::count_if(values.cbegin(), values.cend(),
            [threshold](int value) { return value > threshold; }));
    }
    return {{QStringLiteral("minimum"), ordered.first()},
            {QStringLiteral("p50"), percentileValue(0.50)},
            {QStringLiteral("p95"), percentileValue(0.95)},
            {QStringLiteral("p99"), percentileValue(0.99)},
            {QStringLiteral("maximum"), ordered.last()},
            {QStringLiteral("above_threshold"), above}};
}
}

QVariantMap TrainingShardingService::prepare(const QString &datasetPath, const QString &outputDirectory,
                                              const QString &llamaServerUrl, const QList<int> &limits,
                                              bool writeOutputs) {
    const QFileInfo datasetInfo(datasetPath);
    if (!datasetInfo.isFile())
        return failure(QStringLiteral("找不到训练数据：%1").arg(datasetPath));
    if (outputDirectory.trimmed().isEmpty())
        return failure(QStringLiteral("必须提供输出目录。"));
    const QUrl serverUrl(llamaServerUrl);
    if (!serverUrl.isValid() || (serverUrl.scheme() != QLatin1String("http") && serverUrl.scheme() != QLatin1String("https")))
        return failure(QStringLiteral("llama-server URL 必须是有效的 HTTP(S) 地址。"));
    QString error;
    const QList<Stage> stages = lengthStages(limits, &error);
    if (stages.isEmpty())
        return failure(error);

    QFile input(datasetInfo.absoluteFilePath());
    if (!input.open(QIODevice::ReadOnly | QIODevice::Text))
        return failure(QStringLiteral("无法读取训练数据：%1").arg(input.errorString()));
    LlamaTokenizer tokenizer(llamaServerUrl);
    QHash<QString, QList<QJsonObject>> rowsByStage;
    QHash<QString, QList<int>> lengthsByStage;
    QList<int> sourceLengths;
    QList<int> preparedLengths;
    QJsonArray anomalies;
    int sourceRows = 0;
    int sourceTokens = 0;
    int splitSourceRows = 0;
    int sourceAssistantTurns = 0;
    int preparedAssistantTurns = 0;
    int sourceToolCalls = 0;
    int sourceToolResults = 0;
    int preparedToolCalls = 0;
    int lineNumber = 0;

    while (!input.atEnd()) {
        const QByteArray line = input.readLine();
        ++lineNumber;
        if (line.trimmed().isEmpty())
            continue;
        ++sourceRows;
        QJsonParseError parseError{};
        const QJsonDocument parsed = QJsonDocument::fromJson(line, &parseError);
        const QString fallbackCase = QStringLiteral("line-%1").arg(lineNumber);
        if (parseError.error != QJsonParseError::NoError || !parsed.isObject()) {
            anomalies.append(QJsonObject{{QStringLiteral("line"), lineNumber},
                {QStringLiteral("case"), fallbackCase},
                {QStringLiteral("code"), parseError.error == QJsonParseError::NoError
                     ? QStringLiteral("row_not_object") : QStringLiteral("invalid_json")},
                {QStringLiteral("detail"), parseError.error == QJsonParseError::NoError
                     ? QStringLiteral("该行不是 JSON 对象") : QStringLiteral("JSON 无法解析：%1").arg(parseError.errorString())}});
            continue;
        }
        const QJsonObject row = parsed.object();
        QString sourceCase = row.value(QStringLiteral("metadata")).toObject()
            .value(QStringLiteral("case")).toString();
        if (sourceCase.isEmpty())
            sourceCase = fallbackCase;
        Integrity sourceIntegrity;
        QJsonArray prefix;
        QList<AssistantBlock> blocks;
        error = conversationIntegrity(row, &sourceIntegrity, &prefix, &blocks);
        int sourceLength = 0;
        if (error.isEmpty() && !tokenizer.count(row, &sourceLength, &error))
            error = QStringLiteral("tokenizer: %1").arg(error);
        if (!error.isEmpty()) {
            anomalies.append(QJsonObject{{QStringLiteral("line"), lineNumber},
                {QStringLiteral("case"), sourceCase}, {QStringLiteral("code"), QStringLiteral("source_integrity_failed")},
                {QStringLiteral("detail"), error}});
            continue;
        }
        sourceLengths.append(sourceLength);
        sourceTokens += sourceLength;
        sourceAssistantTurns += sourceIntegrity.assistantCount;
        sourceToolCalls += sourceIntegrity.toolCalls;
        sourceToolResults += sourceIntegrity.toolResults;

        QList<QPair<QJsonObject, int>> candidates;
        if (sourceLength <= stages.last().upper) {
            QJsonObject normalized = row;
            QJsonObject control = row.value(QStringLiteral("training_control")).toObject();
            if (!control.value(QStringLiteral("supervised_assistant_turns")).isArray()
                || control.value(QStringLiteral("supervised_assistant_turns")).toArray().isEmpty()) {
                QJsonArray selected;
                for (int turn = 1; turn <= sourceIntegrity.assistantCount; ++turn)
                    selected.append(turn);
                control.insert(QStringLiteral("supervised_assistant_turns"), selected);
                QJsonArray sourceTurns;
                for (const int turn : sourceIntegrity.selectedSourceTurns)
                    sourceTurns.append(turn);
                control.insert(QStringLiteral("source_assistant_turns"), sourceTurns);
                control.insert(QStringLiteral("context_source_assistant_turn"), QJsonValue(QJsonValue::Null));
                normalized.insert(QStringLiteral("training_control"), control);
            }
            candidates.append({normalized, sourceLength});
        } else {
            if (blocks.size() < 2) {
                anomalies.append(QJsonObject{{QStringLiteral("line"), lineNumber},
                    {QStringLiteral("case"), sourceCase}, {QStringLiteral("code"), QStringLiteral("unsafe_to_split")},
                    {QStringLiteral("source_length"), sourceLength},
                    {QStringLiteral("detail"), QStringLiteral("单个 assistant 回合超过长度上限，不能安全拆分")}});
                continue;
            }
            int cursor = 0;
            bool splitFailed = false;
            while (cursor < blocks.size()) {
                const AssistantBlock *context = cursor > 0 ? &blocks.at(cursor - 1) : nullptr;
                QList<AssistantBlock> targets;
                const int segmentNumber = candidates.size() + 1;
                while (cursor + targets.size() < blocks.size()) {
                    QList<AssistantBlock> trial = targets;
                    trial.append(blocks.at(cursor + targets.size()));
                    const QJsonObject candidate = segmentRow(row, prefix, context, trial, sourceCase, segmentNumber);
                    int rendered = 0;
                    if (!tokenizer.count(candidate, &rendered, &error)) {
                        splitFailed = true;
                        break;
                    }
                    if (rendered > stages.last().upper)
                        break;
                    targets = trial;
                }
                if (splitFailed)
                    break;
                if (targets.isEmpty()) {
                    splitFailed = true;
                    error = context
                        ? QStringLiteral("最近一次完整工具交互与下一个 assistant 回合合并后超过长度上限")
                        : QStringLiteral("首个完整工具交互超过长度上限");
                    break;
                }
                const QJsonObject candidate = segmentRow(row, prefix, context, targets, sourceCase, segmentNumber);
                int rendered = 0;
                if (!tokenizer.count(candidate, &rendered, &error)) {
                    splitFailed = true;
                    break;
                }
                candidates.append({candidate, rendered});
                cursor += targets.size();
            }
            if (splitFailed) {
                anomalies.append(QJsonObject{{QStringLiteral("line"), lineNumber},
                    {QStringLiteral("case"), sourceCase}, {QStringLiteral("code"), QStringLiteral("unsafe_to_split")},
                    {QStringLiteral("source_length"), sourceLength}, {QStringLiteral("detail"), error}});
                continue;
            }
            ++splitSourceRows;
        }

        QStringList preserved;
        for (const auto &candidate : candidates) {
            Integrity candidateIntegrity;
            QJsonArray candidatePrefix;
            QList<AssistantBlock> candidateBlocks;
            error = conversationIntegrity(candidate.first, &candidateIntegrity, &candidatePrefix, &candidateBlocks);
            if (!error.isEmpty())
                break;
            for (qsizetype index = 0; index < candidateIntegrity.selectedSourceTurns.size(); ++index)
                preserved.append(QStringLiteral("%1:%2").arg(candidateIntegrity.selectedSourceTurns.at(index))
                                     .arg(candidateIntegrity.selectedFingerprints.at(index)));
        }
        QStringList expected;
        for (qsizetype index = 0; index < sourceIntegrity.selectedSourceTurns.size(); ++index)
            expected.append(QStringLiteral("%1:%2").arg(sourceIntegrity.selectedSourceTurns.at(index))
                                .arg(sourceIntegrity.selectedFingerprints.at(index)));
        std::sort(preserved.begin(), preserved.end());
        std::sort(expected.begin(), expected.end());
        if (!error.isEmpty() || preserved != expected) {
            anomalies.append(QJsonObject{{QStringLiteral("line"), lineNumber},
                {QStringLiteral("case"), sourceCase}, {QStringLiteral("code"), QStringLiteral("prepared_integrity_failed")},
                {QStringLiteral("source_length"), sourceLength},
                {QStringLiteral("detail"), error.isEmpty() ? QStringLiteral("分档后 assistant 答案缺失、重复或发生改变") : error}});
            continue;
        }

        for (auto candidate : candidates) {
            int stageIndex = -1;
            for (qsizetype index = 0; index < stages.size(); ++index) {
                if (candidate.second >= stages.at(index).lower && candidate.second <= stages.at(index).upper) {
                    stageIndex = static_cast<int>(index);
                    break;
                }
            }
            if (stageIndex < 0) {
                error = QStringLiteral("长度 %1 不属于任何训练阶段").arg(candidate.second);
                break;
            }
            const Stage &stage = stages.at(stageIndex);
            candidate.first.insert(QStringLiteral("training_stage"), QJsonObject{
                {QStringLiteral("rendered_token_count"), candidate.second},
                {QStringLiteral("length_stage"), stage.name}});
            rowsByStage[stage.name].append(candidate.first);
            lengthsByStage[stage.name].append(candidate.second);
            preparedLengths.append(candidate.second);
            Integrity prepared;
            QJsonArray ignoredPrefix;
            QList<AssistantBlock> ignoredBlocks;
            if (conversationIntegrity(candidate.first, &prepared, &ignoredPrefix, &ignoredBlocks).isEmpty()) {
                preparedAssistantTurns += prepared.selectedSourceTurns.size();
                preparedToolCalls += prepared.selectedToolCalls;
            }
        }
    }
    if (input.error() != QFileDevice::NoError)
        return failure(QStringLiteral("读取训练数据失败：%1").arg(input.errorString()));

    QString hashError;
    const QByteArray datasetHash = sha256File(datasetInfo.absoluteFilePath(), &hashError);
    if (datasetHash.isEmpty())
        return failure(hashError);
    QJsonArray limitsJson;
    for (const int limit : limits)
        limitsJson.append(limit);
    QJsonObject sourceSummary = lengthsReport(sourceLengths);
    sourceSummary.insert(QStringLiteral("rendered_rows"), sourceLengths.size());
    sourceSummary.insert(QStringLiteral("tokens"), sourceTokens);
    QJsonObject preparedSummary = lengthsReport(preparedLengths);
    preparedSummary.remove(QStringLiteral("mean"));
    preparedSummary.remove(QStringLiteral("over_3100_rows"));
    preparedSummary.remove(QStringLiteral("over_4100_rows"));
    preparedSummary.insert(QStringLiteral("split_source_rows"), splitSourceRows);
    QJsonObject stagesJson;
    for (const Stage &stage : stages) {
        const QList<int> lengths = lengthsByStage.value(stage.name);
        stagesJson.insert(stage.name, QJsonObject{
            {QStringLiteral("lower"), stage.lower}, {QStringLiteral("upper"), stage.upper},
            {QStringLiteral("rows"), lengths.size()},
            {QStringLiteral("tokens"), std::accumulate(lengths.cbegin(), lengths.cend(), qint64(0))},
            {QStringLiteral("maximum"), lengths.isEmpty() ? 0 : *std::max_element(lengths.cbegin(), lengths.cend())}});
    }
    bool noAnomalies = anomalies.isEmpty();
    QJsonObject report{
        {QStringLiteral("created_at"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
        {QStringLiteral("status"), noAnomalies ? QStringLiteral("检查通过") : QStringLiteral("存在异常，未生成正式训练文件")},
        {QStringLiteral("dataset"), datasetInfo.canonicalFilePath()},
        {QStringLiteral("dataset_sha256"), QString::fromLatin1(datasetHash)},
        {QStringLiteral("tokenizer"), QStringLiteral("llama.cpp: %1").arg(llamaServerUrl)},
        {QStringLiteral("native_text_corpus"), QJsonObject{
            {QStringLiteral("objective"), QStringLiteral("full_sequence_next_token")},
            {QStringLiteral("assistant_only_loss_mask"), false},
            {QStringLiteral("note"), QStringLiteral("The corpus is compatible with llama-finetune; it does not preserve PEFT/TRL assistant-only loss semantics.")}}},
        {QStringLiteral("length_limits"), limitsJson},
        {QStringLiteral("source"), sourceSummary}, {QStringLiteral("prepared"), preparedSummary},
        {QStringLiteral("integrity"), QJsonObject{
            {QStringLiteral("source_assistant_turns"), sourceAssistantTurns},
            {QStringLiteral("prepared_supervised_assistant_turns"), preparedAssistantTurns},
            {QStringLiteral("source_tool_calls"), sourceToolCalls},
            {QStringLiteral("source_tool_results"), sourceToolResults},
            {QStringLiteral("prepared_supervised_tool_calls"), preparedToolCalls},
            {QStringLiteral("assistant_answers_preserved"), noAnomalies && sourceAssistantTurns == preparedAssistantTurns},
            {QStringLiteral("tool_pairs_complete"), noAnomalies && sourceToolCalls == sourceToolResults
                 && sourceToolResults == preparedToolCalls},
            {QStringLiteral("no_assistant_truncation"), noAnomalies && sourceAssistantTurns == preparedAssistantTurns
                 && std::all_of(preparedLengths.cbegin(), preparedLengths.cend(), [&](int length) { return length <= stages.last().upper; })}}},
        {QStringLiteral("stages"), stagesJson}};

    const QString outDir = QFileInfo(outputDirectory).absoluteFilePath();
    if (!QDir().mkpath(outDir))
        return failure(QStringLiteral("无法创建输出目录：%1").arg(outDir));
    const QString reportPath = QDir(outDir).filePath(QStringLiteral("length_stages_report.json"));
    if (writeOutputs && noAnomalies) {
        QJsonObject outputFiles;
        QJsonArray postWriteErrors;
        int checkedRows = 0;
        qint64 checkedTokens = 0;
        for (const Stage &stage : stages) {
            const QString path = QDir(outDir).filePath(stage.name + QStringLiteral(".jsonl"));
            if (!writeJsonl(path, rowsByStage.value(stage.name), &error))
                return failure(error);
            const QByteArray digest = sha256File(path, &error);
            if (digest.isEmpty())
                return failure(error);
            const QString corpusPath = QDir(outDir).filePath(stage.name + QStringLiteral(".full_sequence.txt"));
            QSaveFile corpus(corpusPath);
            if (!corpus.open(QIODevice::WriteOnly | QIODevice::Text))
                return failure(QStringLiteral("无法创建训练文本 %1：%2").arg(corpusPath, corpus.errorString()));
            for (const QJsonObject &row : rowsByStage.value(stage.name)) {
                QString rendered;
                if (!tokenizer.render(row, &rendered, &error))
                    return failure(QStringLiteral("无法渲染 %1 的训练文本：%2").arg(stage.name, error));
                const QByteArray bytes = rendered.toUtf8() + QByteArrayLiteral("\n\n");
                if (corpus.write(bytes) != bytes.size())
                    return failure(QStringLiteral("写入训练文本失败：%1").arg(corpus.errorString()));
            }
            if (!corpus.commit())
                return failure(QStringLiteral("提交训练文本失败：%1").arg(corpus.errorString()));
            const QByteArray corpusDigest = sha256File(corpusPath, &error);
            if (corpusDigest.isEmpty())
                return failure(error);
            outputFiles.insert(stage.name, QJsonObject{
                {QStringLiteral("path"), path}, {QStringLiteral("sha256"), QString::fromLatin1(digest)},
                {QStringLiteral("full_sequence_corpus"), QJsonObject{
                    {QStringLiteral("path"), corpusPath},
                    {QStringLiteral("sha256"), QString::fromLatin1(corpusDigest)},
                    {QStringLiteral("objective"), QStringLiteral("full_sequence_next_token")},
                    {QStringLiteral("assistant_only_loss_mask"), false}}}});

            QFile written(path);
            if (!written.open(QIODevice::ReadOnly | QIODevice::Text))
                return failure(QStringLiteral("无法重新读取生成文件 %1：%2").arg(path, written.errorString()));
            int rowNumber = 0;
            while (!written.atEnd()) {
                ++rowNumber;
                const QByteArray line = written.readLine();
                QJsonParseError parseError{};
                const QJsonDocument parsed = QJsonDocument::fromJson(line, &parseError);
                QString detail;
                if (parseError.error != QJsonParseError::NoError || !parsed.isObject()) {
                    detail = QStringLiteral("生成行不是有效 JSON 对象");
                } else {
                    const QJsonObject row = parsed.object();
                    Integrity integrity;
                    QJsonArray prefix;
                    QList<AssistantBlock> blocks;
                    detail = conversationIntegrity(row, &integrity, &prefix, &blocks);
                    int actualTokens = 0;
                    if (detail.isEmpty() && !tokenizer.count(row, &actualTokens, &detail))
                        detail = QStringLiteral("tokenizer: %1").arg(detail);
                    const QJsonObject stageMetadata = row.value(QStringLiteral("training_stage")).toObject();
                    const int recordedTokens = stageMetadata.value(QStringLiteral("rendered_token_count")).toInt(-1);
                    const QString recordedStage = stageMetadata.value(QStringLiteral("length_stage")).toString();
                    if (detail.isEmpty() && (recordedTokens != actualTokens || recordedStage != stage.name
                        || actualTokens < stage.lower || actualTokens > stage.upper)) {
                        detail = QStringLiteral("回读 token 长度或分档不一致：记录 %1/%2，实际 %3")
                            .arg(recordedTokens).arg(recordedStage).arg(actualTokens);
                    }
                    if (detail.isEmpty() && integrity.selectedSourceTurns.isEmpty())
                        detail = QStringLiteral("生成行没有受监督的 assistant 回合");
                    if (detail.isEmpty()) {
                        ++checkedRows;
                        checkedTokens += actualTokens;
                    }
                }
                if (!detail.isEmpty()) {
                    postWriteErrors.append(QJsonObject{{QStringLiteral("stage"), stage.name},
                        {QStringLiteral("line"), rowNumber}, {QStringLiteral("detail"), detail}});
                }
            }
            if (written.error() != QFileDevice::NoError)
                return failure(QStringLiteral("重新读取生成文件失败：%1").arg(written.errorString()));
        }
        report.insert(QStringLiteral("output_files"), outputFiles);
        noAnomalies = postWriteErrors.isEmpty() && checkedRows == preparedLengths.size();
        if (!noAnomalies) {
            anomalies.append(QJsonObject{{QStringLiteral("code"), QStringLiteral("post_write_check_failed")},
                {QStringLiteral("detail"), postWriteErrors.isEmpty()
                     ? QStringLiteral("回读行数与预期不一致") : QStringLiteral("生成文件回读校验失败")}});
            for (const Stage &stage : stages)
            {
                QFile::remove(QDir(outDir).filePath(stage.name + QStringLiteral(".jsonl")));
                QFile::remove(QDir(outDir).filePath(stage.name + QStringLiteral(".full_sequence.txt")));
            }
            outputFiles = QJsonObject{};
            report.insert(QStringLiteral("output_files"), outputFiles);
        }
        report.insert(QStringLiteral("post_write_check"), QJsonObject{
            {QStringLiteral("status"), noAnomalies ? QStringLiteral("通过") : QStringLiteral("失败")},
            {QStringLiteral("checked_rows"), checkedRows},
            {QStringLiteral("checked_tokens"), checkedTokens},
            {QStringLiteral("error_count"), postWriteErrors.size()}, {QStringLiteral("errors"), postWriteErrors}});
    } else {
        report.insert(QStringLiteral("output_files"), QJsonObject{});
        report.insert(QStringLiteral("post_write_check"), QJsonObject{
            {QStringLiteral("status"), QStringLiteral("未执行")}, {QStringLiteral("checked_rows"), 0},
            {QStringLiteral("checked_tokens"), 0}, {QStringLiteral("error_count"), 0},
            {QStringLiteral("errors"), QJsonArray{}}});
    }
    QJsonObject finalIntegrity = report.value(QStringLiteral("integrity")).toObject();
    finalIntegrity.insert(QStringLiteral("assistant_answers_preserved"), noAnomalies
        && sourceAssistantTurns == preparedAssistantTurns);
    finalIntegrity.insert(QStringLiteral("tool_pairs_complete"), noAnomalies
        && sourceToolCalls == sourceToolResults && sourceToolResults == preparedToolCalls);
    finalIntegrity.insert(QStringLiteral("no_assistant_truncation"), noAnomalies
        && sourceAssistantTurns == preparedAssistantTurns
        && std::all_of(preparedLengths.cbegin(), preparedLengths.cend(), [&](int length) {
            return length <= stages.last().upper;
        }));
    report.insert(QStringLiteral("integrity"), finalIntegrity);
    QJsonObject anomalyCodes;
    for (const QJsonValue &value : anomalies) {
        const QString code = value.toObject().value(QStringLiteral("code")).toString();
        anomalyCodes.insert(code, anomalyCodes.value(code).toInt() + 1);
    }
    report.insert(QStringLiteral("status"), noAnomalies ? QStringLiteral("检查通过") : QStringLiteral("存在异常，未生成正式训练文件"));
    report.insert(QStringLiteral("anomaly_count"), anomalies.size());
    qint64 anomalySourceTokens = 0;
    for (const QJsonValue &value : anomalies)
        anomalySourceTokens += value.toObject().value(QStringLiteral("source_length")).toInt();
    report.insert(QStringLiteral("anomaly_source_tokens"), anomalySourceTokens);
    report.insert(QStringLiteral("anomaly_codes"), anomalyCodes);
    report.insert(QStringLiteral("anomalies"), anomalies);
    if (!atomicJson(reportPath, report, &error))
        return failure(error);
    return {{QStringLiteral("ok"), true}, {QStringLiteral("result"), QVariantMap{
        {QStringLiteral("report"), report.toVariantMap()}, {QStringLiteral("report_path"), reportPath},
        {QStringLiteral("anomaly_count"), anomalies.size()}}}};
}

QVariantMap TrainingShardingService::preflight(const QString &datasetPath, const QString &outputPath,
                                                const QString &llamaServerUrl, int checkedMaxLength) {
    const QFileInfo datasetInfo(datasetPath);
    if (!datasetInfo.isFile())
        return failure(QStringLiteral("训练数据文件不存在：%1").arg(datasetPath));
    if (outputPath.trimmed().isEmpty())
        return failure(QStringLiteral("必须提供报告输出路径。"));
    if (checkedMaxLength != 0 && checkedMaxLength < 128)
        return failure(QStringLiteral("--check-max-length 不得小于 128。"));
    const QUrl serverUrl(llamaServerUrl);
    if (!serverUrl.isValid() || (serverUrl.scheme() != QLatin1String("http")
                                  && serverUrl.scheme() != QLatin1String("https")))
        return failure(QStringLiteral("llama-server URL 必须是有效的 HTTP(S) 地址。"));

    QFile input(datasetInfo.absoluteFilePath());
    if (!input.open(QIODevice::ReadOnly | QIODevice::Text))
        return failure(QStringLiteral("无法读取训练数据：%1").arg(input.errorString()));
    LlamaTokenizer tokenizer(llamaServerUrl);
    QList<int> promptLengths;
    QList<int> fullLengths;
    QSet<QString> policies;
    QList<QJsonObject> longest;
    int rows = 0;
    int decisions = 0;
    int lineNumber = 0;
    while (!input.atEnd()) {
        const QByteArray line = input.readLine();
        ++lineNumber;
        if (line.trimmed().isEmpty())
            continue;
        ++rows;
        QJsonParseError parseError{};
        const QJsonDocument document = QJsonDocument::fromJson(line, &parseError);
        if (parseError.error != QJsonParseError::NoError || !document.isObject())
            return failure(QStringLiteral("训练数据第 %1 行不是有效的 JSON 对象：%2")
                               .arg(lineNumber).arg(parseError.errorString()));
        const QJsonObject row = document.object();
        const QJsonArray sourceMessages = row.value(QStringLiteral("messages")).toArray();
        if (sourceMessages.size() < 3)
            return failure(QStringLiteral("训练数据第 %1 行至少需要 system、user 和 assistant 消息。").arg(lineNumber));
        QJsonArray messages = sourceMessages;
        if (messages.first().toObject().value(QStringLiteral("role")).toString() != QLatin1String("system"))
            return failure(QStringLiteral("训练数据第 %1 行的首条消息必须是 system。不同于 system 的首条角色不受支持。")
                               .arg(lineNumber));
        const QJsonObject metadata = row.value(QStringLiteral("metadata")).toObject();
        const QString policy = metadata.value(QStringLiteral("training_policy")).toString();
        if (!policy.isEmpty())
            policies.insert(policy);
        QJsonObject systemMessage = messages.first().toObject();
        systemMessage.insert(QStringLiteral("content"), preflightSystem(policy));
        messages[0] = systemMessage;
        const QJsonValue toolsValue = row.value(QStringLiteral("tools"));
        if (!toolsValue.isUndefined() && (!toolsValue.isArray() || toolsValue.toArray().isEmpty()))
            return failure(QStringLiteral("训练数据第 %1 行的 tools 必须是非空数组。").arg(lineNumber));

        QList<int> assistantIndexes;
        for (qsizetype index = 0; index < messages.size(); ++index) {
            if (messages.at(index).toObject().value(QStringLiteral("role")).toString()
                == QLatin1String("assistant"))
                assistantIndexes.append(static_cast<int>(index));
        }
        if (assistantIndexes.isEmpty())
            return failure(QStringLiteral("训练数据第 %1 行没有 assistant 决策。").arg(lineNumber));
        const QString baseCase = metadata.value(QStringLiteral("case")).toString().isEmpty()
            ? QStringLiteral("line-%1").arg(lineNumber)
            : metadata.value(QStringLiteral("case")).toString();
        for (qsizetype decision = 0; decision < assistantIndexes.size(); ++decision) {
            const int assistantIndex = assistantIndexes.at(decision);
            QJsonObject promptRow = row;
            QJsonArray promptMessages;
            for (int index = 0; index < assistantIndex; ++index)
                promptMessages.append(messages.at(index));
            promptRow.insert(QStringLiteral("messages"), promptMessages);
            QJsonObject fullRow = promptRow;
            QJsonArray fullMessages = promptMessages;
            fullMessages.append(messages.at(assistantIndex));
            fullRow.insert(QStringLiteral("messages"), fullMessages);
            QJsonArray promptTokens;
            QJsonArray fullTokens;
            QString error;
            if (!tokenizer.tokenIds(promptRow, &promptTokens, &error, true, false)
                || !tokenizer.tokenIds(fullRow, &fullTokens, &error, false, false))
                return failure(QStringLiteral("训练数据第 %1 行 tokenizer 检查失败：%2").arg(lineNumber).arg(error));
            if (promptTokens.size() > fullTokens.size())
                return failure(QStringLiteral("训练数据第 %1 行第 %2 个 assistant 决策的 prompt 长于完整序列。")
                                   .arg(lineNumber).arg(decision + 1));
            for (qsizetype index = 0; index < promptTokens.size(); ++index) {
                if (promptTokens.at(index) != fullTokens.at(index))
                    return failure(QStringLiteral("训练数据第 %1 行第 %2 个 assistant 决策的完整序列不以 prompt token 为前缀。")
                                       .arg(lineNumber).arg(decision + 1));
            }
            const QString caseName = assistantIndexes.size() == 1 ? baseCase
                : QStringLiteral("%1/assistant-%2").arg(baseCase).arg(decision + 1);
            const int promptLength = promptTokens.size();
            const int fullLength = fullTokens.size();
            promptLengths.append(promptLength);
            fullLengths.append(fullLength);
            longest.append(QJsonObject{{QStringLiteral("case"), caseName},
                {QStringLiteral("prompt_tokens"), promptLength},
                {QStringLiteral("full_tokens"), fullLength}});
            ++decisions;
        }
    }
    if (input.error() != QFileDevice::NoError)
        return failure(QStringLiteral("读取训练数据失败：%1").arg(input.errorString()));
    if (fullLengths.isEmpty())
        return failure(QStringLiteral("训练数据没有可审计的 assistant 决策。"));
    QString hashError;
    const QByteArray digest = sha256File(datasetInfo.absoluteFilePath(), &hashError);
    if (digest.isEmpty())
        return failure(hashError);

    const QList<int> thresholds{1280, 2048, 4096, 8192, 16384, 32768, 65536};
    std::sort(longest.begin(), longest.end(), [](const QJsonObject &a, const QJsonObject &b) {
        if (a.value(QStringLiteral("full_tokens")).toInt() != b.value(QStringLiteral("full_tokens")).toInt())
            return a.value(QStringLiteral("full_tokens")).toInt() > b.value(QStringLiteral("full_tokens")).toInt();
        return a.value(QStringLiteral("case")).toString() > b.value(QStringLiteral("case")).toString();
    });
    QJsonArray longestSorted;
    for (qsizetype index = 0; index < qMin<qsizetype>(10, longest.size()); ++index)
        longestSorted.append(longest.at(index));
    QJsonArray policyArray;
    QStringList sortedPolicies = policies.values();
    std::sort(sortedPolicies.begin(), sortedPolicies.end());
    for (const QString &policy : sortedPolicies)
        policyArray.append(policy);
    const int maximum = *std::max_element(fullLengths.cbegin(), fullLengths.cend());
    int recommended = 128;
    while (recommended < maximum && recommended <= (std::numeric_limits<int>::max() / 2))
        recommended *= 2;
    const int overlong = checkedMaxLength > 0
        ? std::count_if(fullLengths.cbegin(), fullLengths.cend(), [checkedMaxLength](int value) {
            return value > checkedMaxLength;
        }) : 0;
    QJsonObject report{
        {QStringLiteral("created_at"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
        {QStringLiteral("dataset"), datasetInfo.canonicalFilePath()},
        {QStringLiteral("dataset_sha256"), QString::fromLatin1(digest)},
        {QStringLiteral("rows"), rows},
        {QStringLiteral("assistant_decisions"), decisions},
        {QStringLiteral("training_policies"), policyArray},
        {QStringLiteral("tokenizer_source"), QStringLiteral("llama.cpp: %1").arg(llamaServerUrl)},
        {QStringLiteral("prompt_tokens"), tokenLengthSummary(promptLengths, thresholds)},
        {QStringLiteral("full_tokens"), tokenLengthSummary(fullLengths, thresholds)},
        {QStringLiteral("recommended_max_length"), recommended},
        {QStringLiteral("longest_examples"), longestSorted},
        {QStringLiteral("truncation_allowed"), false},
        {QStringLiteral("status"), checkedMaxLength == 0 ? QStringLiteral("measured")
             : overlong == 0 ? QStringLiteral("passed") : QStringLiteral("failed")}};
    if (checkedMaxLength > 0) {
        report.insert(QStringLiteral("checked_max_length"), checkedMaxLength);
        report.insert(QStringLiteral("overlong_examples"), overlong);
    }
    QString writeError;
    if (!atomicJson(QFileInfo(outputPath).absoluteFilePath(), report, &writeError))
        return failure(writeError);
    return {{QStringLiteral("ok"), true}, {QStringLiteral("result"), QVariantMap{
        {QStringLiteral("report"), report.toVariantMap()},
        {QStringLiteral("report_path"), QFileInfo(outputPath).absoluteFilePath()},
        {QStringLiteral("exit_code"), checkedMaxLength > 0 && overlong > 0 ? 2 : 0}}}};
}
