#include "nativesubagentservice.h"

#include "agentpromptbuilder.h"
#include "sessioncatalog.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QCryptographicHash>
#include <QSet>
#include <QUuid>
#include <algorithm>

namespace {
QString nowUtc() {
    return QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
}

QJsonObject sessionThread(const QVariantMap &project, const QString &parentId,
                          const QString &agentRoot, const QString &threadId) {
    const QVariantMap loaded = SessionCatalog::dispatch(QStringLiteral("session_runtime_load"), project,
        {{QStringLiteral("thread_id"), threadId}}, agentRoot);
    return QJsonObject::fromVariantMap(loaded.value(QStringLiteral("result")).toMap()
        .value(QStringLiteral("thread")).toMap());
}

bool isOk(const QVariantMap &result) {
    return result.value(QStringLiteral("ok")).toBool();
}

QString callName(const QJsonObject &definition) {
    QString name = definition.value(QStringLiteral("name")).toString();
    if (name.isEmpty())
        name = definition.value(QStringLiteral("function")).toObject()
            .value(QStringLiteral("name")).toString();
    return name;
}

QJsonArray childInput(const QJsonObject &thread, const QString &prompt) {
    QJsonArray input;
    for (const QJsonValue &value : thread.value(QStringLiteral("conversation")).toArray()) {
        const QJsonObject message = value.toObject();
        const QString role = message.value(QStringLiteral("role")).toString();
        const QString text = message.value(QStringLiteral("text")).toString();
        if ((role == QStringLiteral("user") || role == QStringLiteral("assistant")) && !text.isEmpty())
            input.append(QJsonObject{{QStringLiteral("role"), role},
                                     {QStringLiteral("content"), text}});
    }
    input.append(QJsonObject{{QStringLiteral("role"), QStringLiteral("user")},
                             {QStringLiteral("content"), prompt}});
    return input;
}

QJsonArray appendTurnRecord(const QJsonArray &records, const QJsonObject &record) {
    QJsonArray updated = records;
    for (qsizetype index = 0; index < updated.size(); ++index) {
        if (updated.at(index).toObject().value(QStringLiteral("turn_id")).toString()
            == record.value(QStringLiteral("turn_id")).toString()) {
            updated.replace(index, record);
            return updated;
        }
    }
    updated.append(record);
    return updated;
}
}

NativeSubagentService::NativeSubagentService(QObject *parent) : QObject(parent) {}

NativeSubagentService::~NativeSubagentService() {
    const auto active = m_active;
    for (ActiveTask *task : active) {
        if (task->runner) {
            task->runner->cancel();
            delete task->runner;
        }
        delete task;
    }
}

void NativeSubagentService::setMaximumParallelTurns(int maximum) {
    m_maximumParallelTurns = qBound(1, maximum, 32);
    pump();
}

int NativeSubagentService::maximumParallelTurns() const { return m_maximumParallelTurns; }
int NativeSubagentService::activeTurnCount() const { return m_active.size(); }
int NativeSubagentService::queuedTaskCount() const { return m_queue.size(); }

QString NativeSubagentService::taskKey(const QString &role, const QString &task) {
    const QByteArray digest = QCryptographicHash::hash(task.trimmed().toUtf8(), QCryptographicHash::Sha256)
        .toHex().left(12);
    return role.trimmed().toLower() + QLatin1Char(':') + QString::fromLatin1(digest);
}

QString NativeSubagentService::makeName(const QString &role, const QString &specialty,
                                        const QString &childId) {
    static const QStringList names{
        QStringLiteral("Aoi"), QStringLiteral("Mio"), QStringLiteral("Yuna"),
        QStringLiteral("Rin"), QStringLiteral("Hana"), QStringLiteral("Sofia"),
        QStringLiteral("Lucia"), QStringLiteral("Maya"), QStringLiteral("Nana"),
        QStringLiteral("Emi"), QStringLiteral("Clara"), QStringLiteral("Isabel"),
    };
    const uint hash = qHash(childId);
    const QString nick = names.at(static_cast<qsizetype>(hash % static_cast<uint>(names.size())));
    const QString label = specialty.trimmed().isEmpty() ? role.trimmed() : specialty.trimmed();
    return nick + QStringLiteral(" · ") + label.left(48);
}

