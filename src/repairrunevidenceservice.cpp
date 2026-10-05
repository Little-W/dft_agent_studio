#include "repairrunevidenceservice.h"

#include <QCryptographicHash>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QVariantList>

#include <algorithm>
#include <charconv>

namespace {
bool isMapping(const QVariant &value) {
    return value.metaType().id() == QMetaType::QVariantMap
        || value.metaType().id() == QMetaType::QVariantHash;
}

QVariantMap asMap(const QVariant &value) {
    if (value.metaType().id() == QMetaType::QVariantMap)
        return value.toMap();
    QVariantMap converted;
    const QVariantHash hash = value.toHash();
    for (auto it = hash.cbegin(); it != hash.cend(); ++it)
        converted.insert(it.key(), it.value());
    return converted;
}

bool truthy(const QVariant &value) {
    if (!value.isValid() || value.isNull())
        return false;
    switch (value.metaType().id()) {
    case QMetaType::Bool:
        return value.toBool();
    case QMetaType::QString:
        return !value.toString().isEmpty();
    case QMetaType::QVariantList:
        return !value.toList().isEmpty();
    case QMetaType::QVariantMap:
        return !value.toMap().isEmpty();
    case QMetaType::QVariantHash:
        return !value.toHash().isEmpty();
    default:
        if (value.canConvert<double>())
            return value.toDouble() != 0.0;
        return true;
    }
}

QString pythonString(const QVariant &value) {
    if (!value.isValid() || value.isNull())
        return QStringLiteral("None");
    if (value.metaType().id() == QMetaType::Bool)
        return value.toBool() ? QStringLiteral("True") : QStringLiteral("False");
    if (value.metaType().id() == QMetaType::QString)
        return value.toString();
    if (isMapping(value))
        return QString::fromUtf8(QJsonDocument(QJsonObject::fromVariantMap(asMap(value)))
                                     .toJson(QJsonDocument::Compact));
    if (value.metaType().id() == QMetaType::QVariantList) {
        return QString::fromUtf8(QJsonDocument(QJsonArray::fromVariantList(value.toList()))
                                     .toJson(QJsonDocument::Compact));
    }
    return value.toString();
}

void collectMappings(const QVariant &value, QList<QVariantMap> *records) {
    if (isMapping(value)) {
        const QVariantMap map = asMap(value);
        records->append(map);
        for (auto it = map.cbegin(); it != map.cend(); ++it)
            collectMappings(it.value(), records);
    } else if (value.metaType().id() == QMetaType::QVariantList) {
        for (const QVariant &child : value.toList())
            collectMappings(child, records);
    }
}

QString jsonString(const QString &value) {
    const QByteArray encoded = QJsonDocument(QJsonArray{value})
                                   .toJson(QJsonDocument::Compact);
    return QString::fromUtf8(encoded.mid(1, encoded.size() - 2));
}

QString canonicalJson(const QVariant &value) {
    if (isMapping(value)) {
        const QVariantMap object = asMap(value);
        QStringList keys;
        keys.reserve(object.size());
        for (auto it = object.begin(); it != object.end(); ++it)
            keys.append(it.key());
        std::sort(keys.begin(), keys.end());
        QStringList members;
        members.reserve(keys.size());
        for (const QString &key : keys) {
            members.append(jsonString(key) + QLatin1Char(':')
                           + QLatin1Char(' ') + canonicalJson(object.value(key)));
        }
        return QLatin1Char('{') + members.join(QStringLiteral(", ")) + QLatin1Char('}');
    }
    if (value.metaType().id() == QMetaType::QVariantList) {
        QStringList elements;
        const QVariantList array = value.toList();
        elements.reserve(array.size());
        for (const QVariant &element : array)
            elements.append(canonicalJson(element));
        return QLatin1Char('[') + elements.join(QStringLiteral(", ")) + QLatin1Char(']');
    }
    if (value.metaType().id() == QMetaType::QString)
        return jsonString(value.toString());
    if (!value.isValid() || value.isNull())
        return QStringLiteral("null");
    if (value.metaType().id() == QMetaType::Bool)
        return value.toBool() ? QStringLiteral("true") : QStringLiteral("false");
    if (value.metaType().id() == QMetaType::Double || value.metaType().id() == QMetaType::Float) {
        char buffer[64];
        const double number = value.toDouble();
        const auto [end, error] = std::to_chars(buffer, buffer + sizeof(buffer), number);
        if (error == std::errc{}) {
            QString text = QString::fromLatin1(buffer, qsizetype(end - buffer));
            if (!text.contains(QLatin1Char('.')) && !text.contains(QLatin1Char('e'))
                && !text.contains(QLatin1Char('E')))
                text += QStringLiteral(".0");
            return text;
        }
    }
    return value.toString();
}

QString nodeDigest(const QVariantMap &node) {
    const QByteArray bytes = canonicalJson(node).toUtf8();
    return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256)
                                   .toHex());
}

