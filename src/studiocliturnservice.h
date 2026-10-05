#pragma once

#include "responsesturnrunner.h"

#include <QObject>
#include <QJsonArray>
#include <QJsonObject>
#include <QDateTime>
#include <QPointer>
#include <QVariantMap>

class StudioCliTurnService final : public QObject {
    Q_OBJECT
public:
    struct Request {
        QVariantMap project;
        QString agentRoot;
        QString sessionId;
        bool createNewSession = false;
        QString sessionName;
        QString goal;
        QString permissionMode = QStringLiteral("workspace");
        bool multiAgent = false;
        QStringList disabledTools;
        QString baseUrl;
        QString apiKey;
        QString apiKeyFile;
        QString model;
        QString reasoningEffort = QStringLiteral("medium");
        QJsonObject samplingOptions;
        int contextWindow = 65'536;
        int effectiveContextPercent = 92;
        int maximumOutputTokens = 4'096;
        int maximumToolRounds = 500;
        int timeoutMs = 1'800'000;
        int reconnectMaxAttempts = 10;
        int reconnectDelayMs = 1'000;
    };

    explicit StudioCliTurnService(QObject *parent = nullptr);
    ~StudioCliTurnService() override;

    QVariantMap start(const Request &request);
    bool steer(const QString &message);
    QVariantMap decideTool(const QString &requestId, bool approved);
    void pause();
    void resume();
    void cancel();
    bool running() const;
    QString sessionId() const;

signals:
    void activity(const QString &sessionId, const QVariantMap &event);
    void completed(const QString &sessionId, const QVariantMap &result);
    void failed(const QString &sessionId, const QVariantMap &error);

private:
    Request m_request;
    QPointer<ResponsesTurnRunner> m_runner;
    QString m_sessionId;
    QString m_runtimeId;
    QString m_turnId;
    QJsonObject m_thread;
    QJsonArray m_turnEvents;
    QJsonArray m_turnTools;
    QDateTime m_startedAt;
    bool m_finishing = false;

    QVariantMap loadOrCreateSession(const Request &request);
    QVariantMap prepareThread(const QString &goal);
    bool saveThread(QString *error = nullptr);
    void persistActivity(const QJsonObject &event);
    void persistProgress(const QString &status, const QString &state,
                         const QString &phase, int progress, bool final = false);
    void finish(const QString &status, const QString &answer,
                const QString &error, int toolRounds);
};
