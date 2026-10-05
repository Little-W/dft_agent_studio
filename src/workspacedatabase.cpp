#include "workspacedatabase.h"

#include "studiopaths.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QSet>
#include <QVariantList>
#include <QUuid>

#include <sqlite3.h>

#include <algorithm>
#include <cmath>
#include <memory>

namespace {
struct DatabaseCloser {
    void operator()(sqlite3 *database) const {
        if (database)
            sqlite3_close(database);
    }
};
using Database = std::unique_ptr<sqlite3, DatabaseCloser>;

struct StatementCloser {
    void operator()(sqlite3_stmt *statement) const {
        if (statement)
            sqlite3_finalize(statement);
    }
};
using Statement = std::unique_ptr<sqlite3_stmt, StatementCloser>;

QString utcNow() {
    return QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
}

int estimateTokens(const QString &text) {
    int asciiCount = 0;
    for (const QChar character : text) {
        if (character.unicode() < 128)
            ++asciiCount;
    }
    const int baseline = static_cast<int>(std::ceil(asciiCount / 4.0)) + text.size() - asciiCount;
    return qMax(1, static_cast<int>(std::ceil(baseline * 1.5)));
}

QString fitText(QString text, int tokenLimit) {
    if (estimateTokens(text) <= tokenLimit)
        return text;
    const QString marker = QStringLiteral("\n[内容已截短]");
    qsizetype low = 0;
    qsizetype high = text.size();
    while (low < high) {
        const qsizetype middle = (low + high + 1) / 2;
        if (estimateTokens(text.first(middle).trimmed() + marker) <= tokenLimit)
            low = middle;
        else
            high = middle - 1;
    }
    return text.first(low).trimmed() + marker;
}

bool execute(sqlite3 *database, const char *sql, QString *error) {
    char *message = nullptr;
    const int code = sqlite3_exec(database, sql, nullptr, nullptr, &message);
    if (code == SQLITE_OK)
        return true;
    *error = QString::fromUtf8(message ? message : sqlite3_errmsg(database));
    sqlite3_free(message);
    return false;
}

Statement prepare(sqlite3 *database, const char *sql, QString *error) {
    sqlite3_stmt *statement = nullptr;
    if (sqlite3_prepare_v2(database, sql, -1, &statement, nullptr) != SQLITE_OK) {
        *error = QString::fromUtf8(sqlite3_errmsg(database));
        return {};
    }
    return Statement(statement);
}

void bindText(sqlite3_stmt *statement, int index, const QString &value) {
    const QByteArray bytes = value.toUtf8();
    sqlite3_bind_text(statement, index, bytes.constData(), bytes.size(), SQLITE_TRANSIENT);
}

QString columnText(sqlite3_stmt *statement, int index) {
    const auto *value = sqlite3_column_text(statement, index);
    return value ? QString::fromUtf8(reinterpret_cast<const char *>(value)) : QString{};
}

bool hasColumn(sqlite3 *database, const QString &table, const QString &column, QString *error) {
    const QByteArray sql = QStringLiteral("PRAGMA table_info(%1)").arg(table).toUtf8();
    auto statement = prepare(database, sql.constData(), error);
    if (!statement)
        return false;
    int code = SQLITE_ROW;
    while ((code = sqlite3_step(statement.get())) == SQLITE_ROW) {
        if (columnText(statement.get(), 1) == column)
            return true;
    }
    if (code != SQLITE_DONE)
        *error = QString::fromUtf8(sqlite3_errmsg(database));
    return false;
}

QVariantMap errorResult(const QString &message) {
    return {{QStringLiteral("ok"), false}, {QStringLiteral("message"), message}};
}

QStringList stringList(const QVariant &value) {
    QStringList values;
    const QVariantList raw = value.toList();
    QSet<QString> unique;
    for (const QVariant &item : raw) {
        if (!item.canConvert<QString>())
            continue;
        const QString text = item.toString();
        if (!text.isEmpty() && !unique.contains(text)) {
            unique.insert(text);
            values.append(text);
        }
    }
    std::sort(values.begin(), values.end());
    return values;
}

QVariantMap selection(sqlite3 *database, const QString &projectId, QString *error) {
    auto statement = prepare(database,
        "SELECT skill_ids_json, skills_explicit, rule_ids_json, rules_explicit, hooks_enabled "
        "FROM workspace_selection WHERE project_id = ?", error);
    if (!statement)
        return {};
    bindText(statement.get(), 1, projectId);
    const int code = sqlite3_step(statement.get());
    if (code == SQLITE_DONE) {
        return {{QStringLiteral("skill_ids"), QStringList{}}, {QStringLiteral("skills_explicit"), false},
                {QStringLiteral("rule_ids"), QStringList{}}, {QStringLiteral("rules_explicit"), false},
                {QStringLiteral("hooks_enabled"), false}};
    }
    if (code != SQLITE_ROW) {
        *error = QString::fromUtf8(sqlite3_errmsg(database));
        return {};
    }
    auto readList = [](const QString &json) {
        QJsonParseError parseError;
        const auto document = QJsonDocument::fromJson(json.toUtf8(), &parseError);
        QStringList values;
        if (parseError.error == QJsonParseError::NoError && document.isArray()) {
            for (const QJsonValue &value : document.array()) {
                if (value.isString())
                    values.append(value.toString());
            }
        }
        return values;
    };
    return {{QStringLiteral("skill_ids"), readList(columnText(statement.get(), 0))},
            {QStringLiteral("skills_explicit"), sqlite3_column_int(statement.get(), 1) != 0},
            {QStringLiteral("rule_ids"), readList(columnText(statement.get(), 2))},
            {QStringLiteral("rules_explicit"), sqlite3_column_int(statement.get(), 3) != 0},
            {QStringLiteral("hooks_enabled"), sqlite3_column_int(statement.get(), 4) != 0}};
}

QVariantMap run(sqlite3 *database, const QString &action, const QString &projectId,
                const QVariantMap &payload, QString *error) {
    if (action == QStringLiteral("selection_get"))
        return {{QStringLiteral("selection"), selection(database, projectId, error)}};
    if (action == QStringLiteral("selection_update")) {
        const QStringList skillIds = stringList(payload.value(QStringLiteral("skill_ids")));
        const QStringList ruleIds = stringList(payload.value(QStringLiteral("rule_ids")));
        const QByteArray skillJson = QJsonDocument(QJsonArray::fromStringList(skillIds)).toJson(QJsonDocument::Compact);
        const QByteArray ruleJson = QJsonDocument(QJsonArray::fromStringList(ruleIds)).toJson(QJsonDocument::Compact);
        auto statement = prepare(database,
            "INSERT INTO workspace_selection(project_id, skill_ids_json, skills_explicit, rule_ids_json, rules_explicit, hooks_enabled, updated_at) "
            "VALUES (?, ?, ?, ?, ?, 0, ?) ON CONFLICT(project_id) DO UPDATE SET "
            "skill_ids_json=excluded.skill_ids_json, skills_explicit=excluded.skills_explicit, "
            "rule_ids_json=excluded.rule_ids_json, rules_explicit=excluded.rules_explicit, "
            "hooks_enabled=excluded.hooks_enabled, updated_at=excluded.updated_at", error);
        if (!statement)
            return {};
        bindText(statement.get(), 1, projectId);
        sqlite3_bind_text(statement.get(), 2, skillJson.constData(), skillJson.size(), SQLITE_TRANSIENT);
        sqlite3_bind_int(statement.get(), 3, payload.value(QStringLiteral("skills_explicit")).toBool());
        sqlite3_bind_text(statement.get(), 4, ruleJson.constData(), ruleJson.size(), SQLITE_TRANSIENT);
        sqlite3_bind_int(statement.get(), 5, payload.value(QStringLiteral("rules_explicit")).toBool());
        bindText(statement.get(), 6, utcNow());
        if (sqlite3_step(statement.get()) != SQLITE_DONE) {
            *error = QString::fromUtf8(sqlite3_errmsg(database));
            return {};
        }
        return {{QStringLiteral("selection"), selection(database, projectId, error)}};
    }

    const QString threadId = payload.value(QStringLiteral("thread_id")).toString();
    if (action == QStringLiteral("memory_list")) {
        auto statement = prepare(database,
            "SELECT memory_id, project_id, thread_id, title, content, enabled, created_at, updated_at "
            "FROM workspace_memory WHERE project_id = ? AND (thread_id = '' OR thread_id = ?) "
            "ORDER BY updated_at DESC", error);
        if (!statement)
            return {};
        bindText(statement.get(), 1, projectId);
        bindText(statement.get(), 2, threadId);
        QVariantList memories;
        int code = SQLITE_ROW;
        while ((code = sqlite3_step(statement.get())) == SQLITE_ROW) {
            memories.append(QVariantMap{
                {QStringLiteral("memory_id"), columnText(statement.get(), 0)},
                {QStringLiteral("project_id"), columnText(statement.get(), 1)},
                {QStringLiteral("thread_id"), columnText(statement.get(), 2)},
                {QStringLiteral("title"), columnText(statement.get(), 3)},
                {QStringLiteral("content"), columnText(statement.get(), 4)},
                {QStringLiteral("enabled"), sqlite3_column_int(statement.get(), 5)},
                {QStringLiteral("created_at"), columnText(statement.get(), 6)},
                {QStringLiteral("updated_at"), columnText(statement.get(), 7)},
            });
        }
        if (code != SQLITE_DONE) {
            *error = QString::fromUtf8(sqlite3_errmsg(database));
            return {};
        }
        return {{QStringLiteral("memories"), memories}};
    }

    const QString memoryId = payload.value(QStringLiteral("memory_id")).toString().trimmed();
    if (action == QStringLiteral("memory_delete")) {
        auto statement = prepare(database,
            "DELETE FROM workspace_memory WHERE memory_id = ? AND project_id = ?", error);
        if (!statement)
            return {};
        bindText(statement.get(), 1, memoryId);
        bindText(statement.get(), 2, projectId);
        if (sqlite3_step(statement.get()) != SQLITE_DONE) {
            *error = QString::fromUtf8(sqlite3_errmsg(database));
            return {};
        }
        return {{QStringLiteral("deleted"), sqlite3_changes(database) > 0}};
    }
    if (action == QStringLiteral("memory_toggle")) {
        auto statement = prepare(database,
            "UPDATE workspace_memory SET enabled = ?, updated_at = ? WHERE memory_id = ? AND project_id = ?", error);
        if (!statement)
            return {};
        sqlite3_bind_int(statement.get(), 1, payload.value(QStringLiteral("enabled")).toBool());
        bindText(statement.get(), 2, utcNow());
        bindText(statement.get(), 3, memoryId);
        bindText(statement.get(), 4, projectId);
        if (sqlite3_step(statement.get()) != SQLITE_DONE) {
            *error = QString::fromUtf8(sqlite3_errmsg(database));
            return {};
        }
        return {{QStringLiteral("updated"), sqlite3_changes(database) > 0}};
    }
    if (action == QStringLiteral("memory_save")) {
        const QString title = payload.value(QStringLiteral("title")).toString().trimmed().left(160);
        const QString content = fitText(payload.value(QStringLiteral("content")).toString().trimmed(), 900);
        if (title.isEmpty() || content.isEmpty()) {
            *error = QStringLiteral("记忆需要标题和内容。");
            return {};
        }
        const QString identifier = memoryId.isEmpty() ? QUuid::createUuid().toString(QUuid::WithoutBraces) : memoryId;
        int enabled = 1;
        auto existing = prepare(database,
            "SELECT project_id, enabled FROM workspace_memory WHERE memory_id = ?", error);
        if (!existing)
            return {};
        bindText(existing.get(), 1, identifier);
        const int existingCode = sqlite3_step(existing.get());
        if (existingCode == SQLITE_ROW) {
            if (columnText(existing.get(), 0) != projectId) {
                *error = QStringLiteral("记忆不属于当前项目。");
                return {};
            }
            enabled = sqlite3_column_int(existing.get(), 1);
        } else if (existingCode != SQLITE_DONE) {
            *error = QString::fromUtf8(sqlite3_errmsg(database));
            return {};
        }
        const QString now = utcNow();
        auto statement = prepare(database,
            "INSERT INTO workspace_memory(memory_id, project_id, thread_id, title, content, enabled, created_at, updated_at) "
            "VALUES (?, ?, ?, ?, ?, ?, ?, ?) ON CONFLICT(memory_id) DO UPDATE SET "
            "title=excluded.title, content=excluded.content, thread_id=excluded.thread_id, updated_at=excluded.updated_at", error);
        if (!statement)
            return {};
        bindText(statement.get(), 1, identifier);
        bindText(statement.get(), 2, projectId);
        bindText(statement.get(), 3, threadId);
        bindText(statement.get(), 4, title);
        bindText(statement.get(), 5, content);
        sqlite3_bind_int(statement.get(), 6, enabled);
        bindText(statement.get(), 7, now);
        bindText(statement.get(), 8, now);
        if (sqlite3_step(statement.get()) != SQLITE_DONE) {
            *error = QString::fromUtf8(sqlite3_errmsg(database));
            return {};
        }
        return {{QStringLiteral("memory"), QVariantMap{
            {QStringLiteral("memory_id"), identifier}, {QStringLiteral("project_id"), projectId},
            {QStringLiteral("thread_id"), threadId}, {QStringLiteral("title"), title},
            {QStringLiteral("content"), content}, {QStringLiteral("enabled"), enabled},
            {QStringLiteral("updated_at"), now},
        }}};
    }
    return {};
}
}

