#pragma once

#include <QVariantMap>

class AgentEpisodeService final {
public:
    static QVariantMap persist(const QVariantMap &episode, const QString &dataRoot);
    static QVariantMap recordFeedback(const QString &episodeId, const QString &verdict,
                                     const QString &note, const QString &correctedResponse,
                                     const QString &dataRoot);
    static QVariantMap exportSft(const QString &feedbackPath, const QString &policyPath,
                                 const QString &outputPath);
};
