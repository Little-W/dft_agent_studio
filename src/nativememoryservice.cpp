#include "nativememoryservice.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QSaveFile>
#include <QRegularExpression>
#include <QSet>
#include <QUuid>
#include <algorithm>
#include <sqlite3.h>

namespace {

const QSet<QString> kImportantFields = {
    QStringLiteral("action"), QStringLiteral("allowed_actions"),
    QStringLiteral("applicable_when"), QStringLiteral("changed_files"),
    QStringLiteral("atpg_fault_classes"), QStringLiteral("atpg_timed_out"),
    QStringLiteral("blockers"), QStringLiteral("coverage_percent"),
    QStringLiteral("coverage_target_percent"), QStringLiteral("compile_strategy"),
    QStringLiteral("cross_validation"), QStringLiteral("decision"), QStringLiteral("diagnosis"),
    QStringLiteral("dft_drc"), QStringLiteral("errors"), QStringLiteral("error"),
    QStringLiteral("episode_id"), QStringLiteral("evidence"),
    QStringLiteral("evidence_file"), QStringLiteral("fault_coverage"),
    QStringLiteral("mbist"), QStringLiteral("mbist_diagnostic_mode"),
    QStringLiteral("mbist_include_mode"), QStringLiteral("missing_module_names"),
    QStringLiteral("objective_met"), QStringLiteral("post_dft_drc"),
    QStringLiteral("post_dft_drc_breakdown"), QStringLiteral("post_dft_drc_limit"),
    QStringLiteral("effective_fix"), QStringLiteral("profile"), QStringLiteral("reason"), QStringLiteral("note"),
    QStringLiteral("repair_method"), QStringLiteral("root_cause"), QStringLiteral("validation"),
    QStringLiteral("test_result"), QStringLiteral("modifications"), QStringLiteral("reasoning"),
    QStringLiteral("report_file"), QStringLiteral("round"), QStringLiteral("step"),
    QStringLiteral("maximum_actions"), QStringLiteral("last_result"),
    QStringLiteral("previous_actions"), QStringLiteral("previous_unresolved_reference_count"),
    QStringLiteral("path"), QStringLiteral("relative_path"), QStringLiteral("sha256"),
    QStringLiteral("line_number"), QStringLiteral("start_line"),
    QStringLiteral("end_line"), QStringLiteral("matches"), QStringLiteral("status"),
    QStringLiteral("stop_reason"), QStringLiteral("source_compatibility"),
    QStringLiteral("source_dependency_mode"), QStringLiteral("source_exclude_files"),
    QStringLiteral("source_extra_files"), QStringLiteral("source_manifest_dependencies"),
    QStringLiteral("source_preprocess_mode"), QStringLiteral("supervisor_warning"),
    QStringLiteral("terminal_diagnostics"), QStringLiteral("timed_out"),
    QStringLiteral("workspace"), QStringLiteral("workspace_repair"),
    QStringLiteral("verdict"), QStringLiteral("corrected_response")
};

QJsonValue compactJsonValue(const QVariant &value)
{
    const QJsonValue json = QJsonValue::fromVariant(value);
    if (json.isObject())
        return QJsonObject::fromVariantMap(value.toMap());
    if (json.isArray())
        return QJsonArray::fromVariantList(value.toList());
    return json;
}

QByteArray jsonBytes(const QVariant &value, bool indented = false)
{
    const QJsonValue json = compactJsonValue(value);
    const auto format = indented ? QJsonDocument::Indented : QJsonDocument::Compact;
    if (json.isObject())
        return QJsonDocument(json.toObject()).toJson(format);
    if (json.isArray())
        return QJsonDocument(json.toArray()).toJson(format);
    return QJsonDocument(QJsonArray{json}).toJson(format).sliced(1, QJsonDocument(QJsonArray{json}).toJson(format).size() - 3);
}

QVariant importantView(const QVariant &value, int depth = 0)
{
    if (depth > 5)
        return value.toString().left(240);
    if (value.metaType().id() == QMetaType::QVariantMap) {
        QVariantMap selected;
        const QVariantMap map = value.toMap();
        for (auto it = map.cbegin(); it != map.cend(); ++it) {
            if (kImportantFields.contains(it.key())) {
                selected.insert(it.key(), importantView(it.value(), depth + 1));
                continue;
            }
            const QVariant nested = importantView(it.value(), depth + 1);
            if ((nested.metaType().id() == QMetaType::QVariantMap && !nested.toMap().isEmpty())
                || (nested.metaType().id() == QMetaType::QVariantList && !nested.toList().isEmpty()))
                selected.insert(it.key(), nested);
        }
        return selected;
    }
    if (value.metaType().id() == QMetaType::QVariantList) {
        QVariantList selected;
        const QVariantList list = value.toList();
        const int count = int(std::min<qsizetype>(16, list.size()));
        for (int i = 0; i < count; ++i)
            selected.push_back(importantView(list.at(i), depth + 1));
        return selected;
    }
    if (value.metaType().id() == QMetaType::QString) {
        const QString text = value.toString();
        const QString trimmed = text.trimmed();
        if (!trimmed.isEmpty() && (trimmed.startsWith(QLatin1Char('{')) || trimmed.startsWith(QLatin1Char('[')))) {
            QJsonParseError parseError;
            const QJsonDocument parsed = QJsonDocument::fromJson(text.toUtf8(), &parseError);
            if (parseError.error == QJsonParseError::NoError && !parsed.isNull())
                return importantView(parsed.toVariant(), depth + 1);
        }
        return text.left(600);
    }
    return value;
}

bool execSql(sqlite3 *db, const char *sql, QString *error)
{
    char *message = nullptr;
    if (sqlite3_exec(db, sql, nullptr, nullptr, &message) == SQLITE_OK)
        return true;
    if (error)
        *error = QString::fromUtf8(message ? message : sqlite3_errmsg(db));
    sqlite3_free(message);
    return false;
}

class Database {
public:
    explicit Database(const QString &path)
    {
        const QByteArray utf8Path = path.toUtf8();
        if (sqlite3_open_v2(utf8Path.constData(), &handle,
                            SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX,
                            nullptr) == SQLITE_OK) {
            sqlite3_busy_timeout(handle, 30000);
            valid = true;
        }
    }
    ~Database() { if (handle) sqlite3_close(handle); }
    sqlite3 *handle = nullptr;
    bool valid = false;
};

QString queryError(sqlite3 *db)
{
    return QString::fromUtf8(sqlite3_errmsg(db));
}

void bindText(sqlite3_stmt *statement, int index, const QString &value);

QString utcTimestamp()
{
    return QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyy-MM-dd'T'HH:mm:ss+00:00"));
}

QString newHexUuid()
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces).remove(QLatin1Char('-'));
}

