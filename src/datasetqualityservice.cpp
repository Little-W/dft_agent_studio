#include "datasetqualityservice.h"

#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>

namespace {
const QSet<QString> kVolatileKeys{
    QStringLiteral("case"), QStringLiteral("case_id"), QStringLiteral("case_reference"),
    QStringLiteral("case_variant"), QStringLiteral("call_id"), QStringLiteral("content_sha256"),
    QStringLiteral("created_at"), QStringLiteral("dataset_sha256"), QStringLiteral("episode_id"),
    QStringLiteral("project"), QStringLiteral("project_id"), QStringLiteral("run_id"),
    QStringLiteral("sample_id"), QStringLiteral("session_id"), QStringLiteral("sha256"),
    QStringLiteral("source_case"), QStringLiteral("source_id"), QStringLiteral("task_id"),
    QStringLiteral("timestamp"), QStringLiteral("workspace"), QStringLiteral("workspace_id")};
const QSet<QString> kPathKeys{
    QStringLiteral("artifact_name"), QStringLiteral("file"), QStringLiteral("file_path"),
    QStringLiteral("path"), QStringLiteral("relative_path"), QStringLiteral("report"),
    QStringLiteral("report_path"), QStringLiteral("script"), QStringLiteral("script_path"),
    QStringLiteral("source"), QStringLiteral("source_path")};
const QRegularExpression kEngineeringRule(QStringLiteral(R"(\b[A-Z]{1,10}(?:[-_]\d+|\d+)(?:\.\d+)?\b)"));
const QRegularExpression kIeee(QStringLiteral(R"(IEEE\s+\d+(?:\.\d+)*)"), QRegularExpression::CaseInsensitiveOption);
const QRegularExpression kHash(QStringLiteral(R"(\b[0-9a-f]{12,64}\b)"), QRegularExpression::CaseInsensitiveOption);
const QRegularExpression kCallId(QStringLiteral(R"(\bcall[_-][A-Za-z0-9_.:-]+\b)"), QRegularExpression::CaseInsensitiveOption);
const QRegularExpression kDateTime(QStringLiteral(R"(\b(?:20\d{2}[-/]\d{1,2}[-/]\d{1,2}(?:[T ]\d{1,2}:\d{2}(?::\d{2})?)?|\d{1,2}:\d{2}:\d{2})\b)"));
const QRegularExpression kNumber(QStringLiteral(R"([-+]?\d+(?:\.\d+)?%?)"));
const QRegularExpression kProjectPhrase(QStringLiteral(R"((?i:工程(?:代号|名称)?(?:为|是)\s*|project\s*(?:=|:)\s*)[^\s,，。;；"}]+)"));
const QRegularExpression kAbsolutePath(QStringLiteral(R"((?<![A-Za-z0-9_])(?:/[A-Za-z0-9_.+~ -]+){2,})"));
const QRegularExpression kSpace(QStringLiteral(R"(\s+)"));

QJsonValue sortedValue(const QJsonValue &value) {
    if (value.isObject()) {
        QJsonObject result;
        QStringList keys = value.toObject().keys();
        std::sort(keys.begin(), keys.end());
        const QJsonObject source = value.toObject();
        for (const QString &key : keys)
            result.insert(key, sortedValue(source.value(key)));
        return result;
    }
    if (value.isArray()) {
        QJsonArray result;
        for (const QJsonValue &item : value.toArray())
            result.append(sortedValue(item));
        return result;
    }
    return value;
}

QJsonValue valueOr(const QJsonObject &object, const QString &key, const QJsonValue &fallback) {
    const QJsonValue value = object.value(key);
    return value.isUndefined() ? fallback : value;
}

QByteArray compact(const QJsonValue &value) {
    const QJsonValue sorted = sortedValue(value);
    if (sorted.isObject())
        return QJsonDocument(sorted.toObject()).toJson(QJsonDocument::Compact);
    if (sorted.isArray())
        return QJsonDocument(sorted.toArray()).toJson(QJsonDocument::Compact);
    return QJsonDocument(QJsonArray{sorted}).toJson(QJsonDocument::Compact).sliced(1).chopped(1);
}

QString letterIndex(int index) {
    QString result;
    do {
        result.prepend(QChar(QLatin1Char('a').unicode() + index % 26));
        index = index / 26 - 1;
    } while (index >= 0);
    return result;
}

