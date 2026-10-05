#pragma once

#include <QStringList>
#include <QVariantMap>

class PatchActionService final {
public:
    static QVariantMap createProposal(const QString &projectId, const QString &projectRoot,
                                      const QStringList &files, const QString &purpose,
                                      const QString &patch, const QString &agentRoot);
    static QVariantMap normalizePatch(const QString &projectRoot, const QStringList &files,
                                      const QString &patch);
    static QVariantMap createFilePatch(const QString &projectRoot, const QString &path,
                                       const QString &content);
    static QVariantMap applyAutonomous(const QString &projectId, const QString &projectRoot,
                                       const QStringList &files, const QString &purpose,
                                       const QString &patch, const QString &agentRoot);
    static QVariantMap decide(const QString &proposalId, bool approved, const QString &agentRoot,
                              const QString &isolatedPatchesRoot = {});
    static QVariantMap rollback(const QString &editId, const QString &projectId,
                                const QString &projectRoot, const QString &agentRoot);
};