QString absolutePath(const QString &path)
{
    const QFileInfo info(path);
    const QString canonical = info.canonicalFilePath();
    return canonical.isEmpty() ? info.absoluteFilePath() : canonical;
}

bool insertEvent(sqlite3 *db, const QString &projectId, const QString &eventId,
                 const QString &kind, const QVariantMap &content, bool critical,
                 const QString &sourceFile, QString *error)
{
    const QByteArray contentJson = jsonBytes(content);
    const QByteArray summaryJson = jsonBytes(importantView(content));
    sqlite3_stmt *statement = nullptr;
    const char *sql = "INSERT INTO events VALUES (?, ?, ?, ?, ?, ?, ?, ?)";
    if (sqlite3_prepare_v2(db, sql, -1, &statement, nullptr) != SQLITE_OK) {
        if (error)
            *error = queryError(db);
        return false;
    }
    bindText(statement, 1, eventId);
    bindText(statement, 2, projectId);
    bindText(statement, 3, utcTimestamp());
    bindText(statement, 4, kind);
    sqlite3_bind_text(statement, 5, contentJson.constData(), contentJson.size(), SQLITE_TRANSIENT);
    sqlite3_bind_text(statement, 6, summaryJson.constData(), summaryJson.size(), SQLITE_TRANSIENT);
    sqlite3_bind_int(statement, 7, critical ? 1 : 0);
    bindText(statement, 8, sourceFile);
    const bool ok = sqlite3_step(statement) == SQLITE_DONE;
    sqlite3_finalize(statement);
    if (!ok && error)
        *error = queryError(db);
    return ok;
}

bool insertMemory(sqlite3 *db, const QString &projectId, const QString &category,
                  const QVariantMap &content, const QString &evidenceFile,
                  const QString &status, QString *memoryId, QString *error)
{
    const QString normalized = QString::fromUtf8(jsonBytes(importantView(content)));
    const QString identity = projectId + QLatin1Char('\n') + category + QLatin1Char('\n')
        + normalized + QLatin1Char('\n') + evidenceFile + QLatin1Char('\n') + status;
    const QString id = QString::fromLatin1(QCryptographicHash::hash(identity.toUtf8(), QCryptographicHash::Sha256).toHex());
    sqlite3_stmt *statement = nullptr;
    const char *sql = "INSERT OR IGNORE INTO project_memory VALUES (?, ?, ?, ?, ?, ?, ?)";
    if (sqlite3_prepare_v2(db, sql, -1, &statement, nullptr) != SQLITE_OK) {
        if (error)
            *error = queryError(db);
        return false;
    }
    bindText(statement, 1, id);
    bindText(statement, 2, projectId);
    bindText(statement, 3, utcTimestamp());
    bindText(statement, 4, category);
    bindText(statement, 5, normalized);
    bindText(statement, 6, evidenceFile);
    bindText(statement, 7, status);
    const bool ok = sqlite3_step(statement) == SQLITE_DONE;
    sqlite3_finalize(statement);
    if (!ok && error)
        *error = queryError(db);
    if (ok && memoryId)
        *memoryId = id;
    return ok;
}

QVariant boundedValue(const QVariant &value, int stringLimit, int listLimit, int depth = 0)
{
    if (depth > 7)
        return QString::fromUtf8(jsonBytes(value)).left(240);
    if (value.metaType().id() == QMetaType::QVariantMap) {
        QVariantMap result;
        const QVariantMap map = value.toMap();
        for (auto it = map.cbegin(); it != map.cend(); ++it)
            result.insert(it.key(), boundedValue(it.value(), stringLimit, listLimit, depth + 1));
        return result;
    }
    if (value.metaType().id() == QMetaType::QVariantList) {
        QVariantList result;
        const QVariantList list = value.toList();
        for (int i = 0; i < int(std::min<qsizetype>(listLimit, list.size())); ++i)
            result.push_back(boundedValue(list.at(i), stringLimit, listLimit, depth + 1));
        return result;
    }
    if (value.metaType().id() == QMetaType::QString) {
        const QString text = value.toString();
        if (text.size() <= stringLimit)
            return text;
        const int prefix = std::max(128, stringLimit / 3);
        const int suffix = std::max(128, stringLimit - prefix);
        const QString digest = QString::fromLatin1(QCryptographicHash::hash(text.toUtf8(), QCryptographicHash::Sha256).toHex());
        return text.left(prefix) + QStringLiteral("\n[已省略 %1 个字符，sha256=%2]\n").arg(text.size() - prefix - suffix).arg(digest)
            + text.right(suffix);
    }
    return value;
}

QVariant tightView(const QVariant &value, int depth = 0)
{
    if (depth > 5)
        return QString::fromUtf8(jsonBytes(value)).left(120);
    if (value.metaType().id() == QMetaType::QVariantMap) {
        QVariantMap result;
        const QVariantMap map = value.toMap();
        for (auto it = map.cbegin(); it != map.cend(); ++it)
            result.insert(it.key(), tightView(it.value(), depth + 1));
        return result;
    }
    if (value.metaType().id() == QMetaType::QVariantList) {
        QVariantList result;
        const QVariantList list = value.toList();
        for (int i = 0; i < int(std::min<qsizetype>(6, list.size())); ++i)
            result.push_back(tightView(list.at(i), depth + 1));
        return result;
    }
    if (value.metaType().id() == QMetaType::QString)
        return value.toString().left(180);
    return value;
}

QString textExcerpt(const QString &text, int maximumTokens)
{
    if (maximumTokens <= 0)
        return {};
    if (NativeMemoryService::estimateTokens(text) <= maximumTokens)
        return text;
    qsizetype low = 0;
    qsizetype high = text.size();
    while (low < high) {
        const qsizetype middle = (low + high + 1) / 2;
        if (NativeMemoryService::estimateTokens(text.left(middle)) <= maximumTokens)
            low = middle;
        else
            high = middle - 1;
    }
    return text.left(low);
}

bool writeAtomicJson(const QString &path, const QVariantMap &payload, QString *error)
{
    const QFileInfo info(path);
    if (!QDir().mkpath(info.absolutePath())) {
        if (error)
            *error = QStringLiteral("Could not create snapshot directory: %1").arg(info.absolutePath());
        return false;
    }
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error)
            *error = file.errorString();
        return false;
    }
    QByteArray bytes = QJsonDocument(QJsonObject::fromVariantMap(payload)).toJson(QJsonDocument::Indented);
    bytes.append('\n');
    if (file.write(bytes) != bytes.size() || !file.commit()) {
        if (error)
            *error = file.errorString();
        return false;
    }
    return true;
}

