#pragma once

#include "responsesroundclient.h"
#include "responsestooltranscript.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QVector>
#include <functional>

class ResponsesTurnRunner final : public QObject {
    Q_OBJECT
public:
    using ToolResult = std::function<void(QJsonObject)>;
    using ToolExecutor = std::function<void(const QString &, const QJsonObject &,
                                            const QString &, ToolResult)>;

    struct Request {
        QString baseUrl;
        QString apiKey;
        QString model;
        QString protocol = QStringLiteral("responses");
        QString reasoningEffort;
        QString instructions;
        QJsonArray input;
        QJsonArray tools;
        QJsonObject samplingOptions;
        int maximumToolRounds = 500;
        int timeoutMs = 1'800'000;
        int reconnectMaxAttempts = 10;
        int reconnectDelayMs = 1'000;
        int contextWindow = 65'536;
        int effectiveContextPercent = 92;
        int maximumOutputTokens = 4'096;
        QString artifactRoot;
    };

    explicit ResponsesTurnRunner(QObject *parent = nullptr);
    bool running() const;
    bool paused() const;
    void start(const Request &request, ToolExecutor executeTool);
    bool steer(const QString &message);
    void pause();
    void resume();
    void cancel();

signals:
    void modelEvent(const QJsonObject &event);
    void toolStarted(const QString &name, const QString &callId, const QJsonObject &arguments);
    void toolFinished(const QString &name, const QString &callId, const QJsonObject &result);
    void completed(const QString &answer, int toolRounds);
    void failed(const QString &error, int toolRounds);

private:
    struct FunctionCall {
        QString id;
        QString name;
        QJsonObject arguments;
        QString argumentError;
    };

    ResponsesRoundClient m_client;
    Request m_request;
    ToolExecutor m_executeTool;
    ResponsesToolTranscript m_transcript;
    QJsonArray m_lastEvents;
    QVector<FunctionCall> m_calls;
    QVector<QJsonObject> m_toolResults;
    QJsonArray m_toolOutputs;
    QStringList m_pendingSteering;
    QString m_answer;
    QString m_gatewayChatModeBaseUrl;
    int m_toolRounds = 0;
    int m_completedTools = 0;
    int m_emptyOutputBudgetContinuations = 0;
    bool m_parallelBatch = false;
    bool m_running = false;
    bool m_paused = false;

    void requestModelRound();
    void handleRound(const QJsonArray &events, int statusCode, const QString &error);
    void executeNextTool(int index);
    void finishTool(int index, const FunctionCall &call, const QJsonObject &result);
    void continueAfterTools();
    bool appendPendingSteering();
    void finishWithError(const QString &error);
    static QVector<FunctionCall> functionCalls(const QJsonArray &events);
    static QString outputText(const QJsonArray &events);
};
