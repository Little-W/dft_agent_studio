#include "studiouserdata.h"
#include "studiopaths.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QDebug>
#include <QTemporaryDir>

#include <sqlite3.h>

namespace {
bool require(bool condition, const char *message) {
    if (!condition)
        qCritical("%s", message);
    return condition;
}

bool writeFile(const QString &path, const QByteArray &content) {
    if (!QDir().mkpath(QFileInfo(path).absolutePath()))
        return false;
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(content) == content.size();
}

bool createWorkspaceDatabase(const QString &path, const char *projectId) {
    if (!QDir().mkpath(QFileInfo(path).absolutePath()))
        return false;
    sqlite3 *database = nullptr;
    if (sqlite3_open(QFile::encodeName(path).constData(), &database) != SQLITE_OK) {
        if (database)
            sqlite3_close(database);
        return false;
    }
    const char *schema =
        "CREATE TABLE workspace_memory(memory_id TEXT PRIMARY KEY,project_id TEXT NOT NULL,thread_id TEXT NOT NULL,title TEXT NOT NULL,content TEXT NOT NULL,enabled INTEGER NOT NULL DEFAULT 1,created_at TEXT NOT NULL,updated_at TEXT NOT NULL);"
        "CREATE TABLE workspace_selection(project_id TEXT PRIMARY KEY,skill_ids_json TEXT NOT NULL,skills_explicit INTEGER NOT NULL DEFAULT 0,rule_ids_json TEXT NOT NULL,rules_explicit INTEGER NOT NULL DEFAULT 0,hooks_enabled INTEGER NOT NULL DEFAULT 0,updated_at TEXT NOT NULL);";
    QByteArray insert = QByteArrayLiteral("INSERT INTO workspace_selection(project_id,skill_ids_json,skills_explicit,rule_ids_json,rules_explicit,hooks_enabled,updated_at) VALUES('")
        + projectId + QByteArrayLiteral("','[]',0,'[]',0,0,'2026-10-01');");
    char *error = nullptr;
    const bool ok = sqlite3_exec(database, schema, nullptr, nullptr, &error) == SQLITE_OK
        && sqlite3_exec(database, insert.constData(), nullptr, nullptr, &error) == SQLITE_OK;
    sqlite3_free(error);
    sqlite3_close(database);
    return ok;
}

int workspaceSelectionCount(const QString &path) {
    sqlite3 *database = nullptr;
    if (sqlite3_open_v2(QFile::encodeName(path).constData(), &database, SQLITE_OPEN_READONLY, nullptr) != SQLITE_OK) {
        if (database)
            sqlite3_close(database);
        return -1;
    }
    sqlite3_stmt *statement = nullptr;
    int count = -1;
    if (sqlite3_prepare_v2(database, "SELECT COUNT(*) FROM workspace_selection", -1, &statement, nullptr) == SQLITE_OK
        && sqlite3_step(statement) == SQLITE_ROW)
        count = sqlite3_column_int(statement, 0);
    sqlite3_finalize(statement);
    sqlite3_close(database);
    return count;
}
}