void bindText(sqlite3_stmt *statement, int index, const QString &value)
{
    const QByteArray utf8 = value.toUtf8();
    sqlite3_bind_text(statement, index, utf8.constData(), utf8.size(), SQLITE_TRANSIENT);
}

QStringList queryTerms(const QString &text)
{
    static const QRegularExpression terms(
        QStringLiteral("[A-Za-z0-9_.$%:/-]{2,}|[\\x{4e00}-\\x{9fff}]{2,8}"));
    QStringList result;
    QSet<QString> seen;
    auto matches = terms.globalMatch(text);
    while (matches.hasNext()) {
        const QString term = matches.next().captured().toLower();
        if (!term.trimmed().isEmpty() && !seen.contains(term)) {
            seen.insert(term);
            result.push_back(term);
        }
    }
    return result;
}

} // namespace

NativeMemoryService::NativeMemoryService(QString projectId, QString databaseFile, QString snapshotsRoot,
                                         int contextWindow, int outputTokenReserve,
                                         int configuredInputTokenLimit, int fixedPromptReserve)
    : m_projectId(projectId.trimmed().isEmpty() ? QStringLiteral("global") : projectId.trimmed())
    , m_databaseFile(std::move(databaseFile))
    , m_snapshotsRoot(snapshotsRoot.isEmpty()
          ? QFileInfo(m_databaseFile).absolutePath() + QStringLiteral("/snapshots")
          : std::move(snapshotsRoot))
    , m_contextWindow(contextWindow)
    , m_outputTokenReserve(outputTokenReserve)
    , m_configuredInputTokenLimit(configuredInputTokenLimit)
    , m_fixedPromptReserve(fixedPromptReserve)
{
}

int NativeMemoryService::estimateTokens(const QVariant &value)
{
    const QString text = value.metaType().id() == QMetaType::QString
        ? value.toString()
        : QString::fromUtf8(jsonBytes(value));
    const QVector<uint> codepoints = text.toUcs4();
    qsizetype asciiCount = 0;
    for (uint point : codepoints)
        asciiCount += point < 128;
    const qsizetype baseline = (asciiCount + 3) / 4 + codepoints.size() - asciiCount;
    return int(std::max<qsizetype>(1, (baseline * 3 + 1) / 2));
}

QVariant NativeMemoryService::compactModelValue(const QVariant &value, int maximumTokens,
                                                 QVariantMap *report)
{
    const int limit = std::max(256, maximumTokens);
    const int before = estimateTokens(value);
    QVariant bounded = boundedValue(value, 4000, 24);
    QString stage = QStringLiteral("bounded");
    if (estimateTokens(bounded) > limit) {
        bounded = tightView(importantView(bounded));
        stage = QStringLiteral("important_fields");
    }
    if (estimateTokens(bounded) > limit) {
        const QString text = QString::fromUtf8(jsonBytes(bounded));
        const QString digest = QString::fromLatin1(QCryptographicHash::hash(text.toUtf8(), QCryptographicHash::Sha256).toHex());
        const int excerptBudget = std::max(64, limit - 96);
        bounded = QVariantMap{
            {QStringLiteral("truncated"), true},
            {QStringLiteral("sha256"), digest},
            {QStringLiteral("excerpt"), textExcerpt(text, excerptBudget)}
        };
        stage = QStringLiteral("excerpt");
    }
    const int after = estimateTokens(bounded);
    if (after > limit)
        return {};
    if (report) {
        *report = {
            {QStringLiteral("estimated_tokens_before"), before},
            {QStringLiteral("estimated_tokens_after"), after},
            {QStringLiteral("token_limit"), limit},
            {QStringLiteral("compacted"), before != after || stage != QLatin1String("bounded")},
            {QStringLiteral("stage"), stage}
        };
    }
    return bounded;
}

QVariantList NativeMemoryService::compactTextContext(const QVariantList &records, int maximumTokens)
{
    QVariantList compacted = records;
    qsizetype index = 0;
    while (estimateTokens(compacted) > maximumTokens && index < compacted.size() - 2) {
        compacted[index] = importantView(compacted.at(index));
        ++index;
    }
    while (estimateTokens(compacted) > maximumTokens && compacted.size() > 2)
        compacted.removeFirst();
    return compacted;
}

bool NativeMemoryService::initialize(QString *error) const
{
    if (error)
        error->clear();
    const QFileInfo info(m_databaseFile);
    if (!QDir().mkpath(info.absolutePath())) {
        if (error)
            *error = QStringLiteral("Could not create memory database directory: %1").arg(info.absolutePath());
        return false;
    }
    Database db(m_databaseFile);
    if (!db.valid) {
        if (error)
            *error = queryError(db.handle);
        return false;
    }
    return execSql(db.handle,
        "PRAGMA journal_mode=WAL;"
        "PRAGMA synchronous=FULL;"
        "CREATE TABLE IF NOT EXISTS project_memory ("
        "memory_id TEXT PRIMARY KEY, project_id TEXT NOT NULL, created_at TEXT NOT NULL,"
        "category TEXT NOT NULL, content_json TEXT NOT NULL, evidence_file TEXT NOT NULL, status TEXT NOT NULL);"
        "CREATE INDEX IF NOT EXISTS memory_project_time ON project_memory(project_id, created_at DESC);"
        "CREATE TABLE IF NOT EXISTS short_term_memory ("
        "memory_id TEXT PRIMARY KEY, project_id TEXT NOT NULL, created_at TEXT NOT NULL, expires_at TEXT NOT NULL,"
        "category TEXT NOT NULL, content_json TEXT NOT NULL, evidence_file TEXT NOT NULL, status TEXT NOT NULL);"
        "CREATE INDEX IF NOT EXISTS short_memory_project_expiry ON short_term_memory(project_id, expires_at DESC);"
        "CREATE TABLE IF NOT EXISTS events ("
        "event_id TEXT PRIMARY KEY, project_id TEXT NOT NULL, created_at TEXT NOT NULL, kind TEXT NOT NULL,"
        "content_json TEXT NOT NULL, summary_json TEXT NOT NULL, critical INTEGER NOT NULL, source_file TEXT NOT NULL);"
        "CREATE INDEX IF NOT EXISTS events_project_time ON events(project_id, created_at DESC);"
        "CREATE TABLE IF NOT EXISTS imported_sources (source_file TEXT PRIMARY KEY, imported_at TEXT NOT NULL);",
        error);
}