QString protectMatches(const QString &input, const QRegularExpression &pattern,
                       QStringList *preserved) {
    QString result;
    qsizetype offset = 0;
    auto iterator = pattern.globalMatch(input);
    while (iterator.hasNext()) {
        const auto match = iterator.next();
        result.append(input.mid(offset, match.capturedStart() - offset));
        const QString token = match.captured().toLower().replace(QLatin1Char(' '), QLatin1Char('_'));
        preserved->append(token);
        result.append(QStringLiteral("__protected_%1__").arg(letterIndex(preserved->size() - 1)));
        offset = match.capturedEnd();
    }
    result.append(input.mid(offset));
    return result;
}

QString replaceAbsolutePaths(const QString &text) {
    QString result;
    qsizetype offset = 0;
    auto iterator = kAbsolutePath.globalMatch(text);
    while (iterator.hasNext()) {
        const auto match = iterator.next();
        result.append(text.mid(offset, match.capturedStart() - offset));
        QString basename = match.captured().section(QLatin1Char('/'), -1);
        basename.replace(kNumber, QStringLiteral("<n>"));
        result.append(basename.isEmpty() ? QStringLiteral("<path>") : QStringLiteral("<path>/") + basename);
        offset = match.capturedEnd();
    }
    result.append(text.mid(offset));
    return result;
}

QJsonValue normalizeValue(const QJsonValue &value, const QString &key = {}) {
    const QString normalizedKey = key.toLower();
    if (kVolatileKeys.contains(normalizedKey) || normalizedKey.endsWith(QStringLiteral("_sha256"))
        || normalizedKey.endsWith(QStringLiteral("_timestamp")))
        return QStringLiteral("<%1>").arg(normalizedKey);
    if (kPathKeys.contains(normalizedKey) || normalizedKey.endsWith(QStringLiteral("_path"))
        || normalizedKey.endsWith(QStringLiteral("_file")))
        return DatasetQualityService::normalizeTrainingText(value.toVariant().toString());
    if (value.isObject()) {
        QJsonObject result;
        const QJsonObject source = value.toObject();
        QStringList keys = source.keys();
        std::sort(keys.begin(), keys.end());
        for (const QString &childKey : keys)
            result.insert(childKey, normalizeValue(source.value(childKey), childKey));
        return result;
    }
    if (value.isArray()) {
        QJsonArray result;
        for (const QJsonValue &item : value.toArray())
            result.append(normalizeValue(item));
        return result;
    }
    if (value.isString()) {
        const QString text = value.toString();
        QJsonParseError error{};
        const QByteArray wrapped = QByteArrayLiteral("[") + text.toUtf8() + QByteArrayLiteral("]");
        const QJsonDocument parsed = QJsonDocument::fromJson(wrapped, &error);
        if (error.error == QJsonParseError::NoError && parsed.isArray() && parsed.array().size() == 1) {
            const QJsonValue scalar = parsed.array().at(0);
            if (!scalar.isNull())
                return normalizeValue(scalar);
        }
        if (text.trimmed() == QStringLiteral("null"))
            return DatasetQualityService::normalizeTrainingText(text);
        return DatasetQualityService::normalizeTrainingText(text);
    }
    if (value.isDouble())
        return QStringLiteral("<n>");
    return value;
}

QJsonArray messagesOf(const QJsonObject &row) {
    return row.value(QStringLiteral("messages")).toArray();
}

QString caseName(const QJsonObject &row, int index) {
    const QJsonValue value = row.value(QStringLiteral("metadata")).toObject().value(QStringLiteral("case"));
    if (!value.isUndefined() && !value.isNull() && !value.toString().isEmpty())
        return value.toVariant().toString();
    return QStringLiteral("row:%1").arg(index + 1);
}

QJsonArray roleContent(const QJsonObject &row, const QString &role, bool normalized) {
    QJsonArray result;
    for (const QJsonValue &value : messagesOf(row)) {
        const QJsonObject message = value.toObject();
        if (message.value(QStringLiteral("role")).toString() == role) {
            const QJsonValue content = message.value(QStringLiteral("content"));
            result.append(normalized ? normalizeValue(content) : (content.isUndefined() ? QJsonValue(QStringLiteral("")) : content));
        }
    }
    return result;
}

