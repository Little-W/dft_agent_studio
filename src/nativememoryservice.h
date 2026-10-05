#pragma once

#include <QVariant>
#include <QVariantList>
#include <QVariantMap>
#include <QString>

class NativeMemoryService {
public:
    static constexpr int ShortTermCoreCapacity = 24;
    static constexpr int ShortTermTotalCapacity = 128;
    NativeMemoryService(QString projectId, QString databaseFile, QString snapshotsRoot = {},
                        int contextWindow = 131072, int outputTokenReserve = 4096,
                        int configuredInputTokenLimit = -1, int fixedPromptReserve = 4096);

    static int estimateTokens(const QVariant &value);
    static QVariant compactModelValue(const QVariant &value, int maximumTokens,
                                      QVariantMap *report = nullptr);
    static QVariantList compactTextContext(const QVariantList &records, int maximumTokens);

    bool initialize(QString *error = nullptr) const;
    bool recordEvent(const QString &kind, const QVariantMap &content, bool critical = false,
                     const QString &sourceFile = {}, QString *eventId = nullptr,
                     QString *error = nullptr) const;
    bool remember(const QString &category, const QVariantMap &content,
                  const QString &evidenceFile, const QString &status,
                  QString *memoryId = nullptr, QString *error = nullptr) const;
    bool rememberShortTerm(const QString &category, const QVariantMap &content,
                           const QString &evidenceFile, const QString &status,
                           int validForHours, QString *memoryId = nullptr,
                           QString *error = nullptr) const;
    QVariantList recall(const QString &query, int limit = 8,
                        QString *error = nullptr) const;
    QVariantList recallShortTerm(const QString &query, int limit = 8,
                                 QString *error = nullptr) const;
    QVariantList recentEvents(int limit = 40, QString *error = nullptr) const;
    bool recordFeedback(const QString &episodeId, const QString &verdict,
                        const QString &note, const QString &correctedResponse,
                        const QString &feedbackFile, QString *error = nullptr) const;
    // Returns 1 for a newly imported episode, 0 when already imported, and -1 on error.
    int importEpisode(const QVariantMap &episode, const QString &episodeFile,
                      QString *error = nullptr) const;
    QVariantMap backfill(const QString &episodesRoot, const QString &feedbackFile,
                         QString *error = nullptr) const;
    QVariantMap compactContext(const QString &query, const QVariantMap &current,
                               QString *error = nullptr) const;
    QVariantMap stats(QString *error = nullptr) const;

private:
    QString m_projectId;
    QString m_databaseFile;
    QString m_snapshotsRoot;
    int m_contextWindow = 131072;
    int m_outputTokenReserve = 4096;
    int m_configuredInputTokenLimit = -1;
    int m_fixedPromptReserve = 4096;
};
