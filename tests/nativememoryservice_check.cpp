#include "../src/nativememoryservice.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QVariantMap>
#include <sqlite3.h>
#include <cstdio>

namespace {

bool require(bool condition, const char *message)
{
    if (condition)
        return true;
    std::fprintf(stderr, "FAIL: %s\n", message);
    return false;
}

bool createLegacySchema(const QString &path)
{
    sqlite3 *db = nullptr;
    const QByteArray utf8 = path.toUtf8();
    if (sqlite3_open(utf8.constData(), &db) != SQLITE_OK)
        return false;
    const char *sql =
        "CREATE TABLE project_memory (memory_id TEXT PRIMARY KEY, project_id TEXT NOT NULL,"
        "created_at TEXT NOT NULL, category TEXT NOT NULL, content_json TEXT NOT NULL,"
        "evidence_file TEXT NOT NULL, status TEXT NOT NULL);"
        "CREATE INDEX memory_project_time ON project_memory(project_id, created_at DESC);"
        "INSERT INTO project_memory VALUES ('legacy-id','demo_project','2026-01-01T00:00:00+00:00',"
        "'run_result','{\"coverage_percent\":91.5,\"故障\":\"C3\"}','/tmp/legacy.json','failed');";
    char *error = nullptr;
    const bool ok = sqlite3_exec(db, sql, nullptr, nullptr, &error) == SQLITE_OK;
    if (error)
        sqlite3_free(error);
    sqlite3_close(db);
    return ok;
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    bool ok = true;
    const int englishTokens = NativeMemoryService::estimateTokens(QStringLiteral("test").repeated(20));
    const int chineseTokens = NativeMemoryService::estimateTokens(QStringLiteral("测试").repeated(20));
    ok &= require(chineseTokens > englishTokens, "Chinese text should receive a more conservative estimate");
    const QString dense = QStringLiteral("{\"action\":\"terminal\",\"command\":\"find /project/src/unit -type f -name '*.sv'\"}").repeated(100);
    ok &= require(NativeMemoryService::estimateTokens(dense) >= int(dense.size() * 0.35),
                  "dense JSON and paths should retain the safety margin");

    QTemporaryDir temporary;
    ok &= require(temporary.isValid(), "temporary directory should be available");
    if (!temporary.isValid())
        return 1;
    const QString database = temporary.filePath(QStringLiteral("memory.sqlite3"));
    ok &= require(createLegacySchema(database), "create Python-compatible SQLite schema");
    NativeMemoryService first(QStringLiteral("demo_project"), database);
    QString error;
    QVariantList recalled = first.recall(QStringLiteral("coverage C3"), 4, &error);
    ok &= require(error.isEmpty(), "read existing Python-compatible database");
    ok &= require(!recalled.isEmpty(), "legacy memory should be found");
    if (!recalled.isEmpty()) {
        const QVariantMap entry = recalled.first().toMap();
        ok &= require(entry.value(QStringLiteral("memory_id")).toString() == QStringLiteral("legacy-id"),
                      "legacy row identity is preserved");
        ok &= require(entry.value(QStringLiteral("content")).toMap().value(QStringLiteral("故障")).toString() == QStringLiteral("C3"),
                      "UTF-8 JSON content is loaded");
        ok &= require(entry.value(QStringLiteral("evidence_file")).toString() == QStringLiteral("/tmp/legacy.json"),
                      "evidence path is preserved");
    }

    const QVariantMap content{
        {QStringLiteral("coverage_percent"), 93.2},
        {QStringLiteral("report_file"), QStringLiteral("/tmp/atpg.rpt")},
        {QStringLiteral("unrelated_bulk_text"), QStringLiteral("discard me")}
    };
    QString firstId;
    QString secondId;
    ok &= require(first.remember(QStringLiteral("run_result"), content,
                                 QStringLiteral("/tmp/episode.json"), QStringLiteral("failed"),
                                 &firstId, &error), "write memory transactionally");
    ok &= require(error.isEmpty(), "memory write has no SQLite error");
    ok &= require(first.remember(QStringLiteral("run_result"), content,
                                 QStringLiteral("/tmp/episode.json"), QStringLiteral("failed"),
                                 &secondId, &error), "repeat memory insert is idempotent");
    ok &= require(firstId == secondId, "memory identity is deterministic");

    NativeMemoryService reopened(QStringLiteral("demo_project"), database);
    recalled = reopened.recall(QStringLiteral("ATPG coverage"), 4, &error);
    ok &= require(error.isEmpty(), "reopen and search memory database");
    ok &= require(!recalled.isEmpty() && recalled.first().toMap().value(QStringLiteral("memory_id")).toString() == firstId,
                  "search ranks matching memory after process-style reopen");

    QString shortTermId;
    ok &= require(reopened.rememberShortTerm(QStringLiteral("verified_repair"),
        {{QStringLiteral("diagnosis"), QStringLiteral("tool output was stale because the job reused its workspace")},
         {QStringLiteral("evidence"), QStringLiteral("reports/post_dft_drc.rpt")}},
        QStringLiteral("/tmp/reports/post_dft_drc.rpt"), QStringLiteral("verified"), 0,
        &shortTermId, &error), "store expiring project-scoped repair note");
    const QVariantList temporaryMemories = reopened.recallShortTerm(QStringLiteral("stale workspace repair"), 8, &error);
    ok &= require(error.isEmpty() && temporaryMemories.size() == 1
                      && temporaryMemories.first().toMap().value(QStringLiteral("memory_id")).toString() == shortTermId
                      && temporaryMemories.first().toMap().value(QStringLiteral("scope")).toString() == QStringLiteral("short_term")
                      && temporaryMemories.first().toMap().value(QStringLiteral("expires_at")).toString().isEmpty(),
                  "short-term core capacity has no forced TTL");
    for (int i = 0; i < NativeMemoryService::ShortTermCoreCapacity - 1; ++i) {
        ok &= require(reopened.rememberShortTerm(QStringLiteral("core_fixture"),
            {{QStringLiteral("diagnosis"), QStringLiteral("core-%1").arg(i)}}, {}, QStringLiteral("verified"), 1,
            nullptr, &error), "fill protected short-term memory capacity");
    }
    QString overflowId;
    ok &= require(reopened.rememberShortTerm(QStringLiteral("overflow_fixture"),
        {{QStringLiteral("diagnosis"), QStringLiteral("expiring overflow note")}}, {}, QStringLiteral("verified"), 0,
        &overflowId, &error), "store TTL-bound overflow memory");
    const QVariantList overflowMemory = reopened.recallShortTerm(QStringLiteral("expiring overflow note"), 8, &error);
    ok &= require(error.isEmpty() && !overflowMemory.isEmpty()
                      && overflowMemory.first().toMap().value(QStringLiteral("memory_id")).toString() == overflowId
                      && !overflowMemory.first().toMap().value(QStringLiteral("expires_at")).toString().isEmpty(),
                  "records beyond core capacity receive an expiry");
    for (int i = 0; i < NativeMemoryService::ShortTermTotalCapacity - NativeMemoryService::ShortTermCoreCapacity; ++i) {
        ok &= require(reopened.rememberShortTerm(QStringLiteral("bounded_fixture"),
            {{QStringLiteral("diagnosis"), QStringLiteral("overflow-%1").arg(i)}}, {}, QStringLiteral("verified"), 24,
            nullptr, &error), "fill bounded expiring memory tier");
    }
    ok &= require(reopened.rememberShortTerm(QStringLiteral("capacity_fixture"),
        {{QStringLiteral("diagnosis"), QStringLiteral("capacity stays bounded")}}, {}, QStringLiteral("verified"), 24,
        nullptr, &error), "evict old overflow entry at memory capacity");
    sqlite3 *capacityDb = nullptr;
    const QByteArray capacityPath = database.toUtf8();
    const bool capacityDbOpened = sqlite3_open(capacityPath.constData(), &capacityDb) == SQLITE_OK;
    int totalShortTerm = 0;
    int protectedShortTerm = 0;
    if (capacityDbOpened) {
        sqlite3_stmt *capacityStatement = nullptr;
        if (sqlite3_prepare_v2(capacityDb,
                "SELECT COUNT(*), SUM(CASE WHEN expires_at = '' THEN 1 ELSE 0 END) "
                "FROM short_term_memory WHERE project_id='demo_project'", -1, &capacityStatement, nullptr) == SQLITE_OK
            && sqlite3_step(capacityStatement) == SQLITE_ROW) {
            totalShortTerm = sqlite3_column_int(capacityStatement, 0);
            protectedShortTerm = sqlite3_column_int(capacityStatement, 1);
        }
        sqlite3_finalize(capacityStatement);
        sqlite3_close(capacityDb);
    }
    ok &= require(capacityDbOpened && totalShortTerm == NativeMemoryService::ShortTermTotalCapacity
                      && protectedShortTerm == NativeMemoryService::ShortTermCoreCapacity,
                  "memory capacity remains bounded without evicting no-TTL core entries");
    NativeMemoryService unrelatedProject(QStringLiteral("another_project"), database);
    ok &= require(unrelatedProject.recallShortTerm(QStringLiteral("stale workspace"), 8, &error).isEmpty()
                      && error.isEmpty(), "short-term memory does not cross project boundaries");
    if (!recalled.isEmpty()) {
        const QVariantMap result = recalled.first().toMap();
        ok &= require(!result.value(QStringLiteral("content")).toMap().contains(QStringLiteral("unrelated_bulk_text")),
                      "remember keeps only the Python important-field view");
    }

    NativeMemoryService otherProject(QStringLiteral("other_project"), database);
    ok &= require(otherProject.recall(QStringLiteral("coverage"), 10).isEmpty(),
                  "project memories remain isolated");

    QString eventId;
    ok &= require(reopened.recordEvent(QStringLiteral("tool_result"),
                                       {{QStringLiteral("round"), 1},
                                        {QStringLiteral("errors"), QVariantList{QStringLiteral("E1")}},
                                        {QStringLiteral("unrelated"), QStringLiteral("omit me")}},
                                       true, QStringLiteral("/tmp/run.json"), &eventId, &error),
                  "persist event with compact summary");
    ok &= require(!eventId.isEmpty(), "event id is generated");
    QVariantList latest = reopened.recentEvents(1, &error);
    ok &= require(!latest.isEmpty(), "retrieve recent events");
    if (!latest.isEmpty()) {
        const QVariantMap event = latest.last().toMap();
        ok &= require(event.value(QStringLiteral("event_id")).toString() == eventId,
                      "recent event is ordered chronologically");
        ok &= require(event.value(QStringLiteral("critical")).toBool(), "critical event flag is retained");
        const QVariantMap summary = event.value(QStringLiteral("summary")).toMap();
        ok &= require(summary.contains(QStringLiteral("errors")), "event summary keeps important error evidence");
        ok &= require(!summary.contains(QStringLiteral("unrelated")), "event summary omits unrelated field");
    }
    ok &= require(reopened.recordFeedback(QStringLiteral("ep-1"), QStringLiteral("corrected"),
                                          QStringLiteral("C3 未处理"), QStringLiteral("继续修复"),
                                          QStringLiteral("/tmp/feedback.jsonl"), &error),
                  "persist feedback as event and long-term memory");
    const QVariantList feedback = reopened.recall(QStringLiteral("C3"), 8, &error);
    bool foundFeedback = false;
    for (const QVariant &entry : feedback)
        foundFeedback |= entry.toMap().value(QStringLiteral("category")).toString() == QStringLiteral("supervisor_feedback");
    ok &= require(foundFeedback, "feedback is searchable after persistence");

    const QString episodesDirectory = temporary.filePath(QStringLiteral("episodes"));
    QDir().mkpath(episodesDirectory);
    const QString episodeFile = QDir(episodesDirectory).filePath(QStringLiteral("ep-backfill.json"));
    QFile episodeOut(episodeFile);
    const QByteArray episodeJson = QJsonDocument(QJsonObject{
        {QStringLiteral("episode_id"), QStringLiteral("ep-backfill")},
        {QStringLiteral("project_id"), QStringLiteral("demo_project")},
        {QStringLiteral("scope"), QStringLiteral("configured")},
        {QStringLiteral("errors"), QJsonArray{QStringLiteral("C3=4")}},
        {QStringLiteral("tool_trace"), QJsonArray{}}
    }).toJson(QJsonDocument::Compact);
    ok &= require(episodeOut.open(QIODevice::WriteOnly) && episodeOut.write(episodeJson) == episodeJson.size(),
                  "write backfill episode fixture");
    episodeOut.close();
    const QString feedbackFile = temporary.filePath(QStringLiteral("feedback.jsonl"));
    QFile feedbackOut(feedbackFile);
    const QByteArray feedbackJson = QJsonDocument(QJsonObject{
        {QStringLiteral("episode_id"), QStringLiteral("ep-backfill")},
        {QStringLiteral("verdict"), QStringLiteral("corrected")},
        {QStringLiteral("note"), QStringLiteral("必须继续处理 C3。")},
        {QStringLiteral("corrected_response"), QStringLiteral("未完成。")},
        {QStringLiteral("episode"), QJsonDocument::fromJson(episodeJson).object()}
    }).toJson(QJsonDocument::Compact) + "\n";
    ok &= require(feedbackOut.open(QIODevice::WriteOnly) && feedbackOut.write(feedbackJson) == feedbackJson.size(),
                  "write backfill feedback fixture");
    feedbackOut.close();
    NativeMemoryService backfillStore(QStringLiteral("demo_project"), temporary.filePath(QStringLiteral("backfill.sqlite3")));
    QVariantMap backfill = backfillStore.backfill(episodesDirectory, feedbackFile, &error);
    ok &= require(error.isEmpty() && backfill.value(QStringLiteral("episodes")).toInt() == 1
                      && backfill.value(QStringLiteral("feedback")).toInt() == 1,
                  "backfill imports project episode and feedback");
    backfill = backfillStore.backfill(episodesDirectory, feedbackFile, &error);
    ok &= require(error.isEmpty() && backfill.value(QStringLiteral("episodes")).toInt() == 0
                      && backfill.value(QStringLiteral("feedback")).toInt() == 0,
                  "backfill is idempotent");

    NativeMemoryService compactStore(QStringLiteral("demo_project"), temporary.filePath(QStringLiteral("compact.sqlite3")),
                                     temporary.filePath(QStringLiteral("snapshots")), 2048);
    for (int i = 0; i < 30; ++i) {
        ok &= require(compactStore.recordEvent(QStringLiteral("tool_result"),
                                               {{QStringLiteral("round"), i},
                                                {QStringLiteral("errors"), QVariantList{QString(400, QLatin1Char('E'))}},
                                                {QStringLiteral("workspace"), QStringLiteral("/tmp/work-%1").arg(i)}},
                                               i == 3, {}, nullptr, &error),
                      "append compaction fixture event");
    }
    ok &= require(compactStore.remember(QStringLiteral("supervisor_feedback"),
                                        {{QStringLiteral("reason"), QStringLiteral("C3 未处理，不能声明通过。")},
                                         {QStringLiteral("evidence"), QVariantList{QStringLiteral("C3=8")}}},
                                        QStringLiteral("/tmp/feedback.jsonl"), QStringLiteral("corrected"), nullptr, &error),
                  "store compaction fixture feedback");
    QVariantMap compacted = compactStore.compactContext(QStringLiteral("处理 C3 DFT DRC"),
                                                        {{QStringLiteral("post_dft_drc"), 8},
                                                         {QStringLiteral("allowed_actions"), QVariantList{QStringLiteral("drc_autofix")}}},
                                                        &error);
    ok &= require(error.isEmpty(), "compact context and write atomic snapshot");
    ok &= require(compacted.value(QStringLiteral("estimated_tokens_after")).toInt()
                      <= compacted.value(QStringLiteral("input_token_limit")).toInt(),
                  "compacted context is within budget");
    ok &= require(compacted.value(QStringLiteral("omitted_events")).toInt() > 0,
                  "old events are removed first under pressure");
    const QVariantMap context = compacted.value(QStringLiteral("model_context")).toMap();
    ok &= require(context.value(QStringLiteral("current")).toMap().value(QStringLiteral("post_dft_drc")).toInt() == 8,
                  "current evidence remains intact");
    QFile snapshot(compacted.value(QStringLiteral("snapshot_file")).toString());
    ok &= require(snapshot.open(QIODevice::ReadOnly), "snapshot exists");
    if (snapshot.isOpen()) {
        const QJsonDocument saved = QJsonDocument::fromJson(snapshot.readAll());
        ok &= require(saved.object().value(QStringLiteral("preserved_event_ids")).toArray()
                          .toVariantList() == compacted.value(QStringLiteral("preserved_event_ids")).toList(),
                      "snapshot contains selected event ids");
    }

    QVariantMap modelValueReport;
    const QVariant modelValue = NativeMemoryService::compactModelValue(
        QVariantMap{{QStringLiteral("step"), 7},
                    {QStringLiteral("last_result"), QVariantMap{
                         {QStringLiteral("error"), QStringLiteral("Error: missing module")},
                         {QStringLiteral("relative_path"), QStringLiteral("flow/reports/read.rpt")},
                         {QStringLiteral("sha256"), QString(64, QLatin1Char('a'))},
                         {QStringLiteral("output"), QString(80000, QLatin1Char('X'))}}}},
        1024, &modelValueReport);
    const QByteArray modelValueText = QJsonDocument::fromVariant(modelValue).toJson(QJsonDocument::Compact);
    ok &= require(modelValueReport.value(QStringLiteral("estimated_tokens_after")).toInt() <= 1024,
                  "single model value compaction respects its token budget");
    ok &= require(modelValueReport.value(QStringLiteral("compacted")).toBool()
                      && modelValueText.contains("missing module") && modelValueText.contains("flow/reports/read.rpt")
                      && modelValueText.contains(QByteArray(64, 'a')),
                  "model value compaction retains current error, path, and hash");

    QVariantList textRecords{
        QVariantMap{{QStringLiteral("output"), QString(10000, QLatin1Char('X'))}},
        QVariantMap{{QStringLiteral("errors"), QStringLiteral("最近错误")}},
        QVariantMap{{QStringLiteral("current"), QStringLiteral("保留")}}
    };
    const QVariantList compactText = NativeMemoryService::compactTextContext(textRecords, 1024);
    ok &= require(compactText.size() >= 2 && compactText.last().toMap().value(QStringLiteral("current")).toString() == QStringLiteral("保留"),
                  "text context compaction keeps recent records");

    NativeMemoryService largeCurrent(QStringLiteral("demo_project"), temporary.filePath(QStringLiteral("large-current.sqlite3")),
                                     temporary.filePath(QStringLiteral("large-snapshots")), 2048);
    QVariantMap large;
    for (int i = 0; i < 100; ++i)
        large.insert(QStringLiteral("field_%1").arg(i), QStringLiteral("错误").repeated(2000));
    const QVariantMap truncated = largeCurrent.compactContext(QStringLiteral("large record"), large, &error);
    ok &= require(error.isEmpty() && truncated.value(QStringLiteral("current_truncated")).toBool(),
                  "oversized current is excerpted and hashed");
    const QVariantMap truncatedCurrent = truncated.value(QStringLiteral("model_context")).toMap().value(QStringLiteral("current")).toMap();
    ok &= require(truncatedCurrent.value(QStringLiteral("truncated")).toBool()
                      && truncatedCurrent.value(QStringLiteral("sha256")).toString().size() == 64,
                  "large current excerpt carries a stable digest");

    NativeMemoryService contextPolicy(QStringLiteral("policy"), temporary.filePath(QStringLiteral("policy.sqlite3")),
                                      {}, 65536);
    const QVariantMap policyReport = contextPolicy.compactContext(QStringLiteral(""), {}, &error);
    ok &= require(error.isEmpty() && policyReport.value(QStringLiteral("input_token_limit")).toInt() == 49152,
                  "context input budget reserves prompt and output space");
    if (!ok)
        return 1;
    std::puts("NativeMemoryService checks passed");
    return 0;
}