QJsonArray normalizedConversation(const QJsonObject &row) {
    QJsonArray result;
    for (const QJsonValue &value : messagesOf(row)) {
        const QJsonObject message = value.toObject();
        const QString role = message.value(QStringLiteral("role")).toString();
        if (role == QStringLiteral("system"))
            continue;
        QJsonObject item{{QStringLiteral("role"), role}};
        const QJsonValue content = message.value(QStringLiteral("content"));
        if (!content.isUndefined() && !content.isNull() && content != QJsonValue(QStringLiteral("")))
            item.insert(QStringLiteral("content"), normalizeValue(content));
        if (role == QStringLiteral("assistant") && message.value(QStringLiteral("tool_calls")).isArray()) {
            QJsonArray calls;
            for (const QJsonValue &callValue : message.value(QStringLiteral("tool_calls")).toArray()) {
                const QJsonObject function = callValue.toObject().value(QStringLiteral("function")).toObject();
                calls.append(QJsonObject{{QStringLiteral("name"), valueOr(function, QStringLiteral("name"), QStringLiteral(""))},
                    {QStringLiteral("arguments"), normalizeValue(valueOr(function, QStringLiteral("arguments"), QStringLiteral("")))}});
            }
            if (!calls.isEmpty())
                item.insert(QStringLiteral("tool_calls"), calls);
        }
        if (role == QStringLiteral("tool"))
            item.insert(QStringLiteral("name"), message.value(QStringLiteral("name")).toString());
        result.append(item);
    }
    return result;
}

struct DuplicateSummary {
    int unique = 0;
    int duplicateRows = 0;
    int duplicateGroups = 0;
    int largest = 0;
};

DuplicateSummary duplicateSummary(const QStringList &values) {
    QHash<QString, int> counts;
    for (const QString &value : values)
        ++counts[value];
    DuplicateSummary result;
    result.unique = counts.size();
    for (auto it = counts.cbegin(); it != counts.cend(); ++it) {
        result.duplicateRows += qMax(0, it.value() - 1);
        result.duplicateGroups += it.value() > 1;
        result.largest = qMax(result.largest, it.value());
    }
    return result;
}

QJsonObject summaryJson(const DuplicateSummary &summary) {
    return {{QStringLiteral("unique"), summary.unique},
            {QStringLiteral("duplicate_rows"), summary.duplicateRows},
            {QStringLiteral("duplicate_groups"), summary.duplicateGroups},
            {QStringLiteral("largest_group"), summary.largest}};
}

QString jsonText(const QJsonValue &value) {
    return QString::fromUtf8(compact(value));
}

QJsonObject repetition(const QJsonObject &row) {
    QStringList assistants;
    QStringList calls;
    for (const QJsonValue &value : messagesOf(row)) {
        const QJsonObject message = value.toObject();
        if (message.value(QStringLiteral("role")).toString() != QStringLiteral("assistant"))
            continue;
        const QJsonValue content = normalizeValue(valueOr(message, QStringLiteral("content"), QStringLiteral("")));
        if (content != QJsonValue(QStringLiteral("")) && !content.isNull())
            assistants.append(jsonText(content));
        for (const QJsonValue &callValue : message.value(QStringLiteral("tool_calls")).toArray()) {
            const QJsonObject function = callValue.toObject().value(QStringLiteral("function")).toObject();
            calls.append(jsonText(QJsonObject{{QStringLiteral("name"), valueOr(function, QStringLiteral("name"), QStringLiteral(""))},
                {QStringLiteral("arguments"), normalizeValue(valueOr(function, QStringLiteral("arguments"), QStringLiteral("")))}}));
        }
    }
    const QSet<QString> uniqueAssistants(assistants.cbegin(), assistants.cend());
    const QSet<QString> uniqueCalls(calls.cbegin(), calls.cend());
    return {{QStringLiteral("assistant_turns"), assistants.size()},
            {QStringLiteral("repeated_assistant_turns"), assistants.size() - uniqueAssistants.size()},
            {QStringLiteral("tool_calls"), calls.size()},
            {QStringLiteral("repeated_tool_calls"), calls.size() - uniqueCalls.size()}};
}
}

