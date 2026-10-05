#pragma once

#include <QObject>
#include <QJsonArray>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QMap>
#include <QUrl>

class QTimer;

class ResponsesRoundClient final : public QObject {
    Q_OBJECT
public:
    explicit ResponsesRoundClient(QObject *parent = nullptr);
    bool running() const;
    void start(const QString &baseUrl, const QString &apiKey,
               const QJsonObject &payload, int timeoutMs = 1'800'000,
               int reconnectMaxAttempts = 10, int reconnectDelayMs = 1'000);
    void startChatCompletions(const QString &baseUrl, const QString &apiKey,
                              const QJsonObject &payload, int timeoutMs = 1'800'000,
                              int reconnectMaxAttempts = 10, int reconnectDelayMs = 1'000);
    void startEndpoint(const QString &endpointUrl, const QString &apiKey,
                       const QJsonObject &payload, int timeoutMs = 1'800'000);
    void cancel();

signals:
    void eventReceived(const QJsonObject &event);
    void retrying(int attempt, int maximumAttempts, int statusCode, const QString &reason);
    void streamReset();
    void reconnected(int attempts);
    void completed(const QJsonArray &events, int statusCode, const QString &error);

private:
    QNetworkAccessManager m_network;
    QNetworkReply *m_reply = nullptr;
    QTimer *m_timeout = nullptr;
    QTimer *m_retryTimer = nullptr;
    QUrl m_url;
    QByteArray m_apiKey;
    QByteArray m_requestBody;
    QByteArray m_pending;
    QByteArray m_nonStreamBody;
    QJsonArray m_events;
    QMap<int, QJsonObject> m_chatToolCalls;
    QString m_chatOutputText;
    QString m_chatReasoningText;
    QString m_visibleContent;
    QString m_lastContentFragment;
    QString m_visibleReasoning;
    QString m_lastReasoningFragment;
    QJsonObject m_chatUsage;
    bool m_chatCompletions = false;
    bool m_sawSseFrame = false;
    bool m_sawTerminalResponse = false;
    int m_timeoutMs = 1'800'000;
    int m_reconnectMaxAttempts = 10;
    int m_reconnectDelayMs = 1'000;
    int m_attempt = 0;
    bool m_cancelled = false;

    void postAttempt();
    void consumeAvailable();
    void consumeLine(QByteArray line);
    void consumeChatChunk(const QJsonObject &chunk);
    QString normalizeStreamFragment(const QString &channel, const QString &value);
    QJsonObject chatCompletedEvent(const QJsonObject &response = {}) const;
    void emitChatCompletionResponse(const QJsonObject &response);
    void emitEvent(const QJsonObject &event, bool normalizeDelta = true);
    void finish();
    void failBeforeStart(const QString &error);
};
