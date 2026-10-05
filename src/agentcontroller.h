#pragma once

#include <QObject>
#include <QJsonObject>
#include <QHash>
#include <QList>
#include <QProcess>
#include <QPointer>
#include <QNetworkAccessManager>
#include <QStringList>
#include <QTimer>
#include <QVariantMap>
#include <QHash>

class ResponsesTurnRunner;

class QNetworkReply;
class ResponsesRoundClient;

class AgentController final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool running READ running NOTIFY runningChanged)
    Q_PROPERTY(bool paused READ paused NOTIFY pausedChanged)
    Q_PROPERTY(int progress READ progress NOTIFY progressChanged)
    Q_PROPERTY(QString phase READ phase NOTIFY phaseChanged)
    Q_PROPERTY(QString workspace READ workspace NOTIFY workspaceChanged)
    Q_PROPERTY(QString log READ log NOTIFY logChanged)
    Q_PROPERTY(QString detailedLog READ detailedLog NOTIFY detailedLogChanged)
    Q_PROPERTY(QString toolOutput READ toolOutput NOTIFY toolOutputChanged)
    Q_PROPERTY(QVariantList activityEntries READ activityEntries NOTIFY activityEntriesChanged)
    Q_PROPERTY(QVariantMap flowStageStates READ flowStageStates NOTIFY flowStageStatesChanged)
    Q_PROPERTY(QString result READ result NOTIFY resultChanged)
    Q_PROPERTY(QString report READ report NOTIFY reportChanged)
    Q_PROPERTY(QVariantList reportFiles READ reportFiles NOTIFY reportFilesChanged)
    Q_PROPERTY(bool hasError READ hasError NOTIFY hasErrorChanged)
    Q_PROPERTY(bool requiresSupervisorReview READ requiresSupervisorReview NOTIFY requiresSupervisorReviewChanged)
    Q_PROPERTY(QString terminalOutput READ terminalOutput NOTIFY terminalOutputChanged)
    Q_PROPERTY(bool terminalRunning READ terminalRunning NOTIFY terminalRunningChanged)
    Q_PROPERTY(QString intelligenceProvider READ intelligenceProvider NOTIFY intelligenceProviderChanged)
#ifdef DFT_AGENT_STUDIO_TESTING
    Q_PROPERTY(QString testWorkerExecutable READ testWorkerExecutable WRITE setTestWorkerExecutable NOTIFY testWorkerExecutableChanged)
#endif
    Q_PROPERTY(bool demoMode READ demoMode WRITE setDemoMode NOTIFY demoModeChanged)
    Q_PROPERTY(bool detailedMode READ detailedMode WRITE setDetailedMode NOTIFY detailedModeChanged)
    Q_PROPERTY(int contextWindow READ contextWindow NOTIFY contextUsageChanged)
    Q_PROPERTY(int contextEffectiveWindow READ contextEffectiveWindow NOTIFY contextUsageChanged)
    Q_PROPERTY(int contextAutoCompactLimit READ contextAutoCompactLimit NOTIFY contextUsageChanged)
    Q_PROPERTY(int contextInputTokens READ contextInputTokens NOTIFY contextUsageChanged)
    Q_PROPERTY(int contextInputLimit READ contextInputLimit NOTIFY contextUsageChanged)
    Q_PROPERTY(int contextSystemTokens READ contextSystemTokens NOTIFY contextUsageChanged)
    Q_PROPERTY(int contextHistoryTokens READ contextHistoryTokens NOTIFY contextUsageChanged)
    Q_PROPERTY(int contextToolTokens READ contextToolTokens NOTIFY contextUsageChanged)
    Q_PROPERTY(int contextToolLimit READ contextToolLimit NOTIFY contextUsageChanged)
    Q_PROPERTY(int contextOutputTokens READ contextOutputTokens NOTIFY contextUsageChanged)
    Q_PROPERTY(int contextOutputLimit READ contextOutputLimit NOTIFY contextUsageChanged)
    Q_PROPERTY(bool contextCompacted READ contextCompacted NOTIFY contextUsageChanged)
    Q_PROPERTY(bool contextProviderMeasured READ contextProviderMeasured NOTIFY contextUsageChanged)
    Q_PROPERTY(int queuedPromptCount READ queuedPromptCount NOTIFY queuedPromptCountChanged)
    Q_PROPERTY(QVariantList queuedPrompts READ queuedPrompts NOTIFY queuedPromptsChanged)
    Q_PROPERTY(QVariantList codexModels READ codexModels NOTIFY codexModelsChanged)
    Q_PROPERTY(bool codexModelsLoading READ codexModelsLoading NOTIFY codexModelsLoadingChanged)

