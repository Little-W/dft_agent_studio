#include "agentcontroller.h"
#include "agenttoolservice.h"
#include "agentpromptbuilder.h"
#include "studiopaths.h"
#include "workspacedatabase.h"
#include "workspacecatalog.h"
#include "sessioncatalog.h"
#include "patchactionservice.h"
#include "responsesroundclient.h"
#include "responsesturnrunner.h"
#include "localmodelruntimeservice.h"
#include "dftreportevidenceservice.h"
#include "nativememoryservice.h"
#include "agentepisodeservice.h"

#include <algorithm>
#include <QCryptographicHash>
#include <QFileInfo>
#include <QSaveFile>
#include <QHash>
#include <QGuiApplication>
#include <QClipboard>
#include <QFile>
#include <QDir>
#include <QDirIterator>
#include <QDateTime>
#include <QMetaObject>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QProcessEnvironment>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QSet>
#include <QUrl>
#include <QSettings>
#include <QUuid>
#include <QSet>
#include <QStandardPaths>
#include <QThreadPool>
#include <QTextDocument>
#include <QRegularExpression>
#include <QSet>
#include <QUrl>
#include <limits>

#include <utility>
#ifdef Q_OS_UNIX
#include <signal.h>
#endif

namespace {

bool nativeLocalModelConfigured(const QVariantMap &project, const QString &provider) {
    if (provider != QStringLiteral("legacy"))
        return false;
    const QString runtime = project.value(QStringLiteral("modelRuntime")).toString().trimmed();
    if (runtime == QStringLiteral("llama_cpp"))
        return true;
    if (runtime != QStringLiteral("hf_lora"))
        return false;
    const auto isGguf = [](const QString &path) {
        return QFileInfo(path.trimmed()).suffix().compare(QStringLiteral("gguf"), Qt::CaseInsensitive) == 0;
    };
    return isGguf(project.value(QStringLiteral("modelBasePath")).toString())
        && isGguf(project.value(QStringLiteral("modelAdapterPath")).toString());
}

QString displayJson(const QJsonValue &value) {
    if (value.isString())
        return value.toString();
    if (value.isObject())
        return QString::fromUtf8(QJsonDocument(value.toObject()).toJson(QJsonDocument::Indented)).trimmed();
    if (value.isArray())
        return QString::fromUtf8(QJsonDocument(value.toArray()).toJson(QJsonDocument::Indented)).trimmed();
    if (value.isNull() || value.isUndefined())
        return {};
    return value.toVariant().toString();
}

QString displayToolArguments(const QJsonValue &value) {
    if (value.isObject() && value.toObject().isEmpty())
        return QStringLiteral("无参数");
    if (value.isArray() && value.toArray().isEmpty())
        return QStringLiteral("无参数");
    if (value.isString()) {
        const QString text = value.toString().trimmed();
        QJsonParseError error;
        const QJsonDocument document = QJsonDocument::fromJson(text.toUtf8(), &error);
        if (error.error == QJsonParseError::NoError
            && ((document.isObject() && document.object().isEmpty())
                || (document.isArray() && document.array().isEmpty())))
            return QStringLiteral("无参数");
    }
    return displayJson(value);
}

QString canonicalChatText(const QString &value) {
    QString source = value.trimmed();
    if (source.startsWith(QStringLiteral("&lt;!doctype"), Qt::CaseInsensitive)
        || source.startsWith(QStringLiteral("&lt;<html"), Qt::CaseInsensitive)) {
        QTextDocument decoder;
        decoder.setHtml(source);
        source = decoder.toPlainText().trimmed();
    }
    if (!source.startsWith(QStringLiteral("<!doctype"), Qt::CaseInsensitive)
        && !source.startsWith(QStringLiteral("<html"), Qt::CaseInsensitive))
        return value;
    QTextDocument document;
    document.setHtml(source);
    return document.toPlainText().trimmed();
}

QString boundedActivityText(const QString &value, int limit = 16'000) {
    if (value.size() <= limit)
        return value;
    const int half = limit / 2;
    return value.left(half) + QStringLiteral("\n\n… 中间内容已省略 …\n\n") + value.right(half);
}

bool isShellActivityName(const QString &name) {
    const QString normalized = name.trimmed().toLower();
    return normalized == QStringLiteral("shell")
        || normalized == QStringLiteral("shell_execute")
        || normalized == QStringLiteral("shell_poll")
        || normalized == QStringLiteral("terminal_execute")
        || normalized == QStringLiteral("terminal_poll")
        || normalized == QStringLiteral("terminal")
        || normalized == QStringLiteral("exec_command")
        || normalized == QStringLiteral("write_stdin");
}

bool isEdaActivityName(const QString &name) {
    const QString normalized = name.trimmed().toLower();
    return normalized == QStringLiteral("run_dft_flow")
        || normalized == QStringLiteral("wait_dft_job")
        || normalized == QStringLiteral("status_dft_job")
        || normalized == QStringLiteral("interrupt_dft_job")
        || normalized == QStringLiteral("run_dft_iteration")
        || normalized == QStringLiteral("run_dft_optimization")
        || normalized == QStringLiteral("run_approved_patch");
}

QJsonValue findNestedValue(const QJsonValue &value, const QStringList &keys) {
    if (value.isObject()) {
        const QJsonObject object = value.toObject();
        for (const QString &key : keys) {
            if (object.contains(key))
                return object.value(key);
        }
        for (auto it = object.begin(); it != object.end(); ++it) {
            const QJsonValue found = findNestedValue(it.value(), keys);
            if (!found.isUndefined())
                return found;
        }
    } else if (value.isArray()) {
        for (const QJsonValue &item : value.toArray()) {
            const QJsonValue found = findNestedValue(item, keys);
            if (!found.isUndefined())
                return found;
        }
    }
    return {};
}

QString nestedString(const QJsonValue &value, const QStringList &keys) {
    if (value.isObject()) {
        const QJsonObject object = value.toObject();
        for (const QString &key : keys) {
            const QJsonValue candidate = object.value(key);
            if (candidate.isString() && !candidate.toString().trimmed().isEmpty())
                return candidate.toString();
            if (candidate.isDouble())
                return QString::number(candidate.toDouble());
        }
        for (auto it = object.begin(); it != object.end(); ++it) {
            const QString nested = nestedString(it.value(), keys);
            if (!nested.isEmpty())
                return nested;
        }
    } else if (value.isArray()) {
        for (const QJsonValue &item : value.toArray()) {
            const QString nested = nestedString(item, keys);
            if (!nested.isEmpty())
                return nested;
        }
    }
    return {};
}

QString nestedCommandText(const QJsonValue &value) {
    const QJsonValue found = findNestedValue(value, {
        QStringLiteral("command"), QStringLiteral("cmd"), QStringLiteral("argv")
    });
    if (found.isArray()) {
        QStringList parts;
        for (const QJsonValue &item : found.toArray()) {
            if (!item.isString())
                continue;
            QString argument = item.toString();
            argument.replace(QLatin1Char('\''), QStringLiteral("'\\''"));
            parts.append(QLatin1Char('\'') + argument + QLatin1Char('\''));
        }
        return parts.join(QLatin1Char(' '));
    }
    return found.isString() ? found.toString() : QString{};
}

QString nestedShellDisplayCommandText(const QJsonValue &value) {
    const QJsonValue found = findNestedValue(value, {
        QStringLiteral("command"), QStringLiteral("cmd"), QStringLiteral("argv")
    });
    if (found.isArray()) {
        const QJsonArray arguments = found.toArray();
        if (arguments.size() >= 3 && arguments.at(0).isString() && arguments.at(1).isString()) {
            const QString executable = QFileInfo(arguments.at(0).toString()).fileName().toLower();
            const QString mode = arguments.at(1).toString();
            if ((executable == QStringLiteral("bash") || executable == QStringLiteral("sh"))
                && (mode == QStringLiteral("-lc") || mode == QStringLiteral("-c")
                    || mode == QStringLiteral("-l -c"))
                && arguments.at(2).isString())
                return arguments.at(2).toString();
        }
        static const QRegularExpression plainShellArgument(
            QStringLiteral("^[A-Za-z0-9_./:@%+=,-]+$"));
        QStringList displayArguments;
        for (const QJsonValue &argumentValue : arguments) {
            if (!argumentValue.isString())
                continue;
            const QString argument = argumentValue.toString();
            if (!argument.isEmpty() && plainShellArgument.match(argument).hasMatch()) {
                displayArguments.append(argument);
            } else {
                QString escaped = argument;
                escaped.replace(QLatin1Char('\''), QStringLiteral("'\\''"));
                displayArguments.append(QLatin1Char('\'') + escaped + QLatin1Char('\''));
            }
        }
        return displayArguments.join(QLatin1Char(' '));
    }
    return found.isString() ? found.toString() : QString{};
}

void mergeEdaFields(QVariantMap &entry, const QJsonValue &value) {
    entry.insert(QStringLiteral("eda"), true);
    // Completed async jobs wrap the runner payload several times:
    // job.result -> backend.result -> skill.result.execution.  Prefer that
    // exact execution record so the Chat dialog can open the real flow log,
    // rather than the configured workspace root or a JSON tool summary.
    QJsonValue executionValue = value;
    QJsonValue cursor = value;
    for (int depth = 0; depth < 6 && !cursor.isUndefined(); ++depth) {
        if (cursor.isObject() && cursor.toObject().value(QStringLiteral("execution")).isObject()) {
            executionValue = cursor.toObject().value(QStringLiteral("execution"));
            break;
        }
        if (!cursor.isObject() || !cursor.toObject().value(QStringLiteral("result")).isObject())
            break;
        cursor = cursor.toObject().value(QStringLiteral("result"));
    }
    const QString jobId = nestedString(value, {QStringLiteral("job_id"), QStringLiteral("jobId")});
    const QString operation = nestedString(value, {QStringLiteral("operation")});
    const QString state = nestedString(value, {QStringLiteral("state"), QStringLiteral("status")});
    QString workspace = nestedString(executionValue, {QStringLiteral("workspace"), QStringLiteral("flow_directory")});
    if (workspace.isEmpty())
        workspace = nestedString(value, {QStringLiteral("workspace"), QStringLiteral("workspace_path")});
    QString log = nestedString(executionValue, {QStringLiteral("log"), QStringLiteral("stdout_log"), QStringLiteral("log_path")});
    if (log.isEmpty())
        log = nestedString(value, {QStringLiteral("log"), QStringLiteral("stdout_log"), QStringLiteral("log_path")});
    QString output = nestedString(executionValue, {QStringLiteral("stdout"), QStringLiteral("terminal_output"), QStringLiteral("output")});
    if (output.isEmpty())
        output = nestedString(value, {QStringLiteral("stdout"), QStringLiteral("terminal_output"), QStringLiteral("output")});
    const QString startedAt = nestedString(value, {QStringLiteral("started_at"), QStringLiteral("startedAt")});
    const QString completedAt = nestedString(value, {QStringLiteral("completed_at"), QStringLiteral("completedAt")});
    const QString exitCode = nestedString(value, {QStringLiteral("returncode"), QStringLiteral("return_code"), QStringLiteral("exit_code")});
    const QString error = nestedString(value, {QStringLiteral("error"), QStringLiteral("message")});
    const QString command = nestedCommandText(value);
    if (!jobId.isEmpty()) entry.insert(QStringLiteral("jobId"), jobId);
    if (!operation.isEmpty()) entry.insert(QStringLiteral("edaOperation"), operation);
    if (!state.isEmpty()) entry.insert(QStringLiteral("edaState"), state);
    if (!workspace.isEmpty()) entry.insert(QStringLiteral("edaWorkspace"), workspace);
    if (!log.isEmpty()) entry.insert(QStringLiteral("edaLog"), log);
    if (!output.isEmpty()) entry.insert(QStringLiteral("edaOutput"), output);
    if (!startedAt.isEmpty()) entry.insert(QStringLiteral("edaStartedAt"), startedAt);
    if (!completedAt.isEmpty()) entry.insert(QStringLiteral("edaCompletedAt"), completedAt);
    if (!exitCode.isEmpty()) entry.insert(QStringLiteral("edaExitCode"), exitCode);
    if (!error.isEmpty()) entry.insert(QStringLiteral("edaError"), error);
    if (!command.isEmpty()) entry.insert(QStringLiteral("edaCommand"), command);
}

QVariantMap shellArgumentFields(const QString &arguments) {
    QVariantMap fields;
    const QString trimmed = arguments.trimmed();
    if (trimmed.isEmpty())
        return fields;
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(trimmed.toUtf8(), &error);
    if (error.error == QJsonParseError::NoError && document.isObject()) {
        const QJsonObject object = document.object();
        const QString commandKey = object.contains(QStringLiteral("command"))
            ? QStringLiteral("command") : QStringLiteral("cmd");
        if (object.contains(commandKey)) {
            const QString command = nestedCommandText(QJsonValue(object));
            if (!command.isEmpty())
                fields.insert(QStringLiteral("shellCommand"), command);
            const QString displayCommand = nestedShellDisplayCommandText(QJsonValue(object));
            if (!displayCommand.isEmpty())
                fields.insert(QStringLiteral("shellDisplayCommand"), displayCommand);
        }
        const QString workdirKey = object.contains(QStringLiteral("working_directory"))
            ? QStringLiteral("working_directory") : QStringLiteral("workdir");
        if (object.contains(workdirKey))
            fields.insert(QStringLiteral("workingDirectory"), object.value(workdirKey).toString());
        if (object.contains(QStringLiteral("session_id")))
            fields.insert(QStringLiteral("shellSessionId"), object.value(QStringLiteral("session_id")).toString());
    } else {
        fields.insert(QStringLiteral("shellCommand"), trimmed);
    }
    return fields;
}

void mergeShellResult(QVariantMap &entry, const QJsonObject &result) {
    const QString command = nestedCommandText(QJsonValue(result));
    const QString workdir = nestedString(result, {QStringLiteral("working_directory"), QStringLiteral("workdir")});
    const QString sessionId = nestedString(result, {QStringLiteral("session_id")});
    const QString duration = nestedString(result, {QStringLiteral("duration_seconds")});
    const QString exitCode = nestedString(result, {QStringLiteral("exit_code")});
    const QString state = nestedString(result, {QStringLiteral("state")});
    if (!command.isEmpty())
        entry.insert(QStringLiteral("shellCommand"), command);
    const QString displayCommand = nestedShellDisplayCommandText(QJsonValue(result));
    if (!displayCommand.isEmpty())
        entry.insert(QStringLiteral("shellDisplayCommand"), displayCommand);
    if (!workdir.isEmpty())
        entry.insert(QStringLiteral("workingDirectory"), workdir);
    if (!sessionId.isEmpty())
        entry.insert(QStringLiteral("shellSessionId"), sessionId);
    if (!duration.isEmpty())
        entry.insert(QStringLiteral("durationSeconds"), duration.toInt());
    if (!exitCode.isEmpty())
        entry.insert(QStringLiteral("exitCode"), exitCode.toInt());
    if (!state.isEmpty())
        entry.insert(QStringLiteral("shellState"), state);
    const QString output = nestedString(result, {QStringLiteral("output")});
    if (!output.isEmpty())
        entry.insert(QStringLiteral("shellOutput"), output);
    else {
        const QString stdoutText = nestedString(result, {QStringLiteral("stdout")});
        const QString stderrText = nestedString(result, {QStringLiteral("stderr")});
        if (!stdoutText.isEmpty() || !stderrText.isEmpty())
        entry.insert(QStringLiteral("shellOutput"), stdoutText
            + (stdoutText.isEmpty() || stderrText.isEmpty() ? QString{} : QStringLiteral("\n"))
            + stderrText);
    }
    const QJsonValue changes = findNestedValue(result, {QStringLiteral("file_changes")});
    if (changes.isArray() && !changes.toArray().isEmpty())
        entry.insert(QStringLiteral("fileChanges"), changes.toArray().toVariantList());
}

bool isReportSuffix(const QString &suffix) {
    static const QSet<QString> suffixes{
        QStringLiteral("md"), QStringLiteral("markdown"), QStringLiteral("rpt"),
        QStringLiteral("txt"), QStringLiteral("log"), QStringLiteral("json")
    };
    return suffixes.contains(suffix.toLower());
}

int flowStageRank(const QString &stage) {
    static const QStringList order{
        QStringLiteral("readProject"),
        QStringLiteral("executor"),
        QStringLiteral("synthesis"),
        QStringLiteral("scan"),
        QStringLiteral("mbist"),
        QStringLiteral("atpg"),
        QStringLiteral("lbist"),
        QStringLiteral("report"),
    };
    const int index = order.indexOf(stage);
    return index < 0 ? -1 : index;
}

bool isUnresolvedFlowState(const QString &state) {
    const QString normalized = state.trimmed().toLower();
    return normalized == QStringLiteral("running")
        || normalized == QStringLiteral("failed")
        || normalized == QStringLiteral("blocked")
        || normalized == QStringLiteral("needs_review")
        || normalized == QStringLiteral("waiting");
}

QString tracePrefix(const QJsonObject &event) {
    const int step = event.value("step").toInt();
    const QString stepLabel = step > 0 ? QStringLiteral("Step %1 ").arg(step) : QString{};
    const QString type = event.value("event").toString();
    if (type == "model_output")
        return stepLabel + QStringLiteral("Model output");
    if (type == "model_delta")
        return stepLabel + QStringLiteral("Model stream");
    if (type == "model_thinking")
        return stepLabel + QStringLiteral("Model thinking");
    if (type == "tool_call")
        return stepLabel + QStringLiteral("Tool call: %1").arg(event.value("name").toString());
    if (type == "tool_result")
        return stepLabel + QStringLiteral("Tool result: %1").arg(event.value("name").toString());
    if (type == "provider_tool_activity")
        return stepLabel + QStringLiteral("Provider tool activity: %1").arg(event.value("name").toString());
    if (type == "provider_reconnecting")
        return stepLabel + QStringLiteral("Provider reconnecting");
    if (type == "provider_reconnected")
        return stepLabel + QStringLiteral("Provider reconnected");
    if (type == "diff_updated")
        return stepLabel + QStringLiteral("File changes updated");
    if (type == "patch_proposed")
        return stepLabel + QStringLiteral("Patch proposal");
    if (type == "tool_approval_requested")
        return stepLabel + QStringLiteral("Approval requested: %1").arg(event.value("name").toString());
    if (type == "plan_updated")
        return stepLabel + QStringLiteral("Agent plan");
    if (type == "thread_ready")
        return stepLabel + QStringLiteral("Agent session ready");
    if (type == "final_answer")
        return stepLabel + QStringLiteral("Final answer");
    if (type == "error")
        return stepLabel + QStringLiteral("Error");
    if (type == "context_compacted")
        return stepLabel + QStringLiteral("Context compacted");
    if (type == "context_usage")
        return stepLabel + QStringLiteral("Context usage");
    if (type == "subagent_spawned" || type == "subagent_followup")
        return stepLabel + QStringLiteral("Subagent started: %1").arg(event.value("role").toString());
    if (type == "subagent_completed")
        return stepLabel + QStringLiteral("Subagent completed: %1").arg(event.value("role").toString());
    if (type == "provider_protocol_event")
        return stepLabel + QStringLiteral("API protocol event: %1").arg(event.value("method").toString());
    if (type == "provider_protocol_error")
        return stepLabel + QStringLiteral("API protocol error");
    if (type == "provider_error")
        return stepLabel + QStringLiteral("API provider error");
    if (type == "provider_item")
        return stepLabel + QStringLiteral("API item: %1").arg(event.value("item_type").toString());
    if (type == "turn_started")
        return stepLabel + QStringLiteral("API turn started");
    if (type == "turn_completed")
        return stepLabel + QStringLiteral("API turn completed");
    return stepLabel + QStringLiteral("Worker event");
}

} // namespace

AgentController::AgentController(QString workingDirectory, QObject *parent)
    : QObject(parent), m_workingDirectory(std::move(workingDirectory)) {
    m_sessionProgressPersistenceTimer.setSingleShot(true);
    m_sessionProgressPersistenceTimer.setInterval(300);
    connect(&m_sessionProgressPersistenceTimer, &QTimer::timeout, this,
            [this] { persistSessionProgress(); });
    m_intelligenceProvider = qEnvironmentVariable("DFT_AGENT_INTELLIGENCE_PROVIDER", "api").trimmed().toLower();
    if (m_intelligenceProvider.isEmpty())
        m_intelligenceProvider = QStringLiteral("api");
    m_demoMode = qEnvironmentVariableIntValue("DFT_AGENT_STUDIO_DEMO") == 1;
    m_nativeTurnRunner = new ResponsesTurnRunner(this);
    connect(m_nativeTurnRunner, &ResponsesTurnRunner::modelEvent,
            this, &AgentController::handleNativeResponsesEvent);
    connect(m_nativeTurnRunner, &ResponsesTurnRunner::toolStarted, this,
        [this](const QString &name, const QString &callId, const QJsonObject &arguments) {
            const int step = ++m_nativeToolStep;
            m_nativeToolSteps.insert(callId, step);
            appendToolActivity(name, displayToolArguments(arguments), step);
            appendTrace({{QStringLiteral("event"), QStringLiteral("tool_call")},
                         {QStringLiteral("name"), name},
                         {QStringLiteral("arguments"), arguments},
                         {QStringLiteral("request_id"), callId},
                         {QStringLiteral("step"), step}});
        });
    connect(m_nativeTurnRunner, &ResponsesTurnRunner::toolFinished, this,
        [this](const QString &name, const QString &callId, const QJsonObject &result) {
            const int step = m_nativeToolSteps.take(callId);
            const bool failed = result.value(QStringLiteral("ok")).isBool()
                && !result.value(QStringLiteral("ok")).toBool();
            const QString rendered = displayJson(result);
            completeToolActivity(name, rendered, step, failed);
            appendTrace({{QStringLiteral("event"), QStringLiteral("tool_result")},
                         {QStringLiteral("name"), name}, {QStringLiteral("result"), result},
                         {QStringLiteral("request_id"), callId},
                         {QStringLiteral("step"), step}});
        });
    connect(m_nativeTurnRunner, &ResponsesTurnRunner::completed,
            this, &AgentController::finishNativeResponsesRun);
    connect(m_nativeTurnRunner, &ResponsesTurnRunner::failed, this,
        [this](const QString &error, int) {
            if (error.contains(QStringLiteral("cancelled"), Qt::CaseInsensitive))
                return;
            if (!m_nativeToolRuntimeId.isEmpty()) {
                AgentToolService::closeToolRuntime(m_nativeToolRuntimeId);
                m_nativeToolRuntimeId.clear();
            }
            const bool localLlama = m_nativeProjectSnapshot.value(QStringLiteral("modelProvider")).toString()
                    .compare(QStringLiteral("legacy"), Qt::CaseInsensitive) == 0
                && m_nativeProjectSnapshot.value(QStringLiteral("modelRuntime")).toString()
                    == QStringLiteral("llama_cpp");
            setPhase(localLlama ? QStringLiteral("Paused: local model unavailable")
                                : QStringLiteral("Paused: API unavailable"));
            persistSessionEvent({{QStringLiteral("event"), QStringLiteral("turn_finished")},
                                 {QStringLiteral("status"), QStringLiteral("interrupted")},
                                 {QStringLiteral("reason"), QStringLiteral("provider_error")},
                                 {QStringLiteral("answer"), error}});
            setFailureReport(error);
            appendTrace({{QStringLiteral("event"), QStringLiteral("provider_error")},
                         {QStringLiteral("detail"), error}});
            if (!m_paused) {
                m_paused = true;
                emit pausedChanged();
            }
            setRunning(false);
            m_liveSteeringEnabled = false;
            emit finished(false);
        });
#ifdef DFT_AGENT_STUDIO_TESTING
    m_process.setWorkingDirectory(m_workingDirectory);
    m_process.setProcessChannelMode(QProcess::MergedChannels);
    connect(&m_process, &QProcess::readyReadStandardOutput, this, &AgentController::processOutput);
    connect(&m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError) {
        appendLog(QStringLiteral("Process error: %1").arg(m_process.errorString()));
    });
    connect(&m_process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this, [this](int exitCode, QProcess::ExitStatus) {
        processOutput();
        m_liveSteeringEnabled = false;
        if (m_interruptionPending) {
            m_interruptionPending = false;
            setRunning(false);
            emit finished(false);
            QTimer::singleShot(0, this, &AgentController::startNextQueuedPrompt);
            return;
        }
        if (!m_receivedResult && m_running) {
            if (!m_recoveryAttempted
                && !m_workspace.isEmpty()) {
                m_recoveryAttempted = true;
                setProgress(90);
                setPhase(QStringLiteral("Recovering staged evidence"));
                appendLog(QStringLiteral("Long project worker returned without a result; re-validating its staged evidence once."));
                if (recoverStagedProjectEvidence())
                    return;
            }
            setPhase(exitCode == 0 ? QStringLiteral("Finished without evidence") : QStringLiteral("Failed"));
            appendLog(QStringLiteral("Agent process exited before returning evidence."));
            setFailureReport(QStringLiteral("Agent 进程在返回执行证据前结束。"));
            emit errorOccurred(QStringLiteral("Agent 进程在返回执行证据前结束。"));
            emit finished(false);
        }
        setRunning(false);
        QTimer::singleShot(0, this, &AgentController::startNextQueuedPrompt);
    });
