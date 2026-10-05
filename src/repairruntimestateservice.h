#pragma once

#include <QHash>
#include <QMutex>
#include <QString>
#include <QVariantMap>

// Durable orchestration state for one explicit repair turn. Fingerprint
// manifests are produced by the caller; this service owns their lifecycle.
class RepairRuntimeStateService final {
public:
    struct Limits {
        int toolCalls = 16'384;
        int stalledCalls = 32;
        int investigationCalls = 16'384;
        int edaRuns = 500;
    };

    explicit RepairRuntimeStateService(QVariantMap state, QString artifactRoot);
    RepairRuntimeStateService(QVariantMap state, QString artifactRoot, Limits limits);

    static QVariantMap defaultState();
    static QVariantMap loadCheckpoint(const QString &path, QString *error = nullptr);

    // A new explicit user turn clears a prior stop, while preserving evidence,
    // issues, input fingerprints, changes, and the last controlled run.
    QVariantMap beginTurn();

    // inputSnapshot fields: base, key, project_root, complete, missing, options.
    // Returns {allowed:true} or the same guard result shape consumed by tools.
    QVariantMap beforeAction(const QString &requestId, const QString &tool,
                             const QVariantMap &arguments,
                             const QVariantMap &inputSnapshot);

    // observation may provide evidence_id/tool/path/ranges/has_body/summary;
    // otherwise a stable evidence identity is derived from the result.
    QVariantMap afterAction(const QString &requestId, const QVariantMap &result,
                            const QVariantMap &afterSnapshot,
                            QVariantMap observation = {});

    QVariantMap recordIssue(const QVariantMap &arguments);
    QString executionState(const QVariantMap &currentSnapshot) const;
    QVariantMap state() const;
    QString checkpointPath() const;
    bool persist(QString *error = nullptr) const;

private:
    QVariantMap pause(const QString &reason);
    QVariantMap makeObservation(const QString &tool, const QVariantMap &arguments,
                                const QVariantMap &result) const;
    QString repeatedActionGuidance(const QString &tool) const;
    bool informativeTool(const QString &tool) const;
    bool repeatGuardTool(const QString &tool) const;
    bool edaTool(const QString &tool) const;
    bool editTool(const QString &tool) const;
    void setBudgetState();

    mutable QMutex m_mutex;
    QVariantMap m_state;
    QString m_artifactRoot;
    Limits m_limits;
    int m_calls = 0;
    int m_stalled = 0;
    int m_investigationCalls = 0;
    int m_edaRuns = 0;
    int m_repeatBlocks = 0;
    quint64 m_progressSerial = 0;
    QHash<QString, QVariantMap> m_inFlight;
    QString m_lastReadSignature;
    QString m_lastReadBase;
    QString m_lastReadEvidence;
};