public:
    explicit AgentController(QString workingDirectory, QObject *parent = nullptr);
    ~AgentController() override;

    bool running() const;
    bool paused() const;
    int progress() const;
    QString phase() const;
    QString workspace() const;
    QString log() const;
    QString detailedLog() const;
    QString toolOutput() const;
    QVariantList activityEntries() const;
    QVariantMap flowStageStates() const;
    QString result() const;
    QString report() const;
    QVariantList reportFiles() const;
    bool hasError() const;
    bool requiresSupervisorReview() const;
    QString terminalOutput() const;
    bool terminalRunning() const;
    QString intelligenceProvider() const;
#ifdef DFT_AGENT_STUDIO_TESTING
    QString testWorkerExecutable() const;
    void setTestWorkerExecutable(const QString &value);
#endif
    void setModelCatalogPath(const QString &value);
    bool demoMode() const;
    void setDemoMode(bool value);
    bool detailedMode() const;
    void setDetailedMode(bool value);
    int contextWindow() const;
    int contextEffectiveWindow() const;
    int contextAutoCompactLimit() const;
    int contextInputTokens() const;
    int contextInputLimit() const;
    int contextSystemTokens() const;
    int contextHistoryTokens() const;
    int contextToolTokens() const;
    int contextToolLimit() const;
    int contextOutputTokens() const;
    int contextOutputLimit() const;
    bool contextCompacted() const;
    bool contextProviderMeasured() const;
    int queuedPromptCount() const;
    QVariantList queuedPrompts() const;
    QVariantList codexModels() const;
    bool codexModelsLoading() const;
    void configureContextUsage(int contextWindow, int inputTokenLimit, int outputTokenLimit);

    Q_INVOKABLE void run(const QVariantMap &project, const QStringList &disabledCapabilities);
    Q_INVOKABLE void runInBackground(const QVariantMap &project, const QStringList &disabledCapabilities);
    Q_INVOKABLE void showDesktopNotification(const QString &title, const QString &message);
    Q_INVOKABLE void runPrompt(const QVariantMap &project, const QString &prompt, const QStringList &disabledCapabilities);
    Q_INVOKABLE void runPromptNow(const QVariantMap &project, const QString &prompt, const QStringList &disabledCapabilities);
    Q_INVOKABLE bool editQueuedPrompt(const QString &queueId, const QString &prompt);
    Q_INVOKABLE bool removeQueuedPrompt(const QString &queueId);
    Q_INVOKABLE bool steerQueuedPrompt(const QString &queueId);
    Q_INVOKABLE void pause();
    Q_INVOKABLE void resume();
    Q_INVOKABLE void stop();
    Q_INVOKABLE void clearLog();
    Q_INVOKABLE void clearToolOutput();
    Q_INVOKABLE QVariantMap flowStageRecord(const QString &key) const;
    Q_INVOKABLE bool openProjectTerminal(const QString &workingDirectory);
    Q_INVOKABLE void runDebugCommand(const QString &workingDirectory, const QString &command);
    Q_INVOKABLE void clearTerminal();
    Q_INVOKABLE QString readPatchDiff(const QString &patchFile) const;
    Q_INVOKABLE QString readPatchDiffHtml(const QString &patchFile, bool darkMode = false) const;
    Q_INVOKABLE QString renderPatchDiffHtml(const QString &diff, bool darkMode = false) const;
    Q_INVOKABLE QString readReport(const QString &reportFile) const;
    Q_INVOKABLE QVariantMap readReportPage(const QString &reportFile, qint64 beforeOffset = -1,
                                           qint64 maximumBytes = 65536) const;
    Q_INVOKABLE QVariantMap readReportRange(const QString &reportFile, qint64 fromOffset,
                                            qint64 maximumBytes = 65536) const;
    Q_INVOKABLE QStringList edaLogFiles(const QString &workspace) const;
    Q_INVOKABLE QString renderMarkdown(const QString &markdown) const;
    Q_INVOKABLE QString renderPlainText(const QString &markup) const;
    Q_INVOKABLE void copyText(const QString &text);
    Q_INVOKABLE QVariantMap decidePatch(const QString &proposalId, bool approved);
    Q_INVOKABLE QVariantMap rollbackFileEdit(const QString &editId, const QString &projectId, const QString &projectRoot);
    Q_INVOKABLE QVariantMap workspaceAction(const QVariantMap &project, const QString &action, const QVariantMap &payload = {});
    Q_INVOKABLE void workspaceActionAsync(const QString &requestId, const QVariantMap &project,
                                          const QString &action, const QVariantMap &payload = {});
    Q_INVOKABLE void setActivitySession(const QString &sessionId);
    Q_INVOKABLE void updatePathPermissionEntry(const QString &requestId, const QString &status);
    Q_INVOKABLE QVariantMap decideNativeTool(const QString &requestId, bool approved);
    Q_INVOKABLE void loadFlowProgressSnapshot(int progress, const QString &phase, const QVariantMap &stages);
    Q_INVOKABLE void setFlowDisplaySession(const QString &sessionId);
    Q_INVOKABLE bool sessionRunning(const QString &sessionId) const;
    Q_INVOKABLE bool sessionPaused(const QString &sessionId) const;
    Q_INVOKABLE void stopSession(const QString &sessionId);
    Q_INVOKABLE void loadSessionActivity(const QVariantList &entries, bool prepend = false);
    Q_INVOKABLE void refreshCodexModels(const QString &baseUrl, const QString &apiKeyFile = {}, const QString &apiKey = {});

