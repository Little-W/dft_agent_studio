#pragma once

#include <QJsonArray>
#include <QString>
#include <QStringList>
#include <QVector>

class ResponsesToolTranscript final {
public:
    explicit ResponsesToolTranscript(QJsonArray initial = {}, QString artifactRoot = {});

    bool appendRound(const QJsonArray &items, QString *error = nullptr);
    bool build(int inputBudget, QJsonArray *items, QString *error = nullptr,
               const QString &runtimeContext = {});
    bool installCompactionSummary(const QString &summary, int maximumUserTokens = 20'000,
                                  QString *error = nullptr);
    QJsonArray checkpoint(int maximumRounds = 4) const;
    QString boundOutput(const QString &callId, const QString &output,
                        int maximumCharacters = 18'000, QString *error = nullptr) const;

    static bool validateToolPairs(const QJsonArray &items, QString *error = nullptr);
    static int tokenEstimate(const QJsonValue &value);

private:
    struct Round {
        QJsonArray items;
        QString path;
        QString sha256;
    };

    QJsonArray m_initial;
    QString m_artifactRoot;
    QVector<Round> m_rounds;
    QJsonArray m_archived;
    QJsonArray m_archivedMessages;
    QJsonArray m_pinnedSkillItems;
    QStringList m_archivedToolObservations;
    int m_pinnedSkillTokens = 0;
    QString m_compactionCheckpointPath;

    bool writeJson(const QString &path, const QJsonValue &value, QString *error) const;
    bool archiveOldest(QString *error);
    QString extractiveCompactionSummary() const;
};

class ResponsesContextCompactionPolicy final {
public:
    struct Allocation {
        int contextWindow = 0;
        int effectiveContextWindow = 0;
        int fixedPromptTokens = 0;
        int outputReserveTokens = 0;
        int safetyReserveTokens = 0;
        int hardInputBudget = 0;
        int compactAtTokens = 0;
    };

    static bool allocate(int contextWindow, int effectiveContextPercent,
                         int configuredCompactLimit, int outputReserveTokens,
                         int safetyReserveTokens, int fixedPromptTokens,
                         Allocation *allocation, QString *error = nullptr);
    static bool shouldCompact(int currentInputTokens, const Allocation &allocation);

    // Split flat Responses history only at boundaries where every tool call has
    // its matching output. Ordinary messages remain in their original order.
    static bool groupCompleteHistory(const QJsonArray &history, QVector<QJsonArray> *groups,
                                     QString *error = nullptr);
};
