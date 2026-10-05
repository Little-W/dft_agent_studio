#pragma once

#include "agenttoolclient.h"

#include <QObject>
#include <QVariantMap>

class ProjectConfigImporter final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool running READ running NOTIFY runningChanged)
    Q_PROPERTY(QString stage READ stage NOTIFY stateChanged)
    Q_PROPERTY(QString sourcePath READ sourcePath NOTIFY stateChanged)
    Q_PROPERTY(QString status READ status NOTIFY stateChanged)
    Q_PROPERTY(QString error READ error NOTIFY stateChanged)
    Q_PROPERTY(QVariantMap result READ result NOTIFY resultChanged)

public:
    explicit ProjectConfigImporter(QString workingDirectory, QObject *parent = nullptr);
    ~ProjectConfigImporter() override;

    bool running() const;
    QString stage() const;
    QString sourcePath() const;
    QString status() const;
    QString error() const;
    QVariantMap result() const;

    void setModelCatalogPath(const QString &value);

    Q_INVOKABLE void start(const QString &stage, const QString &tclPath, const QString &modelId = {});
    Q_INVOKABLE void cancel();

signals:
    void runningChanged();
    void stateChanged();
    void resultChanged();
    void completed(bool successful);

private:
    QString m_workingDirectory;
    QString m_modelCatalogPath;
    QString m_stage;
    QString m_sourcePath;
    QString m_status;
    QString m_error;
    QVariantMap m_result;
    AgentToolClient m_toolClient;
    bool m_running = false;

    void setRunning(bool value);
    void setStatus(const QString &value);
    void setError(const QString &value);
    void finish(const QJsonObject &arguments, const QString &error);
};
