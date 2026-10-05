#pragma once

#include "responsesturnrunner.h"

#include <QJsonObject>
#include <QObject>
#include <QQueue>
#include <QHash>
#include <QVariantMap>
#include <functional>

class NativeSubagentService final : public QObject {
    Q_OBJECT
public:
    struct Task {
        QVariantMap project;
        QString agentRoot;
        QString parentSessionId;
        QString task;
        QString role = QStringLiteral("researcher");
        QString name;
        QString specialty;
        QString context;
        QString targetChildSessionId;
        ResponsesTurnRunner::Request turn;
    };

    using ToolExecutor = ResponsesTurnRunner::ToolExecutor;

    explicit NativeSubagentService(QObject *parent = nullptr);
    ~NativeSubagentService() override;

    void setMaximumParallelTurns(int maximum);
    int maximumParallelTurns() const;
    int activeTurnCount() const;
    int queuedTaskCount() const;

    // Returns a stable task handle immediately; results arrive asynchronously.
    QString submit(const Task &task, ToolExecutor executeTool);
    bool cancel(const QString &taskHandle);

signals:
    void parentEvent(const QString &parentSessionId, const QJsonObject &event);
    void resultReady(const QString &parentSessionId, const QJsonObject &result);

private:
    struct PendingTask {
        QString handle;
        Task task;
        ToolExecutor executeTool;
        QString childSessionId;
        QString childName;
        QString taskKey;
        QString turnId;
        QString startedAt;
        bool isFollowup = false;
        QJsonObject childThread;
    };
    struct ActiveTask {
        PendingTask pending;
        ResponsesTurnRunner *runner = nullptr;
    };

    int m_maximumParallelTurns = 4;
    QQueue<PendingTask> m_queue;
    QHash<QString, ActiveTask *> m_active;

    bool prepare(PendingTask &pending, QString *error);
    void pump();
    void launch(PendingTask pending);
    void finish(const QString &handle, const QString &status, const QString &answer,
                const QString &error, int toolRounds);
    bool persistThread(const PendingTask &pending, const QJsonObject &thread, QString *error) const;
    static QString taskKey(const QString &role, const QString &task);
    static QString makeName(const QString &role, const QString &specialty, const QString &childId);
    static QJsonArray inheritedTools(const QJsonArray &tools);
};
