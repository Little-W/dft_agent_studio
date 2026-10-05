#include "repairactionfingerprintservice.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonValue>
#include <QRegularExpression>
#include <QSet>
#include <algorithm>

namespace {
QString jsonString(const QString &value) {
    const QByteArray encoded = QJsonDocument(QJsonArray{value}).toJson(QJsonDocument::Compact);
    return QString::fromUtf8(encoded.mid(1, encoded.size() - 2));
}

QByteArray canonical(const QVariant &value) {
    if (!value.isValid() || value.isNull())
        return "null";

    const int type = value.metaType().id();
    if (type == QMetaType::QString || type == QMetaType::QChar)
        return jsonString(value.toString()).toUtf8();
    if (type == QMetaType::Bool)
        return value.toBool() ? QByteArray("true") : QByteArray("false");
    if (type == QMetaType::QVariantMap || type == QMetaType::QVariantHash) {
        QVariantMap map = value.toMap();
        if (type == QMetaType::QVariantHash) {
            const QVariantHash hash = value.toHash();
            for (auto it = hash.cbegin(); it != hash.cend(); ++it)
                map.insert(it.key(), it.value());
        }
        QByteArray output("{");
        bool first = true;
        for (auto it = map.cbegin(); it != map.cend(); ++it) {
            if (!first)
                output += ", ";
            first = false;
            output += jsonString(it.key()).toUtf8();
            output += ": ";
            output += canonical(it.value());
        }
        output += '}';
        return output;
    }
    if (type == QMetaType::QVariantList || type == QMetaType::QStringList) {
        const QVariantList list = value.toList();
        QByteArray output("[");
        for (qsizetype i = 0; i < list.size(); ++i) {
            if (i)
                output += ", ";
            output += canonical(list.at(i));
        }
        output += ']';
        return output;
    }
    if (type == QMetaType::QByteArray)
        return jsonString(QString::fromUtf8(value.toByteArray())).toUtf8();

    // Python json.dumps renders JSON-compatible numeric scalars without whitespace.
    const QJsonValue jsonValue = QJsonValue::fromVariant(value);
    if (jsonValue.isDouble()) {
        const QByteArray encoded = QJsonDocument(QJsonArray{jsonValue})
                                      .toJson(QJsonDocument::Compact);
        return encoded.mid(1, encoded.size() - 2);
    }
    if (jsonValue.isBool())
        return jsonValue.toBool() ? QByteArray("true") : QByteArray("false");
    if (jsonValue.isString())
        return jsonString(jsonValue.toString()).toUtf8();
    return "null";
}

QString pathForPythonPosix(QString path) {
    return path.replace(QDir::separator(), QLatin1Char('/'));
}

QString resolvedAbsolute(const QString &path) {
    QString candidate = QDir::cleanPath(QFileInfo(path).absoluteFilePath());
    QStringList suffix;
    while (!QFileInfo::exists(candidate)) {
        const QFileInfo info(candidate);
        if (info.isSymLink()) {
            const QString target = info.symLinkTarget();
            if (!target.isEmpty())
                candidate = QDir::cleanPath(QFileInfo(target).absoluteFilePath());
        }
        const QString parent = QFileInfo(candidate).absolutePath();
        if (parent == candidate)
            break;
        suffix.prepend(QFileInfo(candidate).fileName());
        candidate = parent;
    }
    QString resolved = QFileInfo(candidate).canonicalFilePath();
    if (resolved.isEmpty())
        resolved = candidate;
    if (!suffix.isEmpty())
        resolved = QDir(resolved).filePath(suffix.join(QDir::separator()));
    return QDir::cleanPath(resolved);
}

QString relativePathForPython(const QString &path) {
    QStringList components;
    for (const QString &component : path.split(QLatin1Char('/'), Qt::SkipEmptyParts)) {
        if (component != QStringLiteral("."))
            components.append(component);
    }
    return components.isEmpty() ? QStringLiteral(".") : components.join(QLatin1Char('/'));
}

QVariant normalizedArgumentsList(const QVariant &value) {
    QVariantList result;
    if (value.metaType().id() == QMetaType::QStringList) {
        const QStringList strings = value.toStringList();
        for (const QString &item : strings)
            result.append(item);
    } else {
        result = value.toList();
    }
    return result;
}
} // namespace

QByteArray RepairActionFingerprintService::canonicalJson(const QVariant &value) {
    return canonical(value);
}

QString RepairActionFingerprintService::digest(const QVariant &value) {
    const QByteArray bytes = value.metaType().id() == QMetaType::QByteArray
        ? value.toByteArray() : canonical(value);
    return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}