QString DatasetQualityService::normalizeTrainingText(const QString &text) {
    QStringList protectedValues;
    QString result = protectMatches(text, kIeee, &protectedValues);
    result = protectMatches(result, kEngineeringRule, &protectedValues);
    result.replace(kHash, QStringLiteral("<hash>"));
    result.replace(kCallId, QStringLiteral("<call>"));
    result.replace(kDateTime, QStringLiteral("<time>"));
    result.replace(kProjectPhrase, QStringLiteral("\\1<project>"));
    result = replaceAbsolutePaths(result);
    result.replace(kNumber, QStringLiteral("<n>"));
    for (int index = 0; index < protectedValues.size(); ++index)
        result.replace(QStringLiteral("__protected_%1__").arg(letterIndex(index)), protectedValues.at(index));
    result = result.toLower();
    result.replace(kSpace, QStringLiteral(" "));
    return result.trimmed();
}

QString DatasetQualityService::conversationFingerprint(const QJsonObject &row) {
    const QByteArray bytes = compact(normalizedConversation(row));
    return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}

QJsonObject DatasetQualityService::auditStructure(const QJsonArray &rows) {
    QHash<QString, int> issues;
    int callsCount = 0;
    int resultsCount = 0;
    for (const QJsonValue &rowValue : rows) {
        const QJsonObject row = rowValue.toObject();
        const QJsonValue messagesValue = row.value(QStringLiteral("messages"));
        if (!messagesValue.isArray() || messagesValue.toArray().isEmpty()) {
            ++issues[QStringLiteral("invalid_messages")];
            continue;
        }
        const QJsonArray messages = messagesValue.toArray();
        QStringList roles;
        bool allObjects = true;
        for (const QJsonValue &messageValue : messages) {
            if (!messageValue.isObject()) {
                allObjects = false;
                roles.append(QString{});
            } else {
                roles.append(messageValue.toObject().value(QStringLiteral("role")).toString());
            }
        }
        const QSet<QString> allowed{QStringLiteral("system"), QStringLiteral("user"),
            QStringLiteral("assistant"), QStringLiteral("tool")};
        if (!allObjects || std::any_of(roles.cbegin(), roles.cend(), [&allowed](const QString &role) { return !allowed.contains(role); }))
            ++issues[QStringLiteral("invalid_role")];
        if (roles.first() != QStringLiteral("system") || roles.count(QStringLiteral("system")) != 1)
            ++issues[QStringLiteral("system_position_or_count")];
        if (roles.count(QStringLiteral("user")) != 1)
            ++issues[QStringLiteral("user_count")];
        if (roles.last() != QStringLiteral("assistant"))
            ++issues[QStringLiteral("final_role")];

        QStringList callPairs;
        QStringList resultPairs;
        for (const QJsonValue &messageValue : messages) {
            const QJsonObject message = messageValue.toObject();
            const QString role = message.value(QStringLiteral("role")).toString();
            if (role == QStringLiteral("assistant")) {
                if (message.value(QStringLiteral("content")).toString().trimmed().isEmpty()
                    && message.value(QStringLiteral("tool_calls")).toArray().isEmpty())
                    ++issues[QStringLiteral("empty_assistant")];
                for (const QJsonValue &callValue : message.value(QStringLiteral("tool_calls")).toArray()) {
                    ++callsCount;
                    const QJsonObject call = callValue.toObject();
                    const QJsonObject function = call.value(QStringLiteral("function")).toObject();
                    const QString id = call.value(QStringLiteral("id")).toString();
                    const QString name = function.value(QStringLiteral("name")).toString();
                    const QJsonValue arguments = function.value(QStringLiteral("arguments"));
                    if (call.isEmpty() || call.value(QStringLiteral("type")).toString() != QStringLiteral("function")
                        || id.isEmpty() || function.isEmpty() || name.isEmpty()) {
                        ++issues[QStringLiteral("invalid_tool_call")];
                    }
                    if (arguments.isString()) {
                        QJsonParseError error{};
                        const QJsonDocument parsed = QJsonDocument::fromJson(arguments.toString().toUtf8(), &error);
                        if (error.error != QJsonParseError::NoError || !parsed.isObject())
                            ++issues[QStringLiteral("invalid_tool_arguments")];
                    } else if (!arguments.isObject()) {
                        ++issues[QStringLiteral("invalid_tool_arguments")];
                    }
                    if (!call.isEmpty() && call.value(QStringLiteral("type")).toString() == QStringLiteral("function")
                        && !id.isEmpty() && !function.isEmpty() && !name.isEmpty())
                        callPairs.append(id + QChar(0x1f) + name);
                }
            } else if (role == QStringLiteral("tool")) {
                ++resultsCount;
                resultPairs.append(message.value(QStringLiteral("tool_call_id")).toString()
                    + QChar(0x1f) + message.value(QStringLiteral("name")).toString());
            }
        }
        if (callPairs != resultPairs)
            ++issues[QStringLiteral("tool_call_result_pairing")];
        QSet<QString> declared;
        for (const QJsonValue &toolValue : row.value(QStringLiteral("tools")).toArray()) {
            const QJsonObject tool = toolValue.toObject();
            if (tool.value(QStringLiteral("type")).toString() == QStringLiteral("function"))
                declared.insert(tool.value(QStringLiteral("function")).toObject().value(QStringLiteral("name")).toString());
        }
        for (const QString &call : callPairs) {
            const QString name = call.section(QChar(0x1f), 1);
            if (!declared.contains(name)) {
                ++issues[QStringLiteral("undeclared_tool")];
                break;
            }
        }
    }
    QJsonObject issueObject;
    QStringList issueKeys = issues.keys();
    std::sort(issueKeys.begin(), issueKeys.end());
    for (const QString &key : issueKeys)
        issueObject.insert(key, issues.value(key));
    return {{QStringLiteral("valid"), issues.isEmpty()}, {QStringLiteral("issues"), issueObject},
            {QStringLiteral("tool_calls"), callsCount}, {QStringLiteral("tool_results"), resultsCount}};
}

