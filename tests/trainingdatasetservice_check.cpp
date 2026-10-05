#include "trainingdatasetservice.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include <cstdlib>
#include <iostream>

namespace {
void require(bool condition, const char *message) {
    if (condition)
        return;
    std::cerr << "FAIL: " << message << '\n';
    std::exit(1);
}

bool write(const QString &path, const QByteArray &bytes) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}

QJsonObject sample(const QString &caseId) {
    return {{QStringLiteral("messages"), QJsonArray{
        QJsonObject{{QStringLiteral("role"), QStringLiteral("system")},
                    {QStringLiteral("content"), QStringLiteral("原系统")}},
        QJsonObject{{QStringLiteral("role"), QStringLiteral("user")},
                    {QStringLiteral("content"), QStringLiteral("工具证据")}},
        QJsonObject{{QStringLiteral("role"), QStringLiteral("assistant")},
                    {QStringLiteral("content"), QStringLiteral("受控回答")}},
    }}, {QStringLiteral("metadata"), QJsonObject{
        {QStringLiteral("training_policy"), QStringLiteral("role_reinforcement_v1")},
        {QStringLiteral("training_category"), QStringLiteral("角色强化")},
        {QStringLiteral("source"), QStringLiteral("监管者检查")},
        {QStringLiteral("case"), caseId},
    }}};
}

QByteArray line(const QJsonObject &row) {
    return QJsonDocument(row).toJson(QJsonDocument::Compact) + '\n';
}

