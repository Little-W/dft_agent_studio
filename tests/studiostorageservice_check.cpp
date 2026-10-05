#include "../src/studiostorageservice.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include <cstdlib>

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    QTemporaryDir temporary;
    if (!temporary.isValid())
        return EXIT_FAILURE;
    const QString agentRoot = temporary.path();
    const QString dataRoot = QDir(agentRoot).filePath(QStringLiteral("studio_data"));
    const QString artifactRoot = QDir(dataRoot).filePath(QStringLiteral("runs"));
    if (!QDir().mkpath(artifactRoot))
        return EXIT_FAILURE;
    const QString canonicalRecord = QDir(artifactRoot).filePath(QStringLiteral("run.log"));
    QFile record(canonicalRecord);
    if (!record.open(QIODevice::WriteOnly) || record.write("evidence") != 8)
        return EXIT_FAILURE;
    record.close();

    const QString aliasesPath = QDir(dataRoot).filePath(QStringLiteral("_migration/path_aliases.json"));
    if (!QDir().mkpath(QFileInfo(aliasesPath).absolutePath()))
        return EXIT_FAILURE;
    QFile aliases(aliasesPath);
    const QByteArray aliasBytes = QJsonDocument(QJsonObject{{QStringLiteral("legacy_roots"),
        QJsonArray{QStringLiteral("/historic/repo")}}}).toJson(QJsonDocument::Compact);
    if (!aliases.open(QIODevice::WriteOnly) || aliases.write(aliasBytes) != aliasBytes.size())
        return EXIT_FAILURE;
    aliases.close();

    const QVariantMap oldPath = StudioStorageService::resolvePath(
        QStringLiteral("/historic/repo/artifacts/runs/run.log"), agentRoot);
    if (!oldPath.value(QStringLiteral("ok")).toBool()
        || !oldPath.value(QStringLiteral("remapped")).toBool()
        || oldPath.value(QStringLiteral("path")).toString() != canonicalRecord)
        return EXIT_FAILURE;
    const QVariantMap traversal = StudioStorageService::resolvePath(
        QStringLiteral("/historic/repo/artifacts/runs/../run.log"), agentRoot);
    if (!traversal.value(QStringLiteral("ok")).toBool()
        || traversal.value(QStringLiteral("path")).toString()
            != QStringLiteral("/historic/repo/artifacts/runs/../run.log"))
        return EXIT_FAILURE;

    const QVariantMap source{{QStringLiteral("evidence_path"), QStringLiteral("/historic/repo/artifacts/runs/run.log")},
                             {QStringLiteral("message"), QStringLiteral("See /historic/repo/artifacts/runs/run.log")}};
    const QVariantMap remapped = StudioStorageService::remapRecord(source, agentRoot);
    const QVariantMap remappedValue = remapped.value(QStringLiteral("value")).toMap();
    if (!remapped.value(QStringLiteral("ok")).toBool()
        || remappedValue.value(QStringLiteral("evidence_path")).toString() != canonicalRecord
        || remappedValue.value(QStringLiteral("message")).toString() != source.value(QStringLiteral("message")).toString())
        return EXIT_FAILURE;

    const QVariantMap diagnosis = StudioStorageService::diagnose(agentRoot);
    const QVariantMap result = diagnosis.value(QStringLiteral("result")).toMap();
    if (!diagnosis.value(QStringLiteral("ok")).toBool()
        || result.value(QStringLiteral("mode")).toString() != QStringLiteral("read_only")
        || result.value(QStringLiteral("configured_data_root")).toString() != dataRoot
        || result.value(QStringLiteral("session_count")).toInt() != 0
        || QFileInfo::exists(QDir(agentRoot).filePath(QStringLiteral(".dft-studio-storage.json"))))
        return EXIT_FAILURE;

    return EXIT_SUCCESS;
}
