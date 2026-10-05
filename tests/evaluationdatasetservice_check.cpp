#include "evaluationdatasetservice.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
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

bool write(const QString &path, const QJsonArray &rows) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return false;
    for (const QJsonValue &row : rows)
        file.write(QJsonDocument(row.toObject()).toJson(QJsonDocument::Compact) + '\n');
    return true;
}

QJsonObject testCase(const QString &id, const QString &project) {
    return {{QStringLiteral("id"), id},
        {QStringLiteral("training_policy"), QStringLiteral("role_reinforcement_v1")},
        {QStringLiteral("project"), project}, {QStringLiteral("user"), QStringLiteral("检查证据")},
        {QStringLiteral("must_contain"), QJsonArray{QStringLiteral("未完成")}}};
}
}

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    QTemporaryDir temporary;
    require(temporary.isValid(), "temporary directory available");
    const QString holdout = temporary.filePath(QStringLiteral("holdout.jsonl"));
    const QString training = temporary.filePath(QStringLiteral("training.jsonl"));
    require(write(holdout, QJsonArray{testCase(QStringLiteral("hold-1"), QStringLiteral("new_soc"))}),
            "holdout fixture written");
    require(write(training, QJsonArray{QJsonObject{{QStringLiteral("metadata"), QJsonObject{
                {QStringLiteral("case"), QStringLiteral("train-1")}}}}}), "training fixture written");

    const QString manifest = temporary.filePath(QStringLiteral("nested/manifest.json"));
    QVariantMap response = EvaluationDatasetService::buildHoldoutManifest(
        holdout, {training}, manifest, {QStringLiteral("old_soc")});
    require(response.value(QStringLiteral("ok")).toBool(), "disjoint holdout builds a manifest");
    QVariantMap result = response.value(QStringLiteral("result")).toMap();
    require(result.value(QStringLiteral("cases")).toInt() == 1
                && result.value(QStringLiteral("case_overlap")).toList().isEmpty()
                && result.value(QStringLiteral("project_overlap")).toList().isEmpty(),
            "manifest counts and disjointness are recorded");
    require(QFileInfo::exists(manifest), "manifest is persisted");

    response = EvaluationDatasetService::buildHoldoutManifest(
        holdout, {training}, temporary.filePath(QStringLiteral("overlap.json")), {}, QStringLiteral("development"));
    require(response.value(QStringLiteral("ok")).toBool()
                && response.value(QStringLiteral("result")).toMap().value(QStringLiteral("description")).toString()
                    .contains(QStringLiteral("不参与最终验收")),
            "development manifest is not labeled as a final holdout");

    require(write(holdout, QJsonArray{testCase(QStringLiteral("same"), QStringLiteral("new_soc"))}),
            "overlap holdout fixture rewritten");
    require(write(training, QJsonArray{QJsonObject{{QStringLiteral("metadata"), QJsonObject{
                {QStringLiteral("case"), QStringLiteral("same")}}}}}), "overlap training fixture rewritten");
    response = EvaluationDatasetService::buildHoldoutManifest(
        holdout, {training}, temporary.filePath(QStringLiteral("rejected.json")));
    require(!response.value(QStringLiteral("ok")).toBool()
                && response.value(QStringLiteral("message")).toString().contains(QStringLiteral("id 重复")),
            "training case reuse is rejected");

    require(write(holdout, QJsonArray{testCase(QStringLiteral("project-overlap"), QStringLiteral("same_soc"))}),
            "project-overlap fixture written");
    response = EvaluationDatasetService::buildHoldoutManifest(
        holdout, {training}, temporary.filePath(QStringLiteral("project-overlap.json")), {QStringLiteral("same_soc")});
    require(!response.value(QStringLiteral("ok")).toBool()
                && response.value(QStringLiteral("message")).toString().contains(QStringLiteral("项目重复")),
            "training project reuse is rejected");

    QJsonObject invalid = testCase(QStringLiteral("bad-alternatives"), QStringLiteral("new_soc"));
    invalid.remove(QStringLiteral("must_contain"));
    invalid.insert(QStringLiteral("must_contain_any"), QJsonArray{QJsonArray{}});
    require(write(holdout, QJsonArray{invalid}), "invalid alternative fixture written");
    response = EvaluationDatasetService::buildHoldoutManifest(
        holdout, {training}, temporary.filePath(QStringLiteral("bad-alternatives.json")));
    require(!response.value(QStringLiteral("ok")).toBool()
                && response.value(QStringLiteral("message")).toString().contains(QStringLiteral("must_contain_any")),
            "empty alternative groups are rejected");

    std::cout << "Native evaluation dataset checks passed\n";
    return 0;
}