bool WorkspaceDatabase::supports(const QString &action) {
    return action == QStringLiteral("selection_get")
        || action == QStringLiteral("selection_update")
        || action == QStringLiteral("memory_list")
        || action == QStringLiteral("memory_save")
        || action == QStringLiteral("memory_delete")
        || action == QStringLiteral("memory_toggle");
}

QVariantMap WorkspaceDatabase::dispatch(const QString &action, const QString &projectId,
                                        const QVariantMap &payload, const QString &agentRoot,
                                        const QString &databasePath) {
    if (!supports(action) || projectId.trimmed().isEmpty())
        return errorResult(QStringLiteral("工作区数据库操作或项目标识不可用。"));
    const QString path = databasePath.isEmpty()
        ? QDir(studioDataRoot(agentRoot)).filePath(QStringLiteral("agent_runtime/codex_workspace.sqlite3"))
        : databasePath;
    if (!QDir().mkpath(QFileInfo(path).absolutePath()))
        return errorResult(QStringLiteral("无法创建工作区数据库目录。"));
    sqlite3 *raw = nullptr;
    const QByteArray pathBytes = QFileInfo(path).absoluteFilePath().toUtf8();
    if (sqlite3_open_v2(pathBytes.constData(), &raw,
                        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX, nullptr) != SQLITE_OK) {
        const QString message = raw ? QString::fromUtf8(sqlite3_errmsg(raw)) : QStringLiteral("无法打开工作区数据库。");
        if (raw)
            sqlite3_close(raw);
        return errorResult(message);
    }
    Database database(raw);
    sqlite3_busy_timeout(database.get(), 15'000);
    QString error;
    QVariantMap result;
    if (!execute(database.get(), "PRAGMA journal_mode=WAL", &error))
        return errorResult(error);
    if (!execute(database.get(), "BEGIN IMMEDIATE", &error))
        return errorResult(error);
    if (!execute(database.get(),
            "CREATE TABLE IF NOT EXISTS workspace_memory ("
            "memory_id TEXT PRIMARY KEY, project_id TEXT NOT NULL, thread_id TEXT NOT NULL, "
            "title TEXT NOT NULL, content TEXT NOT NULL, enabled INTEGER NOT NULL DEFAULT 1, "
            "created_at TEXT NOT NULL, updated_at TEXT NOT NULL)", &error)
        || !execute(database.get(),
            "CREATE INDEX IF NOT EXISTS workspace_memory_scope "
            "ON workspace_memory(project_id, thread_id, updated_at DESC)", &error)
        || !execute(database.get(),
            "CREATE TABLE IF NOT EXISTS workspace_selection ("
            "project_id TEXT PRIMARY KEY, skill_ids_json TEXT NOT NULL, skills_explicit INTEGER NOT NULL DEFAULT 0, "
            "rule_ids_json TEXT NOT NULL, rules_explicit INTEGER NOT NULL DEFAULT 0, "
            "hooks_enabled INTEGER NOT NULL DEFAULT 0, updated_at TEXT NOT NULL)", &error))
        goto rollback;
    if (!hasColumn(database.get(), QStringLiteral("workspace_selection"), QStringLiteral("rules_explicit"), &error)
        && error.isEmpty()
        && !execute(database.get(), "ALTER TABLE workspace_selection ADD COLUMN rules_explicit INTEGER NOT NULL DEFAULT 0", &error))
        goto rollback;
    if (!hasColumn(database.get(), QStringLiteral("workspace_selection"), QStringLiteral("skills_explicit"), &error)
        && error.isEmpty()
        && !execute(database.get(), "ALTER TABLE workspace_selection ADD COLUMN skills_explicit INTEGER NOT NULL DEFAULT 0", &error))
        goto rollback;
    result = run(database.get(), action, projectId, payload, &error);
    if (!error.isEmpty()) {
        goto rollback;
    }
    if (!execute(database.get(), "COMMIT", &error)) {
        QString ignored;
        execute(database.get(), "ROLLBACK", &ignored);
        return errorResult(error);
    }
    return {{QStringLiteral("ok"), true}, {QStringLiteral("result"), result}};

rollback:
    QString ignored;
    execute(database.get(), "ROLLBACK", &ignored);
    return errorResult(error);
}