QString RepairActionFingerprintService::canonicalProjectPath(const QString &rawPath,
                                                              const QString &projectRoot) {
    const QString path = rawPath.trimmed();
    if (path.isEmpty())
        return QStringLiteral(".");
    if (!QDir::isAbsolutePath(path))
        return relativePathForPython(pathForPythonPosix(path));

    const QString resolvedPath = resolvedAbsolute(path);
    const QString resolvedRoot = resolvedAbsolute(projectRoot);
    const QString relative = QDir(resolvedRoot).relativeFilePath(resolvedPath);
    if (relative != QStringLiteral("..") && !relative.startsWith(QStringLiteral("../"))
        && !QDir::isAbsolutePath(relative))
        return pathForPythonPosix(relative);
    return pathForPythonPosix(resolvedPath);
}

QString RepairActionFingerprintService::actionSignature(const QString &name,
                                                         const QVariantMap &arguments,
                                                         const QString &projectRoot) {
    QVariantMap material;
    material.insert(QStringLiteral("tool"), name);
    if (name == QStringLiteral("search_project_text")) {
        material.insert(QStringLiteral("path"), arguments.value(QStringLiteral("path")).toString().trimmed());
        material.insert(QStringLiteral("path_prefix"), arguments.value(QStringLiteral("path_prefix")).toString().trimmed());
        material.insert(QStringLiteral("query"), arguments.value(QStringLiteral("query")).toString().trimmed().toCaseFolded());
    } else if (name == QStringLiteral("apply_patch")) {
        QString patch = arguments.value(QStringLiteral("patch")).toString();
        if (patch.isEmpty())
            patch = arguments.value(QStringLiteral("diff")).toString();
        if (patch.isEmpty())
            patch = arguments.value(QStringLiteral("patch_text")).toString();

        QStringList files;
        const QVariantList rawFiles = normalizedArgumentsList(arguments.value(QStringLiteral("files"))).toList();
        for (const QVariant &file : rawFiles) {
            const QString value = file.toString().trimmed();
            files.append(projectRoot.isEmpty() ? pathForPythonPosix(value)
                                               : canonicalProjectPath(value, projectRoot));
        }

        if (!projectRoot.isEmpty()) {
            static const QRegularExpression codexHeader(
                QStringLiteral(R"(^((?:\*\*\*\s+(?:Update|Add|Delete) File:\s*))(.+?)\s*$)"),
                QRegularExpression::MultilineOption);
            static const QRegularExpression unifiedHeader(
                QStringLiteral(R"(^(---|\+\+\+)\s+((?:a/|b/)?[^\t\n]+))"),
                QRegularExpression::MultilineOption);

            auto canonicalHeaderPath = [projectRoot](const QString &raw, bool stripDiffPrefix) {
                QString path = raw;
                if (stripDiffPrefix && path.startsWith(QStringLiteral("a/")))
                    path.remove(0, 2);
                if (stripDiffPrefix && path.startsWith(QStringLiteral("b/")))
                    path.remove(0, 2);
                return RepairActionFingerprintService::canonicalProjectPath(path, projectRoot);
            };
            auto rewriteMatches = [&patch](const QRegularExpression &expression, const auto &rewrite) {
                QString output;
                qsizetype cursor = 0;
                auto iterator = expression.globalMatch(patch);
                while (iterator.hasNext()) {
                    const QRegularExpressionMatch match = iterator.next();
                    output += patch.mid(cursor, match.capturedStart() - cursor);
                    output += rewrite(match);
                    cursor = match.capturedEnd();
                }
                if (cursor == 0)
                    return;
                output += patch.mid(cursor);
                patch = output;
            };
            rewriteMatches(codexHeader, [canonicalHeaderPath](const QRegularExpressionMatch &match) {
                return match.captured(1) + canonicalHeaderPath(match.captured(2), false);
            });
            rewriteMatches(unifiedHeader, [canonicalHeaderPath](const QRegularExpressionMatch &match) {
                return match.captured(1) + QLatin1Char(' ')
                    + canonicalHeaderPath(match.captured(2), true);
            });
        }
        patch.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
        material.insert(QStringLiteral("patch"), patch.trimmed());
        std::sort(files.begin(), files.end());
        material.insert(QStringLiteral("files"), files);
    } else if (name == QStringLiteral("create_file")) {
        material.insert(QStringLiteral("path"), arguments.value(QStringLiteral("path")).toString().trimmed());
        material.insert(QStringLiteral("content"), arguments.value(QStringLiteral("content")).toString());
    } else {
        material.insert(QStringLiteral("arguments"), arguments);
    }
    return digest(material);
}
