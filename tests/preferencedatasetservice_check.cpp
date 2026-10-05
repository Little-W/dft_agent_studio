#include "preferencedatasetservice.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include <cstdio>

namespace {
bool require(bool condition, const char *message) {
    if (!condition)
        std::fprintf(stderr, "Check failed: %s\n", message);
    return condition;
}

QJsonObject message(const QString &role, const QString &content) {
    return {{QStringLiteral("role"), role}, {QStringLiteral("content"), content}};
}

QJsonObject pair(const QString &caseId, const QString &chosen = QStringLiteral("accepted"),
                 const QString &rejected = QStringLiteral("rejected")) {
    return {{QStringLiteral("prompt"), QJsonArray{message(QStringLiteral("user"), QStringLiteral("question"))}},
        {QStringLiteral("chosen"), QJsonArray{message(QStringLiteral("assistant"), chosen)}},
        {QStringLiteral("rejected"), QJsonArray{message(QStringLiteral("assistant"), rejected)}},
        {QStringLiteral("metadata"), QJsonObject{{QStringLiteral("source"), QStringLiteral("expert-review")},
            {QStringLiteral("case"), caseId}}}};
}

bool writeRows(const QString &path, const QList<QJsonObject> &rows) {
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        return false;
    for (const QJsonObject &row : rows)
        file.write(QJsonDocument(row).toJson(QJsonDocument::Compact) + '\n');
    return true;
}
}

int main() {
    QTemporaryDir temporary;
    if (!require(temporary.isValid(), "temporary directory is valid"))
        return 1;
    const QString dataset = temporary.filePath(QStringLiteral("preference/pairs.jsonl"));
    const QString manifest = temporary.filePath(QStringLiteral("nested/manifest.json"));
    QDir().mkpath(QFileInfo(dataset).absolutePath());
    const bool validWritten = writeRows(dataset, {pair(QStringLiteral("case-a")), pair(QStringLiteral("case-b"))});
    const QVariantMap built = PreferenceDatasetService::buildManifest(dataset, manifest);
    QFile manifestFile(manifest);
    QJsonObject saved;
    if (manifestFile.open(QIODevice::ReadOnly))
        saved = QJsonDocument::fromJson(manifestFile.readAll()).object();
    QFile input(dataset);
    QByteArray digest;
    if (input.open(QIODevice::ReadOnly))
        digest = QCryptographicHash::hash(input.readAll(), QCryptographicHash::Sha256).toHex();

    const bool duplicateWritten = writeRows(dataset, {pair(QStringLiteral("duplicate")), pair(QStringLiteral("duplicate"))});
    const QVariantMap duplicate = PreferenceDatasetService::buildManifest(dataset,
        temporary.filePath(QStringLiteral("duplicate.json")));
    QJsonObject invalidRole = pair(QStringLiteral("bad-role"));
    invalidRole.insert(QStringLiteral("prompt"), QJsonArray{message(QStringLiteral("assistant"), QStringLiteral("not user"))});
    const bool invalidWritten = writeRows(dataset, {invalidRole});
    const QVariantMap invalid = PreferenceDatasetService::buildManifest(dataset,
        temporary.filePath(QStringLiteral("invalid.json")));

    const bool passed = require(validWritten && built.value(QStringLiteral("ok")).toBool(),
                   "valid preference pairs create a manifest")
        && require(saved.value(QStringLiteral("pairs")).toInt() == 2
                       && saved.value(QStringLiteral("review_status")).toString() == QStringLiteral("supervisor_reviewed")
                       && saved.value(QStringLiteral("dataset_sha256")).toString().toLatin1() == digest,
                   "manifest records pair count, review status, and dataset fingerprint")
        && require(duplicateWritten && !duplicate.value(QStringLiteral("ok")).toBool()
                       && duplicate.value(QStringLiteral("message")).toString().contains(QStringLiteral("重复")),
                   "duplicate preference case IDs are rejected")
        && require(invalidWritten && !invalid.value(QStringLiteral("ok")).toBool()
                       && invalid.value(QStringLiteral("message")).toString().contains(QStringLiteral("prompt 必须以 user 结束")),
                   "preference prompt must end with a user message");
    return passed ? 0 : 1;
}
