#include "studiouserdata.h"

#include "studiopaths.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QUrl>
#include <QUuid>

#include <sqlite3.h>

namespace {

bool fail(QString *error, const QString &message) {
    if (error)
        *error = message;
    return false;
}

bool seedConfiguration(const QString &dataRoot, QString *error);

bool copyTree(const QString &sourceRoot, const QString &destinationRoot, QString *error) {
    const QFileInfo sourceInfo(sourceRoot);
    if (!sourceInfo.isDir() || sourceInfo.isSymLink())
        return fail(error, QStringLiteral("Legacy Studio data root is not a regular directory."));
    if (!QDir().mkpath(destinationRoot))
        return fail(error, QStringLiteral("Cannot create Studio data migration directory: %1").arg(destinationRoot));

    QDirIterator iterator(sourceRoot, QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot,
                          QDirIterator::Subdirectories);
    while (iterator.hasNext()) {
        const QString sourcePath = iterator.next();
        const QFileInfo entry(sourcePath);
        const QString relative = QDir(sourceRoot).relativeFilePath(sourcePath);
        if (entry.fileName() == QStringLiteral(".migration.lock")
            || entry.fileName().endsWith(QStringLiteral(".pid")))
            continue;
        const QString destinationPath = QDir(destinationRoot).filePath(relative);
        const QFileInfo destinationInfo(destinationPath);
        if (destinationInfo.exists() || destinationInfo.isSymLink())
            continue;
        if (entry.isSymLink()) {
            if (!QDir().mkpath(QFileInfo(destinationPath).absolutePath()))
                return fail(error, QStringLiteral("Cannot create directory for a migrated symbolic link."));
            if (!QFile::link(entry.symLinkTarget(), destinationPath))
                return fail(error, QStringLiteral("Cannot preserve symbolic link during Studio data migration: %1").arg(relative));
        } else if (entry.isDir()) {
            if (!QDir().mkpath(destinationPath))
                return fail(error, QStringLiteral("Cannot create migrated data directory: %1").arg(relative));
            QFile::setPermissions(destinationPath, entry.permissions());
        } else if (entry.isFile()) {
            if (!QDir().mkpath(QFileInfo(destinationPath).absolutePath())
                || !QFile::copy(sourcePath, destinationPath)) {
                return fail(error, QStringLiteral("Cannot copy Studio data file: %1").arg(relative));
            }
            QFile::setPermissions(destinationPath, entry.permissions());
        } else {
            return fail(error, QStringLiteral("Unsupported special file in Studio data: %1").arg(relative));
        }
    }
    return true;
}

bool isEmptyBootstrapConfig(const QString &path, const QByteArray &emptyValue) {
    QFile file(path);
    return file.open(QIODevice::ReadOnly) && file.readAll().trimmed() == emptyValue.trimmed();
}

bool restoreBootstrapConfig(const QString &legacyRoot, const QString &targetRoot, QString *error) {
    const QString config = QDir(targetRoot).filePath(QStringLiteral("config"));
    const QString legacyConfig = QDir(legacyRoot).filePath(QStringLiteral("config"));
    const QList<QPair<QString, QByteArray>> bootstraps{
        {QStringLiteral("projects.json"), QByteArrayLiteral("{\"schema_version\":1,\"projects\":[]}" )},
        {QStringLiteral("models.json"), QByteArrayLiteral("{\"schema_version\":1,\"active_model_id\":\"\",\"models\":[]}" )},
        {QStringLiteral("gui_capabilities.json"), QByteArrayLiteral("{\"schema_version\":1,\"disabled\":[]}" )},
    };
    for (const auto &[name, emptyValue] : bootstraps) {
        const QString targetPath = QDir(config).filePath(name);
        const QString sourcePath = QDir(legacyConfig).filePath(name);
        if (!QFileInfo::exists(sourcePath) || !isEmptyBootstrapConfig(targetPath, emptyValue))
            continue;
        QFile source(sourcePath);
        if (!source.open(QIODevice::ReadOnly))
            return fail(error, QStringLiteral("Cannot read legacy Studio configuration: %1").arg(name));
        QSaveFile destination(targetPath);
        if (!destination.open(QIODevice::WriteOnly)
            || destination.write(source.readAll()) < 0 || !destination.commit())
            return fail(error, QStringLiteral("Cannot restore legacy Studio configuration: %1").arg(name));
    }
    return true;
}

bool mergeWorkspaceDatabase(const QString &legacyPath, const QString &targetPath, QString *error) {
    if (!QFileInfo::exists(legacyPath) || !QFileInfo::exists(targetPath))
        return true;
    sqlite3 *database = nullptr;
    const QByteArray targetBytes = QFile::encodeName(targetPath);
    int result = sqlite3_open_v2(targetBytes.constData(), &database,
                                 SQLITE_OPEN_READWRITE | SQLITE_OPEN_URI, nullptr);
    if (result != SQLITE_OK) {
        const QString message = database ? QString::fromUtf8(sqlite3_errmsg(database)) : QStringLiteral("SQLite open failed");
        if (database)
            sqlite3_close(database);
        return fail(error, QStringLiteral("Cannot open user workspace database for migration: %1").arg(message));
    }

    const QString sourceUri = QStringLiteral("file:%1?mode=ro")
        .arg(QString::fromLatin1(QUrl::toPercentEncoding(QFileInfo(legacyPath).absoluteFilePath(), "/")));
    char *attachSql = sqlite3_mprintf("ATTACH DATABASE %Q AS legacy", sourceUri.toUtf8().constData());
    char *sqliteError = nullptr;
    result = sqlite3_exec(database, attachSql, nullptr, nullptr, &sqliteError);
    sqlite3_free(attachSql);
    if (result == SQLITE_OK) {
        result = sqlite3_exec(database,
            "BEGIN IMMEDIATE;"
            "INSERT OR IGNORE INTO main.workspace_memory "
            "(memory_id,project_id,thread_id,title,content,enabled,created_at,updated_at) "
            "SELECT memory_id,project_id,thread_id,title,content,enabled,created_at,updated_at FROM legacy.workspace_memory;"
            "INSERT OR IGNORE INTO main.workspace_selection "
            "(project_id,skill_ids_json,skills_explicit,rule_ids_json,rules_explicit,hooks_enabled,updated_at) "
            "SELECT project_id,skill_ids_json,skills_explicit,rule_ids_json,rules_explicit,hooks_enabled,updated_at FROM legacy.workspace_selection;"
            "COMMIT;", nullptr, nullptr, &sqliteError);
        if (result != SQLITE_OK)
            sqlite3_exec(database, "ROLLBACK;", nullptr, nullptr, nullptr);
        sqlite3_exec(database, "DETACH DATABASE legacy;", nullptr, nullptr, nullptr);
    }
    const QString message = sqliteError ? QString::fromUtf8(sqliteError) : QString{};
    sqlite3_free(sqliteError);
    sqlite3_close(database);
    if (result != SQLITE_OK)
        return fail(error, QStringLiteral("Cannot merge workspace database: %1").arg(message));
    return true;
}

bool markLegacyImportComplete(const QString &dataRoot, QString *error) {
    const QString migration = QDir(dataRoot).filePath(QStringLiteral("_migration"));
    if (!QDir().mkpath(migration))
        return fail(error, QStringLiteral("Cannot create Studio migration metadata directory."));
    QSaveFile marker(QDir(migration).filePath(QStringLiteral("checkout_studio_data_imported.json")));
    const QByteArray contents = QByteArrayLiteral("{\"schema_version\":1,\"source\":\"checkout/studio_data\"}\n");
    if (!marker.open(QIODevice::WriteOnly) || marker.write(contents) != contents.size() || !marker.commit())
        return fail(error, QStringLiteral("Cannot record completed Studio data migration."));
    return true;
}

bool importLegacyIntoExisting(const QString &legacyRoot, const QString &targetRoot, QString *error) {
    const QString marker = QDir(targetRoot).filePath(QStringLiteral("_migration/checkout_studio_data_imported.json"));
    if (QFileInfo::exists(marker))
        return seedConfiguration(targetRoot, error);
    const QString sourceDatabase = QDir(legacyRoot).filePath(QStringLiteral("agent_runtime/codex_workspace.sqlite3"));
    const QString targetDatabase = QDir(targetRoot).filePath(QStringLiteral("agent_runtime/codex_workspace.sqlite3"));
    const bool targetDatabaseExisted = QFileInfo::exists(targetDatabase);
    if (!copyTree(legacyRoot, targetRoot, error)
        || !restoreBootstrapConfig(legacyRoot, targetRoot, error)
        || (targetDatabaseExisted && !mergeWorkspaceDatabase(sourceDatabase, targetDatabase, error))
        || !seedConfiguration(targetRoot, error))
        return false;
    return markLegacyImportComplete(targetRoot, error);
}

bool seedIfMissing(const QString &path, const QByteArray &contents, QString *error) {
    if (QFileInfo::exists(path))
        return true;
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(contents) != contents.size() || !file.commit())
        return fail(error, QStringLiteral("Cannot initialize Studio configuration: %1").arg(path));
    QFile::setPermissions(path, QFile::ReadOwner | QFile::WriteOwner);
    return true;
}

