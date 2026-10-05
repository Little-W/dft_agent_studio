#include "../src/sessionauditservice.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <cstdio>
#include <cstdlib>
#include <zlib.h>

namespace {
bool writeFrame(QFile &file, const QJsonObject &event) {
    const QByteArray raw = QJsonDocument(event).toJson(QJsonDocument::Compact);
    uLongf packedSize = compressBound(static_cast<uLong>(raw.size()));
    QByteArray packed(static_cast<qsizetype>(packedSize), Qt::Uninitialized);
    if (compress2(reinterpret_cast<Bytef *>(packed.data()), &packedSize,
                  reinterpret_cast<const Bytef *>(raw.constData()), static_cast<uLong>(raw.size()),
                  Z_DEFAULT_COMPRESSION) != Z_OK)
        return false;
    packed.resize(static_cast<qsizetype>(packedSize));
    const auto appendU32 = [](QByteArray *bytes, quint32 value) {
        bytes->append(static_cast<char>((value >> 24) & 0xff));
        bytes->append(static_cast<char>((value >> 16) & 0xff));
        bytes->append(static_cast<char>((value >> 8) & 0xff));
        bytes->append(static_cast<char>(value & 0xff));
    };
    QByteArray header;
    appendU32(&header, static_cast<quint32>(raw.size()));
    appendU32(&header, static_cast<quint32>(packed.size()));
    return file.write(header) == header.size() && file.write(packed) == packed.size();
}
}

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    QTemporaryDir temporary;
    if (!temporary.isValid())
        return EXIT_FAILURE;
    const QString binaryPath = QDir(temporary.path()).filePath(QStringLiteral("session.events.bin"));
    QFile binary(binaryPath);
    if (!binary.open(QIODevice::WriteOnly) || binary.write(QByteArray("DFTEVT1\0", 8)) != 8)
        return EXIT_FAILURE;
    const QJsonObject events[]{
        {{QStringLiteral("event"), QStringLiteral("turn_requested")},
         {QStringLiteral("thread_id"), QStringLiteral("root-a")},
         {QStringLiteral("timestamp"), QStringLiteral("2026-10-02T00:00:00Z")}},
        {{QStringLiteral("event"), QStringLiteral("tool_call")},
         {QStringLiteral("name"), QStringLiteral("run_dft_iteration")}},
        {{QStringLiteral("event"), QStringLiteral("tool_result")},
         {QStringLiteral("name"), QStringLiteral("run_dft_iteration")},
         {QStringLiteral("timestamp"), QStringLiteral("2026-10-02T00:01:00Z")},
         {QStringLiteral("result"), QJsonObject{{QStringLiteral("result"), QJsonObject{
             {QStringLiteral("observations"), QJsonArray{
                 QJsonObject{{QStringLiteral("observed_dft_drc_violations"), 0}},
                 QJsonObject{{QStringLiteral("observed_dft_drc_violations"), 3}},
                 QJsonObject{{QStringLiteral("observed_dft_drc_violations"), 0}}}}}}}}},
        {{QStringLiteral("event"), QStringLiteral("tool_call")},
         {QStringLiteral("name"), QStringLiteral("apply_patch")}},
        {{QStringLiteral("event"), QStringLiteral("tool_result")},
         {QStringLiteral("name"), QStringLiteral("apply_patch")},
         {QStringLiteral("result"), QJsonObject{{QStringLiteral("edited"), true},
             {QStringLiteral("status"), QStringLiteral("applied")},
             {QStringLiteral("source_project_unchanged"), true}}}},
        {{QStringLiteral("event"), QStringLiteral("tool_no_progress_guard")},
         {QStringLiteral("tool"), QStringLiteral("run_dft_flow")}},
    };
    for (const QJsonObject &event : events) {
        if (!writeFrame(binary, event))
            return EXIT_FAILURE;
    }
    binary.close();

    const QVariantMap audited = SessionAuditService::audit({binaryPath});
    const QVariantMap report = audited.value(QStringLiteral("result")).toMap();
    const QVariantMap session = report.value(QStringLiteral("sessions")).toList().value(0).toMap();
    const QVariantMap eventCounts = session.value(QStringLiteral("event_counts")).toMap();
    const QVariantMap toolCalls = session.value(QStringLiteral("tool_call_counts")).toMap();
    const QVariantMap guardCounts = session.value(QStringLiteral("guard_tool_counts")).toMap();
    const QVariantList drc = session.value(QStringLiteral("drc_observations")).toList();
    if (!audited.value(QStringLiteral("ok")).toBool()
        || report.value(QStringLiteral("schema_version")).toInt() != 1
        || session.value(QStringLiteral("thread_id")).toString() != QStringLiteral("root-a")
        || session.value(QStringLiteral("total_events")).toInt() != 6
        || session.value(QStringLiteral("actual_tool_calls")).toInt() != 2
        || toolCalls.value(QStringLiteral("run_dft_iteration")).toInt() != 1
        || toolCalls.value(QStringLiteral("apply_patch")).toInt() != 1
        || session.value(QStringLiteral("guard_event_count")).toInt() != 1
        || guardCounts.value(QStringLiteral("run_dft_flow")).toInt() != 1
        || session.value(QStringLiteral("recorded_successful_patches")).toInt() != 1
        || eventCounts.value(QStringLiteral("tool_result")).toInt() != 2
        || drc.size() != 1
        || drc.first().toMap().value(QStringLiteral("observed_counts")).toList() != QVariantList{0, 3}
        || session.value(QStringLiteral("first_singapore")).toString() != QStringLiteral("2026-10-02T08:00:00.000+08:00")) {
        std::fprintf(stderr, "session audit binary contract failed: ok=%d session=%s total=%d calls=%d guards=%d patches=%d drc=%s first=%s\n",
            audited.value(QStringLiteral("ok")).toBool(), qPrintable(session.value(QStringLiteral("thread_id")).toString()),
            session.value(QStringLiteral("total_events")).toInt(), session.value(QStringLiteral("actual_tool_calls")).toInt(),
            session.value(QStringLiteral("guard_event_count")).toInt(), session.value(QStringLiteral("recorded_successful_patches")).toInt(),
            qPrintable(QJsonDocument::fromVariant(drc).toJson(QJsonDocument::Compact)),
            qPrintable(session.value(QStringLiteral("first_singapore")).toString()));
        return EXIT_FAILURE;
    }

    const QString jsonlPath = QDir(temporary.path()).filePath(QStringLiteral("session.events.jsonl"));
    QFile jsonl(jsonlPath);
    if (!jsonl.open(QIODevice::WriteOnly)
        || jsonl.write("{\"event\":\"tool_call\",\"name\":\"read_file\"}\n") < 0
        || jsonl.write("{\"event\":\"turn_finished\",\"thread_id\":\"root-b\"}\n") < 0)
        return EXIT_FAILURE;
    jsonl.close();
    const QVariantMap multi = SessionAuditService::audit({binaryPath, jsonlPath});
    if (!multi.value(QStringLiteral("ok")).toBool()
        || multi.value(QStringLiteral("result")).toMap().value(QStringLiteral("sessions")).toList().size() != 2)
        return EXIT_FAILURE;

    const QString malformedPath = QDir(temporary.path()).filePath(QStringLiteral("malformed.events.bin"));
    QFile malformed(malformedPath);
    if (!malformed.open(QIODevice::WriteOnly) || malformed.write(QByteArray("DFTEVT1\0", 8)) != 8
        || malformed.write("short", 5) != 5)
        return EXIT_FAILURE;
    malformed.close();
    const QVariantMap invalid = SessionAuditService::audit({malformedPath});
    return !invalid.value(QStringLiteral("ok")).toBool()
        && !invalid.value(QStringLiteral("message")).toString().isEmpty() ? EXIT_SUCCESS : EXIT_FAILURE;
}