#endif
    m_localModelProcess.setWorkingDirectory(m_workingDirectory);
    m_localModelProcess.setProcessChannelMode(QProcess::MergedChannels);
    connect(&m_localModelProcess, &QProcess::readyReadStandardOutput, this, [this] {
        const QByteArray output = m_localModelProcess.readAllStandardOutput();
        if (output.isEmpty())
            return;
        m_localModelLogTail += QString::fromUtf8(output);
        if (m_localModelLogTail.size() > 8'192)
            m_localModelLogTail.remove(0, m_localModelLogTail.size() - 8'192);
    });
    connect(&m_localModelProcess, &QProcess::errorOccurred, this, [this](QProcess::ProcessError) {
        appendDetailedLog(QStringLiteral("llama-server process error: %1\n%2")
                              .arg(m_localModelProcess.errorString(), m_localModelLogTail.trimmed()));
    });
    connect(&m_localModelProcess, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [this](int exitCode, QProcess::ExitStatus status) {
        appendDetailedLog(QStringLiteral("llama-server exited (code %1, status %2).\n%3")
                              .arg(exitCode).arg(static_cast<int>(status)).arg(m_localModelLogTail.trimmed()));
        m_localModelSignature.clear();
    });
    connect(&m_demoTimer, &QTimer::timeout, this, [this] {
        if (m_demoStep >= m_demoPhases.size()) {
            m_demoTimer.stop();
            setProgress(100);
            setPhase(QStringLiteral("Verified"));
            m_executionState = QStringLiteral("completed");
            updateFlowStage(QJsonObject{
                {QStringLiteral("phase"), QStringLiteral("completed")},
                {QStringLiteral("stage"), QStringLiteral("report")},
                {QStringLiteral("stage_state"), QStringLiteral("completed")},
                {QStringLiteral("stage_percent"), 100},
            });
            appendAgentActivity(
                QStringLiteral("演示任务已完成，工具结果和当前阶段已经整理。"),
                3,
                QStringLiteral("final")
            );
            setResult(QStringLiteral("Demo run completed. No real DFT tool was invoked."));
            setReport(QStringLiteral("运行结果：演示完成\n\n最终结论\nDemo run completed. No real DFT tool was invoked."));
            appendLog(QStringLiteral("Demo evidence recorded."));
            setRunning(false);
            emit finished(true);
            QTimer::singleShot(0, this, &AgentController::startNextQueuedPrompt);
            return;
        }
        const int step = m_demoStep++;
        setPhase(m_demoPhases.at(step));
        static constexpr int demoProgress[] = {8, 18, 34, 38, 40, 50, 86};
        setProgress(demoProgress[qBound(0, step, 6)]);
        if (step == 0) {
            updateFlowStage(QJsonObject{
                {QStringLiteral("phase"), QStringLiteral("source_discovery")},
                {QStringLiteral("stage"), QStringLiteral("readProject")},
                {QStringLiteral("stage_state"), QStringLiteral("running")},
                {QStringLiteral("stage_percent"), 45},
            });
            appendAgentActivity(QStringLiteral("正在检查项目配置和输入文件。"), 1, QStringLiteral("response"));
        } else if (step == 1) {
            updateFlowStage(QJsonObject{
                {QStringLiteral("phase"), QStringLiteral("source_discovery_completed")},
                {QStringLiteral("stage"), QStringLiteral("readProject")},
                {QStringLiteral("stage_state"), QStringLiteral("completed")},
                {QStringLiteral("stage_percent"), 100},
            });
            updateFlowStage(QJsonObject{
                {QStringLiteral("phase"), QStringLiteral("model_tool_selection")},
                {QStringLiteral("stage"), QStringLiteral("executor")},
                {QStringLiteral("stage_state"), QStringLiteral("running")},
                {QStringLiteral("stage_percent"), 65},
            });
            appendToolActivity(QStringLiteral("inspect_configured_project_source_tree"), QString{}, 1);
        } else if (step == 2) {
            completeToolActivity(QStringLiteral("inspect_configured_project_source_tree"), QStringLiteral("{\n  \"source_files\": 42,\n  \"missing\": []\n}"), 1);
            updateFlowStage(QJsonObject{
                {QStringLiteral("phase"), QStringLiteral("staged")},
                {QStringLiteral("stage"), QStringLiteral("executor")},
                {QStringLiteral("stage_state"), QStringLiteral("completed")},
                {QStringLiteral("stage_percent"), 100},
            });
            updateFlowStage(QJsonObject{
                {QStringLiteral("phase"), QStringLiteral("dc_shell")},
                {QStringLiteral("stage"), QStringLiteral("synthesis")},
                {QStringLiteral("stage_state"), QStringLiteral("running")},
                {QStringLiteral("stage_percent"), 5},
            });
            appendToolActivity(QStringLiteral("run_and_verify_project_dft_flow"), QString{}, 2);
            appendToolOutput(QStringLiteral("$ dc_shell -no_gui -f agent_synthesis_dft.tcl\n"));
        } else if (step == 3) {
            updateFlowStage(QJsonObject{
                {QStringLiteral("phase"), QStringLiteral("read_link_completed")},
                {QStringLiteral("stage"), QStringLiteral("synthesis")},
                {QStringLiteral("stage_state"), QStringLiteral("running")},
                {QStringLiteral("stage_percent"), 34},
            });
            appendToolOutput(QStringLiteral("dc_shell> Reading RTL and linking design\nLinking design 'demo_top'\n"));
        } else if (step == 4) {
            updateFlowStage(QJsonObject{
                {QStringLiteral("phase"), QStringLiteral("constraints_applied")},
                {QStringLiteral("stage"), QStringLiteral("synthesis")},
                {QStringLiteral("stage_state"), QStringLiteral("running")},
                {QStringLiteral("stage_percent"), 66},
            });
            appendToolOutput(QStringLiteral("dc_shell> create_clock completed\n"));
            setContextUsage(QJsonObject{
                {QStringLiteral("source"), QStringLiteral("agent_configuration")},
                {QStringLiteral("context_window"), 65'536},
                {QStringLiteral("input_tokens"), 9'216},
                {QStringLiteral("system_prompt_tokens"), 5'120},
                {QStringLiteral("conversation_tokens"), 4'096},
                {QStringLiteral("history_tokens"), 4'096},
                {QStringLiteral("current_session_tokens"), 0},
                {QStringLiteral("tool_schema_tokens"), 3'072},
                {QStringLiteral("output_tokens"), 0},
                {QStringLiteral("output_token_limit"), 44'032},
            });
        } else if (step == 5) {
            updateFlowStage(QJsonObject{
                {QStringLiteral("phase"), QStringLiteral("compile_optimization")},
                {QStringLiteral("stage"), QStringLiteral("synthesis")},
                {QStringLiteral("stage_state"), QStringLiteral("running")},
                {QStringLiteral("stage_percent"), 88},
            });
            appendToolOutput(QStringLiteral("Beginning Pass 1 Mapping\nBeginning Mapping Optimizations\n"));
        } else if (step == 6) {
            appendToolOutput(QStringLiteral("Optimization Complete\nInformation: compile finished.\n"));
            completeToolActivity(QStringLiteral("run_and_verify_project_dft_flow"), QStringLiteral("{\n  \"synthesis\": \"completed\",\n  \"reports\": 3\n}"), 2);
            updateFlowStage(QJsonObject{
                {QStringLiteral("phase"), QStringLiteral("synthesis_completed")},
                {QStringLiteral("stage"), QStringLiteral("synthesis")},
                {QStringLiteral("stage_state"), QStringLiteral("completed")},
                {QStringLiteral("stage_percent"), 100},
            });
            updateFlowStage(QJsonObject{
                {QStringLiteral("phase"), QStringLiteral("evidence_written")},
                {QStringLiteral("stage"), QStringLiteral("report")},
                {QStringLiteral("stage_state"), QStringLiteral("running")},
                {QStringLiteral("stage_percent"), 65},
            });
        }
        appendLog(QStringLiteral("[%1] %2").arg(m_progress).arg(m_phase));
    });
    m_terminalProcess.setProcessChannelMode(QProcess::MergedChannels);
    connect(&m_terminalProcess, &QProcess::readyReadStandardOutput, this, [this] {
        appendTerminalOutput(QString::fromUtf8(m_terminalProcess.readAllStandardOutput()));
    });
    connect(&m_terminalProcess, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this, [this](int exitCode, QProcess::ExitStatus) {
        appendTerminalOutput(QStringLiteral("\n[exit %1]\n").arg(exitCode));
        emit terminalRunningChanged();
    });
}

AgentController::~AgentController() {
    m_demoTimer.stop();
#ifdef DFT_AGENT_STUDIO_TESTING
    QObject::disconnect(&m_process, nullptr, this, nullptr);
#endif
    QObject::disconnect(&m_terminalProcess, nullptr, this, nullptr);
    for (QProcess *process : {&m_terminalProcess, &m_localModelProcess}) {
        if (process->state() != QProcess::NotRunning) {
            process->kill();
            process->waitForFinished(1000);
        }
    }
#ifdef DFT_AGENT_STUDIO_TESTING
    if (m_process.state() != QProcess::NotRunning) {
        m_process.kill();
        m_process.waitForFinished(1000);
    }
#endif
}

bool AgentController::running() const {
    if (m_running)
        return true;
    return std::any_of(m_parallelRuns.cbegin(), m_parallelRuns.cend(), [](const AgentController *run) {
        return run && run->m_running;
    });
}
bool AgentController::paused() const { return m_paused; }
int AgentController::progress() const { return m_progress; }
QString AgentController::phase() const { return m_phase; }
QString AgentController::workspace() const { return m_workspace; }
QString AgentController::log() const { return m_log; }
QString AgentController::detailedLog() const { return m_detailedLog; }
QString AgentController::toolOutput() const { return m_toolOutput; }
QVariantList AgentController::activityEntries() const { return m_activityEntries; }
QVariantMap AgentController::flowStageStates() const { return m_flowStageStates; }
QString AgentController::result() const { return m_result; }
QString AgentController::report() const { return m_report; }
QVariantList AgentController::reportFiles() const {
    QVariantList files;
    QHash<QString, QString> projectNames;
    QFile projectsFile(studioConfigPath(QStringLiteral("projects.json"), m_workingDirectory));
    if (projectsFile.open(QIODevice::ReadOnly) && projectsFile.size() <= 32 * 1024 * 1024) {
        const QJsonDocument projectsDocument = QJsonDocument::fromJson(projectsFile.readAll());
        if (projectsDocument.isObject()) {
            const QJsonArray projects = projectsDocument.object().value(QStringLiteral("projects")).toArray();
            for (const QJsonValue &value : projects) {
                const QJsonObject project = value.toObject();
                const QString id = project.value(QStringLiteral("id")).toString().trimmed();
                const QString name = project.value(QStringLiteral("name")).toString().trimmed();
                if (!id.isEmpty() && !name.isEmpty())
                    projectNames.insert(id, name);
            }
        }
    }
    const QString sessionId = m_liveSessionId.isEmpty() ? m_activitySessionId : m_liveSessionId;
    QVariantMap sessionDetails;
    static const QRegularExpression safeSessionId(QStringLiteral("^[A-Za-z0-9._-]{1,128}$"));
    if (safeSessionId.match(sessionId).hasMatch()) {
        const QString dataRoot = studioDataRoot(m_workingDirectory);
        const QString sessionDirectory = QDir(dataRoot).filePath(
            QStringLiteral("agent_runtime/threads/%1").arg(sessionId));
        QFile sessionFile(QDir(sessionDirectory).filePath(QStringLiteral("session.json")));
        if (sessionFile.open(QIODevice::ReadOnly)) {
            const auto document = QJsonDocument::fromJson(sessionFile.readAll());
            if (document.isObject()) {
                const auto session = document.object();
                sessionDetails.insert(QStringLiteral("session_id"), sessionId);
                sessionDetails.insert(QStringLiteral("session_title"), session.value(QStringLiteral("name")).toString());
                sessionDetails.insert(QStringLiteral("project_id"), session.value(QStringLiteral("project_id")).toString());
            }
        }
    }
    if (!m_report.trimmed().isEmpty()) {
        QVariantMap currentReport{
            {QStringLiteral("path"), QStringLiteral("__current_report__")},
            {QStringLiteral("title"), QStringLiteral("当前运行报告")},
            {QStringLiteral("kind"), QStringLiteral("summary")},
            {QStringLiteral("modified"), QStringLiteral("实时")},
            {QStringLiteral("category"), QStringLiteral("run_report")},
        };
        for (auto it = sessionDetails.cbegin(); it != sessionDetails.cend(); ++it)
            currentReport.insert(it.key(), it.value());
        currentReport.insert(QStringLiteral("project_id"), m_currentProjectId);
        currentReport.insert(QStringLiteral("project_name"), projectNames.value(m_currentProjectId));
        files.append(currentReport);
    }

    const QString dataRoot = studioDataRoot(m_workingDirectory);
    const QString threadsPath = QDir(dataRoot).filePath(QStringLiteral("agent_runtime/threads"));
    const QString canonicalThreadsRoot = QFileInfo(threadsPath).canonicalFilePath();
    QDir threadsDirectory(threadsPath);
    const QFileInfoList sessionDirectories = threadsDirectory.entryInfoList(
        QDir::Dirs | QDir::NoDotAndDotDot | QDir::Readable, QDir::Name);
    constexpr int maximumSessionReports = 2'000;
    int sessionReportCount = 0;
    for (const QFileInfo &sessionInfo : sessionDirectories) {
        if (sessionReportCount >= maximumSessionReports)
            break;
        if (sessionInfo.isSymLink())
            continue;
        const QString groupPath = sessionInfo.canonicalFilePath();
        if (canonicalThreadsRoot.isEmpty() || !groupPath.startsWith(canonicalThreadsRoot + QDir::separator()))
            continue;
        const QString reportsPath = QDir(groupPath).filePath(QStringLiteral("run_reports"));
        const QFileInfo reportsInfo(reportsPath);
        if (!reportsInfo.isDir() || reportsInfo.isSymLink())
            continue;
        const QString canonicalReportsRoot = reportsInfo.canonicalFilePath();
        if (canonicalReportsRoot.isEmpty()
            || !canonicalReportsRoot.startsWith(canonicalThreadsRoot + QDir::separator()))
            continue;
        QDirIterator records(canonicalReportsRoot, {QStringLiteral("*.json")}, QDir::Files | QDir::Readable);
        while (records.hasNext()) {
            const QFileInfo recordInfo(records.next());
            if (recordInfo.isSymLink())
                continue;
            QFile recordFile(recordInfo.absoluteFilePath());
            if (!recordFile.open(QIODevice::ReadOnly) || recordFile.size() > 64 * 1024)
                continue;
            const QJsonDocument document = QJsonDocument::fromJson(recordFile.readAll());
            if (!document.isObject())
                continue;
            const QJsonObject record = document.object();
            const QString session = record.value(QStringLiteral("session_id")).toString();
            const QString category = record.value(QStringLiteral("category")).toString();
            const QString filename = QFileInfo(record.value(QStringLiteral("report_file")).toString()).fileName();
            if (session != sessionInfo.fileName()
                || !QSet<QString>{QStringLiteral("run_report"), QStringLiteral("design_summary")}.contains(category)
                || filename.isEmpty() || !filename.endsWith(QStringLiteral(".md"), Qt::CaseInsensitive))
                continue;
            const QFileInfo markdownInfo(QDir(canonicalReportsRoot).filePath(filename));
            const QString markdownPath = markdownInfo.canonicalFilePath();
            if (!markdownInfo.isFile() || markdownInfo.isSymLink()
                || markdownPath.isEmpty() || !markdownPath.startsWith(canonicalReportsRoot + QDir::separator()))
                continue;
            QVariantMap entry{
                {QStringLiteral("path"), markdownPath},
                {QStringLiteral("title"), record.value(QStringLiteral("title")).toString()},
                {QStringLiteral("kind"), QStringLiteral("md")},
                {QStringLiteral("category"), category},
                {QStringLiteral("modified"), record.value(QStringLiteral("updated_at")).toString()},
                {QStringLiteral("project_id"), record.value(QStringLiteral("project_id")).toString()},
                {QStringLiteral("project_name"), record.value(QStringLiteral("project_name")).toString().trimmed().isEmpty()
                    ? projectNames.value(record.value(QStringLiteral("project_id")).toString())
                    : record.value(QStringLiteral("project_name")).toString()},
                {QStringLiteral("session_id"), session},
                {QStringLiteral("session_title"), record.value(QStringLiteral("session_title")).toString()},
                {QStringLiteral("size"), markdownInfo.size()},
                {QStringLiteral("sort_time"), record.value(QStringLiteral("updated_at")).toString()},
            };
            files.append(entry);
            ++sessionReportCount;
        }
    }

    if (m_workspace.trimmed().isEmpty())
        return files;

    const QString workspaceRoot = QFileInfo(m_workspace).canonicalFilePath();
    if (workspaceRoot.isEmpty())
        return files;
    const QDir workspace(workspaceRoot);
    const QDir reportsDirectory(workspace.filePath(QStringLiteral("reports")));
    if (!reportsDirectory.exists())
        return files;

    QDirIterator iterator(
        reportsDirectory.absolutePath(),
        QDir::Files | QDir::Readable,
        QDirIterator::Subdirectories
    );
    QList<QFileInfo> candidates;
    while (iterator.hasNext()) {
        const QFileInfo info(iterator.next());
        if (isReportSuffix(info.suffix()))
            candidates.append(info);
    }
    std::sort(candidates.begin(), candidates.end(), [](const QFileInfo &left, const QFileInfo &right) {
        return left.lastModified() > right.lastModified();
    });

    constexpr int maximumReports = 200;
    for (int index = 0; index < candidates.size() && index < maximumReports; ++index) {
        const QFileInfo &info = candidates.at(index);
        files.append(QVariantMap{
            {QStringLiteral("path"), info.canonicalFilePath()},
            {QStringLiteral("title"), reportsDirectory.relativeFilePath(info.filePath())},
            {QStringLiteral("kind"), info.suffix().toLower()},
            {QStringLiteral("category"), QStringLiteral("evidence")},
            {QStringLiteral("project_id"), m_currentProjectId},
            {QStringLiteral("project_name"), projectNames.value(m_currentProjectId)},
            {QStringLiteral("session_id"), sessionId},
            {QStringLiteral("session_title"), sessionDetails.value(QStringLiteral("session_title"))},
            {QStringLiteral("modified"), info.lastModified().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"))},
            {QStringLiteral("size"), info.size()},
        });
    }
    return files;
}
bool AgentController::hasError() const { return m_hasError; }
bool AgentController::requiresSupervisorReview() const { return m_requiresSupervisorReview; }
QString AgentController::terminalOutput() const { return m_terminalOutput; }
bool AgentController::terminalRunning() const { return m_terminalProcess.state() != QProcess::NotRunning; }
QString AgentController::intelligenceProvider() const { return m_intelligenceProvider; }
#ifdef DFT_AGENT_STUDIO_TESTING
QString AgentController::testWorkerExecutable() const { return m_testWorkerExecutable; }
void AgentController::setTestWorkerExecutable(const QString &value) { if (m_testWorkerExecutable != value) { m_testWorkerExecutable = value; emit testWorkerExecutableChanged(); } }
#endif
void AgentController::setModelCatalogPath(const QString &value) { m_modelCatalogPath = value; }
bool AgentController::demoMode() const { return m_demoMode; }
void AgentController::setDemoMode(bool value) { if (m_demoMode != value) { m_demoMode = value; emit demoModeChanged(); } }
bool AgentController::detailedMode() const { return m_detailedMode; }
void AgentController::setDetailedMode(bool value) { if (m_detailedMode != value) { m_detailedMode = value; emit detailedModeChanged(); } }
int AgentController::contextWindow() const { return m_contextWindow; }
int AgentController::contextEffectiveWindow() const { return m_contextEffectiveWindow; }
int AgentController::contextAutoCompactLimit() const { return m_contextAutoCompactLimit; }
int AgentController::contextInputTokens() const { return m_contextInputTokens; }
int AgentController::contextInputLimit() const { return m_contextInputLimit; }
int AgentController::contextSystemTokens() const { return m_contextSystemTokens; }
int AgentController::contextHistoryTokens() const { return m_contextHistoryTokens; }
int AgentController::contextToolTokens() const { return m_contextToolTokens; }
int AgentController::contextToolLimit() const { return m_contextToolLimit; }
int AgentController::contextOutputTokens() const { return m_contextOutputTokens; }
int AgentController::contextOutputLimit() const { return m_contextOutputLimit; }
bool AgentController::contextCompacted() const { return m_contextCompacted; }
bool AgentController::contextProviderMeasured() const { return m_contextProviderMeasured; }
int AgentController::queuedPromptCount() const { return m_queuedPrompts.size(); }
QVariantList AgentController::queuedPrompts() const {
    QVariantList result;
    result.reserve(m_queuedPrompts.size());
    for (const QueuedPrompt &item : m_queuedPrompts) {
        result.append(QVariantMap{
            {QStringLiteral("id"), item.id},
            {QStringLiteral("prompt"), item.prompt},
            {QStringLiteral("projectName"), item.project.value(QStringLiteral("name")).toString()},
            {QStringLiteral("status"), item.status},
        });
    }
    return result;
}

void AgentController::notifyQueuedPromptsChanged() {
    emit queuedPromptCountChanged();
    emit queuedPromptsChanged();
}

void AgentController::updateQueuedActivity(const QString &queueId, const QString &prompt, const QString &status) {
    for (qsizetype index = m_activityEntries.size() - 1; index >= 0; --index) {
        QVariantMap entry = m_activityEntries.at(index).toMap();
        if (entry.value(QStringLiteral("kind")).toString() != QStringLiteral("user")
            || entry.value(QStringLiteral("queueId")).toString() != queueId)
            continue;
        entry.insert(QStringLiteral("text"), prompt);
        entry.insert(QStringLiteral("status"), status);
        m_activityEntries[index] = entry;
        emit activityEntriesChanged();
        return;
    }
}

QVariantList AgentController::codexModels() const { return m_codexModels; }

bool AgentController::codexModelsLoading() const { return m_codexModelsLoading; }

void AgentController::refreshCodexModels(const QString &baseUrl, const QString &apiKeyFile, const QString &apiKey) {
    QString root = baseUrl.trimmed();
    while (root.endsWith(QLatin1Char('/')))
        root.chop(1);
    if (root.isEmpty())
        return;

    QString token = apiKey.trimmed();
    if (token.isEmpty() && !apiKeyFile.trimmed().isEmpty()) {
        QFile file(apiKeyFile.trimmed());
        if (file.open(QIODevice::ReadOnly))
            token = QString::fromUtf8(file.readAll()).trimmed();
    }
    const QString requestKey = root + QLatin1Char('\n') + token;
    if (m_codexModelsLoading && requestKey == m_codexModelsRequestKey)
        return;
    // A model/API switch must not be held hostage by an earlier /models
    // request.  Abort the stale request and let its finished callback become
    // a no-op through the generation guard below.
    if (m_codexModelsLoading && m_codexModelsReply)
        m_codexModelsReply->abort();
    const quint64 requestGeneration = ++m_codexModelsRequestGeneration;
    m_codexModelsRequestKey = requestKey;
    m_codexModelsLoading = true;
    emit codexModelsLoadingChanged();

    QNetworkRequest request(QUrl(root + QStringLiteral("/models")));
    // OpenAI-compatible gateways may intentionally allow an unauthenticated
    // /models endpoint.  Only attach Authorization when a key is configured;
    // an empty key must not prevent model discovery.
    if (!token.isEmpty())
        request.setRawHeader("Authorization", QByteArrayLiteral("Bearer ") + token.toUtf8());
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    // Do not leave models from a previously selected API visible while this
    // provider is being queried. The picker must describe the current
    // endpoint, not a stale response from another service.
    if (!m_codexModels.isEmpty()) {
        m_codexModels.clear();
        emit codexModelsChanged();
    }
    QNetworkReply *reply = m_network.get(request);
    m_codexModelsReply = reply;
    reply->setProperty("codexModelsGeneration", QVariant::fromValue<qulonglong>(requestGeneration));
    QTimer::singleShot(8'000, reply, [reply]() {
        if (reply->isRunning())
            reply->abort();
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        if (reply != m_codexModelsReply
            || reply->property("codexModelsGeneration").toULongLong() != m_codexModelsRequestGeneration) {
            reply->deleteLater();
            return;
        }
        m_codexModelsLoading = false;
        m_codexModelsReply = nullptr;
        emit codexModelsLoadingChanged();

        if (reply->error() == QNetworkReply::NoError) {
            QJsonParseError parseError;
            const QJsonDocument document = QJsonDocument::fromJson(reply->readAll(), &parseError);
            if (parseError.error == QJsonParseError::NoError && document.isObject()) {
                QVariantList models;
                const QJsonObject payload = document.object();
                QList<QJsonObject> entries;
                QHash<QString, qsizetype> entryIndexes;
                const auto mergeEntries = [&entries, &entryIndexes](const QJsonArray &source) {
                    for (const QJsonValue &value : source) {
                        const QJsonObject object = value.toObject();
                        const QString id = object.value(QStringLiteral("slug")).toString(
                            object.value(QStringLiteral("id")).toString(
                                object.value(QStringLiteral("model")).toString(
                                    object.value(QStringLiteral("name")).toString()))).trimmed();
                        if (id.isEmpty())
                            continue;
                        const auto existingIndex = entryIndexes.constFind(id);
                        if (existingIndex == entryIndexes.cend()) {
                            entryIndexes.insert(id, entries.size());
                            entries.append(object);
                            continue;
                        }
                        QJsonObject merged = entries.at(*existingIndex);
                        for (auto it = object.constBegin(); it != object.constEnd(); ++it) {
                            if ((it.key() == QStringLiteral("meta") || it.key() == QStringLiteral("limits"))
                                && it.value().isObject()) {
                                QJsonObject nested = merged.value(it.key()).toObject();
                                const QJsonObject incoming = it.value().toObject();
                                for (auto nestedIt = incoming.constBegin(); nestedIt != incoming.constEnd(); ++nestedIt)
                                    nested.insert(nestedIt.key(), nestedIt.value());
                                merged.insert(it.key(), nested);
                            } else {
                                merged.insert(it.key(), it.value());
                            }
                        }
                        entries[*existingIndex] = merged;
                    }
                };
                // Some compatible gateways return a lightweight `models`
                // list and richer OpenAI-style `data` entries side by side.
                // Merge by ID so context metadata from either representation
                // remains available to the settings page.
                mergeEntries(payload.value(QStringLiteral("models")).toArray());
                mergeEntries(payload.value(QStringLiteral("data")).toArray());
                for (const QJsonObject &object : std::as_const(entries)) {
                    if (!object.value(QStringLiteral("supported_in_api")).toBool(true)
                        || object.value(QStringLiteral("visibility")).toString(QStringLiteral("list")) != QStringLiteral("list"))
                        continue;

                    const QString id = object.value(QStringLiteral("slug")).toString(
                        object.value(QStringLiteral("id")).toString(
                            object.value(QStringLiteral("model")).toString(
                                object.value(QStringLiteral("name")).toString()))).trimmed();
                    if (id.isEmpty())
                        continue;
                    QVariantMap model;
                    model.insert(QStringLiteral("id"), id);
                    model.insert(QStringLiteral("label"), object.value(QStringLiteral("display_name")).toString(
                        object.value(QStringLiteral("name")).toString(id)));
                    model.insert(QStringLiteral("description"), object.value(QStringLiteral("description")).toString());
                    // Compatible gateways do not agree on the name or shape
                    // of their context metadata.  Keep the normalized
                    // contextWindow field stable for QML while accepting the
                    // common OpenAI-style aliases and a nested limits object.
                    const auto contextValue = [&object](const QString &key) {
                        const QJsonValue value = object.value(key);
                        if (value.isDouble())
                            return value.toInt();
                        return value.toString().toInt();
                    };
                    int contextWindow = contextValue(QStringLiteral("context_window"));
                    int maxContextWindow = contextValue(QStringLiteral("max_context_window"));
                    if (contextWindow <= 0)
                        contextWindow = contextValue(QStringLiteral("context_length"));
                    if (contextWindow <= 0)
                        contextWindow = contextValue(QStringLiteral("max_context_tokens"));
                    if (contextWindow <= 0)
                        contextWindow = contextValue(QStringLiteral("num_ctx"));
                    if (contextWindow <= 0) {
                        const int inputLimit = contextValue(QStringLiteral("max_input_tokens"));
                        const int outputLimit = contextValue(QStringLiteral("max_output_tokens"));
                        if (inputLimit > 0 && outputLimit > 0)
                            contextWindow = inputLimit + outputLimit;
                    }
                    // llama.cpp-compatible gateways commonly expose the
                    // actual n_ctx only under the OpenAI model's `meta`
                    // object (for example meta.n_ctx=262144).
                    const QJsonObject metadata = object.value(QStringLiteral("meta")).toObject();
                    const auto metadataValue = [&metadata](const QString &key) {
                        const QJsonValue value = metadata.value(key);
                        if (value.isDouble())
                            return value.toInt();
                        return value.toString().toInt();
                    };
                    if (contextWindow <= 0)
                        contextWindow = metadataValue(QStringLiteral("n_ctx"));
                    if (maxContextWindow <= 0)
                        maxContextWindow = metadataValue(QStringLiteral("n_ctx_train"));
                    if (contextWindow <= 0) {
                        const QJsonObject limits = object.value(QStringLiteral("limits")).toObject();
                        const QJsonValue nested = limits.value(QStringLiteral("context_window"));
                        contextWindow = nested.isDouble() ? nested.toInt() : nested.toString().toInt();
                        if (maxContextWindow <= 0) {
                            const QJsonValue nestedMax = limits.value(QStringLiteral("max_context_window"));
                            maxContextWindow = nestedMax.isDouble() ? nestedMax.toInt() : nestedMax.toString().toInt();
                        }
                        if (contextWindow <= 0) {
                            const auto nestedInt = [&limits](const QString &key) {
                                const QJsonValue value = limits.value(key);
                                return value.isDouble() ? value.toInt() : value.toString().toInt();
                            };
                            const int inputLimit = nestedInt(QStringLiteral("max_input_tokens"));
                            const int outputLimit = nestedInt(QStringLiteral("max_output_tokens"));
                            if (inputLimit > 0 && outputLimit > 0)
                                contextWindow = inputLimit + outputLimit;
                        }
                    }
                    if (contextWindow <= 0)
                        contextWindow = maxContextWindow;
                    if (maxContextWindow <= 0)
                        maxContextWindow = contextWindow;
                    model.insert(QStringLiteral("contextWindow"), contextWindow);
                    model.insert(QStringLiteral("maxContextWindow"), maxContextWindow);
                    // Codex model metadata uses a 95% effective window and a
                    // 90% auto-compaction default. Keep both values available
                    // so the settings page can reproduce that allocation for
                    // compatible gateways instead of guessing a fixed split.
                    int effectivePercent = contextValue(QStringLiteral("effective_context_window_percent"));
                    if (effectivePercent <= 0)
                        effectivePercent = 95;
                    model.insert(QStringLiteral("effectiveContextWindowPercent"), qBound(1, effectivePercent, 100));
                    int autoCompactLimit = contextValue(QStringLiteral("auto_compact_token_limit"));
                    if (autoCompactLimit <= 0 && contextWindow > 0)
                        autoCompactLimit = (contextWindow * 9) / 10;
                    if (autoCompactLimit > 0)
                        model.insert(QStringLiteral("autoCompactTokenLimit"), autoCompactLimit);
                    model.insert(QStringLiteral("priority"), object.value(QStringLiteral("priority")).toInt(999));

                    QVariantList reasoningLevels;
                    for (const QJsonValue &levelValue : object.value(QStringLiteral("supported_reasoning_levels")).toArray()) {
                        const QJsonObject level = levelValue.toObject();
                        const QString effort = level.value(QStringLiteral("effort")).toString().trimmed().toLower();
                        if (effort.isEmpty())
                            continue;
                        reasoningLevels.append(QVariantMap{
                            {QStringLiteral("effort"), effort},
                            {QStringLiteral("description"), level.value(QStringLiteral("description")).toString()}
                        });
                    }
                    if (reasoningLevels.isEmpty()) {
                        for (const QString &effort : {QStringLiteral("low"), QStringLiteral("medium"), QStringLiteral("high")}) {
                            reasoningLevels.append(QVariantMap{
                                {QStringLiteral("effort"), effort},
                                {QStringLiteral("description"), QString()}
                            });
                        }
                    }
                    model.insert(QStringLiteral("reasoningLevels"), reasoningLevels);
                    models.append(model);
                }
                if (!models.isEmpty()) {
                    std::stable_sort(models.begin(), models.end(), [](const QVariant &left, const QVariant &right) {
                        const QVariant leftPriority = left.toMap().value(QStringLiteral("priority"));
                        const QVariant rightPriority = right.toMap().value(QStringLiteral("priority"));
                        return (leftPriority.isValid() ? leftPriority.toInt() : 999)
                            < (rightPriority.isValid() ? rightPriority.toInt() : 999);
                    });
                    m_codexModels = models;
                    emit codexModelsChanged();
                }
            }
        }
        reply->deleteLater();
    });
}

void AgentController::configureContextUsage(int contextWindow, int inputTokenLimit, int outputTokenLimit) {
    m_defaultContextWindow = qMax(2'048, contextWindow);
    m_defaultContextInputLimit = qBound(1'024, inputTokenLimit, m_defaultContextWindow - 1);
    const int maximumOutput = qMax(1, m_defaultContextWindow - m_defaultContextInputLimit);
    m_defaultContextOutputLimit = qBound(1, outputTokenLimit, maximumOutput);
    if (!m_running)
        resetContextUsage();
}

void AgentController::run(const QVariantMap &project, const QStringList &disabledCapabilities) {
    if (m_running) {
        const QString requestedSession = project.value(QStringLiteral("codexThreadId")).toString().trimmed();
        if (requestedSession == m_liveSessionId && !requestedSession.isEmpty())
            return;
        for (const AgentController *run : m_parallelRuns) {
            if (run && !requestedSession.isEmpty() && run->m_liveSessionId == requestedSession)
                return;
        }
        startParallelRun(project, disabledCapabilities);
        return;
    }
    const QString requestedSession = project.value(QStringLiteral("codexThreadId")).toString().trimmed();
    for (const AgentController *run : m_parallelRuns) {
        if (run && !requestedSession.isEmpty() && run->m_liveSessionId == requestedSession)
            return;
    }
    startRun(project, disabledCapabilities, project.value(QStringLiteral("goal")).toString(), true);
}

void AgentController::runInBackground(const QVariantMap &project, const QStringList &disabledCapabilities) {
    if (project.value(QStringLiteral("goal")).toString().trimmed().isEmpty())
        return;
    startParallelRun(project, disabledCapabilities);
}

void AgentController::showDesktopNotification(const QString &title, const QString &message) {
    if (title.trimmed().isEmpty() || message.trimmed().isEmpty())
        return;
    QProcess::startDetached(QStringLiteral("notify-send"), {title, message});
}

void AgentController::startParallelRun(const QVariantMap &project, const QStringList &disabledCapabilities) {
    if (project.value(QStringLiteral("goal")).toString().trimmed().isEmpty())
        return;
    auto *run = new AgentController(m_workingDirectory, this);
#ifdef DFT_AGENT_STUDIO_TESTING
    run->setTestWorkerExecutable(m_testWorkerExecutable);
#endif
    run->setModelCatalogPath(m_modelCatalogPath);
    run->setDemoMode(m_demoMode);
    run->setDetailedMode(m_detailedMode);
    run->configureContextUsage(m_defaultContextWindow, m_defaultContextInputLimit, m_defaultContextOutputLimit);
    const QString sessionId = project.value(QStringLiteral("codexThreadId")).toString().trimmed();
    run->setActivitySession(sessionId);
    run->setFlowDisplaySession(sessionId);
    m_parallelRuns.append(run);
    connect(run, &AgentController::runningChanged, this, [this] { emit runningChanged(); });
    connect(run, &AgentController::pausedChanged, this, [this] { emit pausedChanged(); });
    connect(run, &AgentController::activityEntriesChanged, this, [this, run] {
        if (m_activitySessionId != run->m_liveSessionId)
            return;
        m_activityEntries = run->m_activityEntries;
        emit activityEntriesChanged();
    });
    connect(run, &AgentController::sessionTitleUpdated, this, &AgentController::sessionTitleUpdated);
    connect(run, &AgentController::reportFilesChanged, this, &AgentController::reportFilesChanged);
    connect(run, &AgentController::codexSessionReady, this, [this, run](const QString &projectId, const QString &threadId) {
        run->setActivitySession(threadId);
        run->setFlowDisplaySession(threadId);
        emit parallelSessionStarted(projectId, threadId);
        emit codexSessionReady(projectId, threadId);
    });
    connect(run, &AgentController::sessionProgressUpdated,
            this, &AgentController::sessionProgressUpdated);
    connect(run, &AgentController::codexSessionRecoveryRequired,
            this, &AgentController::codexSessionRecoveryRequired);
    connect(run, &AgentController::errorOccurred, this, &AgentController::errorOccurred);
    connect(run, &AgentController::finished, this, [this, run](bool successful) {
        emit parallelSessionFinished(run->m_currentProjectId, run->m_liveSessionId, successful);
        m_parallelRuns.removeAll(run);
        emit runningChanged();
        run->deleteLater();
    });
    emit runningChanged();
    run->run(project, disabledCapabilities);
}

void AgentController::runPrompt(const QVariantMap &project, const QString &prompt, const QStringList &disabledCapabilities) {
    const QString text = prompt.trimmed();
    if (text.isEmpty())
        return;
    const QString requestedSession = project.value(QStringLiteral("codexThreadId")).toString().trimmed();
    for (AgentController *run : std::as_const(m_parallelRuns)) {
        if (run && !requestedSession.isEmpty() && run->m_liveSessionId == requestedSession) {
            run->runPrompt(project, text, disabledCapabilities);
            return;
        }
    }
    const bool requestsNewSession = requestedSession.isEmpty()
        && project.value(QStringLiteral("startNewCodexSession")).toBool();
    const bool targetsAnotherSession = !requestedSession.isEmpty() && requestedSession != m_liveSessionId;
    if (m_running && (requestsNewSession || targetsAnotherSession)) {
        QVariantMap parallelProject = project;
        parallelProject.insert(QStringLiteral("goal"), text);
        startParallelRun(parallelProject, disabledCapabilities);
        return;
    }
    if (!m_running) {
        // A checkpoint resume already has its original request persisted in
        // the thread activity.  Do not add a second local user entry before
        // the worker writes the resumed turn markers.
        startRun(
            project,
            disabledCapabilities,
            text,
            false,
            project.value(QStringLiteral("resumeAgentTurn")).toBool()
        );
        return;
    }
    const QString queueId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_queuedPrompts.append({queueId, project, disabledCapabilities, text});
    appendActivity({
        {QStringLiteral("kind"), QStringLiteral("user")},
        {QStringLiteral("text"), text},
        {QStringLiteral("step"), 0},
        {QStringLiteral("status"), QStringLiteral("queued")},
        {QStringLiteral("queueId"), queueId},
    });
    appendLog(QStringLiteral("Queued follow-up instruction (%1 waiting).").arg(m_queuedPrompts.size()));
    notifyQueuedPromptsChanged();
}

void AgentController::runPromptNow(const QVariantMap &project, const QString &prompt, const QStringList &disabledCapabilities) {
    const QString text = prompt.trimmed();
    if (text.isEmpty())
        return;
    const QString requestedSession = project.value(QStringLiteral("codexThreadId")).toString().trimmed();
    for (AgentController *run : std::as_const(m_parallelRuns)) {
        if (run && !requestedSession.isEmpty() && run->m_liveSessionId == requestedSession) {
            run->runPromptNow(project, text, disabledCapabilities);
            return;
        }
    }
    const bool requestsNewSession = requestedSession.isEmpty()
        && project.value(QStringLiteral("startNewCodexSession")).toBool();
    const bool targetsAnotherSession = !requestedSession.isEmpty() && requestedSession != m_liveSessionId;
    if (m_running && (requestsNewSession || targetsAnotherSession)) {
        QVariantMap parallelProject = project;
        parallelProject.insert(QStringLiteral("goal"), text);
        startParallelRun(parallelProject, disabledCapabilities);
        return;
    }
    if (!m_running) {
        startRun(project, disabledCapabilities, text, false);
        return;
    }
    const QString steeringId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    if (sendSteeringToWorker(text, steeringId)) {
        appendActivity({
            {QStringLiteral("kind"), QStringLiteral("user")},
            {QStringLiteral("text"), text},
            {QStringLiteral("step"), 0},
            {QStringLiteral("status"), QStringLiteral("steering")},
            {QStringLiteral("steeringId"), steeringId},
        });
        appendLog(QStringLiteral("Live guidance delivered to the active Agent turn."));
        return;
    }
    // Providers without a live input channel still keep the active worker and
    // its tools intact; the prompt follows when that turn finishes.
    const QString queueId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_queuedPrompts.prepend({queueId, project, disabledCapabilities, text, QStringLiteral("steering")});
    appendActivity({
        {QStringLiteral("kind"), QStringLiteral("user")},
        {QStringLiteral("text"), text},
        {QStringLiteral("step"), 0},
        {QStringLiteral("status"), QStringLiteral("steering")},
        {QStringLiteral("queueId"), queueId},
    });
    notifyQueuedPromptsChanged();
    appendLog(QStringLiteral("Live steering is unavailable; guidance will run after the active turn without stopping its tools."));
}

bool AgentController::sendSteeringToWorker(const QString &prompt, const QString &steeringId) {
    if (!m_running || !m_liveSteeringEnabled)
        return false;
    if (m_paused)
        resume();
    if (m_nativeTurnRunner && m_nativeTurnRunner->running())
        return m_nativeTurnRunner->steer(prompt);
#ifdef DFT_AGENT_STUDIO_TESTING
    if (m_process.state() == QProcess::NotRunning)
        return false;
    const QByteArray message = QJsonDocument(QJsonObject{
        {QStringLiteral("type"), QStringLiteral("steer")},
        {QStringLiteral("id"), steeringId},
        {QStringLiteral("prompt"), prompt},
    }).toJson(QJsonDocument::Compact) + '\n';
    return m_process.write(message) == message.size();
#else
    Q_UNUSED(steeringId);
    return false;
#endif
}

bool AgentController::editQueuedPrompt(const QString &queueId, const QString &prompt) {
    const QString text = prompt.trimmed();
    if (text.isEmpty())
        return false;
    for (QueuedPrompt &item : m_queuedPrompts) {
        if (item.id != queueId)
            continue;
        item.prompt = text;
        QString status = QStringLiteral("queued");
        for (auto activity = m_activityEntries.crbegin(); activity != m_activityEntries.crend(); ++activity) {
            const QVariantMap entry = activity->toMap();
            if (entry.value(QStringLiteral("queueId")).toString() == queueId) {
                if (entry.value(QStringLiteral("status")).toString() == QStringLiteral("steering"))
                    status = QStringLiteral("steering");
                break;
            }
        }
        updateQueuedActivity(queueId, text, status);
        notifyQueuedPromptsChanged();
        return true;
    }
    return false;
}

bool AgentController::removeQueuedPrompt(const QString &queueId) {
    for (qsizetype index = 0; index < m_queuedPrompts.size(); ++index) {
        if (m_queuedPrompts.at(index).id != queueId)
            continue;
        m_queuedPrompts.removeAt(index);
        for (qsizetype activityIndex = m_activityEntries.size() - 1; activityIndex >= 0; --activityIndex) {
            if (m_activityEntries.at(activityIndex).toMap().value(QStringLiteral("queueId")).toString() == queueId) {
                m_activityEntries.removeAt(activityIndex);
                emit activityEntriesChanged();
                break;
            }
        }
        notifyQueuedPromptsChanged();
        return true;
    }
    return false;
}

bool AgentController::steerQueuedPrompt(const QString &queueId) {
    for (qsizetype index = 0; index < m_queuedPrompts.size(); ++index) {
        if (m_queuedPrompts.at(index).id != queueId)
            continue;
        QueuedPrompt item = m_queuedPrompts.takeAt(index);
        item.prompt = item.prompt.trimmed();
        if (item.prompt.isEmpty())
            return false;
        item.status = QStringLiteral("steering");
        if (!m_running) {
            m_queuedPrompts.prepend(item);
            updateQueuedActivity(queueId, item.prompt, QStringLiteral("steering"));
            notifyQueuedPromptsChanged();
            QTimer::singleShot(0, this, &AgentController::startNextQueuedPrompt);
        } else if (sendSteeringToWorker(item.prompt, item.id)) {
            for (qsizetype activityIndex = 0; activityIndex < m_activityEntries.size(); ++activityIndex) {
                QVariantMap entry = m_activityEntries.at(activityIndex).toMap();
                if (entry.value(QStringLiteral("queueId")).toString() != item.id)
                    continue;
                entry.remove(QStringLiteral("queueId"));
                entry.insert(QStringLiteral("steeringId"), item.id);
                entry.insert(QStringLiteral("status"), QStringLiteral("steering"));
                m_activityEntries[activityIndex] = entry;
                break;
            }
            emit activityEntriesChanged();
            notifyQueuedPromptsChanged();
        } else {
            m_queuedPrompts.prepend(item);
            updateQueuedActivity(queueId, item.prompt, QStringLiteral("steering"));
            notifyQueuedPromptsChanged();
            appendLog(QStringLiteral("Promoted guidance will run after the active turn; its tools continue uninterrupted."));
        }
        return true;
    }
    return false;
}

void AgentController::interruptForQueuedPrompt() {
    if (m_paused)
        resume();
#ifdef DFT_AGENT_STUDIO_TESTING
    m_receivedResult = true;
    setPhase(QStringLiteral("正在切换为立即引导"));
    if (m_process.state() != QProcess::NotRunning) {
        m_interruptionPending = true;
        for (ResponsesRoundClient *client : std::as_const(m_nativeResponseRequests))
            client->cancel();
        m_process.terminate();
        QTimer::singleShot(1'500, this, [this] {
            if (m_interruptionPending && m_process.state() != QProcess::NotRunning)
                m_process.kill();
        });
        return;
    }
    setRunning(false);
    emit finished(false);
    QTimer::singleShot(0, this, &AgentController::startNextQueuedPrompt);
#else
    appendLog(QStringLiteral("Live input is unavailable; guidance remains queued until the active turn completes."));
#endif
}

void AgentController::pause() {
    for (AgentController *run : std::as_const(m_parallelRuns)) {
        if (run && run->m_liveSessionId == m_flowDisplaySessionId) {
            run->pause();
            return;
        }
    }
    if (!m_running || m_paused)
        return;
    if (m_nativeTurnRunner && m_nativeTurnRunner->running()) {
        m_nativeTurnRunner->pause();
        m_paused = true;
        setPhase(QStringLiteral("已暂停；正在完成运行中的工具"));
        appendLog(QStringLiteral("Native turn paused at the next tool boundary; active tools are left running."));
        emit pausedChanged();
        return;
    }
#ifdef DFT_AGENT_STUDIO_TESTING
    if (m_process.state() == QProcess::NotRunning)
        return;
#ifdef Q_OS_UNIX
    if (::kill(static_cast<pid_t>(m_process.processId()), SIGSTOP) != 0)
        return;
    m_paused = true;
    setPhase(QStringLiteral("已暂停，可继续运行"));
    appendLog(QStringLiteral("Agent turn paused by operator."));
    emit pausedChanged();
#endif
#endif
}

void AgentController::resume() {
    for (AgentController *run : std::as_const(m_parallelRuns)) {
        if (run && run->m_liveSessionId == m_flowDisplaySessionId) {
            run->resume();
            return;
        }
    }
    if (!m_paused)
        return;
    if (m_nativeTurnRunner && m_nativeTurnRunner->running()) {
        m_nativeTurnRunner->resume();
        m_paused = false;
        setPhase(QStringLiteral("继续运行"));
        appendLog(QStringLiteral("Native turn resumed."));
        emit pausedChanged();
        return;
    }
    if (m_nativeTurnRunner && !m_nativeTurnRunner->running() && !m_nativeGoal.isEmpty()) {
        m_paused = false;
        emit pausedChanged();
        setHasError(false);
        setRunning(true);
        setPhase(QStringLiteral("Resuming Responses turn"));
        if (!startNativeResponsesRun(m_nativeProjectSnapshot, m_nativeDisabledCapabilities, m_nativeGoal)) {
            m_paused = true;
            emit pausedChanged();
            setRunning(false);
            setFailureReport(QStringLiteral("无法重新初始化原生 Responses 回合。"));
        }
        return;
    }
#ifdef DFT_AGENT_STUDIO_TESTING
    if (m_process.state() == QProcess::NotRunning)
        return;
#ifdef Q_OS_UNIX
    if (::kill(static_cast<pid_t>(m_process.processId()), SIGCONT) != 0)
        return;
    m_paused = false;
    setPhase(QStringLiteral("继续运行"));
    appendLog(QStringLiteral("Agent turn resumed by operator."));
    emit pausedChanged();
#endif
#endif
}

void AgentController::startRun(
    const QVariantMap &project,
    const QStringList &disabledCapabilities,
    const QString &prompt,
    bool clearConversation,
    bool promptAlreadyRecorded
) {
    if (m_running)
        return;
#ifdef DFT_AGENT_STUDIO_TESTING
    m_interruptionPending = false;
#endif
    const QString goal = prompt.trimmed();
    if (goal.isEmpty())
        return;
    m_nativeGoal = goal;
    m_executionState = QStringLiteral("in_progress");
    if (clearConversation)
        clearLog();
    setResult({});
    setReport({});
    m_errorMessages.clear();
    resetFlowStages();
    setHasError(false);
    setRequiresSupervisorReview(false);
    setWorkspace({});
    resetContextUsage();
    setProgress(4);
    setPhase(QStringLiteral("Starting"));
    if (m_paused) {
        m_paused = false;
        emit pausedChanged();
    }
    m_receivedResult = false;
#ifdef DFT_AGENT_STUDIO_TESTING
    m_recoveryAttempted = false;
#endif
    m_currentProjectId = project.value("id").toString();
    m_nativeProjectSnapshot = project;
    m_liveSessionId = project.value(QStringLiteral("codexThreadId")).toString().trimmed();
    if (m_liveSessionId.isEmpty() && project.value(QStringLiteral("startNewCodexSession")).toBool()) {
        const QVariantMap created = SessionCatalog::dispatch(
            QStringLiteral("session_new"), project,
            {{QStringLiteral("name"), project.value(QStringLiteral("name")).toString().trimmed()
                 + QStringLiteral(" DFT 会话")}}, m_workingDirectory);
        const QVariantMap createdSession = created.value(QStringLiteral("result")).toMap()
                                              .value(QStringLiteral("session")).toMap();
        m_liveSessionId = createdSession.value(QStringLiteral("id")).toString().trimmed();
        if (!created.value(QStringLiteral("ok")).toBool() || m_liveSessionId.isEmpty()) {
            const QString message = created.value(QStringLiteral("message")).toString().trimmed().isEmpty()
                ? QStringLiteral("无法创建新的运行会话。")
                : created.value(QStringLiteral("message")).toString().trimmed();
            setPhase(QStringLiteral("Failed"));
            setFailureReport(message);
            emit errorOccurred(message);
            emit finished(false);
            return;
        }
        m_nativeProjectSnapshot.insert(QStringLiteral("codexThreadId"), m_liveSessionId);
        emit codexSessionReady(m_currentProjectId, m_liveSessionId);
        setActivitySession(m_liveSessionId);
        setFlowDisplaySession(m_liveSessionId);
    }
    m_liveTurnId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    if (m_activitySessionId.isEmpty() && !m_liveSessionId.isEmpty())
        m_activitySessionId = m_liveSessionId;
    // Publish the running transition only after the session identity is
    // initialized. Chat observes this signal to swap Send for Pause/Stop.
    setRunning(true);
    if (!clearConversation && !promptAlreadyRecorded)
        appendActivity({
            {QStringLiteral("kind"), QStringLiteral("user")},
            {QStringLiteral("text"), goal},
            {QStringLiteral("step"), 0},
        });
    persistSessionEvent({{QStringLiteral("event"), QStringLiteral("turn_requested")},
                         {QStringLiteral("turn_id"), m_liveTurnId},
                         {QStringLiteral("goal"), goal},
                         {QStringLiteral("resume"), project.value(QStringLiteral("resumeAgentTurn")).toBool()}});
    appendLog(QStringLiteral("Project: %1").arg(project.value("name").toString()));
    appendLog(QStringLiteral("Goal: %1").arg(goal));
    if (m_demoMode) {
        startDemo(project);
        return;
    }
    const QVariantMap &runProject = m_nativeProjectSnapshot;
    const QString selectedProvider = runProject.value(QStringLiteral("modelProvider")).toString().trimmed().toLower();
    const QString provider = selectedProvider == QStringLiteral("legacy") || selectedProvider == QStringLiteral("api")
        ? selectedProvider
        : qEnvironmentVariable("DFT_AGENT_INTELLIGENCE_PROVIDER", "api");
    m_liveSteeringEnabled = provider == QStringLiteral("api") || provider == QStringLiteral("legacy");
    const bool nativeLocalModel = nativeLocalModelConfigured(runProject, provider);
    if (provider == QStringLiteral("api") || nativeLocalModel) {
        if (startNativeResponsesRun(runProject, disabledCapabilities, goal))
            return;
        const QString message = provider == QStringLiteral("api")
            ? QStringLiteral("原生 API 回合初始化失败；请检查模型、API 凭据和 C++ 工具运行时配置。")
            : QStringLiteral("本地模型回合初始化失败；当前 Studio 仅支持通过 llama.cpp 加载 GGUF 基础模型与 GGUF LoRA 适配器。请检查 llama-server、模型文件和本地推理配置。");
        setPhase(QStringLiteral("Native runtime unavailable"));
        setFailureReport(message);
        setRunning(false);
        emit errorOccurred(message);
        emit finished(false);
        QTimer::singleShot(0, this, &AgentController::startNextQueuedPrompt);
        return;
    }
#ifdef DFT_AGENT_STUDIO_TESTING
    if (m_testWorkerExecutable.trimmed().isEmpty()) {
        const QString message = QStringLiteral("No native runtime is available for this provider configuration.");
        setPhase(QStringLiteral("Native runtime unavailable"));
        setFailureReport(message);
        setRunning(false);
        m_liveSteeringEnabled = false;
        emit errorOccurred(message);
        emit finished(false);
        QTimer::singleShot(0, this, &AgentController::startNextQueuedPrompt);
        return;
    }
    QVariantMap workerProject = runProject;
    const QString directApiKey = workerProject.take(QStringLiteral("modelApiKey")).toString().trimmed();
    QStringList arguments{
        "--goal", goal,
        "--model", workerProject.value("modelName").toString(),
        "--project-id", workerProject.value("id").toString(),
        "--project-context-json", QString::fromUtf8(QJsonDocument::fromVariant(workerProject).toJson(QJsonDocument::Compact)),
        "--intelligence-provider", provider,
        "--detailed"
    };
    arguments << "--native-bridge";
    const QString selectedModel = runProject.value(QStringLiteral("modelName")).toString().trimmed();
    if (provider == QStringLiteral("api") && !selectedModel.isEmpty())
        arguments << "--api-model" << selectedModel;
    const QString responsesBaseUrl = runProject.value(QStringLiteral("modelApiBase")).toString().trimmed();
    if (provider == QStringLiteral("api") && !responsesBaseUrl.isEmpty())
        arguments << "--responses-base-url" << responsesBaseUrl;
    const QString responsesApiKeyFile = runProject.value(QStringLiteral("modelApiKeyFile")).toString().trimmed();
    if (provider == QStringLiteral("api") && !responsesApiKeyFile.isEmpty())
        arguments << "--responses-api-key-file" << responsesApiKeyFile;
    const QString reasoningEffort = runProject.value(QStringLiteral("modelReasoningEffort")).toString().trimmed().toLower();
    if (provider == QStringLiteral("api") && !reasoningEffort.isEmpty())
        arguments << "--reasoning-effort" << reasoningEffort;
    if (!m_modelCatalogPath.isEmpty())
        arguments << "--model-config" << m_modelCatalogPath;
    for (const auto &capability : disabledCapabilities)
        arguments << "--disabled-capability" << capability;
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    if (provider == QStringLiteral("api") && !directApiKey.isEmpty())
        environment.insert(QStringLiteral("DFT_AGENT_RESPONSES_API_KEY"), directApiKey);
    m_process.setProcessEnvironment(environment);
    m_process.start(m_testWorkerExecutable, arguments);
#else
    const QString message = QStringLiteral(
        "此会话配置使用了已移除的旧式 Agent provider；请改用 Responses API 或 llama.cpp GGUF 模型运行时。");
    setPhase(QStringLiteral("Native runtime unavailable"));
    setFailureReport(message);
    setRunning(false);
    m_liveSteeringEnabled = false;
    emit errorOccurred(message);
    emit finished(false);
    QTimer::singleShot(0, this, &AgentController::startNextQueuedPrompt);
#endif
}

bool AgentController::startNativeResponsesRun(const QVariantMap &project,
                                             const QStringList &disabledCapabilities,
                                             const QString &goal) {
    const QString model = project.value(QStringLiteral("modelName")).toString().trimmed();
    QString baseUrl = project.value(QStringLiteral("modelApiBase")).toString().trimmed();
    if (baseUrl.isEmpty())
        baseUrl = qEnvironmentVariable("DFT_AGENT_RESPONSES_BASE_URL").trimmed();
    if (baseUrl.isEmpty())
        baseUrl = qEnvironmentVariable("DFT_AGENT_CODEX_RESPONSES_BASE_URL").trimmed();
    if (baseUrl.isEmpty())
        baseUrl = QStringLiteral("https://api.openai.com/v1");
    const bool localLlama = nativeLocalModelConfigured(
        project, project.value(QStringLiteral("modelProvider")).toString().trimmed().toLower());
    QString apiKey = project.value(QStringLiteral("modelApiKey")).toString().trimmed();
    QString apiKeyFile = project.value(QStringLiteral("modelApiKeyFile")).toString().trimmed();
    if (apiKeyFile.isEmpty())
        apiKeyFile = qEnvironmentVariable("DFT_AGENT_RESPONSES_API_KEY_FILE").trimmed();
    if (apiKeyFile.isEmpty())
        apiKeyFile = qEnvironmentVariable("DFT_AGENT_CODEX_RESPONSES_API_KEY_FILE").trimmed();
    if (apiKeyFile.isEmpty())
        apiKeyFile = QDir(studioDataRoot(m_workingDirectory)).filePath(QStringLiteral("agent_runtime/api.key"));
    if (apiKey.isEmpty() && !apiKeyFile.isEmpty()) {
        const QString keyPath = studioAbsolutePath(apiKeyFile, m_workingDirectory);
        const QFileInfo keyInfo(keyPath);
        QFile keyFile(keyPath);
        if (keyInfo.isFile() && !keyInfo.isSymLink() && keyInfo.size() <= 64 * 1024
            && keyFile.open(QIODevice::ReadOnly | QIODevice::Text))
            apiKey = QString::fromUtf8(keyFile.readAll()).trimmed();
    }
    if (apiKey.isEmpty())
        apiKey = qEnvironmentVariable("DFT_AGENT_RESPONSES_API_KEY").trimmed();
    if (apiKey.isEmpty())
        apiKey = qEnvironmentVariable("DFT_AGENT_CODEX_RESPONSES_API_KEY").trimmed();
    if (model.isEmpty() || baseUrl.isEmpty())
        return false;
    if (localLlama) {
        QString runtimeError;
        const QString serverBinary = LocalModelRuntimeService::findLlamaServerBinary(
            m_workingDirectory, project.value(QStringLiteral("modelLlamaServerPath")).toString(), &runtimeError);
        QVariantMap runtimeModel{
            {QStringLiteral("modelId"), model},
            {QStringLiteral("apiBase"), baseUrl},
            {QStringLiteral("baseModelPath"), project.value(QStringLiteral("modelBasePath"))},
            {QStringLiteral("adapterPath"), project.value(QStringLiteral("modelAdapterPath"))},
            {QStringLiteral("contextWindow"), project.value(QStringLiteral("modelContextWindow"), 65'536)},
            {QStringLiteral("maximumNewTokens"), project.value(QStringLiteral("modelMaximumNewTokens"), 1'024)},
            {QStringLiteral("inferenceMode"), project.value(QStringLiteral("modelInferenceMode"), QStringLiteral("cpu_gpu"))},
            {QStringLiteral("gpuMemoryGiB"), project.value(QStringLiteral("modelGpuMemoryGiB"), 0.0)},
        };
        QStringList command;
        if (serverBinary.isEmpty()
            || !LocalModelRuntimeService::llamaServerCommand(runtimeModel, m_workingDirectory,
                                                              serverBinary, 0, &command, &runtimeError)) {
            appendDetailedLog(QStringLiteral("Native local model setup failed: %1").arg(runtimeError));
            return false;
        }
        const QByteArray signature = QCryptographicHash::hash(
            command.join(QChar(0)).toUtf8(), QCryptographicHash::Sha256);
        if (m_localModelProcess.state() == QProcess::NotRunning
            || signature != m_localModelSignature) {
            if (m_localModelProcess.state() != QProcess::NotRunning) {
                m_localModelProcess.terminate();
                if (!m_localModelProcess.waitForFinished(5'000)) {
                    m_localModelProcess.kill();
                    m_localModelProcess.waitForFinished(2'000);
                }
            }
            m_localModelProcess.setProgram(command.takeFirst());
            m_localModelProcess.setArguments(command);
            m_localModelLogTail.clear();
            m_localModelProcess.start();
            if (!m_localModelProcess.waitForStarted(3'000)) {
                appendDetailedLog(QStringLiteral("Unable to start llama-server: %1")
                                      .arg(m_localModelProcess.errorString()));
                return false;
            }
            m_localModelSignature = signature;
            appendLog(QStringLiteral("Started native llama-server for %1.").arg(model));
        }
    }
    m_nativeProjectSnapshot = project;
    m_nativeDisabledCapabilities = disabledCapabilities;
    m_nativeGoal = goal;

    const QString runtimeId = m_liveSessionId.isEmpty()
        ? QUuid::createUuid().toString(QUuid::WithoutBraces) : m_liveSessionId;
    if (!m_nativeToolRuntimeId.isEmpty())
        AgentToolService::closeToolRuntime(m_nativeToolRuntimeId);
    m_nativeToolRuntimeId = runtimeId;
    const QPointer<AgentController> guard(this);
    const QVariantMap initialized = AgentToolService::initializeToolRuntime(
        runtimeId, project, m_workingDirectory, disabledCapabilities,
        [guard](const QVariantMap &event) {
            if (!guard)
                return;
            QMetaObject::invokeMethod(guard, [guard, event] {
                if (guard)
                    guard->appendTrace(QJsonObject::fromVariantMap(event));
            }, Qt::QueuedConnection);
        });
    if (!initialized.value(QStringLiteral("ok")).toBool()) {
        appendDetailedLog(QStringLiteral("Native tool runtime setup failed: %1")
                              .arg(initialized.value(QStringLiteral("message")).toString()));
        AgentToolService::closeToolRuntime(runtimeId);
        m_nativeToolRuntimeId.clear();
        return false;
    }
    QSet<QString> nativeToolNames;
    for (const QVariant &value : AgentToolService::responsesToolDefinitions(runtimeId)) {
        const QVariantMap outer = value.toMap();
        const QVariantMap function = outer.value(QStringLiteral("function")).toMap();
        nativeToolNames.insert((function.isEmpty() ? outer : function)
                                   .value(QStringLiteral("name")).toString());
    }
    QStringList missingCoreTools;
    for (const QString &name : {QStringLiteral("read_file"), QStringLiteral("apply_patch"),
                                QStringLiteral("create_file")}) {
        if (!nativeToolNames.contains(name))
            missingCoreTools.append(name);
    }
    if (!disabledCapabilities.contains(QStringLiteral("agent_shell_and_files"))) {
        if (!nativeToolNames.contains(QStringLiteral("shell")))
            missingCoreTools.append(QStringLiteral("shell"));
    }
    if (project.value(QStringLiteral("multiAgentEnabled")).toBool()) {
        for (const QString &name : {QStringLiteral("spawn_agent"), QStringLiteral("wait_agent"),
                                    QStringLiteral("list_agents"), QStringLiteral("send_message"),
                                    QStringLiteral("followup_task"), QStringLiteral("interrupt_agent")}) {
            if (!nativeToolNames.contains(name))
                missingCoreTools.append(name);
        }
    }
    const QVariantList dispatchBoundaries = initialized.value(QStringLiteral("native_dispatch_boundary")).toList();
    if (!missingCoreTools.isEmpty() || !dispatchBoundaries.isEmpty()) {
        QStringList boundaryNames;
        for (const QVariant &value : dispatchBoundaries)
            boundaryNames.append(value.toString());
        appendDetailedLog(QStringLiteral("Native Responses path remains gated; missing tools: %1; non-native dispatch boundaries: %2")
                              .arg(missingCoreTools.isEmpty() ? QStringLiteral("none") : missingCoreTools.join(QStringLiteral(", ")),
                                   boundaryNames.isEmpty() ? QStringLiteral("none") : boundaryNames.join(QStringLiteral(", "))));
        AgentToolService::closeToolRuntime(runtimeId);
        m_nativeToolRuntimeId.clear();
        return false;
    }

    const QVariantMap workspaceContext = WorkspaceCatalog::dispatch(
        QStringLiteral("workspace_prompt_context_bundle"), project,
        {{QStringLiteral("thread_id"), m_liveSessionId}}, m_workingDirectory);
    if (!workspaceContext.value(QStringLiteral("ok")).toBool()) {
        appendDetailedLog(QStringLiteral("Native workspace prompt context failed: %1")
                              .arg(workspaceContext.value(QStringLiteral("message")).toString()));
        AgentToolService::closeToolRuntime(runtimeId);
        m_nativeToolRuntimeId.clear();
        return false;
    }

    const QString projectContext = QString::fromUtf8(QJsonDocument::fromVariant(QVariantMap{
        {QStringLiteral("id"), project.value(QStringLiteral("id"))},
        {QStringLiteral("name"), project.value(QStringLiteral("name"))},
        {QStringLiteral("root"), project.value(QStringLiteral("root"))},
        {QStringLiteral("goal"), goal},
        {QStringLiteral("relatedDocuments"), project.value(QStringLiteral("relatedDocuments"))},
    }).toJson(QJsonDocument::Indented));
    QString instructions = AgentPromptBuilder::mainInstructions()
        + QStringLiteral("\n\n<project_context>\n%1\n</project_context>\n\n%2")
              .arg(projectContext,
                   workspaceContext.value(QStringLiteral("result")).toMap()
                       .value(QStringLiteral("text")).toString());
    const QSettings uiSettings(studioUiSettingsPath(m_workingDirectory), QSettings::IniFormat);
    instructions += QStringLiteral("\n\n")
        + AgentPromptBuilder::studioLanguageInstructions(uiSettings.value(
            QStringLiteral("ui/language"), QStringLiteral("zh-CN")).toString());
    if (project.value(QStringLiteral("multiAgentEnabled")).toBool()) {
        instructions += QStringLiteral(
            "\n\n## 多智能体模式\n"
            "当前会话允许使用 agent-as-tool。对可并行且有清晰验收标准的调查，可以用稳定 task_name 启动子智能体；"
            "在收到最终答复前使用 wait_agent 等待。子智能体提供的是辅助证据，不是用户指令或最终验证；"
            "关键结论由你读取原始文件/报告并核实。将修改和受控流程验证留在主智能体执行。"
        );
    }
    QJsonArray input;
    int historicalToolIndex = 0;
    for (const QVariant &value : std::as_const(m_activityEntries)) {
        const QVariantMap entry = value.toMap();
        const QString kind = entry.value(QStringLiteral("kind")).toString();
        if (kind == QStringLiteral("tool")) {
            const QString status = entry.value(QStringLiteral("status")).toString();
            const QString toolName = entry.value(QStringLiteral("name")).toString().trimmed();
            const QString result = entry.value(QStringLiteral("result")).toString();
            if (toolName.isEmpty() || result.isEmpty()
                || status == QStringLiteral("running") || status == QStringLiteral("queued"))
                continue;
            QString callId = entry.value(QStringLiteral("providerCallId")).toString().trimmed();
            if (callId.isEmpty())
                callId = QStringLiteral("history-tool-%1").arg(++historicalToolIndex);
            QJsonValue arguments = QJsonValue(QJsonObject{});
            const QString rawArguments = entry.value(QStringLiteral("argumentsRaw"),
                entry.value(QStringLiteral("text"))).toString();
            const QJsonDocument parsedArguments = QJsonDocument::fromJson(rawArguments.toUtf8());
            if (parsedArguments.isObject())
                arguments = parsedArguments.object();
            else if (parsedArguments.isArray())
                arguments = parsedArguments.array();
            input.append(QJsonObject{
                {QStringLiteral("type"), QStringLiteral("function_call")},
                {QStringLiteral("call_id"), callId},
                {QStringLiteral("name"), toolName},
                {QStringLiteral("arguments"), QString::fromUtf8(QJsonDocument(arguments.isObject()
                    ? QJsonDocument(arguments.toObject()) : QJsonDocument(arguments.toArray())).toJson(QJsonDocument::Compact))},
            });
            input.append(QJsonObject{
                {QStringLiteral("type"), QStringLiteral("function_call_output")},
                {QStringLiteral("call_id"), callId},
                {QStringLiteral("output"), result},
            });
            continue;
        }
        QString role;
        if (kind == QStringLiteral("user")) {
            role = QStringLiteral("user");
        } else if (kind == QStringLiteral("agent")) {
            const QString activityRole = entry.value(QStringLiteral("role")).toString();
            if (activityRole != QStringLiteral("response") && activityRole != QStringLiteral("final"))
                continue;
            if (entry.value(QStringLiteral("draft")).toBool())
                continue;
            role = QStringLiteral("assistant");
        } else {
            continue;
        }
        const QString text = entry.value(QStringLiteral("text")).toString().trimmed();
        if (!text.isEmpty())
            input.append(QJsonObject{{QStringLiteral("role"), role},
                                     {QStringLiteral("content"), text}});
    }
    if (input.isEmpty() || input.last().toObject().value(QStringLiteral("role")).toString() != QStringLiteral("user")
        || input.last().toObject().value(QStringLiteral("content")).toString() != goal)
        input.append(QJsonObject{{QStringLiteral("role"), QStringLiteral("user")},
                                 {QStringLiteral("content"), goal}});

    const QVariantList toolList = AgentToolService::responsesToolDefinitions(runtimeId);
    QJsonArray tools;
    for (const QVariant &tool : toolList)
        tools.append(QJsonObject::fromVariantMap(tool.toMap()));

    ResponsesTurnRunner::Request request;
    request.baseUrl = baseUrl;
    request.apiKey = apiKey;
    request.protocol = localLlama ? QStringLiteral("chat_completions") : QStringLiteral("responses");
    request.model = model;
    request.reasoningEffort = project.value(QStringLiteral("modelReasoningEffort"),
                                             QStringLiteral("medium")).toString();
    request.instructions = instructions;
    request.input = input;
    request.tools = tools;
    request.contextWindow = project.value(QStringLiteral("modelContextWindow"), m_contextWindow).toInt();
    request.effectiveContextPercent = project.value(QStringLiteral("modelEffectiveContextWindowPercent"), 95).toInt();
    request.maximumOutputTokens = project.value(QStringLiteral("modelMaximumNewTokens"), m_contextOutputLimit).toInt();
    request.reconnectMaxAttempts = project.value(QStringLiteral("modelReconnectMaxAttempts"), 10).toInt();
    request.reconnectDelayMs = qMax(100, qRound(project.value(QStringLiteral("modelReconnectDelaySeconds"), 1.0).toDouble() * 1000.0));
    request.samplingOptions = QJsonObject{
        {QStringLiteral("temperature"), project.value(QStringLiteral("modelTemperature"), 0.6).toDouble()},
        {QStringLiteral("top_p"), project.value(QStringLiteral("modelTopP"), 0.9).toDouble()},
        {QStringLiteral("top_k"), project.value(QStringLiteral("modelTopK"), 40).toInt()},
        {QStringLiteral("min_p"), project.value(QStringLiteral("modelMinP"), 0.05).toDouble()},
        {QStringLiteral("repeat_penalty"), project.value(QStringLiteral("modelRepeatPenalty"), 1.1).toDouble()},
        {QStringLiteral("repeat_last_n"), project.value(QStringLiteral("modelRepeatLastN"), 256).toInt()},
        {QStringLiteral("dry_multiplier"), project.value(QStringLiteral("modelDryMultiplier"), 0.5).toDouble()},
        {QStringLiteral("presence_penalty"), project.value(QStringLiteral("modelPresencePenalty"), 0.0).toDouble()},
        {QStringLiteral("frequency_penalty"), project.value(QStringLiteral("modelFrequencyPenalty"), 0.0).toDouble()},
    };
    request.artifactRoot = QDir(studioDataRoot(m_workingDirectory)).filePath(
        QStringLiteral("agent_runtime/threads/%1/transcript").arg(runtimeId));
    QDir().mkpath(request.artifactRoot);
    if (project.value(QStringLiteral("multiAgentEnabled")).toBool()) {
        const QVariantMap subagentRuntime = AgentToolService::configureSubagentRuntime(
            runtimeId, m_liveSessionId.isEmpty() ? runtimeId : m_liveSessionId, request);
        if (!subagentRuntime.value(QStringLiteral("ok")).toBool()) {
            appendDetailedLog(QStringLiteral("Native subagent runtime setup failed: %1")
                                  .arg(subagentRuntime.value(QStringLiteral("message")).toString()));
            AgentToolService::closeToolRuntime(runtimeId);
            m_nativeToolRuntimeId.clear();
            return false;
        }
    }
    m_nativeToolStep = 0;
    m_nativeToolSteps.clear();
    m_liveSteeringEnabled = true;
    appendLog(QStringLiteral("Starting native C++ %1 turn for %2.")
                  .arg(localLlama ? QStringLiteral("local Chat Completions") : QStringLiteral("Responses"), model));
    m_nativeTurnRunner->start(request,
        [runtimeId, guard](const QString &name, const QJsonObject &arguments,
                           const QString &callId, ResponsesTurnRunner::ToolResult completed) {
            if (!guard) {
                completed({{QStringLiteral("ok"), false},
                           {QStringLiteral("error"), QStringLiteral("AgentController was destroyed.")}});
                return;
            }
            AgentToolService::dispatchToolAsync(runtimeId, name, arguments.toVariantMap(), callId,
                [guard, completed = std::move(completed)](const QVariantMap &result) mutable {
                    const QJsonObject json = QJsonObject::fromVariantMap(result);
                    if (!guard) {
                        completed(json);
                        return;
                    }
                    QMetaObject::invokeMethod(guard, [guard, completed = std::move(completed), json]() mutable {
                        if (guard)
                            completed(json);
                    }, Qt::QueuedConnection);
                });
        });
    return true;
}

void AgentController::handleNativeResponsesEvent(const QJsonObject &event) {
    const QString type = event.value(QStringLiteral("type")).toString();
    if (type == QStringLiteral("response.output_text.delta")) {
        appendAgentActivity(event.value(QStringLiteral("delta")).toString(),
                            m_nativeToolStep + 1, QStringLiteral("response"), true,
                            QStringLiteral("native-answer"));
        return;
    }
    if (type == QStringLiteral("response.reasoning_summary_text.delta")) {
        appendAgentActivity(event.value(QStringLiteral("delta")).toString(),
                            m_nativeToolStep + 1, QStringLiteral("thinking"), true,
                            QStringLiteral("native-thinking"));
        return;
    }
    if (type == QStringLiteral("response.completed")) {
        const QJsonObject response = event.value(QStringLiteral("response")).toObject();
        const QJsonObject usage = response.value(QStringLiteral("usage")).toObject();
        const int inputTokens = usage.value(QStringLiteral("input_tokens")).toInt();
        if (inputTokens > 0) {
            setContextUsage({{QStringLiteral("event"), QStringLiteral("context_usage")},
                             {QStringLiteral("source"), QStringLiteral("provider_usage")},
                             {QStringLiteral("measured_input_tokens"), inputTokens},
                             {QStringLiteral("context_window"), m_contextWindow},
                             {QStringLiteral("effective_context_window"), m_contextEffectiveWindow},
                             {QStringLiteral("auto_compact_token_limit"), m_contextAutoCompactLimit},
                             {QStringLiteral("working_input_limit"), m_contextInputLimit},
                             {QStringLiteral("output_token_limit"), m_contextOutputLimit}});
        }
    }
    QJsonObject trace = event;
    trace.insert(QStringLiteral("event"), type);
    appendTrace(trace);
}

void AgentController::finishNativeResponsesRun(const QString &answer, int toolRounds) {
    m_receivedResult = true;
    m_liveSteeringEnabled = false;
    if (!m_nativeToolRuntimeId.isEmpty()) {
        AgentToolService::closeToolRuntime(m_nativeToolRuntimeId);
        m_nativeToolRuntimeId.clear();
    }
    if (m_paused) {
        m_paused = false;
        emit pausedChanged();
    }
    const QString finalText = answer.trimmed();
    persistSessionEvent({{QStringLiteral("event"), QStringLiteral("turn_finished")},
                         {QStringLiteral("turn_id"), m_liveTurnId},
                         {QStringLiteral("status"), QStringLiteral("completed")},
                         {QStringLiteral("answer"), finalText},
                         {QStringLiteral("tool_rounds"), toolRounds}});
    if (!finalText.isEmpty()) {
        appendAgentActivity(finalText, qMax(1, toolRounds + 1), QStringLiteral("final"), false,
                            QStringLiteral("native-answer"));
        setResult(finalText);
        setReport(finalText);
        setProgress(100);
        setPhase(QStringLiteral("Finished"));
        m_executionState = QStringLiteral("completed");
    } else {
        setPhase(QStringLiteral("Finished without text"));
        m_executionState = QStringLiteral("incomplete");
        setFailureReport(QStringLiteral("Responses API 完成了回合，但没有返回最终文本。"));
    }
    setRunning(false);
    emit finished(!finalText.isEmpty());
    QTimer::singleShot(0, this, &AgentController::startNextQueuedPrompt);
}

void AgentController::startNextQueuedPrompt() {
    if (m_running || m_queuedPrompts.isEmpty())
        return;
    const QueuedPrompt next = m_queuedPrompts.takeFirst();
    for (qsizetype index = m_activityEntries.size() - 1; index >= 0; --index) {
        QVariantMap entry = m_activityEntries.at(index).toMap();
        const QString status = entry.value(QStringLiteral("status")).toString();
        if (entry.value(QStringLiteral("kind")).toString() != QStringLiteral("user")
            || (status != QStringLiteral("queued") && status != QStringLiteral("steering"))
            || (!next.id.isEmpty() && entry.value(QStringLiteral("queueId")).toString() != next.id))
            continue;
        entry.insert(QStringLiteral("status"), QString{});
        m_activityEntries[index] = entry;
        emit activityEntriesChanged();
        break;
    }
    notifyQueuedPromptsChanged();
    startRun(next.project, next.disabledCapabilities, next.prompt, false, true);
}

bool AgentController::hasActiveEdaJob() const {
    for (const QVariant &value : m_activityEntries) {
        const QVariantMap entry = value.toMap();
        if (!entry.value(QStringLiteral("eda")).toBool())
            continue;
        const QString status = entry.value(QStringLiteral("status")).toString().trimmed().toLower();
        const QString state = entry.value(QStringLiteral("edaState")).toString().trimmed().toLower();
        if (status == QStringLiteral("running")
            || state == QStringLiteral("queued")
            || state == QStringLiteral("running")
            || state == QStringLiteral("waiting_for_eda")) {
            return true;
        }
    }
    return false;
}

QString AgentController::readPatchDiff(const QString &patchFile) const {
    const QFileInfo candidate(resolveStudioRecordPath(patchFile, m_workingDirectory));
    const QString root = QFileInfo(QDir(studioDataRoot(m_workingDirectory)).filePath(
        QStringLiteral("agent_runtime/patches")
    )).canonicalFilePath();
    const QString workspaceRoot = QFileInfo(m_workspace).canonicalFilePath();
    const QString path = candidate.canonicalFilePath();
    const bool inPatchStore = !root.isEmpty() && path.startsWith(root + QDir::separator());
    const bool inShellChanges = !workspaceRoot.isEmpty()
        && path.startsWith(QDir(workspaceRoot).filePath(QStringLiteral("changes")) + QDir::separator());
    if (path.isEmpty() || !candidate.isFile() || (!inPatchStore && !inShellChanges))
        return QStringLiteral("补丁文件不可用。");
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return QStringLiteral("补丁文件无法读取。");
    const QByteArray bytes = file.read(192 * 1024 + 1);
    if (bytes.size() > 192 * 1024)
        return QStringLiteral("补丁文件超过显示上限。");
    return QString::fromUtf8(bytes);
}

QString AgentController::readPatchDiffHtml(const QString &patchFile, bool darkMode) const {
    const QString raw = readPatchDiff(patchFile);
    if (raw.isEmpty() || raw == QStringLiteral("补丁文件不可用。")
        || raw == QStringLiteral("补丁文件无法读取。")
        || raw == QStringLiteral("补丁文件超过显示上限。"))
        return raw.toHtmlEscaped();
    return renderPatchDiffHtml(raw, darkMode);
}

QString AgentController::renderPatchDiffHtml(const QString &raw, bool darkMode) const {
    if (raw.isEmpty())
        return QString{};
    const QStringList lines = raw.split(QLatin1Char('\n'));
    const QString baseBackground = darkMode ? QStringLiteral("#171b20") : QStringLiteral("#ffffff");
    const QString baseForeground = darkMode ? QStringLiteral("#d7e0e8") : QStringLiteral("#425466");
    const QString contextBackground = darkMode ? QStringLiteral("#20262d") : QStringLiteral("#f8fafc");
    const QString hunkBackground = darkMode ? QStringLiteral("#29333d") : QStringLiteral("#edf2f7");
    const QString hunkForeground = darkMode ? QStringLiteral("#aab8c5") : QStringLiteral("#7b8794");
    const QString addedBackground = darkMode ? QStringLiteral("#193729") : QStringLiteral("#e5f5e9");
    const QString addedForeground = darkMode ? QStringLiteral("#9bddae") : QStringLiteral("#216e39");
    const QString removedBackground = darkMode ? QStringLiteral("#402727") : QStringLiteral("#fde8e7");
    const QString removedForeground = darkMode ? QStringLiteral("#ffaaa4") : QStringLiteral("#b42318");
    const QString lineNumberForeground = darkMode ? QStringLiteral("#91a0ad") : QStringLiteral("#7b8794");
    const QString lineNumberBorder = darkMode ? QStringLiteral("#46515c") : QStringLiteral("#d7e3ed");
    QString html = QStringLiteral("<div style=\"font-family:monospace; font-size:13px; background:%1; color:%2;\">")
        .arg(baseBackground, baseForeground);
    int oldLine = 0;
    int newLine = 0;
    const QRegularExpression hunkPattern(QStringLiteral("^@@ -(\\d+)(?:,\\d+)? \\+(\\d+)(?:,\\d+)? @@"));
    for (const QString &line : lines) {
        if (line.startsWith(QStringLiteral("@@"))) {
            const auto match = hunkPattern.match(line);
            if (match.hasMatch()) {
                oldLine = match.captured(1).toInt();
                newLine = match.captured(2).toInt();
            }
            html += QStringLiteral("<div style=\"color:%1;background:%2;padding:3px 8px;\">")
                .arg(hunkForeground, hunkBackground)
                + line.toHtmlEscaped() + QStringLiteral("</div>");
            continue;
        }
        QString oldNumber;
        QString newNumber;
        QString background;
        QString foreground = baseForeground;
        QChar marker = line.isEmpty() ? QLatin1Char(' ') : line.at(0);
        if (marker == QLatin1Char('+') && !line.startsWith(QStringLiteral("+++"))) {
            newNumber = QString::number(newLine++);
            background = addedBackground;
            foreground = addedForeground;
        } else if (marker == QLatin1Char('-') && !line.startsWith(QStringLiteral("---"))) {
            oldNumber = QString::number(oldLine++);
            background = removedBackground;
            foreground = removedForeground;
        } else if (marker == QLatin1Char(' ') && oldLine > 0 && newLine > 0) {
            oldNumber = QString::number(oldLine++);
            newNumber = QString::number(newLine++);
        } else {
            background = contextBackground;
        }
        const QString content = line.toHtmlEscaped();
        html += QStringLiteral("<div style=\"background:%1;color:%2;white-space:pre;\">")
            .arg(background.isEmpty() ? contextBackground : background, foreground)
            + QStringLiteral("<span style=\"display:inline-block;width:52px;color:%1;text-align:right;padding-right:10px;border-right:1px solid %2;\">")
                .arg(lineNumberForeground, lineNumberBorder)
            + (marker == QLatin1Char('-') ? oldNumber : newNumber).toHtmlEscaped()
            + QStringLiteral("</span>")
            + content + QStringLiteral("</div>");
    }
    html += QStringLiteral("</div>");
    return html;
}

QString AgentController::validatedReportPath(const QString &reportFile) const {
    const QFileInfo workspaceInfo(m_workspace);
    const QString workspaceRoot = workspaceInfo.canonicalFilePath();
    const QFileInfo candidate(reportFile);
    const QString path = candidate.canonicalFilePath();
    bool allowed = !workspaceRoot.isEmpty()
        && !path.isEmpty()
        && path.startsWith(workspaceRoot + QDir::separator());
    if (!allowed && !path.isEmpty() && candidate.isFile() && !candidate.isSymLink()
        && candidate.suffix().compare(QStringLiteral("md"), Qt::CaseInsensitive) == 0) {
        const QString dataRoot = studioDataRoot(m_workingDirectory);
        const QString threadsRoot = QFileInfo(QDir(dataRoot).filePath(QStringLiteral("agent_runtime/threads"))).canonicalFilePath();
        const QString reportDirectory = candidate.absoluteDir().canonicalPath();
        const QFileInfo reportDirInfo(reportDirectory);
        const QString groupDirectory = QFileInfo(reportDirectory).dir().canonicalPath();
        const QString sessionId = QFileInfo(groupDirectory).fileName();
        const QFileInfo metadataInfo(QDir(candidate.absolutePath()).filePath(
            candidate.completeBaseName() + QStringLiteral(".json")));
        if (!threadsRoot.isEmpty() && reportDirInfo.fileName() == QStringLiteral("run_reports")
            && groupDirectory.startsWith(threadsRoot + QDir::separator())
            && !reportDirInfo.isSymLink() && !metadataInfo.isSymLink() && metadataInfo.isFile()) {
            QFile metadataFile(metadataInfo.absoluteFilePath());
            if (metadataFile.open(QIODevice::ReadOnly) && metadataFile.size() <= 64 * 1024) {
                const auto document = QJsonDocument::fromJson(metadataFile.readAll());
                if (document.isObject()) {
                    const auto metadata = document.object();
                    allowed = metadata.value(QStringLiteral("session_id")).toString() == sessionId
                        && QFileInfo(metadata.value(QStringLiteral("report_file")).toString()).fileName() == candidate.fileName();
                }
            }
        }
    }
    if (!allowed && candidate.isFile() && !path.isEmpty()
        && candidate.suffix().compare(QStringLiteral("log"), Qt::CaseInsensitive) == 0) {
        QDir flowDirectory(candidate.absoluteDir());
        bool inFlow = flowDirectory.dirName() == QStringLiteral("flow");
        if (!inFlow && flowDirectory.dirName() == QStringLiteral("logs"))
            inFlow = flowDirectory.cdUp() && flowDirectory.dirName() == QStringLiteral("flow");
        QDir workspaceDirectory(flowDirectory);
        const bool hasWorkspace = inFlow && workspaceDirectory.cdUp();
        if (hasWorkspace) {
            const QString workspacePath = workspaceDirectory.canonicalPath();
            const QFileInfo marker(workspaceDirectory.filePath(QStringLiteral(".dft_agent_workspace.json")));
            const QFileInfo stage(workspaceDirectory.filePath(QStringLiteral("stage.json")));
            const QFileInfo evidence(workspaceDirectory.filePath(QStringLiteral("skill_result.json")));
            const bool ownedWorkspace = (marker.isFile() && !marker.isSymLink())
                || (stage.isFile() && !stage.isSymLink())
                || (evidence.isFile() && !evidence.isSymLink());
            const QString flowRoot = workspacePath.isEmpty()
                ? QString{} : QDir(QDir(workspacePath).filePath(QStringLiteral("flow"))).canonicalPath();
            allowed = ownedWorkspace && !flowRoot.isEmpty()
                && path.startsWith(flowRoot + QDir::separator());
        }
    }
    if (path.isEmpty() || !candidate.isFile() || !allowed)
        return {};
    return path;
}

QString AgentController::readReport(const QString &reportFile) const {
    if (reportFile == QStringLiteral("__current_report__"))
        return m_report;
    const QString path = validatedReportPath(reportFile);
    if (path.isEmpty())
        return QStringLiteral("报告文件不可用。\n");

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return QStringLiteral("报告文件无法读取。\n");
    constexpr qint64 maximumBytes = 768 * 1024;
    const QByteArray bytes = file.read(maximumBytes + 1);
    if (bytes.size() > maximumBytes)
        return QString::fromUtf8(bytes.left(maximumBytes)) + QStringLiteral("\n\n> 报告过长，后续内容已省略。\n");
    return QString::fromUtf8(bytes);
}

QVariantMap AgentController::readReportPage(const QString &reportFile, qint64 beforeOffset,
                                            qint64 maximumBytes) const {
    const QString path = validatedReportPath(reportFile);
    if (path.isEmpty())
        return {{QStringLiteral("available"), false}, {QStringLiteral("text"), QString{}}};

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {{QStringLiteral("available"), false}, {QStringLiteral("text"), QString{}}};

    const qint64 fileSize = file.size();
    const qint64 end = beforeOffset < 0 ? fileSize : qBound<qint64>(0, beforeOffset, fileSize);
    const qint64 boundedBytes = qBound<qint64>(qint64(4096), maximumBytes, qint64(262144));
    qint64 start = qMax<qint64>(0, end - boundedBytes);
    if (!file.seek(start))
        return {{QStringLiteral("available"), false}, {QStringLiteral("text"), QString{}}};
    QByteArray bytes = file.read(end - start);
    if (start > 0 && !bytes.isEmpty()) {
        const qsizetype newline = bytes.indexOf('\n');
        if (newline >= 0) {
            start += newline + 1;
            bytes.remove(0, newline + 1);
        }
    }
    return {
        {QStringLiteral("available"), true},
        {QStringLiteral("text"), QString::fromUtf8(bytes)},
        {QStringLiteral("start_offset"), start},
        {QStringLiteral("end_offset"), end},
        {QStringLiteral("file_size"), fileSize},
        {QStringLiteral("has_more"), start > 0},
    };
}

QVariantMap AgentController::readReportRange(const QString &reportFile, qint64 fromOffset,
                                             qint64 maximumBytes) const {
    const QString path = validatedReportPath(reportFile);
    if (path.isEmpty())
        return {{QStringLiteral("available"), false}, {QStringLiteral("text"), QString{}}};

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {{QStringLiteral("available"), false}, {QStringLiteral("text"), QString{}}};

    const qint64 fileSize = file.size();
    const qint64 start = qBound<qint64>(0, fromOffset, fileSize);
    const qint64 boundedBytes = qBound<qint64>(qint64(4096), maximumBytes, qint64(262144));
    if (!file.seek(start))
        return {{QStringLiteral("available"), false}, {QStringLiteral("text"), QString{}}};
    QByteArray bytes = file.read(qMin(boundedBytes, fileSize - start));
    qint64 end = start + bytes.size();
    if (end < fileSize) {
        const qsizetype newline = bytes.lastIndexOf('\n');
        if (newline >= 0) {
            bytes.truncate(newline + 1);
            end = start + newline + 1;
        }
    }
    return {
        {QStringLiteral("available"), true},
        {QStringLiteral("text"), QString::fromUtf8(bytes)},
        {QStringLiteral("start_offset"), start},
        {QStringLiteral("end_offset"), end},
        {QStringLiteral("file_size"), fileSize},
        {QStringLiteral("has_more"), end < fileSize},
    };
}

QStringList AgentController::edaLogFiles(const QString &workspace) const {
    const QFileInfo workspaceInfo(workspace);
    QString workspacePath = workspaceInfo.canonicalFilePath();
    if (workspacePath.isEmpty() || workspaceInfo.isSymLink())
        return {};
    if (QFileInfo(workspacePath).fileName() == QStringLiteral("flow"))
        workspacePath = QFileInfo(workspacePath).dir().canonicalPath();
    const QFileInfo marker(QDir(workspacePath).filePath(QStringLiteral(".dft_agent_workspace.json")));
    const QFileInfo stage(QDir(workspacePath).filePath(QStringLiteral("stage.json")));
    const QFileInfo evidence(QDir(workspacePath).filePath(QStringLiteral("skill_result.json")));
    const bool ownedWorkspace = (marker.isFile() && !marker.isSymLink())
        || (stage.isFile() && !stage.isSymLink())
        || (evidence.isFile() && !evidence.isSymLink());
    const QString flowRoot = QDir(QDir(workspacePath).filePath(QStringLiteral("flow"))).canonicalPath();
    if (!ownedWorkspace || flowRoot.isEmpty())
        return {};

    QStringList files;
    QDirIterator iterator(flowRoot, {QStringLiteral("*.log")},
                          QDir::Files | QDir::Readable | QDir::NoSymLinks,
                          QDirIterator::Subdirectories);
    while (iterator.hasNext()) {
        const QFileInfo file(iterator.next());
        const QString canonical = file.canonicalFilePath();
        if (!canonical.isEmpty() && canonical.startsWith(flowRoot + QDir::separator()))
            files.append(canonical);
    }
    std::sort(files.begin(), files.end(), [](const QString &left, const QString &right) {
        return QFileInfo(left).lastModified() > QFileInfo(right).lastModified();
    });
    return files;
}

QString AgentController::renderMarkdown(const QString &markdown) const {
    QTextDocument document;
    QString source = markdown.trimmed();
    // Some older gateways escaped an entire HTML error page before persisting
    // it in the transcript ("&lt;!DOCTYPE ..."). Decode that wrapper once so
    // the page is parsed as HTML rather than displayed as source code.
    if (source.startsWith(QStringLiteral("&lt;!doctype"), Qt::CaseInsensitive)
        || source.startsWith(QStringLiteral("&lt;<html"), Qt::CaseInsensitive)) {
        QTextDocument decoder;
        decoder.setHtml(source);
        const QString decoded = decoder.toPlainText().trimmed();
        if (decoded.startsWith(QStringLiteral("<!doctype"), Qt::CaseInsensitive)
            || decoded.startsWith(QStringLiteral("<html"), Qt::CaseInsensitive)) {
            source = decoded;
        }
    }
    // Rendering is called from QML bindings and can therefore happen again
    // whenever ListView reuses a delegate. Avoid repeated parsing for normal
    // sized messages while keeping the cache bounded for unusually large
    // transcripts.
    constexpr qsizetype maximumCachedSource = 512 * 1024;
    const bool cacheable = source.size() <= maximumCachedSource;
    if (cacheable) {
        const auto cached = m_markdownRenderCache.constFind(source);
        if (cached != m_markdownRenderCache.constEnd())
            return cached.value();
    }
    QHash<QString, QString> mathImages;
    QSet<QString> displayMathTokens;
    // Persisted sessions can contain a complete HTML response from a gateway
    // that returned a web error page.  Treat that as HTML instead of feeding
    // it to the Markdown parser, which would render the tags as visible text.
    if (source.startsWith(QStringLiteral("<!doctype"), Qt::CaseInsensitive)
        || source.startsWith(QStringLiteral("<html"), Qt::CaseInsensitive)) {
        document.setHtml(source);
    } else {
        QString markdownSource;
        bool inFence = false;
        int mathIndex = 0;
        for (qsizetype i = 0; i < source.size();) {
            if (i == 0 || source.at(i - 1) == QLatin1Char('\n')) {
                qsizetype lineEnd = source.indexOf(QLatin1Char('\n'), i);
                if (lineEnd < 0)
                    lineEnd = source.size();
                const QString trimmed = source.mid(i, lineEnd - i).trimmed();
                if (trimmed.startsWith(QStringLiteral("```")) || trimmed.startsWith(QStringLiteral("~~~"))) {
                    inFence = !inFence;
                    const qsizetype copyEnd = lineEnd < source.size() ? lineEnd + 1 : lineEnd;
                    markdownSource += source.mid(i, copyEnd - i);
                    i = copyEnd;
                    continue;
                }
            }
            if (inFence) {
                markdownSource += source.at(i++);
                continue;
            }
            if (source.at(i) == QLatin1Char('`')) {
                qsizetype tickEnd = i;
                while (tickEnd < source.size() && source.at(tickEnd) == QLatin1Char('`'))
                    ++tickEnd;
                const QString marker = source.mid(i, tickEnd - i);
                qsizetype codeEnd = source.indexOf(marker, tickEnd);
                const qsizetype lineBreak = source.indexOf(QLatin1Char('\n'), tickEnd);
                if (codeEnd >= 0 && (lineBreak < 0 || codeEnd < lineBreak)) {
                    const qsizetype copyEnd = codeEnd + marker.size();
                    markdownSource += source.mid(i, copyEnd - i);
                    i = copyEnd;
                    continue;
                }
            }

            QString opener;
            QString closer;
            bool display = false;
            if (source.at(i) == QLatin1Char('$') && (i == 0 || source.at(i - 1) != QLatin1Char('\\'))) {
                display = i + 1 < source.size() && source.at(i + 1) == QLatin1Char('$');
                opener = display ? QStringLiteral("$$") : QStringLiteral("$");
                closer = opener;
            } else if (source.mid(i, 2) == QStringLiteral("\\[")) {
                opener = QStringLiteral("\\[");
                closer = QStringLiteral("\\]");
                display = true;
            } else if (source.mid(i, 2) == QStringLiteral("\\(")) {
                opener = QStringLiteral("\\(");
                closer = QStringLiteral("\\)");
            }
            if (!opener.isEmpty()) {
                qsizetype close = i + opener.size();
                while (close < source.size()) {
                    close = source.indexOf(closer, close);
                    if (close < 0)
                        break;
                    if (closer.startsWith(QLatin1Char('$')) && !display
                        && close + 1 < source.size() && source.at(close + 1) == QLatin1Char('$')) {
                        ++close;
                        continue;
                    }
                    if (close > 0 && source.at(close - 1) == QLatin1Char('\\')) {
                        close += closer.size();
                        continue;
                    }
                    if (!display && source.mid(i, opener.size()) != QStringLiteral("\\(")
                        && source.mid(i, opener.size()) != QStringLiteral("\\[")
                        && source.mid(i, close - i).contains(QLatin1Char('\n')))
                        break;
                    break;
                }
                if (mathIndex < 128 && close >= i + opener.size() && close >= 0 && close + closer.size() <= source.size()
                    && source.mid(close, closer.size()) == closer) {
                    const QString expression = source.mid(i + opener.size(), close - i - opener.size());
                    const QString token = QStringLiteral("DFTMATHTOKEN%1END").arg(mathIndex++);
                    const QString encoded = QString::fromLatin1(expression.toUtf8().toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
                    const QString imageId = (display ? QStringLiteral("d") : QStringLiteral("i")) + encoded;
                    mathImages.insert(token, QStringLiteral("<img src=\"image://latex/%1\" />").arg(imageId));
                    if (display)
                        displayMathTokens.insert(token);
                    markdownSource += token;
                    i = close + closer.size();
                    continue;
                }
            }
            markdownSource += source.at(i++);
        }
        document.setMarkdown(markdownSource);
    }
    const QString html = document.toHtml();
    // QTextDocument::toHtml() returns a complete HTML document. QML TextEdit
    // expects an inline rich-text fragment; passing the document wrapper makes
    // it display the doctype, head and CSS source instead of the answer.
    const int bodyOpen = html.indexOf(QStringLiteral("<body"), 0, Qt::CaseInsensitive);
    QString rendered;
    if (bodyOpen >= 0) {
        const int contentStart = html.indexOf(QLatin1Char('>'), bodyOpen);
        const int bodyClose = html.lastIndexOf(QStringLiteral("</body>"), -1, Qt::CaseInsensitive);
        if (contentStart >= 0 && bodyClose > contentStart)
            rendered = html.mid(contentStart + 1, bodyClose - contentStart - 1).trimmed();
    }
    if (rendered.isEmpty()) {
        // Never return a complete document wrapper to QML TextEdit. If a
        // broken gateway response defeated Qt's body extraction, plain text
        // is preferable to exposing <!DOCTYPE>, CSS and meta tags.
        if (html.startsWith(QStringLiteral("<!doctype"), Qt::CaseInsensitive)
            || html.startsWith(QStringLiteral("<html"), Qt::CaseInsensitive)) {
            rendered = document.toPlainText().trimmed();
        } else {
            rendered = html;
        }
    }
    // Tokens are inserted after QTextDocument's Markdown conversion so TeX
    // remains isolated from Markdown and is loaded by QML asynchronously.
    for (auto it = mathImages.cbegin(); it != mathImages.cend(); ++it) {
        const QString token = it.key();
        const QString image = it.value();
        if (displayMathTokens.contains(token)) {
            const QRegularExpression paragraph(QStringLiteral("<p[^>]*>\\s*%1\\s*</p>").arg(QRegularExpression::escape(token)),
                                               QRegularExpression::CaseInsensitiveOption);
            if (!paragraph.match(rendered).hasMatch())
                rendered.replace(token, image);
            else
                rendered.replace(paragraph, QStringLiteral("<p align=\"center\">%1</p>").arg(image));
        } else {
            rendered.replace(token, image);
        }
    }
    if (cacheable) {
        if (m_markdownRenderCache.size() >= 256)
            m_markdownRenderCache.clear();
        m_markdownRenderCache.insert(source, rendered);
    }
    return rendered;
}

QString AgentController::renderPlainText(const QString &markup) const {
    QString source = markup.trimmed();
    if (source.startsWith(QStringLiteral("&lt;!doctype"), Qt::CaseInsensitive)
        || source.startsWith(QStringLiteral("&lt;<html"), Qt::CaseInsensitive)) {
        QTextDocument decoder;
        decoder.setHtml(source);
        source = decoder.toPlainText().trimmed();
    }
    QTextDocument document;
    if (source.startsWith(QStringLiteral("<!doctype"), Qt::CaseInsensitive)
        || source.startsWith(QStringLiteral("<html"), Qt::CaseInsensitive)
        || source.startsWith(QStringLiteral("<body"), Qt::CaseInsensitive)) {
        document.setHtml(source);
    } else {
        document.setMarkdown(markup);
    }
    return document.toPlainText().trimmed();
}

QVariantMap AgentController::decidePatch(const QString &proposalId, bool approved) {
    QVariantMap outcome;
    const QString identifier = proposalId.trimmed();
    if (identifier.isEmpty()) {
        outcome.insert(QStringLiteral("ok"), false);
        outcome.insert(QStringLiteral("message"), QStringLiteral("补丁候选标识不可用。"));
        return outcome;
    }
    outcome = PatchActionService::decide(identifier, approved, m_workingDirectory);
    if (!outcome.value(QStringLiteral("ok")).toBool())
        return outcome;
    const QVariantMap result = outcome.value(QStringLiteral("result")).toMap();
    const QString status = result.value(QStringLiteral("status")).toString();
    for (int index = m_activityEntries.size() - 1; index >= 0; --index) {
        QVariantMap entry = m_activityEntries.at(index).toMap();
        if (entry.value(QStringLiteral("kind")).toString() != QStringLiteral("patch")
            || entry.value(QStringLiteral("proposalId")).toString() != identifier)
            continue;
        entry.insert(QStringLiteral("status"), status);
        entry.insert(QStringLiteral("isolatedRoot"), result.value(QStringLiteral("isolated_root")));
        m_activityEntries[index] = entry;
        emit activityEntriesChanged();
        break;
    }
    appendLog(QStringLiteral("Patch %1: %2").arg(identifier, status));
    return outcome;
}

QVariantMap AgentController::rollbackFileEdit(const QString &editId, const QString &projectId, const QString &projectRoot) {
    QVariantMap outcome;
    const QString identifier = editId.trimmed();
    if (identifier.isEmpty() || projectId.trimmed().isEmpty() || projectRoot.trimmed().isEmpty()) {
        outcome.insert(QStringLiteral("ok"), false);
        outcome.insert(QStringLiteral("message"), QStringLiteral("自主编辑回滚参数不可用。"));
        return outcome;
    }
    outcome = PatchActionService::rollback(identifier, projectId.trimmed(), projectRoot.trimmed(), m_workingDirectory);
    if (!outcome.value(QStringLiteral("ok")).toBool())
        return outcome;
    for (int index = m_activityEntries.size() - 1; index >= 0; --index) {
        QVariantMap entry = m_activityEntries.at(index).toMap();
        if (entry.value(QStringLiteral("editId")).toString() != identifier)
            continue;
        entry.insert(QStringLiteral("status"), QStringLiteral("rolled_back"));
        m_activityEntries[index] = entry;
        emit activityEntriesChanged();
        break;
    }
    appendLog(QStringLiteral("Edit %1 rolled back.").arg(identifier));
    return outcome;
}

void AgentController::setActivitySession(const QString &sessionId) {
    m_activitySessionId = sessionId.trimmed();
    for (AgentController *run : std::as_const(m_parallelRuns)) {
        if (run && run->m_liveSessionId == m_activitySessionId) {
            m_activityEntries = run->m_activityEntries;
            emit activityEntriesChanged();
            break;
        }
    }
}

void AgentController::updatePathPermissionEntry(const QString &requestId, const QString &status) {
    const QString requested = requestId.trimmed();
    if (requested.isEmpty() || !shouldAppendLiveActivity())
        return;
    for (int index = m_activityEntries.size() - 1; index >= 0; --index) {
        QVariantMap entry = m_activityEntries.at(index).toMap();
        if (entry.value(QStringLiteral("requestId")).toString() != requested)
            continue;
        entry.insert(QStringLiteral("permissionStatus"), status.trimmed());
        entry.insert(QStringLiteral("status"), status == QStringLiteral("denied")
            ? QStringLiteral("rejected") : QStringLiteral("completed"));
        m_activityEntries[index] = entry;
        emit activityEntriesChanged();
        break;
    }
}

QVariantMap AgentController::decideNativeTool(const QString &requestId, bool approved) {
    if (m_nativeToolRuntimeId.isEmpty() || requestId.trimmed().isEmpty())
        return {{QStringLiteral("ok"), false},
                {QStringLiteral("message"), QStringLiteral("当前没有等待中的原生工具审批。")}};
    const QVariantMap result = AgentToolService::decideTool(m_nativeToolRuntimeId, requestId.trimmed(), approved);
    if (!result.value(QStringLiteral("ok")).toBool())
        return result;
    for (int index = m_activityEntries.size() - 1; index >= 0; --index) {
        QVariantMap entry = m_activityEntries.at(index).toMap();
        if (entry.value(QStringLiteral("kind")).toString() != QStringLiteral("approval")
            || entry.value(QStringLiteral("requestId")).toString() != requestId.trimmed())
            continue;
        entry.insert(QStringLiteral("permissionStatus"), approved ? QStringLiteral("approved")
                                                                  : QStringLiteral("denied"));
        m_activityEntries[index] = entry;
        emit activityEntriesChanged();
        break;
    }
    return result;
}

bool AgentController::shouldAppendLiveActivity() const {
    return m_liveSessionId.isEmpty()
        || m_activitySessionId.isEmpty()
        || m_liveSessionId == m_activitySessionId;
}

QVariantMap AgentController::workspaceAction(const QVariantMap &project, const QString &action, const QVariantMap &payload) {
    QVariantMap outcome;
    const QString requested = action.trimmed();
    if (requested.isEmpty() || project.value(QStringLiteral("id")).toString().trimmed().isEmpty()) {
        outcome.insert(QStringLiteral("ok"), false);
        outcome.insert(QStringLiteral("message"), QStringLiteral("工作台操作或项目标识不可用。"));
        return outcome;
    }
    if (WorkspaceDatabase::supports(requested))
        return WorkspaceDatabase::dispatch(requested, project.value(QStringLiteral("id")).toString(),
                                           payload, m_workingDirectory);
    if (WorkspaceCatalog::supports(requested))
        return WorkspaceCatalog::dispatch(requested, project, payload, m_workingDirectory);
    if (SessionCatalog::supports(requested))
        return SessionCatalog::dispatch(requested, project, payload, m_workingDirectory);
    return {{QStringLiteral("ok"), false},
            {QStringLiteral("message"), QStringLiteral("尚未实现原生 C++ 工作台操作：%1").arg(requested)}};
}

void AgentController::workspaceActionAsync(const QString &requestId, const QVariantMap &project,
                                          const QString &action, const QVariantMap &payload) {
    const QString requested = action.trimmed();
    if (requestId.trimmed().isEmpty() || requested.isEmpty()
        || project.value(QStringLiteral("id")).toString().trimmed().isEmpty()) {
        emit workspaceActionCompleted(requestId, {
            {QStringLiteral("ok"), false},
            {QStringLiteral("message"), QStringLiteral("工作台操作或项目标识不可用。")},
        });
        return;
    }
    if (WorkspaceDatabase::supports(requested)) {
        emit workspaceActionCompleted(requestId,
            WorkspaceDatabase::dispatch(requested, project.value(QStringLiteral("id")).toString(),
                                        payload, m_workingDirectory));
        return;
    }
    if (WorkspaceCatalog::supports(requested)) {
        emit workspaceActionCompleted(requestId,
            WorkspaceCatalog::dispatch(requested, project, payload, m_workingDirectory));
        return;
    }
    if (SessionCatalog::supports(requested)) {
        emit workspaceActionCompleted(requestId,
            SessionCatalog::dispatch(requested, project, payload, m_workingDirectory));
        return;
    }

    emit workspaceActionCompleted(requestId, {
        {QStringLiteral("ok"), false},
        {QStringLiteral("message"), QStringLiteral("尚未实现原生 C++ 工作台操作：%1").arg(requested)},
    });
}

void AgentController::loadSessionActivity(const QVariantList &entries, bool prepend) {
    // Pages are returned in chronological order.  Older pages therefore need
    // to be inserted before the current page; appending them made the top
    // edge appear to reload the same latest entries over and over.
    QVariantList loaded;
    for (const QVariant &value : entries) {
        QVariantMap entry = value.toMap();
        const QString kind = entry.value(QStringLiteral("kind")).toString();
        const bool chatEntry = kind == QStringLiteral("user") || kind == QStringLiteral("agent");
        const bool reconnectEntry = kind == QStringLiteral("reconnect");
        const bool toolEntry = kind == QStringLiteral("tool") || kind == QStringLiteral("approval")
            || kind == QStringLiteral("patch");
        const QString rawText = entry.value(QStringLiteral("text")).toString();
        const QString text = chatEntry ? canonicalChatText(rawText) : rawText.trimmed();
        if ((!chatEntry && !toolEntry && !reconnectEntry) || (chatEntry && text.isEmpty()))
            continue;
        const int textLimit = chatEntry && entry.value(QStringLiteral("role")).toString() == QStringLiteral("thinking")
            ? 64'000 : 16'000;
        entry.insert(QStringLiteral("text"), boundedActivityText(text, textLimit));
        if (!entry.contains(QStringLiteral("time")))
            entry.insert(QStringLiteral("time"), QString{});
        if (kind == QStringLiteral("tool") && isEdaActivityName(entry.value(QStringLiteral("name")).toString())) {
            entry.insert(QStringLiteral("eda"), true);
            for (const QString &field : {QStringLiteral("argumentsRaw"), QStringLiteral("text"), QStringLiteral("result")}) {
                const QString raw = entry.value(field).toString();
                if (raw.trimmed().isEmpty())
                    continue;
                QJsonParseError error;
                const QJsonDocument document = QJsonDocument::fromJson(raw.toUtf8(), &error);
                if (error.error == QJsonParseError::NoError)
                    mergeEdaFields(entry, document.isObject() ? QJsonValue(document.object()) : QJsonValue(document.array()));
            }
            const QString edaState = entry.value(QStringLiteral("edaState")).toString().trimmed().toLower();
            if (edaState == QStringLiteral("queued") || edaState == QStringLiteral("running")
                || edaState == QStringLiteral("waiting_for_eda")) {
                entry.insert(QStringLiteral("status"), QStringLiteral("running"));
            }
        }
        if (kind == QStringLiteral("tool") && isShellActivityName(entry.value(QStringLiteral("name")).toString())) {
            entry.insert(QStringLiteral("shell"), true);
            const QString rawArguments = entry.value(QStringLiteral("argumentsRaw")).toString().trimmed().isEmpty()
                ? entry.value(QStringLiteral("text")).toString()
                : entry.value(QStringLiteral("argumentsRaw")).toString();
            const QVariantMap argumentFields = shellArgumentFields(rawArguments);
            for (auto it = argumentFields.cbegin(); it != argumentFields.cend(); ++it)
                entry.insert(it.key(), it.value());
            const QString rawResult = entry.value(QStringLiteral("result")).toString().trimmed();
            if (!rawResult.isEmpty()) {
                QJsonParseError error;
                const QJsonDocument document = QJsonDocument::fromJson(rawResult.toUtf8(), &error);
                if (error.error == QJsonParseError::NoError && document.isObject())
                    mergeShellResult(entry, document.object());
            }
            if (entry.value(QStringLiteral("shellState")).toString().trimmed().toLower() == QStringLiteral("running"))
                entry.insert(QStringLiteral("status"), QStringLiteral("running"));
        }
        loaded.append(entry);
    }
    if (prepend)
        loaded += m_activityEntries;
    m_activityEntries = loaded;
    emit activityEntriesChanged();
}

void AgentController::stop() {
    stopSession(m_flowDisplaySessionId);
}

void AgentController::stopSession(const QString &sessionId) {
    const QString requested = sessionId.trimmed();
    if (requested.isEmpty()) {
        for (AgentController *run : std::as_const(m_parallelRuns)) {
            if (run && run->m_liveSessionId.isEmpty()) {
                run->stop();
                return;
            }
        }
        if (!m_liveSessionId.isEmpty())
            return;
    }
    if (!requested.isEmpty() && requested != m_liveSessionId) {
        for (AgentController *run : std::as_const(m_parallelRuns)) {
            if (run && run->m_liveSessionId == requested) {
                run->stop();
                return;
            }
        }
        return;
    }
    if (!m_queuedPrompts.isEmpty()) {
        m_queuedPrompts.clear();
        for (qsizetype index = m_activityEntries.size() - 1; index >= 0; --index) {
            const QVariantMap entry = m_activityEntries.at(index).toMap();
            const QString status = entry.value(QStringLiteral("status")).toString();
            if (entry.contains(QStringLiteral("queueId"))
                && (status == QStringLiteral("queued") || status == QStringLiteral("steering")))
                m_activityEntries.removeAt(index);
        }
        emit activityEntriesChanged();
        notifyQueuedPromptsChanged();
    }
    if (m_paused && !m_running && !m_nativeGoal.isEmpty()) {
        m_paused = false;
        emit pausedChanged();
        setPhase(QStringLiteral("Stopped by operator"));
        setFailureReport(QStringLiteral("任务由操作员停止；没有产生可用于最终审核的成功结论。"));
        emit finished(false);
        return;
    }
    if (!m_running)
        return;
    if (m_paused)
        resume();
    if (m_demoTimer.isActive())
        m_demoTimer.stop();
    m_receivedResult = true;
    if (m_nativeTurnRunner && m_nativeTurnRunner->running())
        m_nativeTurnRunner->cancel();
#ifdef DFT_AGENT_STUDIO_TESTING
    if (m_process.state() != QProcess::NotRunning) {
        for (ResponsesRoundClient *client : std::as_const(m_nativeResponseRequests))
            client->cancel();
        m_process.terminate();
        // Give the active worker time to propagate cancellation to the EDA
        // process group and persist the interrupted turn before hard-killing.
        if (!m_process.waitForFinished(8'000)) {
            m_process.kill();
            m_process.waitForFinished(2'000);
        }
    }
#endif
    setPhase(QStringLiteral("Stopped by operator"));
    appendLog(QStringLiteral("Agent stopped by operator. No successful conclusion was produced."));
    setFailureReport(QStringLiteral("任务由操作员停止；没有产生可用于最终审核的成功结论。"));
    setRunning(false);
    emit finished(false);
}

void AgentController::clearLog() {
    if (!m_log.isEmpty()) {
        m_log.clear();
        emit logChanged();
    }
    if (!m_detailedLog.isEmpty()) {
        m_detailedLog.clear();
        emit detailedLogChanged();
    }
    if (!m_activityEntries.isEmpty()) {
        m_activityEntries.clear();
        emit activityEntriesChanged();
    }
    clearToolOutput();
}

void AgentController::clearToolOutput() {
    if (m_toolOutput.isEmpty())
        return;
    m_toolOutput.clear();
    emit toolOutputChanged();
}

bool AgentController::openProjectTerminal(const QString &workingDirectory) {
    const QFileInfo directory(workingDirectory);
    if (!directory.isDir())
        return false;

    struct TerminalCandidate {
        QString executable;
        QStringList arguments;
    };
    QList<TerminalCandidate> candidates;
    const QString configured = qEnvironmentVariable("DFT_AGENT_TERMINAL").trimmed();
    if (!configured.isEmpty()) {
        const QString executable = QFileInfo(configured).isAbsolute()
            ? configured : QStandardPaths::findExecutable(configured);
        if (!executable.isEmpty())
            candidates.append({executable, {}});
    }
    const QString konsole = QStandardPaths::findExecutable(QStringLiteral("konsole"));
    if (!konsole.isEmpty())
        candidates.append({konsole, {QStringLiteral("--workdir"), directory.absoluteFilePath()}});
    const QString gnomeConsole = QStandardPaths::findExecutable(QStringLiteral("kgx"));
    if (!gnomeConsole.isEmpty())
        candidates.append({gnomeConsole, {QStringLiteral("--working-directory"), directory.absoluteFilePath()}});
    const QString gnomeTerminal = QStandardPaths::findExecutable(QStringLiteral("gnome-terminal"));
    if (!gnomeTerminal.isEmpty())
        candidates.append({gnomeTerminal, {QStringLiteral("--working-directory=%1").arg(directory.absoluteFilePath())}});
    const QString xfceTerminal = QStandardPaths::findExecutable(QStringLiteral("xfce4-terminal"));
    if (!xfceTerminal.isEmpty())
        candidates.append({xfceTerminal, {QStringLiteral("--working-directory=%1").arg(directory.absoluteFilePath())}});
    const QString mateTerminal = QStandardPaths::findExecutable(QStringLiteral("mate-terminal"));
    if (!mateTerminal.isEmpty())
        candidates.append({mateTerminal, {QStringLiteral("--working-directory=%1").arg(directory.absoluteFilePath())}});
    const QString qterminal = QStandardPaths::findExecutable(QStringLiteral("qterminal"));
    if (!qterminal.isEmpty())
        candidates.append({qterminal, {QStringLiteral("--workdir"), directory.absoluteFilePath()}});
    for (const QString &name : {
             QStringLiteral("lxterminal"), QStringLiteral("kitty"), QStringLiteral("foot"),
             QStringLiteral("alacritty"), QStringLiteral("wezterm")}) {
        const QString executable = QStandardPaths::findExecutable(name);
        if (!executable.isEmpty())
            candidates.append({executable, {}});
    }
    const QString xterm = QStandardPaths::findExecutable(QStringLiteral("xterm"));
    if (!xterm.isEmpty())
        candidates.append({xterm, {}});

    for (const auto &candidate : std::as_const(candidates)) {
        if (QProcess::startDetached(candidate.executable, candidate.arguments, directory.absoluteFilePath()))
            return true;
    }
    return false;
}

void AgentController::runDebugCommand(const QString &workingDirectory, const QString &command) {
    if (command.trimmed().isEmpty() || terminalRunning())
        return;
    m_terminalProcess.setWorkingDirectory(workingDirectory);
    appendTerminalOutput(QStringLiteral("\n$ %1\n").arg(command));
    emit terminalRunningChanged();
    m_terminalProcess.start(QStringLiteral("/bin/sh"), {QStringLiteral("-lc"), command});
}

void AgentController::clearTerminal() {
    if (m_terminalOutput.isEmpty())
        return;
    m_terminalOutput.clear();
    emit terminalOutputChanged();
}

void AgentController::setRunning(bool value) {
    if (m_running == value)
        return;
    m_running = value;
    emit runningChanged();
    scheduleSessionProgressPersistence(true);
}
void AgentController::setProgress(int value) {
    value = qBound(0, value, 100);
    if (m_progress == value)
        return;
    m_progress = value;
    emit progressChanged();
    scheduleSessionProgressPersistence();
}
void AgentController::setPhase(const QString &value) {
    if (m_phase == value)
        return;
    m_phase = value;
    emit phaseChanged();
    scheduleSessionProgressPersistence();
}
void AgentController::scheduleSessionProgressPersistence(bool immediate) {
    if (m_suppressProgressPersistence || m_liveSessionId.isEmpty() || m_nativeProjectSnapshot.isEmpty())
        return;
    if (!m_running && !immediate)
        return;
    if (immediate) {
        m_sessionProgressPersistenceTimer.stop();
        persistSessionProgress(true);
    } else if (!m_sessionProgressPersistenceTimer.isActive()) {
        m_sessionProgressPersistenceTimer.start();
    }
}

void AgentController::persistSessionProgress(bool final) {
    if (m_liveSessionId.isEmpty() || m_nativeProjectSnapshot.isEmpty())
        return;
    QString executionStatus;
    if (m_running)
        executionStatus = m_paused ? QStringLiteral("paused") : QStringLiteral("running");
    else if (m_hasError || m_executionState == QStringLiteral("failed"))
        executionStatus = QStringLiteral("failed");
    else if (m_phase.contains(QStringLiteral("Stopped"), Qt::CaseInsensitive)
             || m_phase.contains(QStringLiteral("停止"))
             || m_phase.contains(QStringLiteral("已停止")))
        executionStatus = QStringLiteral("stopped");
    else if (m_executionState == QStringLiteral("incomplete")
             || m_executionState == QStringLiteral("execution_blocked"))
        executionStatus = QStringLiteral("incomplete");
    else
        executionStatus = QStringLiteral("completed");
    const QVariantMap response = workspaceAction(m_nativeProjectSnapshot,
        QStringLiteral("session_progress_update"), {
            {QStringLiteral("thread_id"), m_liveSessionId},
            {QStringLiteral("progress"), m_progress},
            {QStringLiteral("phase"), m_phase},
            {QStringLiteral("flow_stage_states"), m_flowStageStates},
            {QStringLiteral("execution_status"), executionStatus},
            {QStringLiteral("execution_state"), m_executionState},
            {QStringLiteral("turn_id"), m_liveTurnId},
            {QStringLiteral("final"), final},
        });
    if (!response.value(QStringLiteral("ok")).toBool())
        appendDetailedLog(QStringLiteral("Failed to persist session flow state: %1")
                              .arg(response.value(QStringLiteral("message")).toString()));
}
void AgentController::setWorkspace(const QString &value) { if (m_workspace != value) { m_workspace = value; emit workspaceChanged(); emit reportFilesChanged(); } }
void AgentController::appendLog(const QString &line) { m_log += (m_log.isEmpty() ? QString{} : QStringLiteral("\n")) + line; emit logChanged(); }
void AgentController::appendDetailedLog(const QString &line) { m_detailedLog += (m_detailedLog.isEmpty() ? QString{} : QStringLiteral("\n\n")) + line; emit detailedLogChanged(); }
void AgentController::appendToolOutput(const QString &text) {
    if (text.isEmpty())
        return;
    m_toolOutput += text;
    constexpr qsizetype maximumCharacters = 262'144;
    if (m_toolOutput.size() > maximumCharacters) {
        const qsizetype excess = m_toolOutput.size() - maximumCharacters;
        const qsizetype newline = m_toolOutput.indexOf(QLatin1Char('\n'), excess);
        m_toolOutput.remove(0, newline >= 0 ? newline + 1 : excess);
        const QString marker = QStringLiteral("… 较早的工具输出已省略 …\n");
        m_toolOutput.prepend(marker);
        if (m_toolOutput.size() > maximumCharacters)
            m_toolOutput.remove(marker.size(), m_toolOutput.size() - maximumCharacters);
    }
    if (!m_toolOutputNotifyPending) {
        m_toolOutputNotifyPending = true;
        QTimer::singleShot(60, this, [this] {
            m_toolOutputNotifyPending = false;
            emit toolOutputChanged();
        });
    }
}
void AgentController::setResult(const QString &value) { if (m_result != value) { m_result = value; emit resultChanged(); } }
void AgentController::setReport(const QString &value) { if (m_report != value) { m_report = value; emit reportChanged(); emit reportFilesChanged(); } }
void AgentController::setHasError(bool value) { if (m_hasError != value) { m_hasError = value; emit hasErrorChanged(); } }
void AgentController::setRequiresSupervisorReview(bool value) { if (m_requiresSupervisorReview != value) { m_requiresSupervisorReview = value; emit requiresSupervisorReviewChanged(); } }
void AgentController::appendTerminalOutput(const QString &value) { m_terminalOutput += value; emit terminalOutputChanged(); }

void AgentController::appendActivity(const QVariantMap &value) {
    QVariantMap entry = value;
    if (entry.value(QStringLiteral("kind")).toString() == QStringLiteral("user")
        || entry.value(QStringLiteral("kind")).toString() == QStringLiteral("agent"))
        entry.insert(QStringLiteral("text"), canonicalChatText(entry.value(QStringLiteral("text")).toString()));
    entry.insert(QStringLiteral("time"), QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss")));
    m_activityEntries.append(entry);
    while (m_activityEntries.size() > 400)
        m_activityEntries.removeFirst();
    emit activityEntriesChanged();
}

void AgentController::appendAgentActivity(
    const QString &text,
    int step,
    const QString &role,
    bool mergeDelta,
    const QString &streamId
) {
    if (!shouldAppendLiveActivity())
        return;
    if (text.isEmpty())
        return;
    if (role == QStringLiteral("final")) {
        for (const auto &value : std::as_const(m_activityEntries)) {
            const QVariantMap entry = value.toMap();
            if (entry.value(QStringLiteral("kind")).toString() == QStringLiteral("agent")
                && entry.value(QStringLiteral("text")).toString() == text)
                return;
        }
    }
    if (!m_activityEntries.isEmpty()) {
        QVariantMap latest = m_activityEntries.constLast().toMap();
        const bool sameStep = latest.value(QStringLiteral("step")).toInt() == step;
        const bool sameStream = streamId.isEmpty()
            ? latest.value(QStringLiteral("streamId")).toString().isEmpty()
            : latest.value(QStringLiteral("streamId")).toString() == streamId;
        if (latest.value(QStringLiteral("kind")).toString() == QStringLiteral("agent") && sameStep && sameStream) {
            if (mergeDelta
                && latest.value(QStringLiteral("draft")).toBool()
                && latest.value(QStringLiteral("role")).toString() == role) {
                const int limit = role == QStringLiteral("thinking") ? 64'000 : 16'000;
                latest.insert(QStringLiteral("text"), boundedActivityText(
                    latest.value(QStringLiteral("text")).toString() + text, limit));
                m_activityEntries.last() = latest;
                emit activityEntriesChanged();
                return;
            }
            if (!mergeDelta && latest.value(QStringLiteral("draft")).toBool()) {
                latest.insert(QStringLiteral("text"), text);
                latest.insert(QStringLiteral("role"), role);
                latest.insert(QStringLiteral("draft"), false);
                m_activityEntries.last() = latest;
                emit activityEntriesChanged();
                return;
            }
            if (latest.value(QStringLiteral("text")).toString() == text) {
                latest.insert(QStringLiteral("role"), role);
                latest.insert(QStringLiteral("draft"), false);
                m_activityEntries.last() = latest;
                emit activityEntriesChanged();
                return;
            }
        }
    }
    appendActivity({
        {QStringLiteral("kind"), QStringLiteral("agent")},
        {QStringLiteral("text"), text},
        {QStringLiteral("step"), step},
        {QStringLiteral("role"), role},
        {QStringLiteral("draft"), mergeDelta},
        {QStringLiteral("streamId"), streamId},
    });
}

void AgentController::appendToolActivity(const QString &name, const QString &arguments, int step) {
    if (!shouldAppendLiveActivity())
        return;
    QVariantMap entry{
        {QStringLiteral("kind"), QStringLiteral("tool")},
        {QStringLiteral("name"), name},
        {QStringLiteral("text"), arguments},
        {QStringLiteral("argumentsRaw"), arguments},
        {QStringLiteral("result"), QString{}},
        {QStringLiteral("step"), step},
        {QStringLiteral("status"), QStringLiteral("running")},
    };
    if (isShellActivityName(name)) {
        entry.insert(QStringLiteral("shell"), true);
        entry.insert(QStringLiteral("startedAtMs"), QDateTime::currentMSecsSinceEpoch());
        const QVariantMap fields = shellArgumentFields(arguments);
        for (auto it = fields.cbegin(); it != fields.cend(); ++it)
            entry.insert(it.key(), it.value());
    }
    if (isEdaActivityName(name)) {
        entry.insert(QStringLiteral("eda"), true);
        QJsonParseError error;
        const QJsonDocument document = QJsonDocument::fromJson(arguments.toUtf8(), &error);
        if (error.error == QJsonParseError::NoError && document.isObject())
            mergeEdaFields(entry, document.object());
    }
    appendActivity(entry);
}

void AgentController::copyText(const QString &text) {
    if (QGuiApplication::clipboard() != nullptr)
        QGuiApplication::clipboard()->setText(text);
}

void AgentController::completeToolActivity(const QString &name, const QString &result, int step, bool failed) {
    if (!shouldAppendLiveActivity())
        return;
    for (int index = m_activityEntries.size() - 1; index >= 0; --index) {
        QVariantMap entry = m_activityEntries.at(index).toMap();
        if (entry.value(QStringLiteral("kind")).toString() != QStringLiteral("tool")
            || entry.value(QStringLiteral("name")).toString() != name
            || entry.value(QStringLiteral("step")).toInt() != step
            || entry.value(QStringLiteral("status")).toString() != QStringLiteral("running"))
            continue;
        entry.insert(QStringLiteral("result"), result);
        entry.insert(QStringLiteral("status"), failed ? QStringLiteral("failed") : QStringLiteral("completed"));
        if (isShellActivityName(name)) {
            entry.insert(QStringLiteral("shell"), true);
            QJsonParseError error;
            const QJsonDocument document = QJsonDocument::fromJson(result.toUtf8(), &error);
            if (error.error == QJsonParseError::NoError && document.isObject()) {
                mergeShellResult(entry, document.object());
                if (!failed && document.object().value(QStringLiteral("state")).toString() == QStringLiteral("running"))
                    entry.insert(QStringLiteral("status"), QStringLiteral("running"));
            }
            if (!entry.contains(QStringLiteral("durationSeconds"))) {
                const qint64 startedAt = entry.value(QStringLiteral("startedAtMs")).toLongLong();
                if (startedAt > 0)
                    entry.insert(QStringLiteral("durationSeconds"), qMax<qint64>(0, (QDateTime::currentMSecsSinceEpoch() - startedAt) / 1000));
            }
        }
        if (isEdaActivityName(name)) {
            entry.insert(QStringLiteral("eda"), true);
            QJsonParseError error;
            const QJsonDocument document = QJsonDocument::fromJson(result.toUtf8(), &error);
            if (error.error == QJsonParseError::NoError && document.isObject()) {
                mergeEdaFields(entry, document.object());
                const QString state = entry.value(QStringLiteral("edaState")).toString().trimmed().toLower();
                if (!failed && (state == QStringLiteral("queued")
                                || state == QStringLiteral("running")
                                || state == QStringLiteral("waiting_for_eda")))
                    entry.insert(QStringLiteral("status"), QStringLiteral("running"));
            }
        }
        m_activityEntries[index] = entry;
        emit activityEntriesChanged();
        return;
    }
    QVariantMap entry{
        {QStringLiteral("kind"), QStringLiteral("tool")},
        {QStringLiteral("name"), name},
        {QStringLiteral("text"), QString{}},
        {QStringLiteral("result"), result},
        {QStringLiteral("step"), step},
        {QStringLiteral("status"), failed ? QStringLiteral("failed") : QStringLiteral("completed")},
    };
    if (isShellActivityName(name)) {
        entry.insert(QStringLiteral("shell"), true);
        QJsonParseError error;
        const QJsonDocument document = QJsonDocument::fromJson(result.toUtf8(), &error);
        if (error.error == QJsonParseError::NoError && document.isObject())
            mergeShellResult(entry, document.object());
    }
    if (isEdaActivityName(name)) {
        entry.insert(QStringLiteral("eda"), true);
        QJsonParseError error;
        const QJsonDocument document = QJsonDocument::fromJson(result.toUtf8(), &error);
        if (error.error == QJsonParseError::NoError && document.isObject())
            mergeEdaFields(entry, document.object());
    }
    appendActivity(entry);
}

void AgentController::updateEdaJobActivity(const QJsonObject &event) {
    if (!shouldAppendLiveActivity())
        return;
    const QString jobId = event.value(QStringLiteral("job_id")).toString().trimmed();
    const QString eventType = event.value(QStringLiteral("event")).toString();
    const QString state = event.value(QStringLiteral("state")).toString().trimmed();
    for (int index = m_activityEntries.size() - 1; index >= 0; --index) {
        QVariantMap entry = m_activityEntries.at(index).toMap();
        if (!entry.value(QStringLiteral("eda")).toBool())
            continue;
        const QString entryJobId = entry.value(QStringLiteral("jobId")).toString();
        if (!jobId.isEmpty() && !entryJobId.isEmpty() && entryJobId != jobId)
            continue;
        if (!jobId.isEmpty())
            entry.insert(QStringLiteral("jobId"), jobId);
        if (!state.isEmpty())
            entry.insert(QStringLiteral("edaState"), state);
        if (eventType == QStringLiteral("eda_job_started")
            || eventType == QStringLiteral("agent_sleeping")) {
            entry.insert(QStringLiteral("status"), QStringLiteral("running"));
        } else if (eventType == QStringLiteral("eda_job_finished")) {
            entry.insert(QStringLiteral("status"), state == QStringLiteral("completed")
                ? QStringLiteral("completed") : QStringLiteral("failed"));
            const QString error = event.value(QStringLiteral("error")).toString();
            if (!error.isEmpty())
                entry.insert(QStringLiteral("edaError"), error);
        }
        m_activityEntries[index] = entry;
        emit activityEntriesChanged();
        return;
    }
}

void AgentController::appendEdaOutput(const QJsonObject &event) {
    if (!shouldAppendLiveActivity())
        return;
    const QString text = event.value(QStringLiteral("text")).toString();
    if (text.isEmpty())
        return;
    const QString log = event.value(QStringLiteral("log")).toString();
    const QString workspace = event.value(QStringLiteral("workspace")).toString();
    for (int index = m_activityEntries.size() - 1; index >= 0; --index) {
        QVariantMap entry = m_activityEntries.at(index).toMap();
        if (!entry.value(QStringLiteral("eda")).toBool())
            continue;
        const QString entryLog = entry.value(QStringLiteral("edaLog")).toString();
        const QString entryWorkspace = entry.value(QStringLiteral("edaWorkspace")).toString();
        const bool matchesLocation = (!log.isEmpty() && !entryLog.isEmpty() && log == entryLog)
            || (!workspace.isEmpty() && !entryWorkspace.isEmpty() && workspace == entryWorkspace);
        const bool isRunning = entry.value(QStringLiteral("status")).toString() == QStringLiteral("running");
        if (!matchesLocation && !isRunning)
            continue;
        if (!log.isEmpty()) entry.insert(QStringLiteral("edaLog"), log);
        if (!workspace.isEmpty()) entry.insert(QStringLiteral("edaWorkspace"), workspace);
        const QString current = entry.value(QStringLiteral("edaOutput")).toString();
        entry.insert(QStringLiteral("edaOutput"), boundedActivityText(current + text, 48'000));
        entry.insert(QStringLiteral("edaLive"), true);
        m_activityEntries[index] = entry;
        emit activityEntriesChanged();
        return;
    }
}

QVariantMap AgentController::flowStageRecord(const QString &key) const {
    return m_flowStageStates.value(key).toMap();
}

void AgentController::loadFlowProgressSnapshot(int progress, const QString &phase, const QVariantMap &stages) {
    m_suppressProgressPersistence = true;
    m_flowStageStates = stages;
    m_activeFlowStage.clear();
    for (auto it = m_flowStageStates.cbegin(); it != m_flowStageStates.cend(); ++it) {
        if (it.value().toMap().value(QStringLiteral("state")).toString() == QStringLiteral("running")) {
            m_activeFlowStage = it.key();
            break;
        }
    }
    emit flowStageStatesChanged();
    setProgress(qBound(0, progress, 100));
    setPhase(phase.isEmpty() ? QStringLiteral("Ready") : phase);
    m_suppressProgressPersistence = false;
}

void AgentController::setFlowDisplaySession(const QString &sessionId) {
    m_flowDisplaySessionId = sessionId.trimmed();
}

bool AgentController::sessionRunning(const QString &sessionId) const {
    const QString requested = sessionId.trimmed();
    if (requested.isEmpty() && m_running && m_liveSessionId.isEmpty())
        return true;
    if (!requested.isEmpty() && requested == m_liveSessionId && m_running)
        return true;
    for (const AgentController *run : m_parallelRuns) {
        if (run && run->m_running
            && (!requested.isEmpty() ? run->m_liveSessionId == requested : run->m_liveSessionId.isEmpty()))
            return true;
    }
    return false;
}

bool AgentController::sessionPaused(const QString &sessionId) const {
    const QString requested = sessionId.trimmed();
    if (requested == m_liveSessionId && m_paused)
        return true;
    for (const AgentController *run : m_parallelRuns) {
        if (run && run->m_liveSessionId == requested && run->m_paused)
            return true;
    }
    return false;
}

void AgentController::resetFlowStages() {
    if (m_flowStageStates.isEmpty() && m_activeFlowStage.isEmpty())
        return;
    m_flowStageStates.clear();
    m_activeFlowStage.clear();
    emit flowStageStatesChanged();
}

bool AgentController::updateFlowStage(const QJsonObject &event) {
    QString stage = event.value(QStringLiteral("stage")).toString();
    QString state = event.value(QStringLiteral("stage_state")).toString();
    const QString phase = event.value(QStringLiteral("phase")).toString();
    int stagePercent = event.value(QStringLiteral("stage_percent")).toInt(-1);
    if (stage.isEmpty()) {
        if (phase == QStringLiteral("agent_started") || phase == QStringLiteral("source_discovery")) {
            stage = QStringLiteral("readProject");
            state = QStringLiteral("running");
        } else if (phase == QStringLiteral("source_discovery_completed")) {
            stage = QStringLiteral("readProject");
            state = QStringLiteral("completed");
        } else if (phase == QStringLiteral("model_tool_selection") || phase == QStringLiteral("coverage_iteration")) {
            stage = QStringLiteral("executor");
            state = QStringLiteral("running");
        } else if (phase == QStringLiteral("staged")) {
            stage = QStringLiteral("executor");
            state = QStringLiteral("completed");
        } else if (phase == QStringLiteral("dc_shell")) {
            stage = QStringLiteral("synthesis");
            state = QStringLiteral("running");
        } else if (phase == QStringLiteral("execution_finished")) {
            stage = QStringLiteral("synthesis");
            state = QStringLiteral("completed");
        } else if (phase == QStringLiteral("testmax")) {
            stage = QStringLiteral("atpg");
            state = QStringLiteral("running");
        } else if (phase == QStringLiteral("mbist_simulation")) {
            stage = QStringLiteral("mbist");
            state = QStringLiteral("running");
        } else if (phase == QStringLiteral("evidence_written") || phase == QStringLiteral("completed")) {
            stage = QStringLiteral("report");
            state = QStringLiteral("completed");
        }
    }
    if (stage.isEmpty())
        return true;
    const bool agentActivity = event.value(QStringLiteral("agent_activity")).toBool(false);
    if (state == QStringLiteral("completed") || state == QStringLiteral("complete"))
        state = QStringLiteral("verified");
    if (state.isEmpty())
        state = QStringLiteral("running");
    if (stagePercent < 0)
        stagePercent = state == QStringLiteral("verified") ? 100 : state == QStringLiteral("running") ? 10 : 0;
    stagePercent = qBound(0, stagePercent, 100);
    const int incomingRank = flowStageRank(stage);
    // Progress callbacks from dc_shell/ATPG can arrive after a buffered
    // report marker.  Never let a later stage become visible while an earlier
    // stage is still running, and never move back to an earlier stage after a
    // later stage has already been verified.
    if (incomingRank >= 0) {
        for (auto it = m_flowStageStates.cbegin(); it != m_flowStageStates.cend(); ++it) {
            const int existingRank = flowStageRank(it.key());
            if (existingRank < 0 || existingRank == incomingRank)
                continue;
            const QString existing = it.value().toMap().value(QStringLiteral("state")).toString();
            if (existingRank < incomingRank && !agentActivity
                && (existing == QStringLiteral("running") || existing == QStringLiteral("waiting")))
                return false;
            if (existingRank > incomingRank && existing == QStringLiteral("verified") && !agentActivity)
                return false;
        }
    }
    QVariantMap record = m_flowStageStates.value(stage).toMap();
    const QString existingState = record.value(QStringLiteral("state")).toString();
    const bool stageWouldMoveBack = state == QStringLiteral("running")
        && !m_activeFlowStage.isEmpty()
        && flowStageRank(stage) >= 0
        && flowStageRank(m_activeFlowStage) > flowStageRank(stage);
    const bool stateWouldMoveBack = existingState == QStringLiteral("verified")
        && state == QStringLiteral("running");
    if ((stageWouldMoveBack || stateWouldMoveBack) && !agentActivity)
        return false;
    record.insert(QStringLiteral("state"), state);
    record.insert(QStringLiteral("progress"), stagePercent);
    record.insert(QStringLiteral("phase"), phase);
    record.insert(QStringLiteral("substep"), event.value(QStringLiteral("substep")).toString());
    m_flowStageStates.insert(stage, record);
    if (state == QStringLiteral("running")) {
        if (m_activeFlowStage.isEmpty()
            || flowStageRank(stage) >= flowStageRank(m_activeFlowStage))
            m_activeFlowStage = stage;
    }
    else if (m_activeFlowStage == stage)
        m_activeFlowStage.clear();
    emit flowStageStatesChanged();
    scheduleSessionProgressPersistence();
    return true;
}

void AgentController::updateAgentActivityStage(const QJsonObject &event) {
    const QString type = event.value(QStringLiteral("event")).toString();
    if (type != QStringLiteral("tool_call") && type != QStringLiteral("tool_result")
        && type != QStringLiteral("tool_approval_requested"))
        return;
    const QString name = event.value(QStringLiteral("name")).toString().trimmed().toLower();
    if (name.isEmpty())
        return;

    QString stage = QStringLiteral("executor");
    if (name == QStringLiteral("inspect_project")
        || name == QStringLiteral("check_dft_readiness")
        || name == QStringLiteral("inspect_studio_project")
        || name == QStringLiteral("inspect_configured_project_source_tree")) {
        stage = QStringLiteral("readProject");
    } else if (name == QStringLiteral("run_dft_flow")
               || name == QStringLiteral("run_dft_iteration")
               || name == QStringLiteral("run_dft_optimization")
               || name == QStringLiteral("run_approved_patch")) {
        if (!m_activeFlowStage.isEmpty()
            && flowStageRank(m_activeFlowStage) >= flowStageRank(QStringLiteral("synthesis")))
            stage = m_activeFlowStage;
        else {
            int highestRank = -1;
            for (auto it = m_flowStageStates.cbegin(); it != m_flowStageStates.cend(); ++it) {
                const QString state = it.value().toMap().value(QStringLiteral("state")).toString();
                const int rank = flowStageRank(it.key());
                if (rank >= flowStageRank(QStringLiteral("synthesis"))
                    && rank > highestRank
                    && (state == QStringLiteral("failed") || state == QStringLiteral("blocked")
                        || state == QStringLiteral("needs_review") || state == QStringLiteral("waiting"))) {
                    stage = it.key();
                    highestRank = rank;
                }
            }
        }
    }

    const QVariantMap current = m_flowStageStates.value(stage).toMap();
    int stagePercent = current.value(QStringLiteral("progress")).toInt(0);
    if (stagePercent <= 0)
        stagePercent = 1;

    // A tool can report a review gate while the Agent is still allowed to
    // inspect evidence or select a bounded retry. Preserve that intermediate
    // state in the flow monitor; the next tool call will transition the same
    // stage back to running through the agent_activity path above.
    if (type == QStringLiteral("tool_result")) {
        QJsonValue result = event.value(QStringLiteral("result"));
        if (result.isString()) {
            QJsonParseError parseError;
            const QJsonDocument parsed = QJsonDocument::fromJson(result.toString().toUtf8(), &parseError);
            if (parseError.error == QJsonParseError::NoError && parsed.isObject())
                result = parsed.object();
        }
        const QJsonValue validation = findNestedValue(result, {QStringLiteral("cross_validation")});
        QString reviewState;
        if (validation.isObject()) {
            const QString status = validation.toObject().value(QStringLiteral("status")).toString().trimmed().toLower();
            if (status == QStringLiteral("needs_review"))
                reviewState = QStringLiteral("needs_review");
            else if (status == QStringLiteral("blocked") || status == QStringLiteral("failed")
                     || status == QStringLiteral("not_verified"))
                reviewState = QStringLiteral("blocked");
        }
        const QJsonValue warning = findNestedValue(result, {QStringLiteral("supervisor_warning")});
        if (reviewState.isEmpty() && warning.isObject() && !warning.toObject().isEmpty())
            reviewState = QStringLiteral("needs_review");
        if (!reviewState.isEmpty()) {
            updateFlowStage(QJsonObject{
                {QStringLiteral("phase"), QStringLiteral("agent_review_required")},
                {QStringLiteral("stage"), stage},
                {QStringLiteral("stage_state"), reviewState},
                {QStringLiteral("stage_percent"), stagePercent},
                {QStringLiteral("substep"), QStringLiteral("agent:review_required")},
                {QStringLiteral("agent_activity"), true},
            });
            return;
        }
    }
    const QString currentState = current.value(QStringLiteral("state")).toString();
    if ((currentState == QStringLiteral("failed") || currentState == QStringLiteral("blocked")
         || currentState == QStringLiteral("needs_review")) && stagePercent >= 100)
        stagePercent = 85;
    QJsonObject activity{
        {QStringLiteral("phase"), type == QStringLiteral("tool_result")
            ? QStringLiteral("agent_tool_result") : QStringLiteral("agent_tool_call")},
        {QStringLiteral("stage"), stage},
        {QStringLiteral("stage_state"), QStringLiteral("running")},
        {QStringLiteral("stage_percent"), stagePercent},
        {QStringLiteral("substep"), QStringLiteral("agent:%1").arg(name)},
        {QStringLiteral("agent_activity"), true},
    };
    updateFlowStage(activity);
}

void AgentController::markActiveFlowStageFailed() {
    if (m_activeFlowStage.isEmpty())
        return;
    QVariantMap record = m_flowStageStates.value(m_activeFlowStage).toMap();
    record.insert(QStringLiteral("state"), QStringLiteral("failed"));
    m_flowStageStates.insert(m_activeFlowStage, record);
    m_activeFlowStage.clear();
    emit flowStageStatesChanged();
    scheduleSessionProgressPersistence();
}

void AgentController::resetContextUsage() {
    const int inputLimit = qMin(m_defaultContextInputLimit, m_defaultContextWindow - 1);
    const bool changed = m_contextWindow != m_defaultContextWindow
        || m_contextEffectiveWindow != m_defaultContextWindow * 95 / 100
        || m_contextAutoCompactLimit != m_defaultContextWindow * 9 / 10
        || m_contextInputTokens != 0
        || m_contextInputLimit != inputLimit
        || m_contextSystemTokens != 0
        || m_contextHistoryTokens != 0
        || m_contextToolTokens != 0
        || m_contextToolLimit != 8'192
        || m_contextOutputTokens != 0
        || m_contextOutputLimit != m_defaultContextOutputLimit
        || m_contextCompacted
        || m_contextProviderMeasured;
    m_contextWindow = m_defaultContextWindow;
    m_contextEffectiveWindow = m_defaultContextWindow * 95 / 100;
    m_contextAutoCompactLimit = m_defaultContextWindow * 9 / 10;
    m_contextInputTokens = 0;
    m_contextInputLimit = inputLimit;
    m_contextSystemTokens = 0;
    m_contextHistoryTokens = 0;
    m_contextToolTokens = 0;
    m_contextToolLimit = 8'192;
    m_contextOutputTokens = 0;
    m_contextOutputLimit = m_defaultContextOutputLimit;
    m_contextCompacted = false;
    m_contextProviderMeasured = false;
    m_contextPlanInitialized = false;
    if (changed)
        emit contextUsageChanged();
}

void AgentController::setContextUsage(const QJsonObject &event) {
    const int contextWindow = qMax(2'048, event.value("context_window").toInt(m_contextWindow));
    const QString source = event.value(QStringLiteral("source")).toString();
    const bool providerUsage = source == QStringLiteral("provider_usage");
    const int effectiveWindow = qBound(1'024,
        event.value("effective_context_window").toInt(contextWindow * event.value("effective_context_window_percent").toInt(95) / 100),
        contextWindow);
    const int autoCompactLimit = qBound(1'024,
        event.value("auto_compact_token_limit").toInt(contextWindow * 9 / 10),
        effectiveWindow);
    const bool initializesPlan = source == QStringLiteral("agent_configuration");
    const bool refinesPlan = source == QStringLiteral("agent_configuration_measurement");
    const bool updatesPlan = initializesPlan || refinesPlan || providerUsage;
    const int requestedInputLimit = qMax(0, event.value("working_input_limit").toInt(m_defaultContextInputLimit));
    const int inputLimit = updatesPlan
        ? qMin(requestedInputLimit, contextWindow - 1)
        : providerUsage ? qMin(requestedInputLimit, contextWindow)
                        : qMin(m_defaultContextInputLimit, contextWindow - 1);
    const int reportedSystemTokens = qMax(
        0,
        event.value("system_prompt_tokens").toInt(m_contextSystemTokens)
    );
    const int reportedConversationTokens = event.contains("conversation_tokens")
        ? qMax(0, event.value("conversation_tokens").toInt())
        : m_contextHistoryTokens;
    const int reportedToolTokens = qMax(
        0,
        event.value("tool_schema_tokens").toInt(m_contextToolTokens)
    );
    const int reportedHistoryTokens = event.contains("history_tokens")
        ? qMax(0, event.value("history_tokens").toInt())
        : reportedConversationTokens;
    const int reportedToolLimit = qMax(
        0,
        event.value("tool_schema_token_limit").toInt(m_contextToolLimit)
    );
    const int generatedTokens = event.contains("output_tokens")
        ? qMax(0, event.value("output_tokens").toInt())
        : qMax(0, event.value("generated_tokens").toInt());
    const int reportedCurrentSessionTokens = event.contains("current_session_tokens")
        ? qMax(0, event.value("current_session_tokens").toInt()) + (providerUsage ? 0 : generatedTokens)
        : generatedTokens;
    const bool compacted = m_contextCompacted || event.value("compacted").toBool(false);
    const bool planStateChanges = updatesPlan && !m_contextPlanInitialized;
    const int systemTokens = updatesPlan || !m_contextPlanInitialized
        ? reportedSystemTokens
        : m_contextSystemTokens;
    const int toolTokens = updatesPlan || !m_contextPlanInitialized
        ? reportedToolTokens
        : m_contextToolTokens;
    const int toolLimit = updatesPlan || !m_contextPlanInitialized
        ? reportedToolLimit
        : m_contextToolLimit;
    int historyTokens = updatesPlan || !m_contextPlanInitialized
        ? reportedHistoryTokens
        : m_contextHistoryTokens;
    if (m_contextPlanInitialized && compacted && event.contains("history_tokens"))
        historyTokens = reportedHistoryTokens;
    int currentSessionTokens = m_contextOutputTokens;
    if (initializesPlan)
        currentSessionTokens = reportedCurrentSessionTokens;
    else if (refinesPlan)
        currentSessionTokens = qMax(currentSessionTokens, reportedCurrentSessionTokens);
    else if (event.contains("current_session_tokens"))
        currentSessionTokens = qMax(currentSessionTokens, reportedCurrentSessionTokens);
    else if (!m_contextPlanInitialized)
        currentSessionTokens = qMax(0, reportedConversationTokens - historyTokens) + generatedTokens;
    const int outputLimit = event.contains("output_token_limit")
        ? qMax(0, event.value("output_token_limit").toInt())
        : qMax(0, effectiveWindow - systemTokens - toolTokens - inputLimit);
    if (providerUsage && event.contains("measured_input_tokens"))
        currentSessionTokens = qMax(0, event.value("measured_input_tokens").toInt() - systemTokens - toolTokens - historyTokens);
    currentSessionTokens = qMax(0, currentSessionTokens);
    const int inputTokens = providerUsage && event.contains("measured_input_tokens")
        ? qMax(0, event.value("measured_input_tokens").toInt())
        : qMin(effectiveWindow, systemTokens + toolTokens + historyTokens + currentSessionTokens);
    if (m_contextWindow == contextWindow
        && m_contextEffectiveWindow == effectiveWindow
        && m_contextAutoCompactLimit == autoCompactLimit
        && m_contextInputTokens == inputTokens
        && m_contextInputLimit == inputLimit
        && m_contextSystemTokens == systemTokens
        && m_contextHistoryTokens == historyTokens
        && m_contextToolTokens == toolTokens
        && m_contextToolLimit == toolLimit
        && m_contextOutputTokens == currentSessionTokens
        && m_contextOutputLimit == outputLimit
        && m_contextCompacted == compacted
        && m_contextProviderMeasured == providerUsage
        && !planStateChanges)
        return;
    m_contextWindow = contextWindow;
    m_contextEffectiveWindow = effectiveWindow;
    m_contextAutoCompactLimit = autoCompactLimit;
    m_contextInputTokens = inputTokens;
    m_contextInputLimit = inputLimit;
    m_contextSystemTokens = systemTokens;
    m_contextHistoryTokens = historyTokens;
    m_contextToolTokens = toolTokens;
    m_contextToolLimit = toolLimit;
    m_contextOutputTokens = currentSessionTokens;
    m_contextOutputLimit = outputLimit;
    m_contextCompacted = compacted;
    m_contextProviderMeasured = providerUsage;
    if (updatesPlan)
        m_contextPlanInitialized = true;
    emit contextUsageChanged();
}

#ifdef DFT_AGENT_STUDIO_TESTING
void AgentController::processOutput() {
    m_buffer += m_process.readAllStandardOutput();
    while (true) {
        const int newline = m_buffer.indexOf('\n');
        if (newline < 0)
            break;
        consumeLine(m_buffer.left(newline));
        m_buffer.remove(0, newline + 1);
    }
}

bool AgentController::recoverStagedProjectEvidence() {
    const QFileInfo workspaceInfo(m_workspace);
    const QString workspace = workspaceInfo.canonicalFilePath();
    const QVariantMap metadata = m_nativeProjectSnapshot.value(QStringLiteral("metadata")).toMap();
    QVariantMap executionConfig = metadata.value(QStringLiteral("dft_execution")).toMap();
    if (executionConfig.isEmpty())
        executionConfig = m_nativeProjectSnapshot.value(QStringLiteral("dft_execution")).toMap();
    QString workspaceRoot = executionConfig.value(QStringLiteral("workspace_path")).toString().trimmed();
    if (workspaceRoot.isEmpty())
        workspaceRoot = executionConfig.value(QStringLiteral("workspace_root")).toString().trimmed();
    if (workspaceRoot.isEmpty())
        workspaceRoot = QStringLiteral("/media/6/Projects/DFT_agent_project_workspaces");
    const QString canonicalRoot = QFileInfo(workspaceRoot).canonicalFilePath();
    if (workspaceInfo.isSymLink() || workspace.isEmpty() || canonicalRoot.isEmpty()
        || !workspace.startsWith(canonicalRoot + QDir::separator())) {
        setFailureReport(QStringLiteral("恢复工作区必须位于项目配置的 DFT workspace 根目录中。"));
        return finishRecovery(false, {});
    }

    const QString evidenceFile = QDir(workspace).filePath(QStringLiteral("skill_result.json"));
    const QFileInfo evidenceInfo(evidenceFile);
    QFile evidence(evidenceFile);
    if (evidenceInfo.isSymLink() || !evidenceInfo.isFile() || evidenceInfo.size() > 32 * 1024 * 1024
        || !evidence.open(QIODevice::ReadOnly)) {
        setFailureReport(QStringLiteral("执行证据不可用或超过安全读取上限：%1").arg(evidenceFile));
        return finishRecovery(false, {});
    }
    QJsonParseError parseError{};
    const QJsonDocument evidenceDocument = QJsonDocument::fromJson(evidence.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !evidenceDocument.isObject()) {
        setFailureReport(QStringLiteral("执行证据不是有效 JSON：%1").arg(parseError.errorString()));
        return finishRecovery(false, {});
    }
    const QJsonObject payload = evidenceDocument.object();
    if (payload.value(QStringLiteral("skill")).toString()
        != QLatin1String("configured_rtl_dft_insert_dft")) {
        setFailureReport(QStringLiteral("恢复证据不是配置化 RTL DFT 执行结果。"));
        return finishRecovery(false, {});
    }

    const QJsonObject toolCall{
        {QStringLiteral("event"), QStringLiteral("tool_call")},
        {QStringLiteral("step"), 1},
        {QStringLiteral("name"), QStringLiteral("recover_project_gui_evidence")},
        {QStringLiteral("arguments"), QJsonObject{{QStringLiteral("workspace"), workspace}}}
    };
    appendTrace(toolCall);
    const QVariantMap verificationResponse = DftReportEvidenceService::crossValidateEvidence(evidenceFile, canonicalRoot);
    if (!verificationResponse.value(QStringLiteral("ok")).toBool()) {
        const QString error = verificationResponse.value(QStringLiteral("message")).toString();
        appendTrace(QJsonObject{
            {QStringLiteral("event"), QStringLiteral("tool_result")},
            {QStringLiteral("step"), 1},
            {QStringLiteral("name"), QStringLiteral("recover_project_gui_evidence")},
            {QStringLiteral("result"), QJsonObject{{QStringLiteral("error"), error}}}
        });
        setFailureReport(error);
        return finishRecovery(false, {});
    }

    const QVariantMap verification = verificationResponse.value(QStringLiteral("result")).toMap();
    const bool verified = verification.value(QStringLiteral("status")).toString() == QLatin1String("verified");
    const QJsonObject executionObject = payload.value(QStringLiteral("execution")).toObject();
    const QVariantMap execution{
        {QStringLiteral("project"), executionObject.value(QStringLiteral("project")).toString(m_currentProjectId)},
        {QStringLiteral("source"), payload.value(QStringLiteral("source")).toVariant()},
        {QStringLiteral("verification"), verification},
        {QStringLiteral("conclusion"), verified ? QStringLiteral("evidence_verified") : QStringLiteral("not_verified")},
        {QStringLiteral("approval"), payload.value(QStringLiteral("approval"))
             .toString(QStringLiteral("DFT 工程师仍需完成后续审核。"))}
    };
    const QString executionJson = QString::fromUtf8(
        QJsonDocument(QJsonObject::fromVariantMap(execution)).toJson(QJsonDocument::Compact));
    appendTrace(QJsonObject{
        {QStringLiteral("event"), QStringLiteral("tool_result")},
        {QStringLiteral("step"), 1},
        {QStringLiteral("name"), QStringLiteral("recover_project_gui_evidence")},
        {QStringLiteral("result"), executionJson}
    });

    const QString answer = verified
        ? QStringLiteral("已从暂存证据恢复执行结果；双轮交叉验证通过。DFT 工程师仍需完成签核复核。")
        : QStringLiteral("已读取暂存证据，但双轮交叉验证未通过（%1）；不能将本次执行标记为通过。")
              .arg(verification.value(QStringLiteral("status")).toString());
    appendTrace(QJsonObject{{QStringLiteral("event"), QStringLiteral("final_answer")},
                            {QStringLiteral("step"), 1}, {QStringLiteral("text"), answer}});

    const QString episodeId = QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMdd'T'HHmmss'Z'"))
        + QLatin1Char('_') + QUuid::createUuid().toString(QUuid::Id128).left(10);
    QVariantMap episode{
        {QStringLiteral("episode_id"), episodeId},
        {QStringLiteral("timestamp"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
        {QStringLiteral("framework"), QStringLiteral("dft_agent_studio.native_project_recovery")},
        {QStringLiteral("model"), QStringLiteral("deterministic-recovery-policy")},
        {QStringLiteral("scope"), QStringLiteral("configured")},
        {QStringLiteral("project_id"), m_currentProjectId},
        {QStringLiteral("goal"), QStringLiteral("Recover GUI handoff by re-validating existing staged configured-project DFT evidence only.")},
        {QStringLiteral("model_answer"), QStringLiteral("GUI 恢复策略未执行 DFT，只重新检查已保存的证据。")},
        {QStringLiteral("answer"), answer},
        {QStringLiteral("tool_trace"), QVariantList{QVariantMap{
            {QStringLiteral("step"), 1},
            {QStringLiteral("name"), QStringLiteral("recover_project_gui_evidence")},
            {QStringLiteral("arguments"), QVariantMap{{QStringLiteral("workspace"), workspace}}},
            {QStringLiteral("result"), executionJson}
        }}},
        {QStringLiteral("errors"), verified ? QVariantList{} : QVariantList{answer}}
    };
    const QString dataRoot = studioDataRoot(m_workingDirectory);
    const QString episodesRoot = QDir(dataRoot).filePath(QStringLiteral("episodes"));
    const QString episodeFile = QDir(episodesRoot).filePath(episodeId + QStringLiteral(".json"));
    QString persistError;
    if (QDir().mkpath(episodesRoot)) {
        const QByteArray bytes = QJsonDocument(QJsonObject::fromVariantMap(episode)).toJson(QJsonDocument::Indented) + '\n';
        QSaveFile episodeOutput(episodeFile);
        if (!episodeOutput.open(QIODevice::WriteOnly) || episodeOutput.write(bytes) != bytes.size()
            || !episodeOutput.commit())
            persistError = episodeOutput.errorString();
    } else {
        persistError = QStringLiteral("无法创建 episode 目录。");
    }
    if (!persistError.isEmpty())
        appendDetailedLog(QStringLiteral("Unable to persist native recovery episode: %1").arg(persistError));
    else {
        const QString database = QDir(dataRoot).filePath(QStringLiteral("memory/agent_memory.sqlite3"));
        NativeMemoryService memory(m_currentProjectId, database);
        QString memoryError;
        if (memory.importEpisode(episode, episodeFile, &memoryError) < 0)
            appendDetailedLog(QStringLiteral("Unable to index native recovery episode: %1").arg(memoryError));
    }

    QJsonObject reportPayload{
        {QStringLiteral("answer"), answer},
        {QStringLiteral("tool_trace"), QJsonArray{QJsonObject{
            {QStringLiteral("step"), 1},
            {QStringLiteral("name"), QStringLiteral("recover_project_gui_evidence")},
            {QStringLiteral("arguments"), QJsonObject{{QStringLiteral("workspace"), workspace}}},
            {QStringLiteral("result"), executionJson}
        }}},
        {QStringLiteral("episode_file"), persistError.isEmpty() ? episodeFile : QString{}}
    };
    if (!verified) {
        reportPayload.insert(QStringLiteral("errors"), QJsonArray{answer});
        setFailureReport(answer);
    }
    updateReport(reportPayload);
    appendActivity({{QStringLiteral("kind"), verified ? QStringLiteral("final") : QStringLiteral("error")},
                    {QStringLiteral("text"), answer}, {QStringLiteral("step"), 0}});
    setResult(QString::fromUtf8(QJsonDocument(reportPayload).toJson(QJsonDocument::Indented)));
    setProgress(100);
    setPhase(verified ? QStringLiteral("Evidence recovered and verified")
                      : QStringLiteral("Evidence recovery requires review"));
    m_receivedResult = true;
    return finishRecovery(verified, answer);
}

bool AgentController::finishRecovery(bool verified, const QString &answer) {
    Q_UNUSED(answer);
    if (!verified && m_errorMessages.isEmpty())
        setHasError(true);
    setRunning(false);
    emit finished(verified);
    QTimer::singleShot(0, this, &AgentController::startNextQueuedPrompt);
    return true;
}
#endif

void AgentController::persistSessionEvent(QJsonObject event) {
    const QString type = event.value(QStringLiteral("event")).toString();
    static const QSet<QString> durableEvents{
        QStringLiteral("turn_requested"), QStringLiteral("turn_finished"),
        QStringLiteral("tool_call"), QStringLiteral("tool_approval_requested"),
        QStringLiteral("tool_result"), QStringLiteral("plan_updated"),
        QStringLiteral("provider_reconnecting"), QStringLiteral("provider_reconnected"),
        QStringLiteral("provider_interrupted"), QStringLiteral("provider_error"),
        QStringLiteral("provider_route_fallback"), QStringLiteral("context_compacted"),
        QStringLiteral("subagent_spawned"), QStringLiteral("subagent_followup"),
        QStringLiteral("subagent_completed"), QStringLiteral("subagent_interrupted"),
    };
    if (!durableEvents.contains(type) || m_liveSessionId.isEmpty()
        || m_currentProjectId.isEmpty() || m_nativeProjectSnapshot.isEmpty())
        return;

    if (!m_liveTurnId.isEmpty())
        event.insert(QStringLiteral("turn_id"), m_liveTurnId);
    if (!event.contains(QStringLiteral("timestamp")))
        event.insert(QStringLiteral("timestamp"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
    const QVariantMap result = workspaceAction(m_nativeProjectSnapshot,
        QStringLiteral("session_runtime_append_event"), {
            {QStringLiteral("thread_id"), m_liveSessionId},
            {QStringLiteral("event"), event.toVariantMap()},
        });
    if (!result.value(QStringLiteral("ok")).toBool())
        appendDetailedLog(QStringLiteral("Unable to persist session event (%1): %2")
            .arg(type, result.value(QStringLiteral("message")).toString()));
}

void AgentController::appendTrace(const QJsonObject &event) {
    const QString title = tracePrefix(event);
    const QString type = event.value("event").toString();
    persistSessionEvent(event);
    if (type == QStringLiteral("tool_result")
        && event.value(QStringLiteral("name")).toString() == QStringLiteral("save_run_report"))
        emit reportFilesChanged();
    if (type == QStringLiteral("user_steering_received")) {
        const QString steeringId = event.value(QStringLiteral("steering_id")).toString();
        if (shouldAppendLiveActivity() && !steeringId.isEmpty()) {
            for (qsizetype index = m_activityEntries.size() - 1; index >= 0; --index) {
                QVariantMap entry = m_activityEntries.at(index).toMap();
                if (entry.value(QStringLiteral("steeringId")).toString() != steeringId)
                    continue;
                entry.insert(QStringLiteral("status"), QString{});
                m_activityEntries[index] = entry;
                emit activityEntriesChanged();
                break;
            }
        }
        appendDetailedLog(QStringLiteral("User guidance admitted at the next safe tool boundary."));
        return;
    }
    QString body;
    if (type == "tool_call") {
        if (event.value(QStringLiteral("name")).toString() == QStringLiteral("save_run_report")) {
            const QJsonObject arguments = event.value(QStringLiteral("arguments")).toObject();
            body = QStringLiteral("%1 · %2")
                .arg(arguments.value(QStringLiteral("title")).toString(),
                     arguments.value(QStringLiteral("category")).toString());
        } else {
            body = displayToolArguments(event.value("arguments"));
        }
    }
    else if (type == "provider_tool_activity") {
        body = displayToolArguments(event.value("arguments"));
        const QString result = displayJson(event.value("result"));
        if (!result.isEmpty())
            body += (body.isEmpty() ? QString{} : QStringLiteral("\n\n")) + result;
    } else if (type == "patch_proposed") {
        body = displayJson(event.value("changes"));
    } else if (type == "plan_updated") {
        body = displayJson(event.value("plan"));
        if (body.isEmpty())
            body = event.value("text").toString();
    } else if (type == "model_thinking") {
        body = QStringLiteral("思考中…");
    } else if (type == "thread_ready") {
        if (m_liveSessionId.isEmpty()) {
            m_liveSessionId = event.value(QStringLiteral("thread_id")).toString().trimmed();
            if (!m_liveSessionId.isEmpty()) {
                // Some providers create the root thread after the run starts.
                // Persist the already-running state as soon as its durable ID
                // arrives; earlier progress callbacks could not be indexed.
                m_nativeProjectSnapshot.insert(QStringLiteral("codexThreadId"), m_liveSessionId);
                setActivitySession(m_liveSessionId);
                setFlowDisplaySession(m_liveSessionId);
                persistSessionProgress(true);
            }
        }
        body = QStringLiteral("DFT Agent session: %1\nProvider session: %2")
            .arg(event.value("thread_id").toString(), event.value("provider_thread_id").toString());
        const QString sessionId = event.value(QStringLiteral("thread_id")).toString();
        if (!m_currentProjectId.isEmpty() && !sessionId.isEmpty())
            emit codexSessionReady(m_currentProjectId, sessionId);
    } else if (type == "tool_approval_requested") {
        const QString name = event.value(QStringLiteral("name")).toString();
        const QJsonObject arguments = event.value(QStringLiteral("arguments")).toObject();
        if (name == QStringLiteral("request_path_access")) {
            body = QStringLiteral("路径：%1\n操作：%2")
                .arg(arguments.value(QStringLiteral("path")).toString(),
                     arguments.value(QStringLiteral("operation")).toString());
        } else {
            body = displayToolArguments(event.value("arguments"));
        }
        const QString reason = event.value("reason").toString();
        if (!reason.isEmpty())
            body += (body.isEmpty() ? QString{} : QStringLiteral("\n\n")) + reason;
    } else if (type == "tool_result") {
        body = displayJson(event.value("result"));
        const QString observation = displayJson(event.value("observation"));
        if (!observation.isEmpty() && observation != body)
            body += (body.isEmpty() ? QString{} : QStringLiteral("\n\n")) + observation;
    } else if (type == "error") {
        body = event.value("message").toString();
        if (!body.isEmpty()) {
            m_errorMessages.append(body);
            setHasError(true);
            appendLog(QStringLiteral("Error: %1").arg(body));
        }
    } else if (type == "provider_reconnecting") {
        body = QStringLiteral("正在重新连接（%1/%2）")
            .arg(event.value(QStringLiteral("attempt")).toInt())
            .arg(event.value(QStringLiteral("max_attempts")).toInt(10));
        const QString route = event.value(QStringLiteral("route")).toString();
        const QString reason = event.value(QStringLiteral("reason")).toString();
        const int statusCode = event.value(QStringLiteral("status_code")).toInt();
        if (!route.isEmpty() || !reason.isEmpty() || statusCode > 0)
            body += QStringLiteral("\n路由：%1；原因：%2%3")
                .arg(route.isEmpty() ? QStringLiteral("unknown") : route,
                     reason.isEmpty() ? QStringLiteral("unknown") : reason,
                     statusCode > 0 ? QStringLiteral("；HTTP %1").arg(statusCode) : QString{});
    } else if (type == "provider_reconnected") {
        body = QStringLiteral("已重新连接（重试 %1 次）")
            .arg(event.value(QStringLiteral("attempts")).toInt());
    } else if (type == "provider_route_fallback") {
        body = QStringLiteral("已切换兼容 API 路由：%1 → %2（HTTP %3）")
            .arg(event.value(QStringLiteral("from_route")).toString(),
                 event.value(QStringLiteral("to_route")).toString())
            .arg(event.value(QStringLiteral("status_code")).toInt());
    } else if (type == "provider_interrupted") {
        body = QStringLiteral("重连结束，已保存检查点（%1/%2；路由 %3；%4）")
            .arg(event.value(QStringLiteral("attempts")).toInt(10))
            .arg(event.value(QStringLiteral("max_attempts")).toInt(10))
            .arg(event.value(QStringLiteral("route")).toString())
            .arg(event.value(QStringLiteral("reason")).toString());
    } else if (type == "context_compacted") {
        body = QString::fromUtf8(QJsonDocument(event).toJson(QJsonDocument::Indented)).trimmed();
    } else if (type == "context_usage") {
        body = QStringLiteral("Input %1 / %2, system %3, tools %4, output %5")
            .arg(event.value("input_tokens").toInt())
            .arg(event.value("context_window").toInt())
            .arg(event.value("system_prompt_tokens").toInt())
            .arg(event.value("tool_schema_tokens").toInt())
            .arg(event.value("output_tokens").toInt());
    } else if (type == "subagent_spawned" || type == "subagent_followup") {
        body = event.value("task").toString();
    } else if (type == "subagent_completed") {
        body = event.value("answer").toString();
        if (body.isEmpty())
            body = event.value("reason").toString();
    } else if (type == "provider_protocol_event") {
        body = QStringLiteral("method: %1\n%2")
            .arg(event.value("method").toString(), displayJson(event.value("params")));
    } else if (type == "provider_protocol_error") {
        body = event.value("detail").toString();
    } else if (type == "provider_error") {
        body = displayJson(event.value("detail"));
    } else if (type == "provider_item") {
        body = displayJson(event.value("item"));
    } else if (type == "diff_updated") {
        body = displayJson(event.value("diff"));
        if (body.isEmpty())
            body = displayJson(event.value("files"));
    } else if (type == "turn_started" || type == "turn_completed") {
        body = QStringLiteral("Turn %1")
            .arg(event.value("turn_id").toString());
    } else {
        body = displayJson(event.value("text"));
    }
    const int step = event.value(QStringLiteral("step")).toInt();
    if (type == QStringLiteral("provider_reconnecting")
        || type == QStringLiteral("provider_reconnected")
        || type == QStringLiteral("provider_route_fallback")
        || type == QStringLiteral("provider_interrupted")) {
        // Keep transport diagnostics in the bounded detailed log without
        // turning them into additional copyable Chat messages.
        appendDetailedLog(body);
    }
    // Flow monitoring belongs to the active worker, not only to the Chat
    // session currently selected in the sidebar. Keep stage/substep state
    // live even while the user is viewing another conversation.
    updateAgentActivityStage(event);
    if (!shouldAppendLiveActivity()) {
        // Keep diagnostics for the active worker, but never merge its live
        // trace into a different session currently selected in Chat.
        appendDetailedLog(body.isEmpty() ? title : QStringLiteral("%1\n%2").arg(title, body));
        return;
    }
    if (type == QStringLiteral("provider_stream_reset")) {
        const QString streamId = event.value(QStringLiteral("previous_model_stream_id")).toString();
        if (!streamId.isEmpty()) {
            bool changed = false;
            for (int index = m_activityEntries.size() - 1; index >= 0; --index) {
                const QVariantMap candidate = m_activityEntries.at(index).toMap();
                if (candidate.value(QStringLiteral("kind")).toString() != QStringLiteral("agent")
                    || candidate.value(QStringLiteral("streamId")).toString() != streamId)
                    continue;
                m_activityEntries.removeAt(index);
                changed = true;
            }
            if (changed)
                emit activityEntriesChanged();
        }
    } else if (type == QStringLiteral("provider_interrupted")) {
        appendActivity({
            {QStringLiteral("kind"), QStringLiteral("reconnect")},
            {QStringLiteral("role"), QStringLiteral("status")},
            {QStringLiteral("status"), QStringLiteral("interrupted")},
            {QStringLiteral("text"), QStringLiteral("Responses API 暂时不可用，已保留当前回合；可点击恢复。")},
            {QStringLiteral("maxAttempts"), event.value(QStringLiteral("max_attempts")).toInt(10)},
            {QStringLiteral("step"), step},
        });
    } else if (type == QStringLiteral("provider_reconnecting")
        || type == QStringLiteral("provider_reconnected")) {
        const QString status = type == QStringLiteral("provider_reconnected")
            ? QStringLiteral("completed") : QStringLiteral("running");
        int reconnectIndex = -1;
        for (int index = m_activityEntries.size() - 1; index >= 0; --index) {
            const QVariantMap candidate = m_activityEntries.at(index).toMap();
            if (candidate.value(QStringLiteral("kind")).toString() == QStringLiteral("reconnect")
                && candidate.value(QStringLiteral("status")).toString() == QStringLiteral("running")) {
                reconnectIndex = index;
                break;
            }
        }
        if (reconnectIndex >= 0) {
            QVariantMap entry = m_activityEntries.at(reconnectIndex).toMap();
            entry.insert(QStringLiteral("text"), body);
            entry.insert(QStringLiteral("status"), status);
            entry.insert(QStringLiteral("attempt"), event.value(QStringLiteral("attempt")).toInt());
            entry.insert(QStringLiteral("maxAttempts"), event.value(QStringLiteral("max_attempts")).toInt(10));
            m_activityEntries[reconnectIndex] = entry;
            emit activityEntriesChanged();
        } else {
            appendActivity({
                {QStringLiteral("kind"), QStringLiteral("reconnect")},
                {QStringLiteral("text"), body},
                {QStringLiteral("step"), step},
                {QStringLiteral("status"), status},
                {QStringLiteral("attempt"), event.value(QStringLiteral("attempt")).toInt()},
                {QStringLiteral("maxAttempts"), event.value(QStringLiteral("max_attempts")).toInt(10)},
            });
        }
    } else if (type == QStringLiteral("tool_call")) {
        const QString name = event.value(QStringLiteral("name")).toString();
        if (name == QStringLiteral("propose_patch") || name == QStringLiteral("edit_project_files")
            || name == QStringLiteral("create_file") || name == QStringLiteral("apply_patch")) {
            const QJsonObject arguments = event.value(QStringLiteral("arguments")).toObject();
            appendActivity({
                {QStringLiteral("kind"), QStringLiteral("patch")},
                {QStringLiteral("name"), name},
                {QStringLiteral("text"), boundedActivityText(body)},
                {QStringLiteral("argumentsRaw"), displayJson(event.value(QStringLiteral("arguments")))},
                {QStringLiteral("patchText"), name == QStringLiteral("create_file")
                    ? event.value(QStringLiteral("patch_text")).toString()
                    : arguments.value(QStringLiteral("patch")).toString()},
                {QStringLiteral("createPath"), arguments.value(QStringLiteral("path")).toString()},
                {QStringLiteral("files"), arguments.value(QStringLiteral("files")).toVariant()},
                {QStringLiteral("purpose"), arguments.value(QStringLiteral("purpose")).toString()},
                {QStringLiteral("result"), QString{}},
                {QStringLiteral("step"), step},
                {QStringLiteral("status"), QStringLiteral("running")},
            });
        } else {
            appendToolActivity(name, boundedActivityText(body), step);
        }
    } else if (type == QStringLiteral("tool_result")) {
        const QString toolName = event.value(QStringLiteral("name")).toString();
        if (toolName == QStringLiteral("request_path_access")) {
            const QString requestId = event.value(QStringLiteral("request_id")).toString();
            const QString permissionStatus = event.value(QStringLiteral("result")).toObject()
                .value(QStringLiteral("status")).toString(QStringLiteral("expired"));
            for (int index = m_activityEntries.size() - 1; index >= 0; --index) {
                QVariantMap entry = m_activityEntries.at(index).toMap();
                if (entry.value(QStringLiteral("kind")).toString() != QStringLiteral("approval")
                    || entry.value(QStringLiteral("requestId")).toString() != requestId)
                    continue;
                entry.insert(QStringLiteral("permissionStatus"), permissionStatus);
                entry.insert(QStringLiteral("status"), permissionStatus == QStringLiteral("denied")
                    ? QStringLiteral("rejected") : QStringLiteral("completed"));
                m_activityEntries[index] = entry;
                emit activityEntriesChanged();
                break;
            }
        }
        const bool isPatchTool = toolName == QStringLiteral("propose_patch")
            || toolName == QStringLiteral("edit_project_files")
            || toolName == QStringLiteral("create_file")
            || toolName == QStringLiteral("apply_patch");
        if (!isPatchTool) {
            completeToolActivity(
                toolName,
                boundedActivityText(body),
                step
            );
        }
        const QJsonObject result = event.value(QStringLiteral("result")).toObject();
        if (isPatchTool) {
            for (int index = m_activityEntries.size() - 1; index >= 0; --index) {
                QVariantMap entry = m_activityEntries.at(index).toMap();
                if (entry.value(QStringLiteral("kind")).toString() != QStringLiteral("patch")
                    || (entry.value(QStringLiteral("name")).toString() != QStringLiteral("propose_patch")
                        && entry.value(QStringLiteral("name")).toString() != QStringLiteral("edit_project_files")
                        && entry.value(QStringLiteral("name")).toString() != QStringLiteral("create_file")
                        && entry.value(QStringLiteral("name")).toString() != QStringLiteral("apply_patch"))
                    || entry.value(QStringLiteral("step")).toInt() != step
                    || entry.value(QStringLiteral("status")).toString() != QStringLiteral("running"))
                    continue;
                entry.insert(QStringLiteral("result"), boundedActivityText(body));
                if (result.value(QStringLiteral("proposed")).toBool()
                    || result.value(QStringLiteral("edited")).toBool()
                    || result.value(QStringLiteral("status")).toString() == QStringLiteral("rolled_back")) {
                    entry.insert(QStringLiteral("proposalId"), result.value(QStringLiteral("proposal_id")).toString());
                    entry.insert(QStringLiteral("editId"), result.value(QStringLiteral("edit_id")).toString());
                    entry.insert(QStringLiteral("patchFile"), result.value(QStringLiteral("patch_file")).toString());
                    entry.insert(QStringLiteral("files"), result.value(QStringLiteral("files")).toVariant());
                    entry.insert(QStringLiteral("purpose"), result.value(QStringLiteral("purpose")).toString());
                    entry.insert(QStringLiteral("lineStats"), result.value(QStringLiteral("line_stats")).toVariant());
                    const QString resultPatch = result.value(QStringLiteral("patch_text")).toString();
                    if (!resultPatch.isEmpty())
                        entry.insert(QStringLiteral("patchText"), resultPatch);
                    entry.insert(QStringLiteral("status"), result.value(QStringLiteral("status")).toString());
                    entry.insert(QStringLiteral("text"), result.value(QStringLiteral("purpose")).toString());
                } else {
                    entry.insert(QStringLiteral("status"), QStringLiteral("failed"));
                    QString errorText = result.value(QStringLiteral("error")).toString().trimmed();
                    if (errorText.isEmpty())
                        errorText = result.value(QStringLiteral("message")).toString().trimmed();
                    const QJsonObject validation = result.value(QStringLiteral("cross_validation")).toObject();
                    if (errorText.isEmpty())
                        errorText = validation.value(QStringLiteral("error")).toString().trimmed();
                    if (errorText.isEmpty())
                        errorText = validation.value(QStringLiteral("reason")).toString().trimmed();
                    if (errorText.isEmpty())
                        errorText = result.value(QStringLiteral("status")).toString().trimmed();
                    if (errorText.isEmpty())
                        errorText = QStringLiteral("文件编辑失败。");
                    entry.insert(QStringLiteral("errorText"), boundedActivityText(errorText));
                }
                m_activityEntries[index] = entry;
                emit activityEntriesChanged();
                break;
            }
        }
    } else if (type == QStringLiteral("provider_tool_activity")) {
        appendActivity({
            {QStringLiteral("kind"), QStringLiteral("tool")},
            {QStringLiteral("name"), event.value(QStringLiteral("name")).toString()},
            {QStringLiteral("text"), boundedActivityText(body)},
            {QStringLiteral("step"), step},
            {QStringLiteral("status"), event.value(QStringLiteral("status")).toString()},
        });
    } else if (type == QStringLiteral("patch_proposed")) {
        appendActivity({
            {QStringLiteral("kind"), QStringLiteral("patch")},
            {QStringLiteral("text"), boundedActivityText(body)},
            {QStringLiteral("step"), step},
        });
    } else if (type == QStringLiteral("tool_approval_requested")) {
        const QJsonObject arguments = event.value(QStringLiteral("arguments")).toObject();
        const bool pathPermission = event.value(QStringLiteral("name")).toString() == QStringLiteral("request_path_access");
        appendActivity({
            {QStringLiteral("kind"), QStringLiteral("approval")},
            {QStringLiteral("name"), event.value(QStringLiteral("name")).toString()},
            {QStringLiteral("text"), boundedActivityText(body)},
            {QStringLiteral("requestId"), event.value(QStringLiteral("request_id")).toString()},
            {QStringLiteral("permissionPath"), pathPermission ? arguments.value(QStringLiteral("path")).toString() : QString{}},
            {QStringLiteral("permissionOperation"), pathPermission ? arguments.value(QStringLiteral("operation")).toString() : QString{}},
            {QStringLiteral("permissionStatus"), QStringLiteral("pending")},
            {QStringLiteral("patchText"), event.value(QStringLiteral("name")).toString() == QStringLiteral("create_file")
                ? event.value(QStringLiteral("patch_text")).toString()
                : arguments.value(QStringLiteral("patch")).toString()},
            {QStringLiteral("createPath"), arguments.value(QStringLiteral("path")).toString()},
            {QStringLiteral("files"), arguments.value(QStringLiteral("files")).toVariant()},
            {QStringLiteral("purpose"), arguments.value(QStringLiteral("purpose")).toString()},
            {QStringLiteral("step"), step},
            {QStringLiteral("status"), QStringLiteral("pending")},
        });
    } else if (type == QStringLiteral("model_thinking")) {
        const QString thinkingDelta = event.value(QStringLiteral("text")).toString();
        const QString streamId = event.value(QStringLiteral("model_stream_id")).toString();
        const bool continuingThinking = !m_activityEntries.isEmpty()
            && m_activityEntries.constLast().toMap().value(QStringLiteral("kind")).toString() == QStringLiteral("agent")
            && m_activityEntries.constLast().toMap().value(QStringLiteral("step")).toInt() == step
            && m_activityEntries.constLast().toMap().value(QStringLiteral("role")).toString() == QStringLiteral("thinking")
            && m_activityEntries.constLast().toMap().value(QStringLiteral("streamId")).toString() == streamId;
        if (continuingThinking && !thinkingDelta.isEmpty()) {
            appendAgentActivity(thinkingDelta, step, QStringLiteral("thinking"), true, streamId);
        } else {
            appendAgentActivity(
                thinkingDelta.isEmpty() ? QStringLiteral("思考中…") : QStringLiteral("思考中…\n") + thinkingDelta,
                step,
                QStringLiteral("thinking"),
                true,
                streamId
            );
        }
    } else if (type == QStringLiteral("model_delta")) {
        appendAgentActivity(
            body,
            step,
            QStringLiteral("draft"),
            true,
            event.value(QStringLiteral("model_stream_id")).toString()
        );
    } else if (type == QStringLiteral("model_output")) {
        appendAgentActivity(boundedActivityText(body), step, QStringLiteral("response"));
    } else if (type == QStringLiteral("final_answer")) {
        appendAgentActivity(boundedActivityText(body), step, QStringLiteral("final"));
    } else if (type == QStringLiteral("plan_updated")) {
        appendAgentActivity(boundedActivityText(body), step, QStringLiteral("plan"));
    } else if (type == QStringLiteral("subagent_spawned")
               || type == QStringLiteral("subagent_followup")) {
        appendAgentActivity(
            event.value(QStringLiteral("task")).toString(),
            step,
            QStringLiteral("status")
        );
        if (!m_activityEntries.isEmpty()) {
            QVariantMap entry = m_activityEntries.constLast().toMap();
            QString subagentName = event.value(QStringLiteral("subagent_name")).toString();
            if (subagentName.isEmpty())
                subagentName = event.value(QStringLiteral("role")).toString();
            entry.insert(QStringLiteral("status"), QStringLiteral("subagent_running"));
            entry.insert(QStringLiteral("subagentId"), event.value(QStringLiteral("subagent_id")).toString());
            entry.insert(QStringLiteral("subagentRole"), event.value(QStringLiteral("role")).toString());
            entry.insert(QStringLiteral("subagentName"), subagentName);
            entry.insert(QStringLiteral("subagentSpecialty"), event.value(QStringLiteral("specialty")).toString());
            m_activityEntries.last() = entry;
            emit activityEntriesChanged();
        }
    } else if (type == QStringLiteral("subagent_completed")) {
        const QString status = event.value(QStringLiteral("status")).toString();
        const bool successful = status == QStringLiteral("completed")
            || status == QStringLiteral("verified")
            || status == QStringLiteral("passed")
            || status == QStringLiteral("success");
        if (body.trimmed().isEmpty())
            body = successful
                ? QStringLiteral("子智能体已完成，但没有返回文字报告。")
                : QStringLiteral("隔离复核未返回具体原因；请查看子智能体的结构化检查结果。");
        appendAgentActivity(
            QStringLiteral("子智能体复核%1：%2")
                .arg(successful ? QStringLiteral("完成") : QStringLiteral("失败"),
                     boundedActivityText(body)),
            step,
            QStringLiteral("status")
        );
        if (!m_activityEntries.isEmpty()) {
            QVariantMap entry = m_activityEntries.constLast().toMap();
            QString subagentName = event.value(QStringLiteral("subagent_name")).toString();
            if (subagentName.isEmpty())
                subagentName = event.value(QStringLiteral("role")).toString();
            entry.insert(QStringLiteral("status"), successful ? QStringLiteral("subagent_completed") : QStringLiteral("subagent_failed"));
            entry.insert(QStringLiteral("subagentId"), event.value(QStringLiteral("subagent_id")).toString());
            entry.insert(QStringLiteral("subagentRole"), event.value(QStringLiteral("role")).toString());
            entry.insert(QStringLiteral("subagentName"), subagentName);
            entry.insert(QStringLiteral("subagentSpecialty"), event.value(QStringLiteral("specialty")).toString());
            m_activityEntries.last() = entry;
            emit activityEntriesChanged();
        }
    } else if (type == QStringLiteral("provider_protocol_event")
               || type == QStringLiteral("provider_protocol_error")
               || type == QStringLiteral("provider_error")
               || type == QStringLiteral("provider_item")) {
        appendActivity({
            {QStringLiteral("kind"), QStringLiteral("provider")},
            {QStringLiteral("name"), event.value(QStringLiteral("method")).toString()},
            {QStringLiteral("text"), boundedActivityText(body)},
            {QStringLiteral("step"), step},
            {QStringLiteral("status"), type == QStringLiteral("provider_protocol_error")
                || type == QStringLiteral("provider_error") ? QStringLiteral("error") : QStringLiteral("info")},
        });
    } else if (type == QStringLiteral("diff_updated")) {
        appendActivity({
            {QStringLiteral("kind"), QStringLiteral("diff")},
            {QStringLiteral("name"), QStringLiteral("File changes")},
            {QStringLiteral("text"), boundedActivityText(body)},
            {QStringLiteral("step"), step},
            {QStringLiteral("status"), QStringLiteral("updated")},
        });
    } else if (type == QStringLiteral("turn_started") || type == QStringLiteral("turn_completed")) {
        // Turn identifiers are persistence/debug metadata, not chat content.
        // Keep them in the detailed trace but do not render another status
        // bubble below the model response.
    } else if (type == QStringLiteral("error")) {
        const bool connectionError = body.contains(QStringLiteral("Responses API"), Qt::CaseInsensitive)
            || body.contains(QStringLiteral("无法连接"));
        int reconnectIndex = -1;
        if (connectionError) {
            for (int index = m_activityEntries.size() - 1; index >= 0; --index) {
                const QVariantMap candidate = m_activityEntries.at(index).toMap();
                if (candidate.value(QStringLiteral("kind")).toString() == QStringLiteral("reconnect")
                    && candidate.value(QStringLiteral("status")).toString() == QStringLiteral("running")) {
                    reconnectIndex = index;
                    break;
                }
            }
        }
        if (reconnectIndex >= 0) {
            QVariantMap entry = m_activityEntries.at(reconnectIndex).toMap();
            const int maxAttempts = entry.contains(QStringLiteral("maxAttempts"))
                ? entry.value(QStringLiteral("maxAttempts")).toInt() : 10;
            entry.insert(QStringLiteral("text"), QStringLiteral("连接失败，已重试 %1 次").arg(maxAttempts));
            entry.insert(QStringLiteral("status"), QStringLiteral("failed"));
            m_activityEntries[reconnectIndex] = entry;
            emit activityEntriesChanged();
        } else {
            appendActivity({
                {QStringLiteral("kind"), QStringLiteral("error")},
                {QStringLiteral("text"), boundedActivityText(body)},
                {QStringLiteral("step"), step},
            });
        }
    } else if (type == QStringLiteral("terminal_diagnostic_command")) {
        appendToolActivity(
            QStringLiteral("terminal"),
            boundedActivityText(event.value(QStringLiteral("command")).toString()),
            step
        );
    } else if (type == QStringLiteral("terminal_diagnostic_result")) {
        completeToolActivity(
            QStringLiteral("terminal"),
            boundedActivityText(displayJson(event)),
            step,
            event.value(QStringLiteral("exit_code")).toInt() != 0
        );
    } else if (type == QStringLiteral("workspace_agent_action")) {
        const QString action = event.value(QStringLiteral("action")).toString();
        QString arguments = displayToolArguments(event.value(QStringLiteral("arguments")));
        const QString reason = event.value(QStringLiteral("reason")).toString();
        if (!reason.isEmpty())
            arguments = QStringLiteral("%1%2原因：%3").arg(arguments, arguments.isEmpty() ? QString{} : QStringLiteral("\n\n"), reason);
        appendToolActivity(action, boundedActivityText(arguments), step);
    } else if (type == QStringLiteral("workspace_agent_result")) {
        completeToolActivity(
            event.value(QStringLiteral("action")).toString(),
            boundedActivityText(displayJson(event.value(QStringLiteral("result")))),
            step,
            event.value(QStringLiteral("result")).toObject().contains(QStringLiteral("error"))
        );
    }
    appendDetailedLog(body.isEmpty() ? title : QStringLiteral("%1\n%2").arg(title, body));
}

void AgentController::updateReport(const QJsonObject &payload) {
    const QString executionState = payload.value("execution_state").toString();
    const bool planningOnly = executionState == QStringLiteral("planning_only");
    const bool analysisWithTools = executionState == QStringLiteral("analysis_with_tools");
    const bool executionBlocked = executionState == QStringLiteral("execution_blocked");
    const QJsonObject supervisorWarning = payload.value("supervisor_warning").toObject();
    const QString warningKind = supervisorWarning.value("kind").toString();
    setRequiresSupervisorReview(
        warningKind == QStringLiteral("rtl_compatibility_requires_manual_review")
        || warningKind == QStringLiteral("dft_drc_requires_manual_review"));
    if (m_requiresSupervisorReview) {
        const QString diagnosis = supervisorWarning.value("diagnosis").toString();
        const QString message = diagnosis.isEmpty()
            ? QStringLiteral("监管者告警：RTL 兼容性问题需要人工审核。")
            : QStringLiteral("监管者告警：%1").arg(diagnosis);
        if (!m_errorMessages.contains(message))
            m_errorMessages.append(message);
    }
    const QJsonArray errors = payload.value("errors").toArray();
    for (const auto &value : errors) {
        const QString message = displayJson(value);
        if (!message.isEmpty() && !m_errorMessages.contains(message))
            m_errorMessages.append(message);
    }
    setHasError(!m_errorMessages.isEmpty());

    QStringList lines;
    lines << (m_hasError ? QStringLiteral("运行结果：需要处理错误")
             : executionBlocked ? QStringLiteral("运行结果：已执行流程，但被工具阻塞")
             : analysisWithTools ? QStringLiteral("运行结果：已完成受控检查与 Agent 分析，尚未执行 DFT 流程")
             : planningOnly ? QStringLiteral("运行结果：已生成 Agent 计划，尚未执行受控 DFT 工具")
             : QStringLiteral("运行结果：已生成执行报告"));
    const QJsonObject startupCheck = payload.value(QStringLiteral("startup_check")).toObject();
    const QJsonObject readiness = startupCheck.value(QStringLiteral("readiness")).toObject();
    if (!startupCheck.isEmpty() || !readiness.isEmpty()) {
        const QString filelist = readiness.value(QStringLiteral("source_filelist")).toString();
        const int sourceCount = readiness.value(QStringLiteral("source_file_count")).toInt(-1);
        const QJsonArray missing = readiness.value(QStringLiteral("source_manifest_missing")).toArray();
        const QJsonArray unresolved = readiness.value(QStringLiteral("source_manifest_unresolved")).toArray();
        lines << QStringLiteral("\n启动检查\n状态：%1\n源文件清单：%2\n源文件数：%3\n缺失：%4\n未解析：%5")
                     .arg(startupCheck.value(QStringLiteral("ready")).toBool() ? QStringLiteral("通过") : QStringLiteral("阻塞"))
                     .arg(filelist.isEmpty() ? QStringLiteral("未配置") : filelist)
                     .arg(sourceCount >= 0 ? QString::number(sourceCount) : QStringLiteral("未知"))
                     .arg(missing.isEmpty() ? QStringLiteral("无") : QString::number(missing.size()))
                     .arg(unresolved.isEmpty() ? QStringLiteral("无") : QString::number(unresolved.size()));
    }
    const QString answer = payload.value("answer").toString();
    if (!answer.isEmpty())
        lines << QStringLiteral("\n最终结论\n%1").arg(answer);

    const QJsonArray calls = payload.value("tool_trace").toArray();
    if (!calls.isEmpty()) {
        lines << QStringLiteral("\n执行的工具\n");
        for (const auto &value : calls) {
            const QJsonObject call = value.toObject();
            const int step = call.value("step").toInt();
            QString item = QStringLiteral("- %1%2").arg(step > 0 ? QStringLiteral("Step %1: ").arg(step) : QString{}, call.value("name").toString());
            const QString arguments = displayToolArguments(call.value("arguments"));
            if (!arguments.isEmpty())
                item += QStringLiteral("\n  参数：%1").arg(arguments);
            const QString result = displayJson(call.value("result"));
            if (!result.isEmpty())
                item += QStringLiteral("\n  工具结果：%1").arg(result);
            lines << item;
        }
    } else if (planningOnly) {
        lines << QStringLiteral("\n受控执行\n本回合仅完成项目分析与计划；尚未启动综合、DFT DRC、Scan、MBIST 或 ATPG。");
    } else if (executionBlocked) {
        lines << QStringLiteral("\n受控执行\n流程已经启动并返回工具证据，但最终交叉验证被阻塞；请先处理错误后重新运行。");
    } else {
        lines << QStringLiteral("\n执行的工具\n未记录到任何受控工具调用。");
    }

    if (!m_errorMessages.isEmpty()) {
        lines << QStringLiteral("\n错误与阻塞项\n");
        for (const auto &message : m_errorMessages)
            lines << QStringLiteral("- %1").arg(message);
    }
    const QString episodeFile = payload.value("episode_file").toString();
    if (!episodeFile.isEmpty())
        lines << QStringLiteral("\n执行记录\n%1").arg(episodeFile);
    setReport(lines.join(QStringLiteral("\n")));
}

void AgentController::setFailureReport(const QString &message) {
    if (!m_errorMessages.contains(message))
        m_errorMessages.append(message);
    setHasError(true);
    setReport(QStringLiteral("运行结果：失败\n\n错误与阻塞项\n- %1\n\n没有返回可验证的执行证据。").arg(message));
}

#ifdef DFT_AGENT_STUDIO_TESTING
void AgentController::consumeLine(const QByteArray &line) {
    const auto document = QJsonDocument::fromJson(line);
    if (!document.isObject()) {
        const QString text = QString::fromUtf8(line);
        appendLog(text);
        appendDetailedLog(QStringLiteral("Worker output\n%1").arg(text));
        return;
    }
    const auto message = document.object();
    const auto channel = message.value("gui_channel").toString();
    const auto payload = message.value("payload");
    if (channel == QStringLiteral("native_response_round") && payload.isObject()) {
        const QJsonObject request = payload.toObject();
        const QString requestId = request.value(QStringLiteral("request_id")).toString();
        const QString endpoint = request.value(QStringLiteral("endpoint")).toString();
        const QString apiKey = request.value(QStringLiteral("api_key")).toString();
        const QJsonObject body = request.value(QStringLiteral("body")).toObject();
        if (requestId.isEmpty() || endpoint.isEmpty() || body.isEmpty()) {
            appendDetailedLog(QStringLiteral("Dropped an incomplete native Responses request."));
            return;
        }
        if (m_nativeResponseRequests.contains(requestId)) {
            appendDetailedLog(QStringLiteral("Dropped a duplicate native Responses request ID."));
            return;
        }
        auto *client = new ResponsesRoundClient(this);
        m_nativeResponseRequests.insert(requestId, client);
        connect(client, &ResponsesRoundClient::eventReceived, this,
            [this, requestId](const QJsonObject &event) {
                if (m_process.state() != QProcess::Running)
                    return;
                const QJsonObject response{{QStringLiteral("type"), QStringLiteral("native_response_event")},
                    {QStringLiteral("request_id"), requestId}, {QStringLiteral("event"), event}};
                m_process.write(QJsonDocument(response).toJson(QJsonDocument::Compact) + '\n');
            });
        connect(client, &ResponsesRoundClient::completed, this,
            [this, client, requestId](const QJsonArray &events, int statusCode, const QString &error) {
                m_nativeResponseRequests.remove(requestId);
                if (m_process.state() == QProcess::Running) {
                    const QJsonObject response{{QStringLiteral("type"), QStringLiteral("native_response_complete")},
                        {QStringLiteral("request_id"), requestId}, {QStringLiteral("events"), events},
                        {QStringLiteral("status_code"), statusCode}, {QStringLiteral("error"), error}};
                    m_process.write(QJsonDocument(response).toJson(QJsonDocument::Compact) + '\n');
                }
                client->deleteLater();
            });
        client->startEndpoint(endpoint, apiKey, body,
            request.value(QStringLiteral("timeout_ms")).toInt(1'800'000));
        return;
    }
    if (channel == QStringLiteral("native_tool_call") && payload.isObject()) {
        const QJsonObject request = payload.toObject();
        const QString requestId = request.value(QStringLiteral("request_id")).toString();
        const QString action = request.value(QStringLiteral("action")).toString();
        if (requestId.isEmpty()) {
            appendDetailedLog(QStringLiteral("Dropped a native tool request without request_id."));
            return;
        }
        const QVariantMap project = request.value(QStringLiteral("project")).toObject().toVariantMap();
        const QVariantMap arguments = request.value(QStringLiteral("arguments")).toObject().toVariantMap();
        const QString agentRoot = m_workingDirectory;
        const QPointer<AgentController> guard(this);
        QThreadPool::globalInstance()->start([guard, requestId, action, project, arguments, agentRoot] {
            const QVariantMap response = AgentToolService::dispatch(action, project, arguments, agentRoot);
            if (!guard)
                return;
            QMetaObject::invokeMethod(guard, [guard, requestId, response] {
                if (!guard)
                    return;
                const QJsonObject reply{
                    {QStringLiteral("type"), QStringLiteral("native_tool_response")},
                    {QStringLiteral("request_id"), requestId},
                    {QStringLiteral("result"), QJsonObject::fromVariantMap(response)},
                };
                if (guard->m_process.state() == QProcess::Running) {
                    guard->m_process.write(QJsonDocument(reply).toJson(QJsonDocument::Compact) + '\n');
                } else {
                    guard->appendDetailedLog(QStringLiteral("Native tool reply dropped because the worker is no longer running."));
                }
            }, Qt::QueuedConnection);
        });
        return;
    }
    if (channel == "progress" && payload.isObject()) {
        const auto event = payload.toObject();
        if (!m_flowDisplaySessionId.isEmpty() && !m_liveSessionId.isEmpty()
            && m_flowDisplaySessionId != m_liveSessionId) {
            emit sessionProgressUpdated(m_currentProjectId, m_liveSessionId, m_progress, m_phase, m_flowStageStates);
            appendDetailedLog(QStringLiteral("Progress for background session %1\n%2")
                                  .arg(m_liveSessionId,
                                       QString::fromUtf8(QJsonDocument(event).toJson(QJsonDocument::Compact))));
            return;
        }
        const bool accepted = updateFlowStage(event);
        if (accepted) {
            setProgress(qMax(m_progress, event.value("percent").toInt(m_progress)));
            setPhase(event.value("phase").toString(m_phase));
        }
        if (event.contains("workspace"))
            setWorkspace(event.value("workspace").toString());
        if (accepted)
            appendLog(QStringLiteral("[%1] %2").arg(m_progress).arg(m_phase));
        emit sessionProgressUpdated(m_currentProjectId, m_liveSessionId, m_progress, m_phase, m_flowStageStates);
        appendDetailedLog(QStringLiteral("Progress\n%1").arg(QString::fromUtf8(QJsonDocument(event).toJson(QJsonDocument::Indented)).trimmed()));
        return;
    }
    if (channel == "trace" && payload.isObject()) {
        auto event = payload.toObject();
        const auto type = event.value("event").toString();
        if (type == QStringLiteral("provider_token_usage")) {
            const QJsonObject usage = event.value(QStringLiteral("usage")).toObject();
            const int measuredInput = usage.value(QStringLiteral("input_tokens")).toInt(
                usage.value(QStringLiteral("prompt_tokens")).toInt());
            if (measuredInput > 0) {
                event.insert(QStringLiteral("event"), QStringLiteral("context_usage"));
                event.insert(QStringLiteral("source"), QStringLiteral("provider_usage"));
                event.insert(QStringLiteral("context_window"), m_contextWindow);
                event.insert(QStringLiteral("effective_context_window"), m_contextEffectiveWindow);
                event.insert(QStringLiteral("auto_compact_token_limit"), m_contextAutoCompactLimit);
                event.insert(QStringLiteral("working_input_limit"), m_contextInputLimit);
                event.insert(QStringLiteral("output_token_limit"), m_contextOutputLimit);
                event.insert(QStringLiteral("measured_input_tokens"), measuredInput);
                event.insert(QStringLiteral("output_tokens"), usage.value(QStringLiteral("output_tokens")).toInt(
                    usage.value(QStringLiteral("completion_tokens")).toInt()));
                setContextUsage(event);
            }
            return;
        }
        if (type == QStringLiteral("context_usage")
            || type == QStringLiteral("model_generation_progress")
            || type == QStringLiteral("context_compacted"))
            setContextUsage(event);
        if (type == QStringLiteral("thread_title_updated"))
            emit sessionTitleUpdated(
                event.value(QStringLiteral("thread_id")).toString(),
                event.value(QStringLiteral("name")).toString());
        if (type == QStringLiteral("eda_job_started")) {
            setPhase(QStringLiteral("EDA 工具执行中，Agent 休眠"));
            appendLog(QStringLiteral("EDA 作业 %1 已启动，等待工具完成。")
                          .arg(event.value(QStringLiteral("job_id")).toString()));
        } else if (type == QStringLiteral("agent_sleeping")) {
            setPhase(QStringLiteral("等待 EDA 工具完成"));
            appendLog(QStringLiteral("Agent 已休眠，等待 %1 秒后检查 EDA 作业。")
                          .arg(event.value(QStringLiteral("wait_seconds")).toInt()));
        } else if (type == QStringLiteral("agent_awake")) {
            setPhase(QStringLiteral("EDA 工具已完成，Agent 恢复"));
        } else if (type == QStringLiteral("agent_timeout")) {
            setPhase(QStringLiteral("EDA 工具等待超时，正在中断"));
            appendLog(QStringLiteral("EDA 作业等待 %1 秒超时，运行时将请求中断。")
                          .arg(event.value(QStringLiteral("wait_seconds")).toInt()));
        } else if (type == QStringLiteral("eda_job_finished")) {
            const QString state = event.value(QStringLiteral("state")).toString();
            appendLog(QStringLiteral("EDA 作业 %1：%2")
                          .arg(event.value(QStringLiteral("job_id")).toString(), state));
        }
        if (type == QStringLiteral("agent_sleeping")) {
            appendAgentActivity(
                QStringLiteral("Agent 已休眠，等待 EDA 工具完成（作业 %1）")
                    .arg(event.value(QStringLiteral("job_id")).toString()),
                0, QStringLiteral("status"));
        } else if (type == QStringLiteral("agent_awake")) {
            appendAgentActivity(
                QStringLiteral("EDA 工具已返回，Agent 正在恢复分析"),
                0, QStringLiteral("status"));
        } else if (type == QStringLiteral("agent_timeout")) {
            appendAgentActivity(
                QStringLiteral("EDA 工具等待超时，Agent 请求中断作业"),
                0, QStringLiteral("status"));
        }
        if (type == QStringLiteral("eda_job_started")
            || type == QStringLiteral("eda_job_finished")
            || type == QStringLiteral("eda_job_interrupt_requested")
            || type == QStringLiteral("agent_sleeping")
            || type == QStringLiteral("agent_awake")
            || type == QStringLiteral("agent_timeout")) {
            updateEdaJobActivity(event);
        }
        appendTrace(event);
        if (type == QStringLiteral("terminal_diagnostic_command")) {
            const QString command = event.value(QStringLiteral("command")).toString().trimmed();
            if (!command.isEmpty())
                appendToolOutput(QStringLiteral("\n$ %1\n").arg(command));
        } else if (type == QStringLiteral("workspace_agent_action")
                   && event.value(QStringLiteral("action")).toString() == QStringLiteral("terminal")) {
            const QString command = event.value(QStringLiteral("arguments")).toObject()
                .value(QStringLiteral("command")).toString().trimmed();
            if (!command.isEmpty())
                appendToolOutput(QStringLiteral("\n$ %1\n").arg(command));
        } else if (type == QStringLiteral("tool_call")
                   && (event.value(QStringLiteral("name")).toString() == QStringLiteral("terminal_execute")
                       || event.value(QStringLiteral("name")).toString() == QStringLiteral("shell_execute"))) {
            const QString command = event.value(QStringLiteral("arguments")).toObject()
                .value(QStringLiteral("command")).toString().trimmed();
            if (!command.isEmpty())
                appendToolOutput(QStringLiteral("\n$ %1\n").arg(command));
        } else if (type == QStringLiteral("provider_tool_activity")
                   && event.value(QStringLiteral("tool_type")).toString() == QStringLiteral("commandExecution")) {
            QString command = event.value(QStringLiteral("name")).toString().trimmed();
            if (command.isEmpty())
                command = event.value(QStringLiteral("arguments")).toObject()
                    .value(QStringLiteral("command")).toString().trimmed();
            if (!command.isEmpty())
                appendToolOutput(QStringLiteral("\n$ %1\n").arg(command));
            QString output = event.value(QStringLiteral("result")).toString();
            if (output.isEmpty())
                output = displayJson(event.value(QStringLiteral("result")));
            if (!output.isEmpty())
                appendToolOutput(output);
        } else if (type == QStringLiteral("tool_result")
                   && (event.value(QStringLiteral("name")).toString() == QStringLiteral("shell_execute")
                       || event.value(QStringLiteral("name")).toString() == QStringLiteral("terminal_execute"))) {
            const QJsonObject result = event.value(QStringLiteral("result")).toObject();
            const QString command = result.value(QStringLiteral("command")).toString().trimmed();
            if (!command.isEmpty())
                appendToolOutput(QStringLiteral("\n$ %1\n").arg(command));
            const QString output = result.value(QStringLiteral("output")).toString();
            if (!output.isEmpty())
                appendToolOutput(output);
        }
        return;
    }
    if (channel == "tool_output" && payload.isObject()) {
        const QJsonObject output = payload.toObject();
        appendToolOutput(output.value("text").toString());
        appendEdaOutput(output);
        return;
    }
    if (channel == "stdout" && payload.isObject()) {
        appendDetailedLog(QStringLiteral("Worker output\n%1").arg(payload.toObject().value("text").toString()));
        return;
    }
    if (channel == "result") {
        m_receivedResult = true;
        const QJsonObject result = payload.toObject();
        const QString resultAnswer = result.value(QStringLiteral("answer")).toString();
        // Older workers returned the interruption text but omitted the
        // structured flag. Keep those sessions resumable instead of rendering
        // the text as a normal copyable assistant answer.
        const bool connectionInterrupted = result.value(QStringLiteral("connection_interrupted")).toBool()
            || resultAnswer.startsWith(QStringLiteral("Responses API 暂时不可用，已保留当前回合"));
        const QString executionState = result.value(QStringLiteral("execution_state")).toString();
        m_executionState = executionState.isEmpty() ? QStringLiteral("incomplete") : executionState;
        const bool projectRun = result.value(QStringLiteral("project_run")).toBool();
        const bool displayThisRun = m_flowDisplaySessionId.isEmpty() || m_liveSessionId.isEmpty()
            || m_flowDisplaySessionId == m_liveSessionId;
        bool hasEvidenceCall = false;
        const QJsonArray toolTrace = result.value(QStringLiteral("tool_trace")).toArray();
        for (const QJsonValue &item : toolTrace) {
            const QString name = item.toObject().value(QStringLiteral("name")).toString();
            if (name == QStringLiteral("analyze_dft_results")
                || name == QStringLiteral("inspect_latest_dft_evidence")
                || name == QStringLiteral("export_dft_deliverables")) {
                hasEvidenceCall = true;
                break;
            }
        }
        const bool planningOnly = executionState != QStringLiteral("evidence_verified");
        const bool verifiedEvidence = executionState == QStringLiteral("evidence_verified") && hasEvidenceCall;
        QString unfinishedStage;
        QString unfinishedState;
        int unfinishedRank = std::numeric_limits<int>::max();
        for (auto it = m_flowStageStates.cbegin(); it != m_flowStageStates.cend(); ++it) {
            const QString state = it.value().toMap().value(QStringLiteral("state")).toString();
            const int rank = flowStageRank(it.key());
            if (isUnresolvedFlowState(state) && rank < unfinishedRank) {
                unfinishedStage = it.key();
                unfinishedState = state;
                if (rank >= 0)
                    unfinishedRank = rank;
            }
        }
        const bool executionEvidenceIsComplete = verifiedEvidence;
        const bool flowIncomplete = projectRun
            ? (!executionEvidenceIsComplete || !unfinishedStage.isEmpty())
            : (!planningOnly && (!executionEvidenceIsComplete || !unfinishedStage.isEmpty()));
        // A late result envelope is not proof that the configured flow ended.
        // Keep the progress bar below 100 and leave the active stage visible
        // until its own verified callback arrives.
        if (displayThisRun && flowIncomplete) {
            if (unfinishedState == QStringLiteral("failed")
                || unfinishedState == QStringLiteral("blocked")
                || unfinishedState == QStringLiteral("needs_review")) {
                setHasError(true);
            }
            setProgress(qMin(99, qMax(1, m_progress)));
            const QString detail = unfinishedStage.isEmpty()
                ? (projectRun && planningOnly
                       ? QStringLiteral("Agent 尚未启动受控 DFT 工具")
                       : QStringLiteral("最终验证证据尚未记录"))
                : QStringLiteral("阶段“%1”处于 %2").arg(unfinishedStage, unfinishedState);
            appendAgentActivity(
                QStringLiteral("执行结果已返回，但%1；暂不标记流程完成。").arg(detail),
                0,
                QStringLiteral("status")
            );
        } else if (displayThisRun) {
            setProgress(100);
        }
        if (connectionInterrupted) {
            bool hasRecoveryBubble = false;
            for (auto it = m_activityEntries.crbegin(); it != m_activityEntries.crend(); ++it) {
                const QVariantMap entry = it->toMap();
                if (entry.value(QStringLiteral("kind")).toString() == QStringLiteral("reconnect")
                    && entry.value(QStringLiteral("status")).toString() == QStringLiteral("interrupted")) {
                    hasRecoveryBubble = true;
                    break;
                }
            }
            if (!hasRecoveryBubble) {
                appendActivity({
                    {QStringLiteral("kind"), QStringLiteral("reconnect")},
                    {QStringLiteral("role"), QStringLiteral("status")},
                    {QStringLiteral("status"), QStringLiteral("interrupted")},
                    {QStringLiteral("text"), QStringLiteral("Responses API 暂时不可用，已保留当前回合；可点击恢复。")},
                });
            }
            emit codexSessionRecoveryRequired(
                m_currentProjectId,
                result.value(QStringLiteral("thread_id")).toString().isEmpty()
                    ? m_liveSessionId : result.value(QStringLiteral("thread_id")).toString(),
                result.value(QStringLiteral("recovery_goal")).toString(),
                result.value(QStringLiteral("recovery_reason")).toString(QStringLiteral("provider_interrupted"))
            );
        } else {
            appendAgentActivity(
                boundedActivityText(resultAnswer),
                0,
                QStringLiteral("final")
            );
        }
        QJsonObject reportPayload = result;
        if (projectRun && planningOnly) {
            m_executionState = QStringLiteral("planning_only");
            reportPayload.insert(QStringLiteral("answer"),
                                 result.value(QStringLiteral("answer")).toString()
                                     + QStringLiteral("\n\n项目运行尚未开始：Agent 没有返回受控 DFT 工具调用或可验证报告。"));
        } else if (!planningOnly && !executionEvidenceIsComplete) {
            m_executionState = QStringLiteral("execution_blocked");
            reportPayload.insert(QStringLiteral("execution_state"), QStringLiteral("execution_blocked"));
            reportPayload.insert(QStringLiteral("answer"),
                                 result.value(QStringLiteral("answer")).toString()
                                     + QStringLiteral("\n\n最终报告尚未生成：未收到 analyze_dft_results/inspect_latest_dft_evidence 的可验证证据。"));
        }
        if (!connectionInterrupted) {
            const QString scope = projectRun ? QStringLiteral("configured") : QStringLiteral("chat");
            const QVariantMap episode{
                {QStringLiteral("framework"), QStringLiteral("dft_agent_studio.native_agent_runtime")},
                {QStringLiteral("model"), m_nativeProjectSnapshot.value(QStringLiteral("modelName"))},
                {QStringLiteral("scope"), scope},
                {QStringLiteral("project_id"), m_currentProjectId},
                {QStringLiteral("project_context"), m_nativeProjectSnapshot},
                {QStringLiteral("goal"), m_nativeGoal},
                {QStringLiteral("model_answer"), result.value(QStringLiteral("answer")).toString()},
                {QStringLiteral("answer"), reportPayload.value(QStringLiteral("answer")).toString()},
                {QStringLiteral("tool_trace"), result.value(QStringLiteral("tool_trace")).toArray().toVariantList()},
                {QStringLiteral("errors"), reportPayload.value(QStringLiteral("errors")).toArray().toVariantList()},
                {QStringLiteral("execution_state"), reportPayload.value(QStringLiteral("execution_state")).toString()},
                {QStringLiteral("session_id"), m_liveSessionId},
                {QStringLiteral("turn_id"), m_liveTurnId},
            };
            const QVariantMap episodeSaved = AgentEpisodeService::persist(
                episode, studioDataRoot(m_workingDirectory));
            if (episodeSaved.value(QStringLiteral("ok")).toBool()) {
                const QVariantMap episodeRecord = episodeSaved.value(QStringLiteral("result")).toMap();
                reportPayload.insert(QStringLiteral("episode_id"), episodeRecord.value(QStringLiteral("episode_id")).toString());
                reportPayload.insert(QStringLiteral("episode_file"), episodeRecord.value(QStringLiteral("path")).toString());
                const QString memoryWarning = episodeRecord.value(QStringLiteral("memory_warning")).toString();
                if (!memoryWarning.isEmpty())
                    appendDetailedLog(QStringLiteral("Episode saved, but memory indexing failed: %1").arg(memoryWarning));
            } else {
                appendDetailedLog(QStringLiteral("Unable to persist native Agent episode: %1")
                                      .arg(episodeSaved.value(QStringLiteral("message")).toString()));
            }
        }
        if (displayThisRun)
            updateReport(reportPayload);
        if (displayThisRun && flowIncomplete) {
            setPhase(unfinishedStage.isEmpty()
                         ? QStringLiteral("Execution incomplete: final evidence not recorded")
                         : QStringLiteral("Execution incomplete: %1 (%2)").arg(unfinishedStage, unfinishedState));
        } else if (displayThisRun && planningOnly) {
            updateFlowStage(QJsonObject{
                {QStringLiteral("phase"), QStringLiteral("api_completed")},
                {QStringLiteral("stage"), QStringLiteral("executor")},
                {QStringLiteral("stage_state"), m_hasError ? QStringLiteral("failed") : QStringLiteral("completed")},
                {QStringLiteral("stage_percent"), 100},
                {QStringLiteral("substep"), QStringLiteral("agent_plan")},
            });
        } else if (displayThisRun) {
            updateFlowStage(QJsonObject{
                {QStringLiteral("phase"), QStringLiteral("completed")},
                {QStringLiteral("stage"), QStringLiteral("report")},
                {QStringLiteral("stage_state"), m_hasError ? QStringLiteral("failed") : QStringLiteral("completed")},
                {QStringLiteral("stage_percent"), 100},
                {QStringLiteral("substep"), QStringLiteral("final_report")},
            });
        }
        if (displayThisRun && !flowIncomplete) {
            setPhase(m_hasError ? QStringLiteral("Completed with errors")
                     : planningOnly ? QStringLiteral("Plan ready") : QStringLiteral("Evidence recorded"));
        }
        setResult(QString::fromUtf8(QJsonDocument(result).toJson(QJsonDocument::Indented)));
        appendDetailedLog(QStringLiteral("Result\n%1").arg(QString::fromUtf8(QJsonDocument(result).toJson(QJsonDocument::Indented)).trimmed()));
        appendLog(QStringLiteral("Agent result recorded."));
        emit finished(!m_hasError && !flowIncomplete);
        return;
    }
    if (channel == "failure" && payload.isObject()) {
        m_receivedResult = true;
        m_executionState = QStringLiteral("failed");
        const QJsonObject failure = payload.toObject();
        setPhase(failure.value(QStringLiteral("recovery_pending")).toBool()
                     ? QStringLiteral("连接中断，可恢复") : QStringLiteral("Failed"));
        const QString message = failure.value("message").toString();
        markActiveFlowStageFailed();
        if (failure.value(QStringLiteral("recovery_pending")).toBool()) {
            appendActivity({
                {QStringLiteral("kind"), QStringLiteral("reconnect")},
                {QStringLiteral("role"), QStringLiteral("status")},
                {QStringLiteral("status"), QStringLiteral("interrupted")},
                {QStringLiteral("text"), QStringLiteral("Responses API 暂时不可用，已保留当前回合；可点击恢复。")},
                {QStringLiteral("step"), 0},
            });
            emit codexSessionRecoveryRequired(
                m_currentProjectId,
                m_liveSessionId,
                failure.value(QStringLiteral("recovery_goal")).toString(),
                failure.value(QStringLiteral("recovery_reason")).toString(QStringLiteral("provider_error"))
            );
        } else {
            appendActivity({
                {QStringLiteral("kind"), QStringLiteral("error")},
                {QStringLiteral("text"), message},
                {QStringLiteral("step"), 0},
            });
        }
        appendLog(message);
        appendDetailedLog(QStringLiteral("Failure\n%1").arg(message));
        if (!failure.value(QStringLiteral("recovery_pending")).toBool())
            setFailureReport(message);
        emit errorOccurred(message);
        emit finished(false);
        return;
    }
    const QString text = QString::fromUtf8(line);
    appendLog(text);
    appendDetailedLog(QStringLiteral("Worker output\n%1").arg(text));
}
#endif

void AgentController::startDemo(const QVariantMap &project) {
    m_demoStep = 0;
    m_demoPhases = {
        "source_discovery",
        "model_tool_selection",
        "dc_shell",
        "read_link_completed",
        "constraints_applied",
        "compile_optimization",
        "evidence_written",
    };
    appendLog(QStringLiteral("Demo mode enabled for %1.").arg(project.value("name").toString()));
    setWorkspace(QStringLiteral("/tmp/dft-agent-studio-demo/workspace"));
    m_demoTimer.start(420);
}