QVariantMap identifiedEnvelope(const QVariantMap &node, const QVariantMap &payload) {
    const QString workspace = payload.contains(QStringLiteral("workspace"))
        ? pythonString(payload.value(QStringLiteral("workspace"))) : QString{};
    QVariant runId;
    const QVariant directId = payload.value(QStringLiteral("run_id"));
    const QVariant selectedId = payload.value(QStringLiteral("selected_run_id"));
    if (truthy(directId))
        runId = directId;
    else if (truthy(selectedId))
        runId = selectedId;
    else if (!workspace.isEmpty())
        runId = QDir::cleanPath(workspace).section(QLatin1Char('/'), -1);
    else
        runId = QString{};

    QVariantMap result = node;
    result.insert(QStringLiteral("run_id"), runId);
    result.insert(QStringLiteral("workspace"), workspace);
    return result;
}

bool hasFanAtpgEvidence(const QVariantMap &node) {
    if (node.value(QStringLiteral("backend")).toString() != QStringLiteral("fan_atpg")
        || !isMapping(node.value(QStringLiteral("cross_validation"))))
        return false;
    QList<QVariantMap> nested;
    collectMappings(node, &nested);
    for (const QVariantMap &item : nested) {
        if (item.value(QStringLiteral("candidates")).metaType().id() == QMetaType::QVariantList
            && isMapping(item.value(QStringLiteral("goal"))))
            return true;
    }
    return false;
}
} // namespace

QList<QVariantMap> RepairRunEvidenceService::controlledRunPayloads(const QVariant &value) {
    QList<QVariantMap> nodes;
    collectMappings(value, &nodes);

    QList<QVariantMap> records;
    for (const QVariantMap &node : nodes) {
        const QVariant executed = node.value(QStringLiteral("executed"));
        const bool legacyExecuted = executed.metaType().id() == QMetaType::Bool && executed.toBool();
        if (!legacyExecuted) {
            // The native asynchronous flow returns a verified result envelope,
            // not the legacy Python adapter's {executed: true} wrapper.
            const QVariantMap execution = asMap(node.value(QStringLiteral("execution")));
            const QString workspace = execution.value(QStringLiteral("workspace")).toString().trimmed();
            const bool hasTerminalExecution = execution.contains(QStringLiteral("completed_cleanly"))
                || execution.contains(QStringLiteral("returncode"));
            if (workspace.isEmpty() || !hasTerminalExecution)
                continue;

            QVariantMap nativeRecord = node;
            nativeRecord.insert(QStringLiteral("executed"), true);
            nativeRecord.insert(QStringLiteral("workspace"), workspace);
            if (!truthy(nativeRecord.value(QStringLiteral("run_id"))))
                nativeRecord.insert(QStringLiteral("run_id"),
                    QDir::cleanPath(workspace).section(QLatin1Char('/'), -1));
            records.append(nativeRecord);
            continue;
        }

        QVariantMap payload = node;
        bool identified = false;
        for (int depth = 0; depth < 8; ++depth) {
            if (truthy(payload.value(QStringLiteral("run_id")))
                || truthy(payload.value(QStringLiteral("selected_run_id")))
                || truthy(payload.value(QStringLiteral("workspace")))) {
                records.append(identifiedEnvelope(node, payload));
                identified = true;
                break;
            }
            const QVariant result = payload.value(QStringLiteral("result"));
            const QVariant execution = payload.value(QStringLiteral("execution"));
            if (isMapping(result))
                payload = asMap(result);
            else if (isMapping(execution))
                payload = asMap(execution);
            else
                break;
        }

        if (!identified && hasFanAtpgEvidence(node)) {
            QVariantMap fanRecord = node;
            fanRecord.insert(QStringLiteral("run_id"),
                             QStringLiteral("fan-") + nodeDigest(node).left(16));
            fanRecord.insert(QStringLiteral("workspace"), QString{});
            records.append(fanRecord);
        }
    }
    return records;
}