signals:
    void runningChanged();
    void pausedChanged();
    void progressChanged();
    void phaseChanged();
    void workspaceChanged();
    void logChanged();
    void detailedLogChanged();
    void toolOutputChanged();
    void activityEntriesChanged();
    void sessionTitleUpdated(const QString &threadId, const QString &name);
    void flowStageStatesChanged();
    void resultChanged();
    void reportChanged();
    void reportFilesChanged();
    void hasErrorChanged();
    void requiresSupervisorReviewChanged();
    void terminalOutputChanged();
    void terminalRunningChanged();
    void intelligenceProviderChanged();
#ifdef DFT_AGENT_STUDIO_TESTING
    void testWorkerExecutableChanged();
#endif
    void demoModeChanged();
    void detailedModeChanged();
    void contextUsageChanged();
    void queuedPromptCountChanged();
    void queuedPromptsChanged();
    void codexModelsChanged();
    void codexModelsLoadingChanged();
    void codexSessionReady(const QString &projectId, const QString &sessionId);
    void codexSessionRecoveryRequired(const QString &projectId, const QString &sessionId,
                                      const QString &goal, const QString &reason);
    void errorOccurred(const QString &message);
    void finished(bool successful);
    void parallelSessionStarted(const QString &projectId, const QString &sessionId);
    void parallelSessionFinished(const QString &projectId, const QString &sessionId, bool successful);
    void sessionProgressUpdated(const QString &projectId, const QString &sessionId,
                                int progress, const QString &phase, const QVariantMap &stages);
    void workspaceActionCompleted(const QString &requestId, const QVariantMap &response);

private:
    QString validatedReportPath(const QString &reportFile) const;
    void scheduleSessionProgressPersistence(bool immediate = false);
    void persistSessionProgress(bool final = false);
#ifdef DFT_AGENT_STUDIO_TESTING
    bool recoverStagedProjectEvidence();
    bool finishRecovery(bool verified, const QString &answer);
#endif
    struct QueuedPrompt {
        QString id;
        QVariantMap project;
        QStringList disabledCapabilities;
        QString prompt;
        QString status = QStringLiteral("queued");
    };

    QString m_workingDirectory;
#ifdef DFT_AGENT_STUDIO_TESTING
    QString m_testWorkerExecutable;
#endif
    QString m_intelligenceProvider = QStringLiteral("api");
    QString m_modelCatalogPath;
#ifdef DFT_AGENT_STUDIO_TESTING
    QProcess m_process;
#endif
    QProcess m_terminalProcess;
    QProcess m_localModelProcess;
    QByteArray m_localModelSignature;
    QString m_localModelLogTail;
    ResponsesTurnRunner *m_nativeTurnRunner = nullptr;
    QString m_nativeToolRuntimeId;
    int m_nativeToolStep = 0;
    QHash<QString, int> m_nativeToolSteps;
    QVariantMap m_nativeProjectSnapshot;
    QStringList m_nativeDisabledCapabilities;
    QString m_nativeGoal;
#ifdef DFT_AGENT_STUDIO_TESTING
    QHash<QString, ResponsesRoundClient *> m_nativeResponseRequests;