QJsonObject DatasetQualityService::auditRows(const QJsonArray &rows, int maximumExampleGroups) {
    QStringList exactCases, exactMessages, exactUsers, exactAssistants;
    QStringList normalizedUsers, normalizedAssistants, normalizedConversations;
    QStringList cases;
    QList<QJsonObject> rowObjects;
    QHash<QString, QStringList> conversationGroups;
    int rowsRepeatedAssistant = 0, repeatedAssistant = 0, rowsRepeatedCalls = 0, repeatedCalls = 0;
    for (qsizetype index = 0; index < rows.size(); ++index) {
        const QJsonObject row = rows.at(index).toObject();
        rowObjects.append(row);
        const QString caseValue = caseName(row, static_cast<int>(index));
        cases.append(caseValue);
        exactCases.append(caseValue);
        exactMessages.append(jsonText(messagesOf(row)));
        exactUsers.append(jsonText(roleContent(row, QStringLiteral("user"), false)));
        exactAssistants.append(jsonText(roleContent(row, QStringLiteral("assistant"), false)));
        normalizedUsers.append(jsonText(roleContent(row, QStringLiteral("user"), true)));
        normalizedAssistants.append(jsonText(roleContent(row, QStringLiteral("assistant"), true)));
        const QString conversation = jsonText(normalizedConversation(row));
        normalizedConversations.append(conversation);
        conversationGroups[conversation].append(caseValue);
        const QJsonObject counts = repetition(row);
        const int repeatedA = counts.value(QStringLiteral("repeated_assistant_turns")).toInt();
        const int repeatedT = counts.value(QStringLiteral("repeated_tool_calls")).toInt();
        rowsRepeatedAssistant += repeatedA > 0;
        repeatedAssistant += repeatedA;
        rowsRepeatedCalls += repeatedT > 0;
        repeatedCalls += repeatedT;
    }
    QList<QJsonObject> examples;
    for (auto it = conversationGroups.cbegin(); it != conversationGroups.cend(); ++it) {
        if (it.value().size() <= 1)
            continue;
        QStringList names = it.value();
        std::sort(names.begin(), names.end());
        QJsonArray items;
        for (int index = 0; index < qMin(8, names.size()); ++index)
            items.append(names.at(index));
        examples.append(QJsonObject{{QStringLiteral("count"), names.size()}, {QStringLiteral("cases"), items}});
    }
    std::sort(examples.begin(), examples.end(), [](const QJsonObject &left, const QJsonObject &right) {
        const int lc = left.value(QStringLiteral("count")).toInt();
        const int rc = right.value(QStringLiteral("count")).toInt();
        if (lc != rc) return lc > rc;
        return left.value(QStringLiteral("cases")).toArray().first().toString()
            < right.value(QStringLiteral("cases")).toArray().first().toString();
    });
    QJsonArray exampleArray;
    for (int index = 0; index < qMin(maximumExampleGroups, examples.size()); ++index)
        exampleArray.append(examples.at(index));

    const DuplicateSummary summaries[] = {duplicateSummary(exactCases), duplicateSummary(exactMessages),
        duplicateSummary(exactUsers), duplicateSummary(exactAssistants), duplicateSummary(normalizedUsers),
        duplicateSummary(normalizedAssistants), duplicateSummary(normalizedConversations)};
    const QString keys[] = {QStringLiteral("exact_case"), QStringLiteral("exact_messages"),
        QStringLiteral("exact_user"), QStringLiteral("exact_assistant"), QStringLiteral("normalized_user"),
        QStringLiteral("normalized_assistant"), QStringLiteral("normalized_conversation")};
    QJsonObject report{{QStringLiteral("rows"), rows.size()},
        {QStringLiteral("structure"), auditStructure(rows)},
        {QStringLiteral("normalized_conversation_examples"), exampleArray},
        {QStringLiteral("rows_with_repeated_assistant_turns"), rowsRepeatedAssistant},
        {QStringLiteral("repeated_assistant_turns"), repeatedAssistant},
        {QStringLiteral("rows_with_repeated_tool_calls"), rowsRepeatedCalls},
        {QStringLiteral("repeated_tool_calls"), repeatedCalls}};
    bool strictUnique = rowsRepeatedAssistant == 0 && rowsRepeatedCalls == 0;
    for (int index = 0; index < 7; ++index) {
        report.insert(keys[index], summaryJson(summaries[index]));
        strictUnique &= summaries[index].duplicateRows == 0;
    }
    const bool passed = strictUnique && report.value(QStringLiteral("structure")).toObject().value(QStringLiteral("valid")).toBool();
    report.insert(QStringLiteral("strict_unique"), strictUnique);
    report.insert(QStringLiteral("passed"), passed);
    return report;
}

