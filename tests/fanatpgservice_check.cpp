#include "fanatpgservice.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QVariantMap>

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
}

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    QTemporaryDir temporary;
    require(temporary.isValid(), "temporary FAN workspace is available");
    const QString root = temporary.filePath(QStringLiteral("fan-root"));
    require(write(QDir(root).filePath(QStringLiteral("mod_netlist/s27.v")), "module s27; endmodule\n"),
            "allow-listed circuit fixture is written");
    require(write(QDir(root).filePath(QStringLiteral("techlib/mod_nangate45.mdt")), "library fixture\n"),
            "FAN technology library fixture is written");
    const QString fakeFan = QDir(root).filePath(QStringLiteral("bin/opt/fan"));
    const QByteArray fanScript = R"SH(#!/bin/sh
if grep -q 'set_dynamic_compression on' atpg.script; then
  coverage=94.55; patterns=5
elif grep -q 'set_static_compression on' atpg.script; then
  coverage=94.60; patterns=6
else
  coverage=96.00; patterns=8
fi
printf 'test coverage %s%%\nfault coverage %s%%\n#Patterns %s\nATPG runtime 0.25 s\n' "$coverage" "$coverage" "$patterns" > report.rpt
echo "ran profile with $patterns patterns"
)SH";
    require(write(fakeFan, fanScript), "fake FAN executable is written");
    require(QFile::setPermissions(fakeFan, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner
        | QFileDevice::ReadGroup | QFileDevice::ExeGroup | QFileDevice::ReadOther | QFileDevice::ExeOther),
        "fake FAN executable is marked executable");
    qputenv("DFT_AGENT_FAN_ROOT", root.toUtf8());

    const QVariantMap project{
        {QStringLiteral("id"), QStringLiteral("fan_fixture")},
        {QStringLiteral("root"), temporary.filePath(QStringLiteral("project-root"))},
        {QStringLiteral("flow_profile"), QStringLiteral("fan_atpg_compression")},
        {QStringLiteral("minimum_coverage"), 94.5},
        {QStringLiteral("metadata"), QVariantMap{{QStringLiteral("fan_atpg"), QVariantMap{
            {QStringLiteral("circuit"), QStringLiteral("s27")},
            {QStringLiteral("maximum_patterns"), 5}}}}},
    };
    require(FanAtpgService::selected(project), "FAN execution profile is detected");
    const QVariantMap inspection = FanAtpgService::inspect(project);
    require(inspection.value(QStringLiteral("netlist_bytes")).toLongLong() > 0
                && inspection.value(QStringLiteral("profiles")).toList().size() == 3,
            "inspection reports the fixed circuit and three approved profiles");
    const QVariantMap ready = FanAtpgService::readiness(project);
    require(ready.value(QStringLiteral("ready")).toBool(), "FAN binary and benchmark are readiness-checked");

    const QVariantMap response = FanAtpgService::run(project, {}, temporary.path());
    if (!response.value(QStringLiteral("ok")).toBool())
        std::cerr << QJsonDocument(QJsonObject::fromVariantMap(response)).toJson(QJsonDocument::Compact).constData() << '\n';
    require(response.value(QStringLiteral("ok")).toBool(), "FAN profiles execute in native service");
    const QVariantMap result = response.value(QStringLiteral("result")).toMap();
    const QVariantMap optimized = result.value(QStringLiteral("result")).toMap();
    if (!optimized.value(QStringLiteral("objective_met")).toBool()
        || optimized.value(QStringLiteral("selected_profile")).toString() != QStringLiteral("static_dynamic"))
        std::cerr << QJsonDocument(QJsonObject::fromVariantMap(optimized)).toJson(QJsonDocument::Compact).constData() << '\n';
    require(optimized.value(QStringLiteral("objective_met")).toBool()
                && optimized.value(QStringLiteral("selected_profile")).toString() == QStringLiteral("static_dynamic"),
            "hard coverage and pattern goals select the satisfying compression profile");
    const QVariantList candidates = optimized.value(QStringLiteral("candidates")).toList();
    require(candidates.size() == 3, "all approved FAN configurations are evaluated");
    for (const QVariant &candidateValue : candidates) {
        const QVariantMap candidate = candidateValue.toMap();
        require(candidate.value(QStringLiteral("success")).toBool()
                    && QFileInfo(candidate.value(QStringLiteral("report_file")).toString()).isFile()
                    && QFileInfo(candidate.value(QStringLiteral("run_dir")).toString() + QStringLiteral("/techlib")).isSymLink(),
                "each candidate keeps report evidence and links the shared input tree read-only");
    }
    require(result.value(QStringLiteral("cross_validation")).toMap().value(QStringLiteral("status")).toString()
                == QStringLiteral("verified"),
            "goal satisfaction is reported as verified from parsed candidate evidence");
    const QVariantMap iteration = FanAtpgService::runIteration(project, {});
    require(!iteration.value(QStringLiteral("result")).toMap().value(QStringLiteral("executed")).toBool(),
            "FAN iteration explains that the approved finite profiles are exhausted");

    QVariantMap invalidProject = project;
    QVariantMap metadata = invalidProject.value(QStringLiteral("metadata")).toMap();
    QVariantMap settings = metadata.value(QStringLiteral("fan_atpg")).toMap();
    settings.insert(QStringLiteral("circuit"), QStringLiteral("../../other"));
    metadata.insert(QStringLiteral("fan_atpg"), settings);
    invalidProject.insert(QStringLiteral("metadata"), metadata);
    require(!FanAtpgService::readiness(invalidProject).value(QStringLiteral("ready")).toBool(),
            "circuit names outside the benchmark allowlist are rejected");

    std::cout << "Native FAN ATPG checks passed\n";
    return 0;
}