bool NativeMemoryService::recordEvent(const QString &kind, const QVariantMap &content,
                                      bool critical, const QString &sourceFile,
                                      QString *eventId, QString *error) const
{
    if (error)
        error->clear();
    if (eventId)
        eventId->clear();
    if (!initialize(error))
        return false;
    Database db(m_databaseFile);
    if (!db.valid) {
        if (error)
            *error = queryError(db.handle);
        return false;
    }
    const QString id = newHexUuid();
    if (!execSql(db.handle, "BEGIN IMMEDIATE", error))
        return false;
    if (!insertEvent(db.handle, m_projectId, id, kind, content, critical, sourceFile, error)
        || !execSql(db.handle, "COMMIT", error)) {
        execSql(db.handle, "ROLLBACK", nullptr);
        return false;
    }
    if (eventId)
        *eventId = id;
    return true;
}

bool NativeMemoryService::remember(const QString &category, const QVariantMap &content,
                                   const QString &evidenceFile, const QString &status,
                                   QString *memoryId, QString *error) const
{
    if (error)
        error->clear();
    if (memoryId)
        memoryId->clear();
    if (!initialize(error))
        return false;
    const QString normalized = QString::fromUtf8(jsonBytes(importantView(content)));
    const QString identity = m_projectId + QLatin1Char('\n') + category + QLatin1Char('\n')
        + normalized + QLatin1Char('\n') + evidenceFile + QLatin1Char('\n') + status;
    const QString id = QString::fromLatin1(QCryptographicHash::hash(identity.toUtf8(), QCryptographicHash::Sha256).toHex());

    Database db(m_databaseFile);
    if (!db.valid) {
        if (error)
            *error = queryError(db.handle);
        return false;
    }
    if (!execSql(db.handle, "BEGIN IMMEDIATE", error))
        return false;
    sqlite3_stmt *statement = nullptr;
    const char *sql = "INSERT OR IGNORE INTO project_memory VALUES (?, ?, ?, ?, ?, ?, ?)";
    if (sqlite3_prepare_v2(db.handle, sql, -1, &statement, nullptr) != SQLITE_OK) {
        if (error)
            *error = queryError(db.handle);
        execSql(db.handle, "ROLLBACK", nullptr);
        return false;
    }
    bindText(statement, 1, id);
    bindText(statement, 2, m_projectId);
    bindText(statement, 3, QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyy-MM-dd'T'HH:mm:ss+00:00")));
    bindText(statement, 4, category);
    bindText(statement, 5, normalized);
    bindText(statement, 6, evidenceFile);
    bindText(statement, 7, status);
    const bool inserted = sqlite3_step(statement) == SQLITE_DONE;
    sqlite3_finalize(statement);
    if (!inserted || !execSql(db.handle, "COMMIT", error)) {
        if (error && error->isEmpty())
            *error = queryError(db.handle);
        execSql(db.handle, "ROLLBACK", nullptr);
        return false;
    }
    if (memoryId)
        *memoryId = id;
    return true;
}

bool NativeMemoryService::rememberShortTerm(const QString &category, const QVariantMap &content,
                                             const QString &evidenceFile, const QString &status,
                                             int validForHours, QString *memoryId, QString *error) const
{
    if (error)
        error->clear();
    if (memoryId)
        memoryId->clear();
    if (category.trimmed().isEmpty() || validForHours < 0 || validForHours > 720) {
        if (error)
            *error = QStringLiteral("Short-term memory requires a category and a TTL from 0 to 720 hours.");
        return false;
    }
    if (!initialize(error))
        return false;
    const QString normalized = QString::fromUtf8(jsonBytes(importantView(content)));
    const QString identity = m_projectId + QLatin1Char('\n') + category + QLatin1Char('\n')
        + normalized + QLatin1Char('\n') + evidenceFile + QLatin1Char('\n') + status;
    const QString id = QString::fromLatin1(QCryptographicHash::hash(identity.toUtf8(), QCryptographicHash::Sha256).toHex());
    Database db(m_databaseFile);
    if (!db.valid) {
        if (error)
            *error = queryError(db.handle);
        return false;
    }
    if (!execSql(db.handle, "BEGIN IMMEDIATE", error))
        return false;
    sqlite3_stmt *statement = nullptr;
    const QString now = utcTimestamp();
    if (sqlite3_prepare_v2(db.handle,
            "DELETE FROM short_term_memory WHERE project_id = ? AND expires_at <> '' AND expires_at <= ?",
            -1, &statement, nullptr) != SQLITE_OK) {
        if (error)
            *error = queryError(db.handle);
        execSql(db.handle, "ROLLBACK", nullptr);
        return false;
    }
    bindText(statement, 1, m_projectId);
    bindText(statement, 2, now);
    const bool expiredRemoved = sqlite3_step(statement) == SQLITE_DONE;
    sqlite3_finalize(statement);
    if (!expiredRemoved) {
        if (error)
            *error = queryError(db.handle);
        execSql(db.handle, "ROLLBACK", nullptr);
        return false;
    }

    QString expires;
    if (sqlite3_prepare_v2(db.handle,
            "SELECT expires_at FROM short_term_memory WHERE memory_id = ? AND project_id = ?",
            -1, &statement, nullptr) != SQLITE_OK) {
        if (error)
            *error = queryError(db.handle);
        execSql(db.handle, "ROLLBACK", nullptr);
        return false;
    }
    bindText(statement, 1, id);
    bindText(statement, 2, m_projectId);
    const bool existed = sqlite3_step(statement) == SQLITE_ROW;
    if (existed) {
        const auto *bytes = reinterpret_cast<const char *>(sqlite3_column_text(statement, 0));
        expires = QString::fromUtf8(bytes ? bytes : "");
    }
    sqlite3_finalize(statement);
    if (!existed) {
        if (sqlite3_prepare_v2(db.handle,
                "SELECT COUNT(*) FROM short_term_memory WHERE project_id = ? AND expires_at = ''",
                -1, &statement, nullptr) != SQLITE_OK) {
            if (error)
                *error = queryError(db.handle);
            execSql(db.handle, "ROLLBACK", nullptr);
            return false;
        }
        bindText(statement, 1, m_projectId);
        const int coreCount = sqlite3_step(statement) == SQLITE_ROW ? sqlite3_column_int(statement, 0) : 0;
        sqlite3_finalize(statement);
        if (coreCount >= ShortTermCoreCapacity) {
            const int ttlHours = validForHours == 0 ? 168 : validForHours;
            expires = QDateTime::currentDateTimeUtc().addSecs(ttlHours * 3600)
                .toString(QStringLiteral("yyyy-MM-dd'T'HH:mm:ss+00:00"));
            if (sqlite3_prepare_v2(db.handle,
                    "SELECT COUNT(*) FROM short_term_memory WHERE project_id = ?", -1,
                    &statement, nullptr) != SQLITE_OK) {
                if (error)
                    *error = queryError(db.handle);
                execSql(db.handle, "ROLLBACK", nullptr);
                return false;
            }
            bindText(statement, 1, m_projectId);
            const int totalCount = sqlite3_step(statement) == SQLITE_ROW ? sqlite3_column_int(statement, 0) : 0;
            sqlite3_finalize(statement);
            if (totalCount >= ShortTermTotalCapacity) {
                if (sqlite3_prepare_v2(db.handle,
                        "DELETE FROM short_term_memory WHERE memory_id = (SELECT memory_id FROM short_term_memory "
                        "WHERE project_id = ? AND expires_at <> '' ORDER BY created_at ASC, rowid ASC LIMIT 1)",
                        -1, &statement, nullptr) != SQLITE_OK) {
                    if (error)
                        *error = queryError(db.handle);
                    execSql(db.handle, "ROLLBACK", nullptr);
                    return false;
                }
                bindText(statement, 1, m_projectId);
                const bool evicted = sqlite3_step(statement) == SQLITE_DONE && sqlite3_changes(db.handle) == 1;
                sqlite3_finalize(statement);
                if (!evicted) {
                    if (error)
                        *error = QStringLiteral("Short-term memory reached its protected capacity and could not evict an expiring record.");
                    execSql(db.handle, "ROLLBACK", nullptr);
                    return false;
                }
            }
        }
    }

    const QString expiresAt = expires;
    const char *sql = "INSERT OR REPLACE INTO short_term_memory VALUES (?, ?, ?, ?, ?, ?, ?, ?)";
    if (sqlite3_prepare_v2(db.handle, sql, -1, &statement, nullptr) != SQLITE_OK) {
        if (error)
            *error = queryError(db.handle);
        execSql(db.handle, "ROLLBACK", nullptr);
        return false;
    }
    bindText(statement, 1, id);
    bindText(statement, 2, m_projectId);
    bindText(statement, 3, now);
    bindText(statement, 4, expiresAt);
    bindText(statement, 5, category.trimmed());
    bindText(statement, 6, normalized);
    bindText(statement, 7, evidenceFile);
    bindText(statement, 8, status);
    const bool inserted = sqlite3_step(statement) == SQLITE_DONE;
    sqlite3_finalize(statement);
    if (!inserted || !execSql(db.handle, "COMMIT", error)) {
        if (error && error->isEmpty())
            *error = queryError(db.handle);
        execSql(db.handle, "ROLLBACK", nullptr);
        return false;
    }
    if (memoryId)
        *memoryId = id;
    return true;
}

QVariantList NativeMemoryService::recall(const QString &query, int limit, QString *error) const
{
    if (error)
        error->clear();
    QVariantList result;
    if (!initialize(error) || limit <= 0)
        return result;
    Database db(m_databaseFile);
    if (!db.valid) {
        if (error)
            *error = queryError(db.handle);
        return result;
    }
    sqlite3_stmt *statement = nullptr;
    const char *sql = "SELECT memory_id, created_at, category, content_json, evidence_file, status "
                      "FROM project_memory WHERE project_id = ? "
                      "ORDER BY created_at DESC, rowid DESC LIMIT 80";
    if (sqlite3_prepare_v2(db.handle, sql, -1, &statement, nullptr) != SQLITE_OK) {
        if (error)
            *error = queryError(db.handle);
        return result;
    }
    bindText(statement, 1, m_projectId);
    struct Entry { QVariantMap value; int score; int recency; };
    QVector<Entry> entries;
    const QStringList terms = queryTerms(query);
    int recency = 0;
    while (sqlite3_step(statement) == SQLITE_ROW) {
        auto column = [statement](int index) {
            const auto *bytes = reinterpret_cast<const char *>(sqlite3_column_text(statement, index));
            const int size = sqlite3_column_bytes(statement, index);
            return QString::fromUtf8(bytes ? bytes : "", size);
        };
        const QString contentText = column(3);
        QJsonParseError parseError;
        const QJsonDocument document = QJsonDocument::fromJson(contentText.toUtf8(), &parseError);
        const QVariant content = parseError.error == QJsonParseError::NoError
            ? document.toVariant() : QVariantMap{};
        const QString category = column(2);
        const QString status = column(5);
        const QString haystack = (category + QLatin1Char(' ') + contentText).toLower();
        int score = 0;
        for (const QString &term : terms)
            score += haystack.contains(term);
        if (status == QLatin1String("corrected") || status == QLatin1String("failed") || status == QLatin1String("rejected"))
            score += 2;
        entries.push_back({{
            {QStringLiteral("memory_id"), column(0)},
            {QStringLiteral("created_at"), column(1)},
            {QStringLiteral("category"), category},
            {QStringLiteral("content"), content},
            {QStringLiteral("evidence_file"), column(4)},
            {QStringLiteral("status"), status}
        }, score, recency++});
    }
    sqlite3_finalize(statement);
    std::stable_sort(entries.begin(), entries.end(), [](const Entry &left, const Entry &right) {
        if (left.score != right.score)
            return left.score > right.score;
        return left.recency < right.recency;
    });
    const int count = int(std::min<qsizetype>(limit, entries.size()));
    for (int i = 0; i < count; ++i)
        result.push_back(entries.at(i).value);
    return result;
}

QVariantList NativeMemoryService::recallShortTerm(const QString &query, int limit, QString *error) const
{
    if (error)
        error->clear();
    QVariantList result;
    if (!initialize(error) || limit <= 0)
        return result;
    Database db(m_databaseFile);
    if (!db.valid) {
        if (error)
            *error = queryError(db.handle);
        return result;
    }
    const QString now = QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyy-MM-dd'T'HH:mm:ss+00:00"));
    sqlite3_stmt *statement = nullptr;
    if (sqlite3_prepare_v2(db.handle,
            "DELETE FROM short_term_memory WHERE project_id = ? AND expires_at <> '' AND expires_at <= ?", -1,
            &statement, nullptr) != SQLITE_OK) {
        if (error)
            *error = queryError(db.handle);
        return result;
    }
    bindText(statement, 1, m_projectId);
    bindText(statement, 2, now);
    const bool deleted = sqlite3_step(statement) == SQLITE_DONE;
    sqlite3_finalize(statement);
    if (!deleted) {
        if (error)
            *error = queryError(db.handle);
        return result;
    }
    if (sqlite3_prepare_v2(db.handle,
            "SELECT memory_id, created_at, expires_at, category, content_json, evidence_file, status "
            "FROM short_term_memory WHERE project_id = ? AND (expires_at = '' OR expires_at > ?) "
            "ORDER BY created_at DESC LIMIT ?", -1, &statement, nullptr) != SQLITE_OK) {
        if (error)
            *error = queryError(db.handle);
        return result;
    }
    bindText(statement, 1, m_projectId);
    bindText(statement, 2, now);
    sqlite3_bind_int(statement, 3, qBound(1, limit, 32));
    const QStringList terms = queryTerms(query);
    struct Entry { QVariantMap value; int score; };
    QVector<Entry> entries;
    while (sqlite3_step(statement) == SQLITE_ROW) {
        auto column = [statement](int index) {
            const auto *bytes = reinterpret_cast<const char *>(sqlite3_column_text(statement, index));
            const int size = sqlite3_column_bytes(statement, index);
            return QString::fromUtf8(bytes ? bytes : "", size);
        };
        const QString contentText = column(4);
        QJsonParseError parseError;
        const QJsonDocument document = QJsonDocument::fromJson(contentText.toUtf8(), &parseError);
        const QVariant content = parseError.error == QJsonParseError::NoError ? document.toVariant() : QVariantMap{};
        QString searchable = column(3) + QLatin1Char(' ') + contentText + QLatin1Char(' ') + column(6);
        int score = 0;
        for (const QString &term : terms)
            if (searchable.contains(term, Qt::CaseInsensitive))
                score += 3;
        entries.append({{{QStringLiteral("memory_id"), column(0)}, {QStringLiteral("created_at"), column(1)},
                         {QStringLiteral("expires_at"), column(2)}, {QStringLiteral("category"), column(3)},
                         {QStringLiteral("content"), content}, {QStringLiteral("evidence_file"), column(5)},
                         {QStringLiteral("status"), column(6)}, {QStringLiteral("scope"), QStringLiteral("short_term")}}, score});
    }
    sqlite3_finalize(statement);
    std::stable_sort(entries.begin(), entries.end(), [](const Entry &left, const Entry &right) {
        return left.score > right.score;
    });
    for (const Entry &entry : std::as_const(entries)) {
        result.append(entry.value);
        if (result.size() >= qBound(1, limit, 32))
            break;
    }
    return result;
}

QVariantList NativeMemoryService::recentEvents(int limit, QString *error) const
{
    QVariantList result;
    if (error)
        error->clear();
    if (!initialize(error) || limit <= 0)
        return result;
    Database db(m_databaseFile);
    if (!db.valid) {
        if (error)
            *error = queryError(db.handle);
        return result;
    }
    sqlite3_stmt *statement = nullptr;
    const char *sql = "SELECT event_id, created_at, kind, summary_json, critical, source_file "
                      "FROM events WHERE project_id = ? ORDER BY created_at DESC, rowid DESC LIMIT ?";
    if (sqlite3_prepare_v2(db.handle, sql, -1, &statement, nullptr) != SQLITE_OK) {
        if (error)
            *error = queryError(db.handle);
        return result;
    }
    bindText(statement, 1, m_projectId);
    sqlite3_bind_int(statement, 2, limit);
    while (sqlite3_step(statement) == SQLITE_ROW) {
        auto column = [statement](int index) {
            const auto *bytes = reinterpret_cast<const char *>(sqlite3_column_text(statement, index));
            return QString::fromUtf8(bytes ? bytes : "", sqlite3_column_bytes(statement, index));
        };
        const QByteArray summaryText = column(3).toUtf8();
        const QJsonDocument summary = QJsonDocument::fromJson(summaryText);
        result.push_front(QVariantMap{
            {QStringLiteral("event_id"), column(0)},
            {QStringLiteral("created_at"), column(1)},
            {QStringLiteral("kind"), column(2)},
            {QStringLiteral("summary"), summary.toVariant()},
            {QStringLiteral("critical"), sqlite3_column_int(statement, 4) != 0},
            {QStringLiteral("source_file"), column(5)}
        });
    }
    sqlite3_finalize(statement);
    return result;
}

bool NativeMemoryService::recordFeedback(const QString &episodeId, const QString &verdict,
                                         const QString &note, const QString &correctedResponse,
                                         const QString &feedbackFile, QString *error) const
{
    const QVariantMap content{
        {QStringLiteral("episode_id"), episodeId},
        {QStringLiteral("verdict"), verdict},
        {QStringLiteral("note"), note},
        {QStringLiteral("corrected_response"), correctedResponse}
    };
    if (!recordEvent(QStringLiteral("supervisor_feedback"), content,
                     verdict != QLatin1String("approved"), feedbackFile, nullptr, error))
        return false;
    return remember(QStringLiteral("supervisor_feedback"), content, feedbackFile, verdict, nullptr, error);
}

int NativeMemoryService::importEpisode(const QVariantMap &episode, const QString &episodeFile,
                                       QString *error) const
{
    if (error)
        error->clear();
    if (!initialize(error))
        return -1;
    const QString source = absolutePath(episodeFile);
    Database db(m_databaseFile);
    if (!db.valid) {
        if (error)
            *error = queryError(db.handle);
        return -1;
    }
    if (!execSql(db.handle, "BEGIN IMMEDIATE", error))
        return -1;
    sqlite3_stmt *statement = nullptr;
    if (sqlite3_prepare_v2(db.handle, "SELECT 1 FROM imported_sources WHERE source_file = ? LIMIT 1",
                           -1, &statement, nullptr) != SQLITE_OK) {
        if (error)
            *error = queryError(db.handle);
        execSql(db.handle, "ROLLBACK", nullptr);
        return -1;
    }
    bindText(statement, 1, source);
    const bool alreadyImported = sqlite3_step(statement) == SQLITE_ROW;
    sqlite3_finalize(statement);
    if (alreadyImported) {
        execSql(db.handle, "COMMIT", error);
        return 0;
    }

    QVariantMap event{
        {QStringLiteral("episode_id"), episode.value(QStringLiteral("episode_id"))},
        {QStringLiteral("goal"), episode.value(QStringLiteral("goal"))},
        {QStringLiteral("answer"), episode.value(QStringLiteral("answer"))},
        {QStringLiteral("errors"), episode.value(QStringLiteral("errors"), QVariantList{})},
        {QStringLiteral("tool_trace"), episode.value(QStringLiteral("tool_trace"), QVariantList{})},
        {QStringLiteral("supervisor_warning"), episode.value(QStringLiteral("supervisor_warning"))}
    };
    const QVariantList errors = episode.value(QStringLiteral("errors")).toList();
    const QString status = errors.isEmpty() ? QStringLiteral("observed") : QStringLiteral("failed");
    const QString eventId = newHexUuid();
    bool ok = insertEvent(db.handle, m_projectId, eventId, QStringLiteral("episode"), event,
                          status == QLatin1String("failed"), source, error);
    const QVariantMap summary = importantView(event).toMap();
    if (ok && !summary.isEmpty())
        ok = insertMemory(db.handle, m_projectId, QStringLiteral("run_result"), summary, source, status, nullptr, error);
    if (ok) {
        if (sqlite3_prepare_v2(db.handle, "INSERT OR IGNORE INTO imported_sources VALUES (?, ?)",
                               -1, &statement, nullptr) != SQLITE_OK) {
            if (error)
                *error = queryError(db.handle);
            ok = false;
        } else {
            bindText(statement, 1, source);
            bindText(statement, 2, utcTimestamp());
            ok = sqlite3_step(statement) == SQLITE_DONE;
            sqlite3_finalize(statement);
            if (!ok && error)
                *error = queryError(db.handle);
        }
    }
    if (!ok || !execSql(db.handle, "COMMIT", error)) {
        execSql(db.handle, "ROLLBACK", nullptr);
        return -1;
    }
    return 1;
}

QVariantMap NativeMemoryService::backfill(const QString &episodesRoot, const QString &feedbackFile,
                                         QString *error) const
{
    if (error)
        error->clear();
    int importedEpisodes = 0;
    int importedFeedback = 0;
    const QDir directory(episodesRoot);
    const QFileInfoList files = directory.entryInfoList({QStringLiteral("*.json")}, QDir::Files, QDir::Name);
    for (const QFileInfo &fileInfo : files) {
        QFile file(fileInfo.absoluteFilePath());
        if (!file.open(QIODevice::ReadOnly))
            continue;
        QJsonParseError parseError;
        const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
        if (parseError.error != QJsonParseError::NoError || !document.isObject())
            continue;
        const QVariantMap episode = document.object().toVariantMap();
        const QString projectId = episode.value(QStringLiteral("project_id")).toString().isEmpty()
            ? episode.value(QStringLiteral("scope"), QStringLiteral("global")).toString() + QStringLiteral("_shared")
            : episode.value(QStringLiteral("project_id")).toString();
        if (projectId != m_projectId)
            continue;
        const int imported = importEpisode(episode, fileInfo.absoluteFilePath(), error);
        if (imported < 0)
            return {};
        importedEpisodes += imported;
    }

    QFile feedback(feedbackFile);
    if (feedback.open(QIODevice::ReadOnly | QIODevice::Text)) {
        while (!feedback.atEnd()) {
            const QByteArray line = feedback.readLine();
            QJsonParseError parseError;
            const QJsonDocument document = QJsonDocument::fromJson(line, &parseError);
            if (parseError.error != QJsonParseError::NoError || !document.isObject())
                continue;
            const QVariantMap record = document.object().toVariantMap();
            const QVariantMap episode = record.value(QStringLiteral("episode")).toMap();
            if (episode.isEmpty())
                continue;
            const QString projectId = episode.value(QStringLiteral("project_id")).toString().isEmpty()
                ? episode.value(QStringLiteral("scope"), QStringLiteral("global")).toString() + QStringLiteral("_shared")
                : episode.value(QStringLiteral("project_id")).toString();
            if (projectId != m_projectId)
                continue;
            QVariantMap content{
                {QStringLiteral("episode_id"), record.value(QStringLiteral("episode_id"))},
                {QStringLiteral("verdict"), record.value(QStringLiteral("verdict"))},
                {QStringLiteral("note"), record.value(QStringLiteral("note"))},
                {QStringLiteral("corrected_response"), record.value(QStringLiteral("corrected_response"))}
            };
            int before = 0;
            {
                Database db(m_databaseFile);
                if (!db.valid) {
                    if (error)
                        *error = queryError(db.handle);
                    return {};
                }
                sqlite3_stmt *statement = nullptr;
                if (sqlite3_prepare_v2(db.handle, "SELECT COUNT(*) FROM project_memory WHERE project_id = ?",
                                       -1, &statement, nullptr) != SQLITE_OK) {
                    if (error)
                        *error = queryError(db.handle);
                    return {};
                }
                bindText(statement, 1, m_projectId);
                if (sqlite3_step(statement) == SQLITE_ROW)
                    before = sqlite3_column_int(statement, 0);
                sqlite3_finalize(statement);
            }
            QString verdict = record.value(QStringLiteral("verdict")).toString();
            if (verdict.isEmpty())
                verdict = QStringLiteral("observed");
            const QString source = absolutePath(feedbackFile);
            QString memoryId;
            if (!remember(QStringLiteral("supervisor_feedback"), content, source, verdict, &memoryId, error))
                return {};
            int after = before;
            {
                Database db(m_databaseFile);
                sqlite3_stmt *statement = nullptr;
                if (!db.valid || sqlite3_prepare_v2(db.handle,
                        "SELECT COUNT(*) FROM project_memory WHERE project_id = ?", -1, &statement, nullptr) != SQLITE_OK) {
                    if (error)
                        *error = db.valid ? queryError(db.handle) : queryError(db.handle);
                    if (statement)
                        sqlite3_finalize(statement);
                    return {};
                }
                bindText(statement, 1, m_projectId);
                if (sqlite3_step(statement) == SQLITE_ROW)
                    after = sqlite3_column_int(statement, 0);
                sqlite3_finalize(statement);
            }
            importedFeedback += after > before;
        }
    }
    return {{QStringLiteral("episodes"), importedEpisodes}, {QStringLiteral("feedback"), importedFeedback}};
}

QVariantMap NativeMemoryService::stats(QString *error) const
{
    if (error)
        error->clear();
    if (!initialize(error))
        return {};
    Database db(m_databaseFile);
    if (!db.valid) {
        if (error)
            *error = queryError(db.handle);
        return {};
    }
    auto countForProject = [this, &db, error](const char *table) {
        const QByteArray sql = QByteArray("SELECT COUNT(*) FROM ") + table + " WHERE project_id = ?";
        sqlite3_stmt *statement = nullptr;
        if (sqlite3_prepare_v2(db.handle, sql.constData(), -1, &statement, nullptr) != SQLITE_OK) {
            if (error)
                *error = queryError(db.handle);
            return 0;
        }
        bindText(statement, 1, m_projectId);
        int count = sqlite3_step(statement) == SQLITE_ROW ? sqlite3_column_int(statement, 0) : 0;
        sqlite3_finalize(statement);
        return count;
    };
    return {
        {QStringLiteral("project_id"), m_projectId},
        {QStringLiteral("events"), countForProject("events")},
        {QStringLiteral("memories"), countForProject("project_memory")},
        {QStringLiteral("database_file"), m_databaseFile},
        {QStringLiteral("snapshots_root"), QDir(m_snapshotsRoot).filePath(m_projectId)}
    };
}

QVariantMap NativeMemoryService::compactContext(const QString &query, const QVariantMap &current,
                                                QString *error) const
{
    if (error)
        error->clear();
    const int fractionalLimit = int(m_contextWindow * 0.75);
    const int reservedLimit = m_contextWindow - m_outputTokenReserve - m_fixedPromptReserve;
    int limit = std::min(fractionalLimit, reservedLimit);
    if (m_configuredInputTokenLimit >= 0)
        limit = std::min(limit, m_configuredInputTokenLimit);
    limit = std::max(1024, limit);

    QVariantList allEvents = recentEvents(40, error);
    if (error && !error->isEmpty())
        return {};
    QVariantList events = allEvents;
    while (events.size() > 12)
        events.removeFirst();
    QVariantList memories = recall(query, 8, error);
    if (error && !error->isEmpty())
        return {};
    const QString notice = QStringLiteral("项目记忆只用于提示。完成状态必须重新读取本轮工具证据。");
    QVariantMap view{
        {QStringLiteral("notice"), notice},
        {QStringLiteral("project_id"), m_projectId},
        {QStringLiteral("recalled_project_memory"), memories},
        {QStringLiteral("recent_events"), events},
        {QStringLiteral("current"), current}
    };
    const int beforeTokens = estimateTokens(view);
    int omittedEvents = int(allEvents.size() - events.size());
    int omittedMemories = 0;
    bool currentTruncated = false;
    while (estimateTokens(view) > limit && !events.isEmpty()) {
        events.removeFirst();
        view[QStringLiteral("recent_events")] = events;
        ++omittedEvents;
    }
    while (estimateTokens(view) > limit && !memories.isEmpty()) {
        memories.removeLast();
        view[QStringLiteral("recalled_project_memory")] = memories;
        ++omittedMemories;
    }
    if (estimateTokens(view) > limit) {
        const QString currentText = QString::fromUtf8(jsonBytes(current));
        const QString digest = QString::fromLatin1(QCryptographicHash::hash(currentText.toUtf8(), QCryptographicHash::Sha256).toHex());
        view[QStringLiteral("current")] = QVariantMap{
            {QStringLiteral("truncated"), true},
            {QStringLiteral("sha256"), digest},
            {QStringLiteral("summary"), tightView(importantView(current))}
        };
        currentTruncated = true;
    }
    if (estimateTokens(view) > limit) {
        events.clear();
        memories.clear();
        omittedEvents = int(allEvents.size());
        omittedMemories = int(recall(query, 8, error).size());
        const QString currentText = QString::fromUtf8(jsonBytes(importantView(current)));
        const QString digest = QString::fromLatin1(QCryptographicHash::hash(currentText.toUtf8(), QCryptographicHash::Sha256).toHex());
        QVariantMap compactCurrent{
            {QStringLiteral("truncated"), true},
            {QStringLiteral("sha256"), digest}
        };
        QVariantMap baseView{
            {QStringLiteral("notice"), notice},
            {QStringLiteral("project_id"), m_projectId},
            {QStringLiteral("recalled_project_memory"), QVariantList{}},
            {QStringLiteral("recent_events"), QVariantList{}},
            {QStringLiteral("current"), compactCurrent}
        };
        const int excerptBudget = std::max(64, limit - estimateTokens(baseView) - 32);
        compactCurrent.insert(QStringLiteral("excerpt"), textExcerpt(currentText, excerptBudget));
        baseView.insert(QStringLiteral("current"), compactCurrent);
        view = baseView;
        if (error && !error->isEmpty())
            return {};
    }
    const int afterTokens = estimateTokens(view);
    if (afterTokens > limit) {
        if (error)
            *error = QStringLiteral("Compacted context still uses %1 tokens, exceeding input budget %2.").arg(afterTokens).arg(limit);
        return {};
    }
    QVariantList preservedIds;
    for (const QVariant &entry : events)
        preservedIds.push_back(entry.toMap().value(QStringLiteral("event_id")));
    QVariantList recalledIds;
    for (const QVariant &entry : memories)
        recalledIds.push_back(entry.toMap().value(QStringLiteral("memory_id")));
    QVariantMap report{
        {QStringLiteral("context_window"), m_contextWindow},
        {QStringLiteral("input_token_limit"), limit},
        {QStringLiteral("output_token_reserve"), m_outputTokenReserve},
        {QStringLiteral("fixed_prompt_reserve"), m_fixedPromptReserve},
        {QStringLiteral("estimated_tokens_before"), beforeTokens},
        {QStringLiteral("estimated_tokens_after"), afterTokens},
        {QStringLiteral("omitted_events"), omittedEvents},
        {QStringLiteral("omitted_memories"), omittedMemories},
        {QStringLiteral("current_truncated"), currentTruncated},
        {QStringLiteral("preserved_event_ids"), preservedIds},
        {QStringLiteral("recalled_memory_ids"), recalledIds},
        {QStringLiteral("model_context"), view}
    };
    const QString snapshotsDirectory = QDir(m_snapshotsRoot).filePath(m_projectId);
    const QString snapshotName = QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMdd'T'HHmmss"))
        + QLatin1Char('_') + newHexUuid().left(8) + QStringLiteral(".json");
    const QString snapshotPath = QDir(snapshotsDirectory).filePath(snapshotName);
    if (!writeAtomicJson(snapshotPath, report, error))
        return {};
    report.insert(QStringLiteral("snapshot_file"), snapshotPath);
    return report;
}