QJsonArray NativeSubagentService::inheritedTools(const QJsonArray &tools) {
    static const QSet<QString> disabled{
        QStringLiteral("spawn_subagent"), QStringLiteral("spawn_agent"),
        QStringLiteral("list_agents"), QStringLiteral("wait_agent"),
        QStringLiteral("send_message"), QStringLiteral("followup_task"),
        QStringLiteral("interrupt_agent"), QStringLiteral("wait_agents"),
    };
    QJsonArray filtered;
    for (const QJsonValue &value : tools) {
        const QJsonObject definition = value.toObject();
        if (!disabled.contains(callName(definition)))
            filtered.append(definition);
    }
    return filtered;
}

QString NativeSubagentService::submit(const Task &task, ToolExecutor executeTool) {
    PendingTask pending;
    pending.handle = QUuid::createUuid().toString(QUuid::WithoutBraces);
    pending.task = task;
    pending.executeTool = std::move(executeTool);
    pending.task.task = task.task.trimmed();
    pending.task.role = task.role.trimmed().toLower();
    if (pending.task.role.isEmpty())
        pending.task.role = QStringLiteral("researcher");
    pending.task.name = task.name.trimmed().left(80);
    if (pending.task.name.isEmpty())
        pending.task.name = task.specialty.trimmed().left(80);
    pending.task.specialty = task.specialty.trimmed().left(80);
    pending.taskKey = taskKey(pending.task.role, pending.task.task);
    QString error;
    if (!prepare(pending, &error)) {
        const QJsonObject result{
            {QStringLiteral("status"), QStringLiteral("failed")},
            {QStringLiteral("reason"), error},
            {QStringLiteral("task_handle"), pending.handle},
        };
        emit parentEvent(task.parentSessionId, QJsonObject{
            {QStringLiteral("event"), QStringLiteral("subagent_completed")},
            {QStringLiteral("status"), QStringLiteral("failed")},
            {QStringLiteral("reason"), error},
        });
        emit resultReady(task.parentSessionId, result);
        return pending.handle;
    }

    emit parentEvent(task.parentSessionId, QJsonObject{
        {QStringLiteral("event"), pending.isFollowup ? QStringLiteral("subagent_followup")
                                                     : QStringLiteral("subagent_spawned")},
        {QStringLiteral("subagent_id"), pending.childSessionId},
        {QStringLiteral("subagent_name"), pending.childName},
        {QStringLiteral("role"), pending.task.role},
        {QStringLiteral("specialty"), pending.task.specialty},
        {QStringLiteral("task"), pending.task.task},
        {QStringLiteral("task_handle"), pending.handle},
    });
    const QString handle = pending.handle;
    m_queue.enqueue(std::move(pending));
    pump();
    return handle;
}

bool NativeSubagentService::cancel(const QString &taskHandle) {
    for (qsizetype index = 0; index < m_queue.size(); ++index) {
        if (m_queue.at(index).handle != taskHandle)
            continue;
        PendingTask pending = m_queue.takeAt(index);
        QJsonObject thread = pending.childThread;
        thread.insert(QStringLiteral("active_turn_id"), QString{});
        thread.insert(QStringLiteral("recovery_pending"), false);
        thread.insert(QStringLiteral("recovery_reason"), QStringLiteral("cancelled_before_start"));
        persistThread(pending, thread, nullptr);
        emit resultReady(pending.task.parentSessionId, QJsonObject{
            {QStringLiteral("status"), QStringLiteral("cancelled")},
            {QStringLiteral("subagent_id"), pending.childSessionId},
            {QStringLiteral("task_handle"), taskHandle},
        });
        return true;
    }
    for (auto iterator = m_active.begin(); iterator != m_active.end(); ++iterator) {
        if (iterator.value()->pending.handle != taskHandle)
            continue;
        iterator.value()->runner->cancel();
        return true;
    }
    return false;
}