#endif
    QTimer m_demoTimer;
    QTimer m_sessionProgressPersistenceTimer;
    QStringList m_demoPhases;
    int m_demoStep = 0;
    bool m_running = false;
    bool m_suppressProgressPersistence = false;
    bool m_paused = false;
    bool m_demoMode = false;
    bool m_detailedMode = false;
    bool m_hasError = false;
    bool m_requiresSupervisorReview = false;
    int m_progress = 0;
    QString m_executionState = QStringLiteral("idle");
    QString m_phase = "Ready";
    QString m_workspace;
    QString m_log;
    QString m_detailedLog;
    QString m_toolOutput;
    bool m_toolOutputNotifyPending = false;
    QVariantList m_activityEntries;
    QVariantMap m_flowStageStates;
    QString m_activeFlowStage;
    QString m_result;
    QString m_report;
    QStringList m_errorMessages;
    QString m_terminalOutput;
    int m_contextWindow = 65'536;
    int m_contextEffectiveWindow = 62'259;
    int m_contextAutoCompactLimit = 58'982;
    int m_contextInputTokens = 0;
    int m_contextInputLimit = 16'384;
    int m_contextSystemTokens = 0;
    int m_contextHistoryTokens = 0;
    int m_contextToolTokens = 0;
    int m_contextToolLimit = 8'192;
    int m_contextOutputTokens = 0;
    int m_contextOutputLimit = 4'096;
    int m_defaultContextWindow = 65'536;
    int m_defaultContextInputLimit = 16'384;
    int m_defaultContextOutputLimit = 4'096;
    bool m_contextCompacted = false;
    bool m_contextProviderMeasured = false;
    bool m_contextPlanInitialized = false;
#ifdef DFT_AGENT_STUDIO_TESTING
    QByteArray m_buffer;
#endif
    bool m_receivedResult = false;
#ifdef DFT_AGENT_STUDIO_TESTING
    bool m_recoveryAttempted = false;
    bool m_interruptionPending = false;
#endif
    bool m_liveSteeringEnabled = false;
    QString m_currentProjectId;
    // The worker may continue a turn while the user views another durable
    // session.  Keep those live events from being appended to the selected
    // session until the user switches back.
    QString m_liveSessionId;
    QString m_liveTurnId;
    QString m_activitySessionId;
    QString m_flowDisplaySessionId;
    QList<AgentController *> m_parallelRuns;
    QList<QueuedPrompt> m_queuedPrompts;
    QNetworkAccessManager m_network;
    QPointer<QNetworkReply> m_codexModelsReply;
    QVariantList m_codexModels;
    bool m_codexModelsLoading = false;
    quint64 m_codexModelsRequestGeneration = 0;
    QString m_codexModelsRequestKey;
    // Chat delegates are recycled while scrolling. Keep recently rendered
    // Markdown fragments so revisiting a long answer does not rebuild a
    // QTextDocument on every delegate assignment.
    mutable QHash<QString, QString> m_markdownRenderCache;

    void setRunning(bool value);
    void startParallelRun(const QVariantMap &project, const QStringList &disabledCapabilities);
    void setProgress(int value);
    void setPhase(const QString &value);
    void setWorkspace(const QString &value);
    void appendLog(const QString &line);
    void appendDetailedLog(const QString &line);
    void appendToolOutput(const QString &text);
    void appendActivity(const QVariantMap &entry);
    bool shouldAppendLiveActivity() const;
    void appendAgentActivity(const QString &text, int step, const QString &role, bool mergeDelta = false,
                             const QString &streamId = QString());
    void appendToolActivity(const QString &name, const QString &arguments, int step);
    void completeToolActivity(const QString &name, const QString &result, int step, bool failed = false);
    void updateEdaJobActivity(const QJsonObject &event);
    void appendEdaOutput(const QJsonObject &event);
    bool updateFlowStage(const QJsonObject &event);
    void updateAgentActivityStage(const QJsonObject &event);
    void markActiveFlowStageFailed();
    void resetFlowStages();
    void setResult(const QString &value);
    void setReport(const QString &value);
    void setHasError(bool value);
    void setRequiresSupervisorReview(bool value);
    void appendTerminalOutput(const QString &value);
    void resetContextUsage();
    void setContextUsage(const QJsonObject &event);
#ifdef DFT_AGENT_STUDIO_TESTING
    void processOutput();
    void consumeLine(const QByteArray &line);
#endif
    void startDemo(const QVariantMap &project);
    void appendTrace(const QJsonObject &event);
    void persistSessionEvent(QJsonObject event);
    void updateReport(const QJsonObject &payload);
    void setFailureReport(const QString &message);
    void startRun(
        const QVariantMap &project,
        const QStringList &disabledCapabilities,
        const QString &prompt,
        bool clearConversation,
        bool promptAlreadyRecorded = false
    );
    bool startNativeResponsesRun(const QVariantMap &project, const QStringList &disabledCapabilities,
                                 const QString &goal);
    void handleNativeResponsesEvent(const QJsonObject &event);
    void finishNativeResponsesRun(const QString &answer, int toolRounds);
    void startNextQueuedPrompt();
    void interruptForQueuedPrompt();
    bool sendSteeringToWorker(const QString &prompt, const QString &steeringId);
    void notifyQueuedPromptsChanged();
    void updateQueuedActivity(const QString &queueId, const QString &prompt, const QString &status);
    bool hasActiveEdaJob() const;
};
