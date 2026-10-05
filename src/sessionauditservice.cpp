#include "sessionauditservice.h"

#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QMap>
#include <QSet>
#include <QVariantList>
#include <numeric>
#include <utility>
#include <zlib.h>

namespace {
constexpr qsizetype MaximumFrameBytes = 64 * 1024 * 1024;
const QByteArray EventMagic("DFTEVT1\0", 8);
const QSet<QString> TrustedDftTools{
    QStringLiteral("run_dft_flow"), QStringLiteral("run_dft_iteration"),
    QStringLiteral("run_dft_optimization"), QStringLiteral("wait_dft_job"),
    QStringLiteral("run_approved_patch")};

bool fail(QString *error, const QString &message) {
    if (error)
        *error = message;
    return false;
}

QString localTime(const QString &text) {
    QDateTime timestamp = QDateTime::fromString(text, Qt::ISODateWithMs);
    if (!timestamp.isValid())
        timestamp = QDateTime::fromString(text, Qt::ISODate);
    if (!timestamp.isValid())
        return text;
    return timestamp.toOffsetFromUtc(8 * 60 * 60)
        .toString(Qt::ISODateWithMs);
}

bool parseObject(const QByteArray &bytes, QJsonObject *object, QString *error) {
    QJsonParseError parseError{};
    const QJsonDocument document = QJsonDocument::fromJson(bytes, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
        return fail(error, QStringLiteral("invalid JSON event: %1").arg(parseError.errorString()));
    *object = document.object();
    return true;
}

bool readEventFile(const QString &path, QVector<QJsonObject> *events, QString *error) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return fail(error, QStringLiteral("cannot read %1: %2").arg(QFileInfo(path).fileName(), file.errorString()));
    const QString suffix = QFileInfo(path).suffix().toLower();
    if (suffix == QStringLiteral("bin")) {
        if (file.read(EventMagic.size()) != EventMagic)
            return fail(error, QStringLiteral("%1: invalid event magic").arg(QFileInfo(path).fileName()));
        quint64 frame = 0;
        while (!file.atEnd()) {
            ++frame;
            const QByteArray header = file.read(8);
            if (header.size() != 8)
                return fail(error, QStringLiteral("%1: incomplete frame %2").arg(QFileInfo(path).fileName()).arg(frame));
            const auto u32 = [&header](qsizetype offset) {
                return (static_cast<quint32>(static_cast<uchar>(header.at(offset))) << 24)
                    | (static_cast<quint32>(static_cast<uchar>(header.at(offset + 1))) << 16)
                    | (static_cast<quint32>(static_cast<uchar>(header.at(offset + 2))) << 8)
                    | static_cast<quint32>(static_cast<uchar>(header.at(offset + 3)));
            };
            const quint32 rawSize = u32(0);
            const quint32 packedSize = u32(4);
            if (rawSize == 0 || packedSize == 0 || rawSize > MaximumFrameBytes || packedSize > MaximumFrameBytes)
                return fail(error, QStringLiteral("%1: oversized or empty frame %2").arg(QFileInfo(path).fileName()).arg(frame));
            const QByteArray packed = file.read(packedSize);
            if (packed.size() != static_cast<qsizetype>(packedSize))
                return fail(error, QStringLiteral("%1: truncated frame %2").arg(QFileInfo(path).fileName()).arg(frame));
            QByteArray raw(static_cast<qsizetype>(rawSize), Qt::Uninitialized);
            uLongf actualSize = rawSize;
            const int status = uncompress(reinterpret_cast<Bytef *>(raw.data()), &actualSize,
                reinterpret_cast<const Bytef *>(packed.constData()), packedSize);
            if (status != Z_OK || actualSize != rawSize)
                return fail(error, QStringLiteral("%1: invalid compressed frame %2").arg(QFileInfo(path).fileName()).arg(frame));
            QJsonObject event;
            if (!parseObject(raw, &event, error))
                return false;
            events->append(event);
        }
        return true;
    }
    if (suffix == QStringLiteral("json")) {
        if (file.size() > 512 * 1024 * 1024)
            return fail(error, QStringLiteral("%1: JSON audit input exceeds 512 MiB").arg(QFileInfo(path).fileName()));
        QJsonParseError parseError{};
        const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
        if (parseError.error != QJsonParseError::NoError || !document.isArray())
            return fail(error, QStringLiteral("%1: expected a JSON array (%2)")
                .arg(QFileInfo(path).fileName(), parseError.errorString()));
        for (const QJsonValue &value : document.array()) {
            if (value.isObject())
                events->append(value.toObject());
        }
        return true;
    }
    while (!file.atEnd()) {
        const QByteArray line = file.readLine();
        if (line.trimmed().isEmpty())
            continue;
        QJsonObject event;
        if (!parseObject(line, &event, error))
            return fail(error, QStringLiteral("%1: %2").arg(QFileInfo(path).fileName(), *error));
        events->append(event);
    }
    return true;
}

void collectDrcObservations(const QJsonValue &value, QVariantList *observations) {
    if (value.isObject()) {
        const QJsonObject object = value.toObject();
        if (object.contains(QStringLiteral("observed_dft_drc_violations"))) {
            const QVariant candidate = object.value(QStringLiteral("observed_dft_drc_violations")).toVariant();
            if (!observations->contains(candidate))
                observations->append(candidate);
        }
        for (const QJsonValue &child : object)
            collectDrcObservations(child, observations);
    } else if (value.isArray()) {
        for (const QJsonValue &child : value.toArray())
            collectDrcObservations(child, observations);
    }
}

QVariantMap auditFile(const QString &path, QString *error) {
    QVector<QJsonObject> events;
    if (!readEventFile(path, &events, error))
        return {};
    QMap<QString, int> eventCounts;
    QMap<QString, int> toolCalls;
    QMap<QString, int> guards;
    QVariantList patches;
    QVariantList drcObservations;
    QString first;
    QString last;
    QString threadId;
    int successfulPatches = 0;
    for (const QJsonObject &event : std::as_const(events)) {
        const QString type = event.value(QStringLiteral("event")).toString(QStringLiteral("unknown"));
        const QString name = event.value(QStringLiteral("name")).toString();
        const QString stamp = event.value(QStringLiteral("timestamp")).toString();
        ++eventCounts[type];
        if (first.isEmpty())
            first = stamp;
        if (!stamp.isEmpty())
            last = stamp;
        if (threadId.isEmpty())
            threadId = event.value(QStringLiteral("thread_id")).toString();
        if (type == QStringLiteral("tool_call"))
            ++toolCalls[name];
        if (type == QStringLiteral("no_progress_guard") || type == QStringLiteral("tool_no_progress_guard")) {
            const QString toolName = !name.isEmpty() ? name
                : event.value(QStringLiteral("tool")).toString(QStringLiteral("unknown"));
            ++guards[toolName];
        }
        if (type != QStringLiteral("tool_result") || !event.value(QStringLiteral("result")).isObject())
            continue;
        const QJsonObject result = event.value(QStringLiteral("result")).toObject();
        if (name == QStringLiteral("apply_patch")) {
            const bool edited = result.value(QStringLiteral("edited")).toBool();
            successfulPatches += edited ? 1 : 0;
            patches.append(QVariantMap{{QStringLiteral("timestamp"), stamp},
                {QStringLiteral("edited"), edited}, {QStringLiteral("status"), result.value(QStringLiteral("status")).toVariant()},
                {QStringLiteral("source_project_unchanged"), result.value(QStringLiteral("source_project_unchanged")).toVariant()}});
        }
        if (TrustedDftTools.contains(name)) {
            QVariantList values;
            collectDrcObservations(result, &values);
            if (!values.isEmpty())
                drcObservations.append(QVariantMap{{QStringLiteral("timestamp"), stamp},
                    {QStringLiteral("tool"), name}, {QStringLiteral("observed_counts"), values}});
        }
    }
    QVariantMap eventCountMap;
    for (auto it = eventCounts.cbegin(); it != eventCounts.cend(); ++it)
        eventCountMap.insert(it.key(), it.value());
    QVariantMap toolCallMap;
    for (auto it = toolCalls.cbegin(); it != toolCalls.cend(); ++it)
        toolCallMap.insert(it.key(), it.value());
    QVariantMap guardMap;
    for (auto it = guards.cbegin(); it != guards.cend(); ++it)
        guardMap.insert(it.key(), it.value());
    const int guardCount = eventCounts.value(QStringLiteral("no_progress_guard"))
        + eventCounts.value(QStringLiteral("tool_no_progress_guard"));
    return {{QStringLiteral("source_file"), QFileInfo(path).fileName()},
        {QStringLiteral("thread_id"), threadId}, {QStringLiteral("first_timestamp"), first},
        {QStringLiteral("last_timestamp"), last}, {QStringLiteral("first_singapore"), localTime(first)},
        {QStringLiteral("last_singapore"), localTime(last)}, {QStringLiteral("total_events"), events.size()},
        {QStringLiteral("event_counts"), eventCountMap},
        {QStringLiteral("actual_tool_calls"), std::accumulate(toolCalls.cbegin(), toolCalls.cend(), 0,
            [](int total, const int count) { return total + count; })},
        {QStringLiteral("tool_call_counts"), toolCallMap}, {QStringLiteral("guard_event_count"), guardCount},
        {QStringLiteral("guard_tool_counts"), guardMap}, {QStringLiteral("patch_attempts"), patches},
        {QStringLiteral("recorded_successful_patches"), successfulPatches},
        {QStringLiteral("drc_observations"), drcObservations},
        {QStringLiteral("scope_note"), QStringLiteral("Recorded events only. No inference about unlogged external filesystem edits or actual RTL root cause.")}};
}
} // namespace

QVariantMap SessionAuditService::audit(const QStringList &paths) {
    if (paths.isEmpty())
        return {{QStringLiteral("ok"), false}, {QStringLiteral("message"), QStringLiteral("At least one event log is required.")}};
    QVariantList sessions;
    for (const QString &path : paths) {
        QString error;
        const QVariantMap result = auditFile(path, &error);
        if (!error.isEmpty())
            return {{QStringLiteral("ok"), false}, {QStringLiteral("message"), error}};
        sessions.append(result);
    }
    return {{QStringLiteral("ok"), true}, {QStringLiteral("result"), QVariantMap{
        {QStringLiteral("schema_version"), 1}, {QStringLiteral("sessions"), sessions}}}};
}