QJsonObject feedbackRecord(const QString &id, const QString &verdict, const QString &status,
                           const QString &answer, const QString &error = {}) {
    QJsonObject result{
        {QStringLiteral("cross_validation"), QJsonObject{
            {QStringLiteral("status"), status}, {QStringLiteral("rounds"), 2},
            {QStringLiteral("stable_snapshot"), true},
            {QStringLiteral("verification_file"), QStringLiteral("/runs/%1/verification.json").arg(id)}}},
        {QStringLiteral("execution"), QJsonObject{
            {QStringLiteral("error_count"), error.isEmpty() ? 0 : 1},
            {QStringLiteral("errors"), error.isEmpty() ? QJsonArray{} : QJsonArray{error}}}},
        {QStringLiteral("acceptance"), QJsonArray{QJsonObject{{QStringLiteral("observed_percentage"), 99.25}}}},
        {QStringLiteral("evidence_file"), QStringLiteral("/runs/%1/evidence.json").arg(id)},
    };
    const QJsonObject episode{
        {QStringLiteral("project_id"), QStringLiteral("secworks_%1").arg(id)},
        {QStringLiteral("scope"), QStringLiteral("dft")},
        {QStringLiteral("answer"), QStringLiteral("原始审核回答")},
        {QStringLiteral("tool_trace"), QJsonArray{QJsonObject{
            {QStringLiteral("result"), QString::fromUtf8(QJsonDocument(result).toJson(QJsonDocument::Compact))}}}},
    };
    return {{QStringLiteral("episode_id"), id}, {QStringLiteral("verdict"), verdict},
            {QStringLiteral("corrected_response"), answer}, {QStringLiteral("episode"), episode}};
}
}

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    QTemporaryDir temporary;
    require(temporary.isValid(), "temporary directory available");
    const QString sourceA = temporary.filePath(QStringLiteral("a.jsonl"));
    const QString sourceB = temporary.filePath(QStringLiteral("b.jsonl"));
    require(write(sourceA, line(sample(QStringLiteral("same")))), "first source fixture written");
    require(write(sourceB, line(sample(QStringLiteral("same"))) + line(sample(QStringLiteral("second")))),
            "second source fixture written");
    const QString output = temporary.filePath(QStringLiteral("nested/mix.jsonl"));
    const QString manifest = temporary.filePath(QStringLiteral("provenance/mix.json"));
    const QVariantMap response = TrainingDatasetService::buildSupervisedMix({sourceA, sourceB}, output, manifest);
    require(response.value(QStringLiteral("ok")).toBool(), "valid supervised data builds");
    const QVariantMap result = response.value(QStringLiteral("result")).toMap();
    require(result.value(QStringLiteral("rows")).toInt() == 2, "duplicate rows are removed across inputs");
    require(result.value(QStringLiteral("categories")).toMap().value(QStringLiteral("角色强化")).toInt() == 2,
            "category counts include unique rows only");
    const QVariantList sources = result.value(QStringLiteral("sources")).toList();
    require(sources.size() == 2 && sources.at(1).toMap().value(QStringLiteral("rows")).toInt() == 1,
            "per-source unique row counts and provenance are recorded");
    QFile dataset(output);
    require(dataset.open(QIODevice::ReadOnly), "mixed dataset is written");
    require(dataset.readAll().count('\n') == 2, "JSONL output contains two rows");
    QFile manifestFile(manifest);
    require(manifestFile.open(QIODevice::ReadOnly), "manifest is written");
    const QJsonObject manifestJson = QJsonDocument::fromJson(manifestFile.readAll()).object();
    require(manifestJson.value(QStringLiteral("dataset_sha256")).toString().size() == 64,
            "dataset SHA-256 is recorded");

    const QString invalid = temporary.filePath(QStringLiteral("invalid.jsonl"));
    QJsonObject bad = sample(QStringLiteral("bad"));
    QJsonObject metadata = bad.value(QStringLiteral("metadata")).toObject();
    metadata.remove(QStringLiteral("source"));
    bad.insert(QStringLiteral("metadata"), metadata);
    require(write(invalid, line(bad)), "invalid row fixture written");
    const QString rejectedOutput = temporary.filePath(QStringLiteral("rejected.jsonl"));
    const QVariantMap rejected = TrainingDatasetService::buildSupervisedMix(
        {invalid}, rejectedOutput, temporary.filePath(QStringLiteral("rejected.json")));
    require(!rejected.value(QStringLiteral("ok")).toBool()
                && rejected.value(QStringLiteral("message")).toString().contains(QStringLiteral("缺少 source")),
            "invalid metadata is rejected with a source location");
    require(!QFileInfo::exists(rejectedOutput), "failed validation does not commit a partial dataset");

    const QVariantMap missing = TrainingDatasetService::buildSupervisedMix(
        {temporary.filePath(QStringLiteral("absent.jsonl"))}, output, manifest);
    require(!missing.value(QStringLiteral("ok")).toBool(), "missing input is rejected");

    const QString policy = temporary.filePath(QStringLiteral("runtime-policy.md"));
    const QByteArray policyText = "受证据约束的 DFT Agent 策略\n";
    require(write(policy, policyText), "runtime policy fixture written");
    QJsonObject staleApproved = feedbackRecord(QStringLiteral("verified"), QStringLiteral("approved"),
        QStringLiteral("blocked"), QStringLiteral("过期回答"));
    QJsonObject corrected = feedbackRecord(QStringLiteral("verified"), QStringLiteral("corrected"),
        QStringLiteral("verified"), QStringLiteral("校正后的已审核回答"));
    const QJsonObject blocked = feedbackRecord(QStringLiteral("blocked"), QStringLiteral("approved"),
        QStringLiteral("blocked"), QStringLiteral("审核的阻塞回答"), QStringLiteral("post-DFT DRC has one violation"));
    QJsonObject superseded = feedbackRecord(QStringLiteral("superseded"), QStringLiteral("approved"),
        QStringLiteral("verified"), QStringLiteral("不应进入训练集"));
    superseded.insert(QStringLiteral("verdict"), QStringLiteral("rejected"));
    const QString feedback = temporary.filePath(QStringLiteral("feedback.jsonl"));
    require(write(feedback, line(staleApproved) + line(corrected) + line(blocked) + line(superseded)),
            "review feedback fixture written");
    const QString augmentedOutput = temporary.filePath(QStringLiteral("augmented/training.jsonl"));
    const QString augmentedManifest = temporary.filePath(QStringLiteral("augmented/training.manifest.json"));
    const QVariantMap augmentedResponse = TrainingDatasetService::buildEvidenceAugmentedSft(
        feedback, policy, augmentedOutput, augmentedManifest);
    require(augmentedResponse.value(QStringLiteral("ok")).toBool(), "reviewed evidence SFT variants build natively");
    const QJsonObject augmented = QJsonObject::fromVariantMap(
        augmentedResponse.value(QStringLiteral("result")).toMap());
    require(augmented.value(QStringLiteral("reviewed_source_episodes")).toInt() == 2,
            "only the latest approved or corrected review per episode is selected");
    require(augmented.value(QStringLiteral("source_episodes_with_structured_evidence")).toInt() == 2,
            "structured evidence source count is recorded");
    require(augmented.value(QStringLiteral("rows")).toInt() == 15
                && augmented.value(QStringLiteral("blocked_extra_rows")).toInt() == 5,
            "verified episodes get five variants and blocked episodes add five guardrail variants");
    require(augmented.value(QStringLiteral("dataset_sha256")).toString().size() == 64
                && augmented.value(QStringLiteral("feedback_sha256")).toString().size() == 64
                && augmented.value(QStringLiteral("policy_sha256")).toString().size() == 64,
            "output, reviewed feedback, and policy hashes are recorded");
    QFile augmentedFile(augmentedOutput);
    require(augmentedFile.open(QIODevice::ReadOnly), "augmented dataset is written");
    const QList<QByteArray> augmentedLines = augmentedFile.readAll().split('\n');
    require(augmentedLines.size() == 16, "augmented JSONL has fifteen rows plus a final newline");
    const QJsonObject firstAugmented = QJsonDocument::fromJson(augmentedLines.first()).object();
    const QJsonArray firstMessages = firstAugmented.value(QStringLiteral("messages")).toArray();
    require(firstMessages.size() == 3
                && firstMessages.at(0).toObject().value(QStringLiteral("content")).toString() == QString::fromUtf8(policyText)
                && firstMessages.at(2).toObject().value(QStringLiteral("content")).toString() == QStringLiteral("校正后的已审核回答"),
            "SFT rows preserve runtime policy and the latest corrected canonical answer");
    QFile augmentedManifestFile(augmentedManifest);
    require(augmentedManifestFile.open(QIODevice::ReadOnly), "augmentation provenance manifest is written");
    const QJsonObject augmentedManifestJson = QJsonDocument::fromJson(augmentedManifestFile.readAll()).object();
    require(augmentedManifestJson.value(QStringLiteral("status_counts_by_source_episode")).toObject()
                .value(QStringLiteral("blocked")).toInt() == 1,
            "status counts use the latest review per source episode");

    std::cout << "Native supervised dataset checks passed\n";
    return 0;
}