bool NativeSubagentService::prepare(PendingTask &pending, QString *error) {
    const Task &task = pending.task;
    if (task.task.isEmpty() || task.task.size() > 12'000) {
        *error = QStringLiteral("子智能体必须提供 1 至 12000 字符的详细任务目标。");
        return false;
    }
    if (task.parentSessionId.trimmed().isEmpty() || task.agentRoot.trimmed().isEmpty()
        || task.project.value(QStringLiteral("id")).toString().trimmed().isEmpty()) {
        *error = QStringLiteral("项目、agent 根目录和父会话标识均为必填项。");
        return false;
    }
    if (!task.turn.maximumToolRounds || task.turn.model.trimmed().isEmpty()) {
        *error = QStringLiteral("子智能体 Responses turn 配置缺少模型或工具轮数限制。");
        return false;
    }
    static const QSet<QString> supportedRoles{
        QStringLiteral("researcher"), QStringLiteral("diagnostics"), QStringLiteral("reviewer"),
    };
    if (!supportedRoles.contains(task.role)) {
        *error = QStringLiteral("不支持的子智能体角色；可用 researcher、diagnostics 或 reviewer。");
        return false;
    }
    const QVariantMap parentLoad = SessionCatalog::dispatch(QStringLiteral("session_runtime_load"), task.project,
        {{QStringLiteral("thread_id"), task.parentSessionId}}, task.agentRoot);
    const QJsonObject parentThread = QJsonObject::fromVariantMap(parentLoad.value(QStringLiteral("result")).toMap()
        .value(QStringLiteral("thread")).toMap());
    if (!isOk(parentLoad) || parentThread.isEmpty()
        || !parentThread.value(QStringLiteral("parent_thread_id")).toString().isEmpty()) {
        *error = QStringLiteral("子智能体必须挂接到当前项目有效的根会话。");
        return false;
    }
    const QString canonicalWorkspace = parentThread.value(QStringLiteral("workspace")).toString();

    pending.childThread = {};
    const QString requested = task.targetChildSessionId.trimmed();
    if (!requested.isEmpty()) {
        pending.childThread = sessionThread(task.project, task.parentSessionId, task.agentRoot, requested);
        if (pending.childThread.value(QStringLiteral("parent_thread_id")).toString() != task.parentSessionId) {
            *error = QStringLiteral("指定的子会话不存在或不属于当前父会话。");
            return false;
        }
        pending.isFollowup = true;
    } else {
        const QVariantMap childrenResult = SessionCatalog::dispatch(QStringLiteral("session_children"), task.project,
            {{QStringLiteral("thread_id"), task.parentSessionId}}, task.agentRoot);
        if (isOk(childrenResult)) {
            const QVariantList children = childrenResult.value(QStringLiteral("result")).toMap()
                .value(QStringLiteral("children")).toList();
            for (const QVariant &entry : children) {
                const QString childId = entry.toMap().value(QStringLiteral("id")).toString();
                const QJsonObject child = sessionThread(task.project, task.parentSessionId, task.agentRoot, childId);
                if (child.value(QStringLiteral("subagent_task_name")).toString() != pending.taskKey)
                    continue;
                pending.childThread = child;
                pending.isFollowup = true;
                break;
            }
        }
    }

    if (pending.childThread.isEmpty()) {
        pending.childSessionId = QUuid::createUuid().toString(QUuid::WithoutBraces);
        pending.childName = makeName(task.role, task.name, pending.childSessionId);
        const QString now = nowUtc();
        pending.childThread = QJsonObject{
            {QStringLiteral("schema_version"), 2},
            {QStringLiteral("id"), pending.childSessionId},
            {QStringLiteral("project_id"), task.project.value(QStringLiteral("id")).toString()},
            {QStringLiteral("workspace"), canonicalWorkspace},
            {QStringLiteral("created_at"), now}, {QStringLiteral("updated_at"), now},
            {QStringLiteral("provider"), QStringLiteral("api")},
            {QStringLiteral("provider_thread_id"), pending.childSessionId},
            {QStringLiteral("active_turn_id"), QString{}},
            {QStringLiteral("turns"), QJsonArray{}}, {QStringLiteral("turn_records"), QJsonArray{}},
            {QStringLiteral("conversation"), QJsonArray{}},
            {QStringLiteral("compacted_context"), QJsonArray{}},
            {QStringLiteral("tool_history"), QJsonArray{}},
            {QStringLiteral("name"), pending.childName}, {QStringLiteral("archived"), false},
            {QStringLiteral("parent_thread_id"), task.parentSessionId},
            {QStringLiteral("subagent_task_name"), pending.taskKey},
            {QStringLiteral("subagent_role"), task.role},
            {QStringLiteral("subagent_name"), task.name},
            {QStringLiteral("subagent_specialty"), task.specialty},
            {QStringLiteral("subagent_task_goal"), task.task},
            {QStringLiteral("recovery_pending"), false},
            {QStringLiteral("recovery_goal"), QString{}},
            {QStringLiteral("recovery_turn_id"), QString{}},
            {QStringLiteral("recovery_reason"), QString{}},
            {QStringLiteral("recovery_state"), QJsonObject{}},
            {QStringLiteral("execution_progress"), 0},
            {QStringLiteral("execution_phase"), QStringLiteral("Ready")},
            {QStringLiteral("settings_snapshot"), QJsonObject{}},
            {QStringLiteral("token_usage"), QJsonObject{}},
        };
    } else {
        pending.childSessionId = pending.childThread.value(QStringLiteral("id")).toString();
        pending.childName = pending.childThread.value(QStringLiteral("name")).toString();
    }

    for (const ActiveTask *active : m_active) {
        if (active->pending.childSessionId == pending.childSessionId) {
            *error = QStringLiteral("该子智能体会话已有运行中的 turn；请等待完成后再 follow up。");
            return false;
        }
    }
    for (const PendingTask &queued : m_queue) {
        if (queued.childSessionId == pending.childSessionId) {
            *error = QStringLiteral("该子智能体会话已有排队中的 turn；请等待完成后再 follow up。");
            return false;
        }
    }

    pending.turnId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    pending.startedAt = nowUtc();
    const QString prompt = task.context.trimmed().isEmpty() ? task.task
        : task.task + QStringLiteral("\n\n主智能体提供的上下文：\n") + task.context.left(8'000);
    QJsonObject child = pending.childThread;
    child.insert(QStringLiteral("name"), pending.childName);
    child.insert(QStringLiteral("active_turn_id"), pending.turnId);
    child.insert(QStringLiteral("updated_at"), pending.startedAt);
    child.insert(QStringLiteral("recovery_pending"), true);
    child.insert(QStringLiteral("recovery_goal"), task.task);
    child.insert(QStringLiteral("recovery_turn_id"), pending.turnId);
    child.insert(QStringLiteral("recovery_reason"), QStringLiteral("running"));
    child.insert(QStringLiteral("recovery_updated_at"), pending.startedAt);
    child.insert(QStringLiteral("subagent_task_goal"), task.task);

    QJsonArray conversation = child.value(QStringLiteral("conversation")).toArray();
    conversation.append(QJsonObject{{QStringLiteral("role"), QStringLiteral("user")},
                                    {QStringLiteral("text"), prompt}});
    child.insert(QStringLiteral("conversation"), conversation);
    QJsonObject turnRecord{
        {QStringLiteral("turn_id"), pending.turnId}, {QStringLiteral("status"), QStringLiteral("running")},
        {QStringLiteral("goal"), task.task}, {QStringLiteral("answer"), QString{}},
        {QStringLiteral("started_at"), pending.startedAt}, {QStringLiteral("finished_at"), QString{}},
        {QStringLiteral("resume"), pending.isFollowup}, {QStringLiteral("interaction_mode"), QStringLiteral("default")},
        {QStringLiteral("reason"), QString{}}, {QStringLiteral("usage"), QJsonObject{}},
    };
    child.insert(QStringLiteral("turn_records"), appendTurnRecord(child.value(QStringLiteral("turn_records")).toArray(), turnRecord));
    if (!persistThread(pending, child, error))
        return false;
    pending.childThread = child;
    pending.task.turn.instructions = AgentPromptBuilder::subagentInstructions(task.role, task.task);
    pending.task.turn.input = childInput(child, prompt);
    pending.task.turn.tools = inheritedTools(task.turn.tools);
    return true;
}

bool NativeSubagentService::persistThread(const PendingTask &pending, const QJsonObject &thread,
                                          QString *error) const {
    QVariantMap payload{{QStringLiteral("thread"), thread.toVariantMap()}};
    const QVariantMap saved = SessionCatalog::dispatch(QStringLiteral("session_runtime_save"),
        pending.task.project, payload, pending.task.agentRoot);
    if (isOk(saved))
        return true;
    if (error) {
        *error = saved.value(QStringLiteral("error")).toString();
        if (error->isEmpty())
            *error = QStringLiteral("无法持久化子智能体会话。");
    }
    return false;
}

void NativeSubagentService::pump() {
    while (m_active.size() < m_maximumParallelTurns && !m_queue.isEmpty())
        launch(m_queue.dequeue());
}

void NativeSubagentService::launch(PendingTask pending) {
    auto *active = new ActiveTask;
    active->pending = std::move(pending);
    active->runner = new ResponsesTurnRunner(this);
    const QString handle = active->pending.handle;
    const QString parentId = active->pending.task.parentSessionId;
    const QString childId = active->pending.childSessionId;
    const QString childName = active->pending.childName;
    const QString role = active->pending.task.role;
    connect(active->runner, &ResponsesTurnRunner::modelEvent, this,
        [this, parentId, childId, childName, role](const QJsonObject &modelEvent) {
            emit parentEvent(parentId, QJsonObject{
                {QStringLiteral("event"), QStringLiteral("subagent_activity")},
                {QStringLiteral("subagent_id"), childId},
                {QStringLiteral("subagent_name"), childName},
                {QStringLiteral("subagent_role"), role},
                {QStringLiteral("activity"), modelEvent},
            });
        });
    connect(active->runner, &ResponsesTurnRunner::toolStarted, this,
        [this, parentId, childId, childName, role](const QString &name, const QString &callId,
                                                   const QJsonObject &arguments) {
            emit parentEvent(parentId, QJsonObject{
                {QStringLiteral("event"), QStringLiteral("subagent_tool_started")},
                {QStringLiteral("subagent_id"), childId}, {QStringLiteral("subagent_name"), childName},
                {QStringLiteral("subagent_role"), role}, {QStringLiteral("name"), name},
                {QStringLiteral("call_id"), callId}, {QStringLiteral("arguments"), arguments},
            });
        });
    connect(active->runner, &ResponsesTurnRunner::toolFinished, this,
        [this, parentId, childId, childName, role](const QString &name, const QString &callId,
                                                   const QJsonObject &result) {
            emit parentEvent(parentId, QJsonObject{
                {QStringLiteral("event"), QStringLiteral("subagent_tool_finished")},
                {QStringLiteral("subagent_id"), childId}, {QStringLiteral("subagent_name"), childName},
                {QStringLiteral("subagent_role"), role}, {QStringLiteral("name"), name},
                {QStringLiteral("call_id"), callId}, {QStringLiteral("result"), result},
            });
        });
    connect(active->runner, &ResponsesTurnRunner::completed, this,
        [this, handle](const QString &answer, int rounds) {
            finish(handle, QStringLiteral("completed"), answer, {}, rounds);
        });
    connect(active->runner, &ResponsesTurnRunner::failed, this,
        [this, handle](const QString &error, int rounds) {
            finish(handle, QStringLiteral("failed"), {}, error, rounds);
        });
    m_active.insert(handle, active);
    active->runner->start(active->pending.task.turn, active->pending.executeTool);
}

void NativeSubagentService::finish(const QString &handle, const QString &status,
                                   const QString &answer, const QString &error, int toolRounds) {
    ActiveTask *active = m_active.take(handle);
    if (!active)
        return;
    PendingTask pending = active->pending;
    const QString finished = nowUtc();
    QJsonObject thread = pending.childThread;
    if (status == QStringLiteral("completed")) {
        QJsonArray conversation = thread.value(QStringLiteral("conversation")).toArray();
        conversation.append(QJsonObject{{QStringLiteral("role"), QStringLiteral("assistant")},
                                        {QStringLiteral("text"), answer.left(12'000)}});
        thread.insert(QStringLiteral("conversation"), conversation);
        QJsonArray turns = thread.value(QStringLiteral("turns")).toArray();
        turns.append(pending.turnId);
        thread.insert(QStringLiteral("turns"), turns);
    }
    thread.insert(QStringLiteral("active_turn_id"), QString{});
    thread.insert(QStringLiteral("recovery_pending"), status != QStringLiteral("completed"));
    thread.insert(QStringLiteral("recovery_reason"), status == QStringLiteral("completed") ? QString{} : status);
    thread.insert(QStringLiteral("recovery_updated_at"), finished);
    thread.insert(QStringLiteral("updated_at"), finished);
    QJsonObject record;
    for (const QJsonValue &value : thread.value(QStringLiteral("turn_records")).toArray()) {
        if (value.toObject().value(QStringLiteral("turn_id")).toString() == pending.turnId) {
            record = value.toObject();
            break;
        }
    }
    record.insert(QStringLiteral("status"), status);
    record.insert(QStringLiteral("answer"), answer.left(12'000));
    record.insert(QStringLiteral("reason"), error.left(1'000));
    record.insert(QStringLiteral("finished_at"), finished);
    record.insert(QStringLiteral("tool_rounds"), toolRounds);
    thread.insert(QStringLiteral("turn_records"), appendTurnRecord(thread.value(QStringLiteral("turn_records")).toArray(), record));
    QString persistError;
    if (!persistThread(pending, thread, &persistError) && status == QStringLiteral("completed")) {
        emit resultReady(pending.task.parentSessionId, QJsonObject{
            {QStringLiteral("status"), QStringLiteral("failed")},
            {QStringLiteral("subagent_id"), pending.childSessionId},
            {QStringLiteral("reason"), QStringLiteral("结果未能持久化：") + persistError},
        });
        emit parentEvent(pending.task.parentSessionId, QJsonObject{
            {QStringLiteral("event"), QStringLiteral("subagent_completed")},
            {QStringLiteral("subagent_id"), pending.childSessionId},
            {QStringLiteral("status"), QStringLiteral("failed")},
            {QStringLiteral("reason"), persistError},
        });
    } else {
        const QJsonObject result{
            {QStringLiteral("status"), status}, {QStringLiteral("subagent_id"), pending.childSessionId},
            {QStringLiteral("subagent_name"), pending.childName}, {QStringLiteral("role"), pending.task.role},
            {QStringLiteral("answer"), answer.left(12'000)}, {QStringLiteral("reason"), error.left(1'000)},
            {QStringLiteral("tool_rounds"), toolRounds}, {QStringLiteral("task_handle"), handle},
        };
        emit resultReady(pending.task.parentSessionId, result);
        emit parentEvent(pending.task.parentSessionId, QJsonObject{
            {QStringLiteral("event"), QStringLiteral("subagent_completed")},
            {QStringLiteral("subagent_id"), pending.childSessionId},
            {QStringLiteral("subagent_name"), pending.childName},
            {QStringLiteral("role"), pending.task.role},
            {QStringLiteral("specialty"), pending.task.specialty},
            {QStringLiteral("status"), status},
            {QStringLiteral("answer"), answer.left(12'000)},
            {QStringLiteral("reason"), error.left(1'000)},
        });
    }
    if (active->runner) {
        active->runner->deleteLater();
        active->runner = nullptr;
    }
    delete active;
    pump();
}