bool seedConfiguration(const QString &dataRoot, QString *error) {
    const QString config = QDir(dataRoot).filePath(QStringLiteral("config"));
    if (!QDir().mkpath(config))
        return fail(error, QStringLiteral("Cannot create Studio configuration directory: %1").arg(config));
    return seedIfMissing(QDir(config).filePath(QStringLiteral("projects.json")),
                         QByteArrayLiteral("{\"schema_version\":1,\"projects\":[]}\n"), error)
        && seedIfMissing(QDir(config).filePath(QStringLiteral("models.json")),
                         QByteArrayLiteral("{\"schema_version\":1,\"active_model_id\":\"\",\"models\":[]}\n"), error)
        && seedIfMissing(QDir(config).filePath(QStringLiteral("gui_capabilities.json")),
                         QByteArrayLiteral("{\"schema_version\":1,\"disabled\":[]}\n"), error);
}

} // namespace

bool initializeStudioUserDataRoot(const QString &agentRoot, QString *error,
                                  const QString &targetRootOverride) {
    const QString target = targetRootOverride.isEmpty()
        ? studioDataRoot(agentRoot) : QDir::cleanPath(QFileInfo(targetRootOverride).absoluteFilePath());
    const QFileInfo targetInfo(target);
    if (targetInfo.exists()) {
        if (!targetInfo.isDir() || targetInfo.isSymLink())
            return fail(error, QStringLiteral("Studio data path exists but is not a regular directory: %1").arg(target));
        const QString legacy = QDir(agentRoot).absoluteFilePath(QStringLiteral("studio_data"));
        if (QFileInfo(legacy).isDir() && !QFileInfo(legacy).isSymLink())
            return importLegacyIntoExisting(legacy, target, error);
        return seedConfiguration(target, error);
    }

    const QString legacy = QDir(agentRoot).absoluteFilePath(QStringLiteral("studio_data"));
    const QFileInfo legacyInfo(legacy);
    if (!legacyInfo.exists()) {
        if (!QDir().mkpath(target))
            return fail(error, QStringLiteral("Cannot create Studio data directory: %1").arg(target));
        QFile::setPermissions(target, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
        return seedConfiguration(target, error);
    }

    const QString staging = target + QStringLiteral(".migrating-")
        + QUuid::createUuid().toString(QUuid::WithoutBraces);
    if (!copyTree(legacy, staging, error)) {
        QDir(staging).removeRecursively();
        return false;
    }
    if (!markLegacyImportComplete(staging, error)) {
        QDir(staging).removeRecursively();
        return false;
    }
    QFile::setPermissions(staging, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
    if (QFileInfo::exists(target) || !QDir().rename(staging, target)) {
        QDir(staging).removeRecursively();
        return fail(error, QStringLiteral("Studio data migration target appeared or could not be atomically installed: %1").arg(target));
    }
    return seedConfiguration(target, error);
}
