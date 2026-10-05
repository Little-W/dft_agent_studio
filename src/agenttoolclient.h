#pragma once

#include <QObject>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>

class QTimer;

class AgentToolClient final : public QObject {
    Q_OBJECT
public:
    struct Request {
        QString baseUrl;
        QString apiKey;
        QString model;
        QString toolName;
        QString toolDescription;
        QJsonObject parameters;
        QJsonObject options;
        QJsonObject agentOptions;
        QJsonArray messages;
        QJsonArray tools;
        bool allowFinalAnswer = false;
        int timeoutMs = 30'000;
        int maxTokens = 4096;
        double temperature = 0.6;
    };

    explicit AgentToolClient(QObject *parent = nullptr);
    bool running() const;
    void start(const Request &request);
    void startConversation(const Request &request);
    void cancel();

signals:
    void completed(const QJsonObject &arguments, const QString &error);
    void conversationCompleted(const QJsonObject &message, const QString &error);

private:
    QNetworkAccessManager m_network;
    QNetworkReply *m_reply = nullptr;
    QTimer *m_timeout = nullptr;
    QString m_expectedToolName;
    QJsonObject m_expectedParameters;
    QHash<QString, QJsonObject> m_expectedTools;
    bool m_conversationRequest = false;
    bool m_allowFinalAnswer = false;

    void finish(QNetworkReply *reply);
    void begin(const Request &request, bool conversationRequest);
    static bool validate(const QJsonValue &value, const QJsonObject &schema, const QString &path, QString *error);
};