QVariantMap DatasetQualityService::selectUniqueRows(const QJsonArray &rows,
                                                     const QStringList &existingFingerprints,
                                                     int maximumRows) {
    QSet<QString> fingerprints(existingFingerprints.cbegin(), existingFingerprints.cend());
    QJsonArray selected;
    int rejected = 0;
    for (const QJsonValue &value : rows) {
        if (!value.isObject())
            continue;
        const QString fingerprint = conversationFingerprint(value.toObject());
        if (fingerprints.contains(fingerprint)) {
            ++rejected;
            continue;
        }
        fingerprints.insert(fingerprint);
        selected.append(value);
        if (maximumRows >= 0 && selected.size() >= maximumRows)
            break;
    }
    return {{QStringLiteral("rows"), selected.toVariantList()}, {QStringLiteral("rejected"), rejected}};
}

QVariantMap DatasetQualityService::auditJsonl(const QString &datasetPath, int maximumExampleGroups) {
    QFile file(datasetPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return {{QStringLiteral("ok"), false}, {QStringLiteral("message"), file.errorString()}};
    QJsonArray rows;
    qsizetype lineNumber = 0;
    while (!file.atEnd()) {
        const QByteArray line = file.readLine();
        ++lineNumber;
        if (line.trimmed().isEmpty())
            continue;
        QJsonParseError error{};
        const QJsonDocument document = QJsonDocument::fromJson(line, &error);
        if (error.error != QJsonParseError::NoError || !document.isObject())
            return {{QStringLiteral("ok"), false}, {QStringLiteral("message"),
                QStringLiteral("%1:%2 不是 JSON 对象：%3").arg(datasetPath).arg(lineNumber).arg(error.errorString())}};
        rows.append(document.object());
    }
    if (file.error() != QFileDevice::NoError)
        return {{QStringLiteral("ok"), false}, {QStringLiteral("message"), file.errorString()}};
    return {{QStringLiteral("ok"), true}, {QStringLiteral("report"), auditRows(rows, maximumExampleGroups)}};
}