int main() {
    bool ok = true;
    const QString expectedUserRoot = QDir::home().filePath(QStringLiteral(".dft_agent_studio"));
    ok &= require(QDir::cleanPath(studioDataRoot()) == QDir::cleanPath(expectedUserRoot),
                  "production data root resolves under the user's home directory");

    QTemporaryDir temporary;
    if (!temporary.isValid())
        return 1;
    const QString agentRoot = temporary.filePath(QStringLiteral("checkout"));
    const QString legacy = QDir(agentRoot).filePath(QStringLiteral("studio_data"));
    const QString target = temporary.filePath(QStringLiteral("home/.dft_agent_studio"));
    const QString outside = temporary.filePath(QStringLiteral("library/rtl.v"));
    ok &= require(writeFile(QDir(legacy).filePath(QStringLiteral("config/projects.json")), "{\"projects\":[]}"),
                  "legacy config fixture is created");
    ok &= require(writeFile(outside, "module rtl; endmodule\n"), "external link target fixture is created");
    const QString link = QDir(legacy).filePath(QStringLiteral("runs/run-1/rtl"));
    QDir().mkpath(QFileInfo(link).absolutePath());
    ok &= require(QFile::link(outside, link), "legacy run evidence symlink fixture is created");

    QString error;
    ok &= require(initializeStudioUserDataRoot(agentRoot, &error, target),
                  "legacy data is copied into the user root");
    QFile copied(QDir(target).filePath(QStringLiteral("config/projects.json")));
    ok &= require(copied.open(QIODevice::ReadOnly)
                      && copied.readAll() == QByteArray("{\"projects\":[]}"),
                  "configuration bytes are preserved");
    ok &= require(QFileInfo::exists(QDir(legacy).filePath(QStringLiteral("config/projects.json"))),
                  "legacy source remains untouched");
    const QFileInfo copiedLink(QDir(target).filePath(QStringLiteral("runs/run-1/rtl")));
    ok &= require(copiedLink.isSymLink() && copiedLink.symLinkTarget() == QFileInfo(outside).absoluteFilePath(),
                  "external run evidence symlinks are recreated without following them");

    ok &= require(writeFile(QDir(target).filePath(QStringLiteral("config/projects.json")), "new-data"),
                  "existing target can be modified by the application");
    ok &= require(initializeStudioUserDataRoot(agentRoot, &error, target),
                  "reopening an existing user data root succeeds");
    QFile retained(QDir(target).filePath(QStringLiteral("config/projects.json")));
    ok &= require(retained.open(QIODevice::ReadOnly) && retained.readAll() == QByteArray("new-data"),
                  "an existing user root is never overwritten by legacy data");

    const QString mergeRoot = temporary.filePath(QStringLiteral("merge-checkout"));
    const QString mergeTarget = temporary.filePath(QStringLiteral("merge-home/.dft_agent_studio"));
    const QByteArray oldProjects = QByteArrayLiteral("{\"schema_version\":1,\"projects\":[{\"id\":\"legacy\"}]}\n");
    ok &= require(writeFile(QDir(mergeRoot).filePath(QStringLiteral("studio_data/config/projects.json")), oldProjects),
                  "legacy project configuration is prepared for a non-destructive merge");
    ok &= require(writeFile(QDir(mergeTarget).filePath(QStringLiteral("config/projects.json")),
                            "{\"schema_version\":1,\"projects\":[]}\n"),
                  "fresh user configuration fixture is prepared");
    const QString oldRun = QDir(mergeRoot).filePath(QStringLiteral("studio_data/runs/old-run/report.rpt"));
    ok &= require(writeFile(oldRun, "legacy report"), "legacy report fixture is created");
    const QString oldDatabase = QDir(mergeRoot).filePath(QStringLiteral("studio_data/agent_runtime/codex_workspace.sqlite3"));
    const QString newDatabase = QDir(mergeTarget).filePath(QStringLiteral("agent_runtime/codex_workspace.sqlite3"));
    ok &= require(createWorkspaceDatabase(oldDatabase, "legacy-project"), "legacy workspace database fixture is created");
    ok &= require(createWorkspaceDatabase(newDatabase, "new-project"), "current workspace database fixture is created");
    ok &= require(initializeStudioUserDataRoot(mergeRoot, &error, mergeTarget),
                  "legacy records merge into a partially initialized user data root");
    QFile mergedProjects(QDir(mergeTarget).filePath(QStringLiteral("config/projects.json")));
    ok &= require(mergedProjects.open(QIODevice::ReadOnly) && mergedProjects.readAll() == oldProjects,
                  "a blank bootstrap project list is restored from the legacy configuration");
    ok &= require(QFileInfo::exists(QDir(mergeTarget).filePath(QStringLiteral("runs/old-run/report.rpt"))),
                  "missing legacy reports are imported");
    ok &= require(workspaceSelectionCount(newDatabase) == 2,
                  "workspace database rows are merged without replacing current rows");
    return ok ? 0 : 1;
}
