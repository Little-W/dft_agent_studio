#include "patchactionservice.h"

#include "studiopaths.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QTemporaryDir>
#include <QStringList>
#include <QUuid>
#include <utility>

namespace {
QVariantMap failure(const QString &message) {
    return {{QStringLiteral("ok"), false}, {QStringLiteral("message"), message}};
}

QString runtimePatchRoot(const QString &agentRoot) {
    return QDir(studioDataRoot(agentRoot)).filePath(QStringLiteral("agent_runtime/patches"));
}

QString digestFile(const QString &path, bool *ok = nullptr) {
    QFile file(path);
    const bool readOk = file.open(QIODevice::ReadOnly);
    if (ok)
        *ok = readOk;
    if (!readOk)
        return {};
    QCryptographicHash hash(QCryptographicHash::Sha256);
    while (!file.atEnd()) {
        const QByteArray chunk = file.read(256 * 1024);
        if (chunk.isEmpty() && file.error() != QFileDevice::NoError) {
            if (ok)
                *ok = false;
            return {};
        }
        hash.addData(chunk);
    }
    if (ok && file.error() != QFileDevice::NoError)
        *ok = false;
    return QString::fromLatin1(hash.result().toHex());
}

QString nowUtc() {
    return QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
}

bool validId(const QString &id) {
    static const QRegularExpression expression(QStringLiteral("^[0-9a-f]{32}$"));
    return expression.match(id).hasMatch();
}

bool pathWithin(const QString &root, const QString &path) {
    const QString relative = QDir(root).relativeFilePath(path);
    return relative != QStringLiteral("..") && !relative.startsWith(QStringLiteral("../"))
        && !QFileInfo(relative).isAbsolute();
}

QString normalizedProjectRoot(const QString &path) {
    const QFileInfo info(path);
    return info.canonicalFilePath();
}

bool safeRelativePath(const QString &projectRoot, const QString &relative, QString *resolved) {
    const QString clean = QDir::cleanPath(relative);
    if (relative.trimmed().isEmpty() || QDir::isAbsolutePath(relative)
        || clean == QStringLiteral("..") || clean.startsWith(QStringLiteral("../")))
        return false;
    const QString target = QDir(projectRoot).absoluteFilePath(clean);
    const QFileInfo info(target);
    if (info.isSymLink())
        return false;
    QString probe = info.absolutePath();
    QStringList missingComponents;
    while (!QFileInfo::exists(probe)) {
        const QFileInfo component(probe);
        missingComponents.prepend(component.fileName());
        const QString parent = component.absolutePath();
        if (parent == probe)
            return false;
        probe = parent;
    }
    const QString canonicalParent = QFileInfo(probe).canonicalFilePath();
    if (canonicalParent.isEmpty() || !pathWithin(projectRoot, canonicalParent))
        return false;
    for (const QString &component : missingComponents) {
        if (component == QStringLiteral(".") || component == QStringLiteral(".."))
            return false;
    }
    *resolved = QDir(canonicalParent).filePath(missingComponents.isEmpty()
        ? info.fileName() : missingComponents.join(QDir::separator()) + QDir::separator() + info.fileName());
    return true;
}

QVariantMap readRecord(const QString &path, QJsonObject *record) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return failure(QStringLiteral("编辑记录无法读取。"));
    QJsonParseError error{};
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject())
        return failure(QStringLiteral("编辑记录格式无效。"));
    *record = doc.object();
    return {};
}

bool saveRecord(const QString &path, const QJsonObject &record, QString *error) {
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        *error = file.errorString();
        return false;
    }
    file.write(QJsonDocument(record).toJson(QJsonDocument::Indented));
    if (!file.commit()) {
        *error = file.errorString();
        return false;
    }
    return true;
}

bool saveBytes(const QString &path, const QByteArray &bytes, QString *error) {
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        *error = file.errorString();
        return false;
    }
    if (file.write(bytes) != bytes.size() || !file.commit()) {
        *error = file.errorString();
        return false;
    }
    return true;
}

bool copyFile(const QString &source, const QString &destination, QString *error) {
    QFile in(source);
    if (!in.open(QIODevice::ReadOnly)) {
        *error = in.errorString();
        return false;
    }
    if (!QDir().mkpath(QFileInfo(destination).absolutePath())) {
        *error = QStringLiteral("无法创建补丁目标目录。");
        return false;
    }
    QSaveFile out(destination);
    if (!out.open(QIODevice::WriteOnly)) {
        *error = out.errorString();
        return false;
    }
    while (!in.atEnd()) {
        const QByteArray chunk = in.read(256 * 1024);
        if (chunk.isEmpty() && in.error() != QFileDevice::NoError) {
            *error = in.errorString();
            return false;
        }
        if (out.write(chunk) != chunk.size()) {
            *error = out.errorString();
            return false;
        }
    }
    out.setPermissions(QFile::permissions(source));
    if (!out.commit()) {
        *error = out.errorString();
        return false;
    }
    return true;
}

struct DiffFile {
    QString oldPath;
    QString newPath;
    QStringList hunks;
};

QString diffName(QString header) {
    if (header.startsWith(QStringLiteral("a/")) || header.startsWith(QStringLiteral("b/")))
        header.remove(0, 2);
    const qsizetype tab = header.indexOf(QLatin1Char('\t'));
    if (tab >= 0)
        header.truncate(tab);
    return header;
}

QString unescapePatchPath(QString path) {
    QString result;
    result.reserve(path.size());
    for (qsizetype i = 0; i < path.size(); ++i) {
        const QChar current = path.at(i);
        if (current == QLatin1Char('\\') && i + 1 < path.size()) {
            const QChar next = path.at(i + 1);
            if (next.isPunct() || next.isSymbol()) {
                result.append(next);
                ++i;
                continue;
            }
        }
        result.append(current);
    }
    return result;
}

bool applyDiffFile(const QString &root, const DiffFile &file, QString *error) {
    const bool adds = file.oldPath == QStringLiteral("/dev/null");
    const bool deletes = file.newPath == QStringLiteral("/dev/null");
    const QString relative = adds ? file.newPath : file.oldPath;
    QString target;
    if (!safeRelativePath(root, relative, &target)) {
        *error = QStringLiteral("补丁路径无效：%1").arg(relative);
        return false;
    }
    QStringList sourceLines;
    bool hasTrailingNewline = true;
    if (!adds) {
        QFile input(target);
        if (!input.open(QIODevice::ReadOnly)) {
            *error = QStringLiteral("补丁源文件无法读取：%1").arg(relative);
            return false;
        }
        QByteArray bytes = input.readAll();
        hasTrailingNewline = bytes.endsWith('\n');
        sourceLines = QString::fromUtf8(bytes).split(QLatin1Char('\n'));
        if (hasTrailingNewline)
            sourceLines.removeLast();
    } else if (QFileInfo::exists(target)) {
        *error = QStringLiteral("补丁目标文件已存在：%1").arg(relative);
        return false;
    }

    QStringList output;
    qsizetype cursor = 0;
    for (qsizetype index = 0; index < file.hunks.size(); ++index) {
        const QString line = file.hunks.at(index);
        if (!line.startsWith(QStringLiteral("@@")))
            continue;
        QStringList expected;
        QStringList replacement;
        QChar previousKind;
        QChar lastOutputKind;
        bool outputHasNoNewlineMarker = false;
        for (++index; index < file.hunks.size(); ++index) {
            const QString hunkLine = file.hunks.at(index);
            if (hunkLine.startsWith(QStringLiteral("@@"))) {
                --index;
                break;
            }
            if (hunkLine == QStringLiteral("\\ No newline at end of file")) {
                if (previousKind == QLatin1Char('+') || previousKind == QLatin1Char(' '))
                    outputHasNoNewlineMarker = true;
                continue;
            }
            if (hunkLine.isEmpty() && index == file.hunks.size() - 1)
                continue;
            if (hunkLine.isEmpty()) {
                *error = QStringLiteral("补丁 hunk 缺少行类型标记。 ");
                return false;
            }
            const QChar kind = hunkLine.front();
            const QString content = hunkLine.mid(1);
            if (kind == QLatin1Char(' ') || kind == QLatin1Char('-'))
                expected.append(content);
            if (kind == QLatin1Char(' ') || kind == QLatin1Char('+')) {
                replacement.append(content);
                lastOutputKind = kind;
                outputHasNoNewlineMarker = false;
            }
            if (kind != QLatin1Char(' ') && kind != QLatin1Char('-') && kind != QLatin1Char('+')) {
                *error = QStringLiteral("补丁 hunk 含无法识别的行。 ");
                return false;
            }
            previousKind = kind;
        }
        qsizetype matchAt = -1;
        for (qsizetype start = cursor; start + expected.size() <= sourceLines.size(); ++start) {
            bool matches = true;
            for (qsizetype offset = 0; offset < expected.size(); ++offset) {
                if (sourceLines.at(start + offset) != expected.at(offset)) {
                    matches = false;
                    break;
                }
            }
            if (matches) {
                matchAt = start;
                break;
            }
        }
        if (matchAt < 0) {
            const auto decodeEscapedTabs = [](QString value) {
                value.replace(QStringLiteral("\\t"), QStringLiteral("\t"));
                return value;
            };
            const auto withoutIndentation = [](QString value) {
                qsizetype first = 0;
                while (first < value.size()
                       && (value.at(first) == QLatin1Char(' ') || value.at(first) == QLatin1Char('\t')))
                    ++first;
                value.remove(0, first);
                return value;
            };
            qsizetype candidate = -1;
            bool ambiguous = false;
            for (qsizetype start = cursor; start + expected.size() <= sourceLines.size(); ++start) {
                bool matches = true;
                for (qsizetype offset = 0; offset < expected.size(); ++offset) {
                    if (withoutIndentation(sourceLines.at(start + offset))
                        != withoutIndentation(decodeEscapedTabs(expected.at(offset)))) {
                        matches = false;
                        break;
                    }
                }
                if (!matches)
                    continue;
                if (candidate >= 0) {
                    ambiguous = true;
                    break;
                }
                candidate = start;
            }
            if (candidate >= 0 && !ambiguous) {
                matchAt = candidate;
                for (QString &line : replacement)
                    line.replace(QStringLiteral("\\t"), QStringLiteral("\t"));
            }
        }
        if (matchAt < 0) {
            *error = QStringLiteral("补丁上下文与源文件不匹配：%1。请重新读取精确源行，并基于当前正文重新构造更小的补丁 hunk。")
                          .arg(relative);
            return false;
        }
        if (matchAt + expected.size() == sourceLines.size()) {
            if (outputHasNoNewlineMarker) {
                hasTrailingNewline = false;
            } else if (lastOutputKind == QLatin1Char('+')) {
                hasTrailingNewline = true;
            } else if (replacement.isEmpty()) {
                hasTrailingNewline = !output.isEmpty();
            }
        }
        while (cursor < matchAt)
            output.append(sourceLines.at(cursor++));
        output.append(replacement);
        cursor += expected.size();
    }
    while (cursor < sourceLines.size())
        output.append(sourceLines.at(cursor++));
    if (deletes) {
        if (!QFile::remove(target)) {
            *error = QStringLiteral("无法删除补丁目标文件：%1").arg(relative);
            return false;
        }
        return true;
    }
    if (adds && output.isEmpty())
        hasTrailingNewline = false;
    if (!QDir().mkpath(QFileInfo(target).absolutePath())) {
        *error = QStringLiteral("无法创建补丁目标目录：%1").arg(relative);
        return false;
    }
    QSaveFile outputFile(target);
    if (!outputFile.open(QIODevice::WriteOnly)) {
        *error = outputFile.errorString();
        return false;
    }
    outputFile.setPermissions(adds
        ? QFile::ReadOwner | QFile::WriteOwner | QFile::ReadGroup | QFile::ReadOther
        : QFile::permissions(target));
    const QByteArray bytes = (output.join(QLatin1Char('\n'))
        + (hasTrailingNewline ? QStringLiteral("\n") : QString{})).toUtf8();
    if (outputFile.write(bytes) != bytes.size() || !outputFile.commit()) {
        *error = outputFile.errorString();
        return false;
    }
    return true;
}

bool applyUnifiedDiff(const QString &root, const QString &patch, const QStringList &declaredFiles,
                      QString *error) {
    const QStringList lines = patch.split(QLatin1Char('\n'));
    QList<DiffFile> files;
    for (qsizetype i = 0; i < lines.size(); ++i) {
        if (!lines.at(i).startsWith(QStringLiteral("--- ")))
            continue;
        if (i + 1 >= lines.size() || !lines.at(i + 1).startsWith(QStringLiteral("+++ "))) {
            *error = QStringLiteral("补丁缺少配对的文件头。 ");
            return false;
        }
        DiffFile file;
        file.oldPath = diffName(lines.at(i).mid(4));
        file.newPath = diffName(lines.at(++i).mid(4));
        if (file.oldPath != QStringLiteral("/dev/null") && file.newPath != QStringLiteral("/dev/null")
            && file.oldPath != file.newPath) {
            *error = QStringLiteral("补丁输入和输出路径不一致。 ");
            return false;
        }
        while (i + 1 < lines.size() && !lines.at(i + 1).startsWith(QStringLiteral("--- ")))
            file.hunks.append(lines.at(++i));
        files.append(file);
    }
    if (files.isEmpty()) {
        *error = QStringLiteral("补丁不包含 unified diff 文件头。 ");
        return false;
    }
    QSet<QString> changed;
    for (const DiffFile &file : files)
        changed.insert(file.oldPath == QStringLiteral("/dev/null") ? file.newPath : file.oldPath);
    QSet<QString> declared;
    for (const QString &path : declaredFiles)
        declared.insert(path);
    if (changed != declared) {
        *error = QStringLiteral("补丁文件与审核记录不一致。 ");
        return false;
    }
    for (const DiffFile &file : files) {
        if (!applyDiffFile(root, file, error))
            return false;
    }
    return true;
}

bool allowedPatchFile(const QString &path) {
    static const QSet<QString> suffixes = {
        QStringLiteral(".cfg"), QStringLiteral(".cmd"), QStringLiteral(".do"), QStringLiteral(".f"),
        QStringLiteral(".filelist"), QStringLiteral(".json"), QStringLiteral(".lst"), QStringLiteral(".sdc"),
        QStringLiteral(".sh"), QStringLiteral(".sv"), QStringLiteral(".svh"), QStringLiteral(".tcl"),
        QStringLiteral(".toml"), QStringLiteral(".v"), QStringLiteral(".vh"), QStringLiteral(".vhd"),
        QStringLiteral(".vhdl"), QStringLiteral(".vf"), QStringLiteral(".yaml"), QStringLiteral(".yml"),
        QStringLiteral(".py"), QStringLiteral(".qml"), QStringLiteral(".cpp"), QStringLiteral(".cc"),
        QStringLiteral(".c"), QStringLiteral(".h"), QStringLiteral(".hpp"), QStringLiteral(".js"),
        QStringLiteral(".ts"), QStringLiteral(".css"), QStringLiteral(".html"), QStringLiteral(".xml"),
        QStringLiteral(".ini"), QStringLiteral(".conf"), QStringLiteral(".md"), QStringLiteral(".txt")
    };
    return suffixes.contains(QFileInfo(path).suffix().prepend(QLatin1Char('.')).toLower());
}

QString unifiedPatch(const QString &relative, const QStringList &before, const QStringList &after) {
    qsizetype prefix = 0;
    while (prefix < before.size() && prefix < after.size()
           && before.at(prefix) == after.at(prefix))
        ++prefix;
    qsizetype suffix = 0;
    while (suffix < before.size() - prefix && suffix < after.size() - prefix
           && before.at(before.size() - suffix - 1) == after.at(after.size() - suffix - 1))
        ++suffix;
    if (prefix == before.size() && prefix == after.size())
        return {};

    const qsizetype contextBefore = qMin<qsizetype>(3, prefix);
    const qsizetype contextAfter = qMin<qsizetype>(3, suffix);
    const qsizetype oldStart = prefix - contextBefore + 1;
    const qsizetype newStart = oldStart;
    const qsizetype oldCount = contextBefore + before.size() - prefix - suffix + contextAfter;
    const qsizetype newCount = contextBefore + after.size() - prefix - suffix + contextAfter;
    const QString from = before.isEmpty() ? QStringLiteral("/dev/null") : QStringLiteral("a/") + relative;
    const QString to = after.isEmpty() ? QStringLiteral("/dev/null") : QStringLiteral("b/") + relative;
    QStringList lines{
        QStringLiteral("--- ") + from,
        QStringLiteral("+++ ") + to,
        QStringLiteral("@@ -%1,%2 +%3,%4 @@")
            .arg(oldStart).arg(oldCount).arg(newStart).arg(newCount),
    };
    for (qsizetype i = prefix - contextBefore; i < prefix; ++i)
        lines.append(QLatin1Char(' ') + before.at(i));
    for (qsizetype i = prefix; i < before.size() - suffix; ++i)
        lines.append(QLatin1Char('-') + before.at(i));
    for (qsizetype i = prefix; i < after.size() - suffix; ++i)
        lines.append(QLatin1Char('+') + after.at(i));
    for (qsizetype i = after.size() - suffix; i < after.size(); ++i)
        lines.append(QLatin1Char(' ') + after.at(i));
    return lines.join(QLatin1Char('\n')) + QLatin1Char('\n');
}

bool applyCodexUpdate(const QString &root, QStringList *source, const QStringList &body,
                      const QString &relative, QString *error) {
    QStringList normalizedBody = body;
    qsizetype firstContent = 0;
    while (firstContent < normalizedBody.size() && normalizedBody.at(firstContent).trimmed().isEmpty())
        ++firstContent;
    if (firstContent < normalizedBody.size()
        && normalizedBody.at(firstContent).startsWith(QStringLiteral("--- "))) {
        if (firstContent + 1 >= normalizedBody.size()
            || !normalizedBody.at(firstContent + 1).startsWith(QStringLiteral("+++ "))) {
            *error = QStringLiteral("Codex Update File 内嵌的 unified diff 缺少配对文件头：%1").arg(relative);
            return false;
        }
        const auto relativeHeaderPath = [&root](QString path) {
            path = unescapePatchPath(diffName(std::move(path)));
            if (path.isEmpty() || path == QStringLiteral("/dev/null"))
                return QString{};
            const QFileInfo candidate(QDir::isAbsolutePath(path) ? path : QDir(root).filePath(path));
            const QString canonical = candidate.exists() ? candidate.canonicalFilePath()
                : QDir::cleanPath(candidate.absoluteFilePath());
            if (canonical.isEmpty() || !pathWithin(root, canonical))
                return QString{};
            return QDir(root).relativeFilePath(canonical);
        };
        const QString oldPath = relativeHeaderPath(normalizedBody.at(firstContent).mid(4));
        const QString newPath = relativeHeaderPath(normalizedBody.at(firstContent + 1).mid(4));
        if (oldPath != relative || newPath != relative) {
            *error = QStringLiteral("Codex Update File 与内嵌 unified diff 路径不一致：%1").arg(relative);
            return false;
        }
        normalizedBody.remove(firstContent, 2);
    }
    QList<QStringList> hunks;
    QStringList current;
    bool inHunk = false;
    for (const QString &line : normalizedBody) {
        if (line.startsWith(QStringLiteral("@@"))) {
            if (inHunk)
                hunks.append(current);
            current.clear();
            inHunk = true;
        } else if (inHunk && !line.isEmpty()
                   && (line.front() == QLatin1Char(' ') || line.front() == QLatin1Char('+')
                       || line.front() == QLatin1Char('-'))) {
            current.append(line);
        } else if (!line.trimmed().isEmpty() && line.trimmed() != QStringLiteral("*** End of File ***")) {
            *error = QStringLiteral("补丁含无法识别内容：%1").arg(relative);
            return false;
        }
    }
    if (inHunk)
        hunks.append(current);
    if (hunks.isEmpty()) {
        *error = QStringLiteral("Update File 缺少 hunk：%1").arg(relative);
        return false;
    }

    qsizetype cursor = 0;
    for (const QStringList &hunk : std::as_const(hunks)) {
        QStringList expected;
        QStringList replacement;
        QList<QPair<qsizetype, qsizetype>> preservedContexts;
        for (const QString &line : hunk) {
            const bool context = line.front() == QLatin1Char(' ');
            if (context || line.front() == QLatin1Char('-'))
                expected.append(line.mid(1));
            if (context || line.front() == QLatin1Char('+')) {
                if (context)
                    preservedContexts.append({replacement.size(), expected.size() - 1});
                replacement.append(line.mid(1));
            }
        }
        qsizetype position = -1;
        bool indentationFallback = false;
        for (qsizetype start = cursor; start + expected.size() <= source->size(); ++start) {
            bool matches = true;
            for (qsizetype offset = 0; offset < expected.size(); ++offset) {
                if (source->at(start + offset) != expected.at(offset)) {
                    matches = false;
                    break;
                }
            }
            if (matches) {
                position = start;
                break;
            }
        }
        if (position < 0) {
            const auto decodeEscapedTabs = [](QString value) {
                value.replace(QStringLiteral("\\t"), QStringLiteral("\t"));
                return value;
            };
            const auto withoutIndentation = [](QString value) {
                qsizetype first = 0;
                while (first < value.size()
                       && (value.at(first) == QLatin1Char(' ') || value.at(first) == QLatin1Char('\t')))
                    ++first;
                value.remove(0, first);
                return value;
            };
            qsizetype candidate = -1;
            bool ambiguous = false;
            for (qsizetype start = cursor; start + expected.size() <= source->size(); ++start) {
                bool matches = true;
                for (qsizetype offset = 0; offset < expected.size(); ++offset) {
                    if (withoutIndentation(source->at(start + offset))
                        != withoutIndentation(decodeEscapedTabs(expected.at(offset)))) {
                        matches = false;
                        break;
                    }
                }
                if (!matches)
                    continue;
                if (candidate >= 0) {
                    ambiguous = true;
                    break;
                }
                candidate = start;
            }
            if (candidate >= 0 && !ambiguous)
                position = candidate;
            indentationFallback = position >= 0;
        }
        if (position < 0) {
            *error = QStringLiteral("补丁上下文不匹配：%1。请重读目标区间，并提交更小的补丁；如果只有缩进不同，工具会在唯一匹配时自动保留目标位置。").arg(relative);
            return false;
        }
        if (indentationFallback) {
            for (QString &line : replacement)
                line.replace(QStringLiteral("\\t"), QStringLiteral("\t"));
            for (const auto &[replacementIndex, sourceIndex] : std::as_const(preservedContexts))
                replacement[replacementIndex] = source->at(position + sourceIndex);
        }
        for (qsizetype i = 0; i < expected.size(); ++i)
            source->removeAt(position);
        for (qsizetype i = replacement.size() - 1; i >= 0; --i)
            source->insert(position, replacement.at(i));
        cursor = position + replacement.size();
    }
    return true;
}
}

QVariantMap PatchActionService::normalizePatch(const QString &projectRootText,
                                               const QStringList &requestedFiles,
                                               const QString &patch) {
    const QString root = normalizedProjectRoot(projectRootText);
    if (root.isEmpty() || !QFileInfo(root).isDir())
        return failure(QStringLiteral("项目根目录不可用。"));
    QString compatiblePatch = patch;
    static const QRegularExpression splitPatchBoundary(
        QStringLiteral(R"(^\*\*\*[ \t]+(?:Begin Patch[ \t]*/[ \t]*\*\*\*[ \t]*Update File:.*|End Patch[ \t]*/[ \t]*\*\*\*)[ \t]*$)"),
        QRegularExpression::MultilineOption);
    if (patch.contains(QStringLiteral("--- ")) && patch.contains(QStringLiteral("+++ "))
        && splitPatchBoundary.match(patch).hasMatch())
        compatiblePatch.remove(splitPatchBoundary);
    const QString stripped = compatiblePatch.trimmed();
    const bool wrapped = stripped.startsWith(QStringLiteral("*** Begin Patch"));
    static const QRegularExpression bareOperation(QStringLiteral("^\\*\\*\\*\\s+(?:Update|Add|Delete) File:"));
    if (patch.size() > 180 * 1024)
        return failure(QStringLiteral("补丁超过 180 KB 限制。"));

    const auto normalizePath = [root](const QString &raw, QString *relative) {
        QString input = unescapePatchPath(raw.trimmed());
        if (input.isEmpty())
            return false;
        if (input == QStringLiteral("/dev/null")) {
            *relative = input;
            return true;
        }
        const auto infoForPath = [root](const QString &value) {
            return QFileInfo(QDir::isAbsolutePath(value) ? value : QDir(root).filePath(value));
        };
        QFileInfo candidate = infoForPath(input);
        if (!candidate.exists()) {
            static const QRegularExpression extensionSpacing(
                QStringLiteral("\\.\\s+(cfg|cmd|do|f|filelist|json|lst|sdc|sh|svh?|tcl|toml|v|vhd|vhdl|vf|yaml|yml|py|qml|cpp|cc|c|h|hpp|js|ts|css|html|xml|ini|conf|md|txt)$"),
                QRegularExpression::CaseInsensitiveOption);
            QString repaired = input;
            repaired.replace(extensionSpacing, QStringLiteral(".\\1"));
            if (repaired != input) {
                const QFileInfo repairedCandidate = infoForPath(repaired);
                if (repairedCandidate.exists() && repairedCandidate.isFile()) {
                    input = repaired;
                    candidate = repairedCandidate;
                }
            }
        }
        const QString canonical = candidate.exists() ? candidate.canonicalFilePath()
                                                      : QDir::cleanPath(candidate.absoluteFilePath());
        if (canonical.isEmpty() || !pathWithin(root, canonical))
            return false;
        *relative = QDir(root).relativeFilePath(canonical);
        return *relative != QStringLiteral(".") && *relative != QStringLiteral("..")
            && !relative->startsWith(QStringLiteral("../"));
    };

    if (!wrapped && !bareOperation.match(stripped).hasMatch()) {
        if (requestedFiles.isEmpty() || requestedFiles.size() > 12)
            return failure(QStringLiteral("补丁文件数量必须在 1 到 12 之间。"));
        QStringList normalizedRequested;
        for (const QString &path : requestedFiles) {
            QString relative;
            if (!normalizePath(path, &relative) || relative == QStringLiteral("/dev/null"))
                return failure(QStringLiteral("声明的补丁路径无效或超出项目目录：%1").arg(path));
            if (!allowedPatchFile(relative))
                return failure(QStringLiteral("补丁文件类型不允许：%1").arg(relative));
            if (normalizedRequested.contains(relative))
                return failure(QStringLiteral("补丁文件列表存在重复项：%1").arg(relative));
            normalizedRequested.append(relative);
        }

        QStringList lines = compatiblePatch.split(QLatin1Char('\n'));
        QStringList output;
        QStringList changedFiles;
        for (qsizetype i = 0; i < lines.size(); ++i) {
            if (!lines.at(i).startsWith(QStringLiteral("--- "))) {
                output.append(lines.at(i));
                continue;
            }
            if (i + 1 >= lines.size() || !lines.at(i + 1).startsWith(QStringLiteral("+++ ")))
                return failure(QStringLiteral("补丁缺少配对的文件头。"));
            QString oldPath;
            QString newPath;
            if (!normalizePath(diffName(lines.at(i).mid(4)), &oldPath)
                || !normalizePath(diffName(lines.at(++i).mid(4)), &newPath))
                return failure(QStringLiteral("补丁文件头路径无效或超出项目目录。请使用项目内相对路径。"));
            if (oldPath != QStringLiteral("/dev/null") && newPath != QStringLiteral("/dev/null")
                && oldPath != newPath) {
                const bool oldDeclared = normalizedRequested.contains(oldPath);
                const bool newDeclared = normalizedRequested.contains(newPath);
                if (oldDeclared == newDeclared)
                    return failure(QStringLiteral("补丁输入和输出路径不一致：%1 -> %2。若其中一个路径正确，请让 files 与正确路径完全一致。")
                        .arg(oldPath, newPath));
                if (oldDeclared)
                    newPath = oldPath;
                else
                    oldPath = newPath;
            }
            const QString changed = oldPath == QStringLiteral("/dev/null") ? newPath : oldPath;
            if (changed == QStringLiteral("/dev/null") || !allowedPatchFile(changed))
                return failure(QStringLiteral("补丁文件类型不允许：%1").arg(changed));
            if (!changedFiles.contains(changed))
                changedFiles.append(changed);
            output.append(QStringLiteral("--- ") + (oldPath == QStringLiteral("/dev/null")
                ? oldPath : QStringLiteral("a/") + changed));
            output.append(QStringLiteral("+++ ") + (newPath == QStringLiteral("/dev/null")
                ? newPath : QStringLiteral("b/") + changed));
        }
        QStringList sortedDeclared = normalizedRequested;
        QStringList sortedChanged = changedFiles;
        sortedDeclared.sort();
        sortedChanged.sort();
        if (sortedChanged.isEmpty())
            return failure(QStringLiteral("补丁不包含 unified diff 文件头。"));
        if (sortedDeclared != sortedChanged) {
            return failure(QStringLiteral("补丁文件头与 files 清单不一致。files: %1；补丁头: %2。请让两者使用同一项目相对路径。")
                .arg(normalizedRequested.join(QStringLiteral(", ")),
                     changedFiles.join(QStringLiteral(", "))));
        }
        return {{QStringLiteral("ok"), true}, {QStringLiteral("patch"), output.join(QLatin1Char('\n'))},
                {QStringLiteral("files"), normalizedRequested}};
    }

    QStringList lines = stripped.split(QLatin1Char('\n'));
    if (wrapped && (lines.size() < 2 || lines.first().trimmed() != QStringLiteral("*** Begin Patch")
                    || lines.last().trimmed() != QStringLiteral("*** End Patch")))
        return failure(QStringLiteral("补丁必须包含 Begin Patch 和 End Patch。"));
    const qsizetype begin = wrapped ? 1 : 0;
    const qsizetype end = wrapped ? lines.size() - 1 : lines.size();
    struct Operation { QString kind; QString path; QStringList body; };
    QList<Operation> operations;
    static const QRegularExpression operationHeader(
        QStringLiteral("^\\*\\*\\*\\s+(Update|Add|Delete) File:\\s*(.+?)\\s*$"));
    for (qsizetype i = begin; i < end; ++i) {
        const QString line = lines.at(i);
        const auto match = operationHeader.match(line);
        if (match.hasMatch()) {
            QString relative;
            if (!normalizePath(match.captured(2), &relative))
                return failure(QStringLiteral("补丁路径超出项目目录或无效：%1").arg(match.captured(2)));
            QString target;
            if (!safeRelativePath(root, relative, &target))
                return failure(QStringLiteral("补丁路径无效：%1").arg(relative));
            if (relative == QStringLiteral("."))
                return failure(QStringLiteral("补丁文件路径无效。"));
            if (!allowedPatchFile(relative))
                return failure(QStringLiteral("补丁文件类型不允许：%1").arg(relative));
            operations.append({match.captured(1).toLower(), relative, {}});
            continue;
        }
        if (operations.isEmpty()) {
            if (!line.trimmed().isEmpty())
                return failure(QStringLiteral("补丁缺少文件操作头。"));
            continue;
        }
        if (operations.last().body.isEmpty() && !line.startsWith(QStringLiteral("@@"))
            && (line == QStringLiteral("---") || line.startsWith(QStringLiteral("--- "))
                || line == QStringLiteral("+++") || line.startsWith(QStringLiteral("+++ "))))
            continue;
        operations.last().body.append(line);
    }
    if (operations.isEmpty())
        return failure(QStringLiteral("补丁不包含文件操作。"));

    QStringList normalizedRequested;
    if (requestedFiles.isEmpty() || requestedFiles.size() > 12)
        return failure(QStringLiteral("补丁文件数量必须在 1 到 12 之间。"));
    for (QString path : requestedFiles) {
        QString relative;
        if (!normalizePath(path, &relative))
            return failure(QStringLiteral("声明的补丁路径无效：%1").arg(path));
        if (normalizedRequested.contains(relative))
            return failure(QStringLiteral("补丁文件列表存在重复项。"));
        if (!allowedPatchFile(relative))
            return failure(QStringLiteral("补丁文件类型不允许：%1").arg(relative));
        normalizedRequested.append(relative);
    }
    QStringList operationFiles;
    for (const Operation &operation : std::as_const(operations)) {
        if (!operationFiles.contains(operation.path))
            operationFiles.append(operation.path);
    }
    QStringList sortedDeclared = normalizedRequested;
    QStringList sortedOperations = operationFiles;
    sortedDeclared.sort();
    sortedOperations.sort();
    if (sortedDeclared != sortedOperations)
        return failure(QStringLiteral("补丁路径与 files 声明不一致。请将 files 精确设为补丁头中的路径；files=[%1]，patch=[%2]。")
            .arg(sortedDeclared.join(QStringLiteral(", ")), sortedOperations.join(QStringLiteral(", "))));

    QString normalizedPatch;
    for (const Operation &operation : std::as_const(operations)) {
        QString target;
        if (!safeRelativePath(root, operation.path, &target))
            return failure(QStringLiteral("补丁路径无效：%1").arg(operation.path));
        const bool exists = QFileInfo::exists(target);
        if (operation.kind == QStringLiteral("add") && exists)
            return failure(QStringLiteral("补丁目标文件已存在：%1；请使用 Update File。 ").arg(operation.path));
        if (operation.kind != QStringLiteral("add") && (!exists || !QFileInfo(target).isFile()))
            return failure(QStringLiteral("补丁目标文件不存在：%1").arg(operation.path));
        QStringList before;
        bool trailingNewline = true;
        if (exists) {
            QFile source(target);
            if (!source.open(QIODevice::ReadOnly))
                return failure(QStringLiteral("补丁源文件无法读取：%1").arg(operation.path));
            const QByteArray bytes = source.readAll();
            if (bytes.contains('\0'))
                return failure(QStringLiteral("不支持编辑二进制文件：%1").arg(operation.path));
            trailingNewline = bytes.endsWith('\n');
            before = QString::fromUtf8(bytes).split(QLatin1Char('\n'));
            if (trailingNewline)
                before.removeLast();
        }
        QStringList after;
        if (operation.kind == QStringLiteral("add")) {
            for (const QString &line : operation.body) {
                if (line.startsWith(QLatin1Char('+')))
                    after.append(line.mid(1));
                else if (!line.trimmed().isEmpty())
                    return failure(QStringLiteral("Add File 只接受以 + 开头的内容。"));
            }
        } else if (operation.kind == QStringLiteral("delete")) {
            after.clear();
        } else {
            after = before;
            QString error;
            if (!applyCodexUpdate(root, &after, operation.body, operation.path, &error))
                return failure(error);
        }
        if (operation.kind == QStringLiteral("update") && before == after)
            continue;
        if (operation.kind == QStringLiteral("add") && after.isEmpty())
        return failure(QStringLiteral("Add File 未提供文件内容：%1").arg(operation.path));
        if (operation.kind == QStringLiteral("delete") && before.isEmpty())
            return failure(QStringLiteral("补丁目标文件为空：%1").arg(operation.path));
        normalizedPatch += unifiedPatch(operation.path, before, after);
    }
    if (normalizedPatch.isEmpty())
        return failure(QStringLiteral("补丁不会改变任何文件。"));
    if (normalizedPatch.toUtf8().size() > 180 * 1024)
        return failure(QStringLiteral("转换后的补丁超过 180 KB 限制。"));
    return {{QStringLiteral("ok"), true}, {QStringLiteral("patch"), normalizedPatch},
            {QStringLiteral("files"), operationFiles}};
}

QVariantMap PatchActionService::createFilePatch(const QString &projectRootText,
                                                const QString &pathText,
                                                const QString &content) {
    const QString root = normalizedProjectRoot(projectRootText);
    if (root.isEmpty() || !QFileInfo(root).isDir())
        return failure(QStringLiteral("项目根目录不可用。"));
    if (pathText.trimmed().isEmpty() || pathText.size() > 512 || pathText.contains(QChar::Null))
        return failure(QStringLiteral("文件路径无效。"));
    if (content.contains(QChar::Null) || content.toUtf8().size() > 180 * 1024)
        return failure(QStringLiteral("文本内容包含 NUL 或超过 180 KiB 上限。"));

    const QFileInfo requested(pathText.trimmed());
    if (requested.isSymLink())
        return failure(QStringLiteral("不允许通过符号链接编辑文件：%1").arg(pathText));
    QString relative;
    if (requested.isAbsolute()) {
        const QString absolute = requested.exists()
            ? requested.canonicalFilePath() : QDir::cleanPath(requested.absoluteFilePath());
        if (absolute.isEmpty() || !pathWithin(root, absolute))
            return failure(QStringLiteral("文件路径超出项目目录。"));
        relative = QDir(root).relativeFilePath(absolute);
    } else {
        relative = QDir::cleanPath(pathText.trimmed());
    }
    QString target;
    if (!allowedPatchFile(relative) || !safeRelativePath(root, relative, &target))
        return failure(QStringLiteral("补丁路径无效或文件类型不允许：%1").arg(pathText));

    const QFileInfo targetInfo(target);
    if (targetInfo.isSymLink() || (targetInfo.exists() && !targetInfo.isFile()))
        return failure(QStringLiteral("目标路径已存在但不是普通文件：%1").arg(relative));

    const bool exists = targetInfo.isFile();
    QByteArray currentBytes;
    if (exists) {
        QFile currentFile(target);
        if (!currentFile.open(QIODevice::ReadOnly))
            return failure(QStringLiteral("目标文件无法读取：%1").arg(relative));
        currentBytes = currentFile.readAll();
        if (currentFile.error() != QFileDevice::NoError)
            return failure(currentFile.errorString());
        if (currentBytes.contains('\0'))
            return failure(QStringLiteral("不支持覆盖二进制文件：%1").arg(relative));
    }
    const QString current = QString::fromUtf8(currentBytes);
    if (exists && current == content)
        return {{QStringLiteral("ok"), true}, {QStringLiteral("already_exists"), true},
                {QStringLiteral("files"), QStringList{relative}}};

    const bool oldTrailingNewline = exists && current.endsWith(QLatin1Char('\n'));
    const bool newTrailingNewline = content.endsWith(QLatin1Char('\n'));
    if (exists && oldTrailingNewline != newTrailingNewline)
        return {{QStringLiteral("ok"), true}, {QStringLiteral("native_supported"), false}};
    if (content.isEmpty() || (!exists && !newTrailingNewline))
        return {{QStringLiteral("ok"), true}, {QStringLiteral("native_supported"), false}};

    auto linesWithoutTrailingNewline = [](QString value) {
        if (value.isEmpty())
            return QStringList{};
        if (value.endsWith(QLatin1Char('\n')))
            value.chop(1);
        return value.split(QLatin1Char('\n'));
    };
    const QString patch = unifiedPatch(relative,
        linesWithoutTrailingNewline(current), linesWithoutTrailingNewline(content));
    if (patch.isEmpty() || patch.toUtf8().size() > 180 * 1024)
        return failure(patch.isEmpty()
            ? QStringLiteral("文件内容没有可应用的文本差异。")
            : QStringLiteral("生成的补丁超过 180 KiB 上限。"));

    return {{QStringLiteral("ok"), true}, {QStringLiteral("native_supported"), true},
            {QStringLiteral("files"), QStringList{relative}}, {QStringLiteral("patch"), patch},
            {QStringLiteral("created"), !exists}, {QStringLiteral("updated"), exists}};
}

QVariantMap PatchActionService::createProposal(const QString &projectId, const QString &projectRootText,
                                               const QStringList &requestedFiles, const QString &purpose,
                                               const QString &patch, const QString &agentRoot) {
    const QString idProject = projectId.trimmed();
    const QString projectRoot = normalizedProjectRoot(projectRootText);
    const QByteArray patchBytes = patch.toUtf8();
    if (idProject.isEmpty() || projectRoot.isEmpty() || !QFileInfo(projectRoot).isDir())
        return failure(QStringLiteral("补丁候选缺少有效项目标识或目录。"));
    if (requestedFiles.isEmpty() || requestedFiles.size() > 12)
        return failure(QStringLiteral("补丁文件数量必须在 1 到 12 之间。"));
    if (patchBytes.size() > 180 * 1024 || patch.contains(QChar::Null)
        || patch.contains(QStringLiteral("GIT binary patch")))
        return failure(QStringLiteral("补丁超过 180 KiB 上限，或包含不支持的二进制内容。"));

    QStringList files;
    QSet<QString> unique;
    for (const QString &relativeValue : requestedFiles) {
        const QString relative = QDir::cleanPath(relativeValue.trimmed());
        QString target;
        if (!allowedPatchFile(relative) || !safeRelativePath(projectRoot, relative, &target))
            return failure(QStringLiteral("补丁路径无效：%1").arg(relativeValue));
        const QFileInfo info(target);
        if (info.isSymLink() || (info.exists() && !info.isFile()))
            return failure(QStringLiteral("补丁目标不是可用的常规文件：%1").arg(relative));
        if (unique.contains(relative))
            return failure(QStringLiteral("补丁文件列表存在重复项。"));
        unique.insert(relative);
        files.append(relative);
    }

    QTemporaryDir staging;
    if (!staging.isValid())
        return failure(QStringLiteral("无法创建临时补丁校验目录。"));
    QString error;
    for (const QString &relative : files) {
        QString source;
        if (!safeRelativePath(projectRoot, relative, &source))
            return failure(QStringLiteral("补丁路径无效：%1").arg(relative));
        if (QFileInfo(source).isFile()
            && !copyFile(source, QDir(staging.path()).filePath(relative), &error))
            return failure(error);
    }
    if (!applyUnifiedDiff(staging.path(), patch, files, &error))
        return failure(error);

    QString storedPatch = patch;
    while (storedPatch.endsWith(QLatin1Char('\n')))
        storedPatch.chop(1);
    storedPatch.append(QLatin1Char('\n'));
    const QByteArray storedBytes = storedPatch.toUtf8();
    const QString root = runtimePatchRoot(agentRoot);
    if (!QDir().mkpath(root))
        return failure(QStringLiteral("无法创建补丁候选存储目录。"));
    const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces).remove(QLatin1Char('-')).toLower();
    const QString diffPath = QDir(root).filePath(id + QStringLiteral(".diff"));
    const QString recordPath = QDir(root).filePath(id + QStringLiteral(".json"));
    QJsonArray fileArray;
    QJsonObject baseline;
    int added = 0;
    int removed = 0;
    const QStringList patchLines = storedPatch.split(QLatin1Char('\n'));
    for (const QString &line : patchLines) {
        if (line.startsWith(QStringLiteral("+++") ) || line.startsWith(QStringLiteral("---")))
            continue;
        if (line.startsWith(QLatin1Char('+'))) ++added;
        else if (line.startsWith(QLatin1Char('-'))) ++removed;
    }
    for (const QString &relative : files) {
        fileArray.append(relative);
        QString target;
        safeRelativePath(projectRoot, relative, &target);
        bool hashOk = true;
        const QString hash = QFileInfo(target).isFile() ? digestFile(target, &hashOk) : QString{};
        if (!hashOk)
            return failure(QStringLiteral("无法读取补丁基线文件：%1").arg(relative));
        baseline.insert(relative, hash);
    }
    const QString patchHash = QString::fromLatin1(QCryptographicHash::hash(
        storedBytes, QCryptographicHash::Sha256).toHex());
    const QJsonObject record{
        {QStringLiteral("id"), id},
        {QStringLiteral("project_id"), idProject},
        {QStringLiteral("project_root"), projectRoot},
        {QStringLiteral("files"), fileArray},
        {QStringLiteral("purpose"), purpose.trimmed()},
        {QStringLiteral("patch_sha256"), patchHash},
        {QStringLiteral("created_at"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate)},
        {QStringLiteral("base_sha256"), baseline},
        {QStringLiteral("status"), QStringLiteral("awaiting_approval")},
    };
    if (!saveBytes(diffPath, storedBytes, &error))
        return failure(error);
    if (!saveRecord(recordPath, record, &error)) {
        QFile::remove(diffPath);
        return failure(error);
    }
    return {{QStringLiteral("ok"), true}, {QStringLiteral("result"), QVariantMap{
        {QStringLiteral("proposed"), true},
        {QStringLiteral("proposal_id"), id},
        {QStringLiteral("status"), QStringLiteral("awaiting_approval")},
        {QStringLiteral("files"), files},
        {QStringLiteral("purpose"), purpose.trimmed()},
        {QStringLiteral("patch_sha256"), patchHash},
        {QStringLiteral("line_stats"), QVariantMap{
            {QStringLiteral("added"), added}, {QStringLiteral("removed"), removed}}},
        {QStringLiteral("patch_file"), diffPath},
        {QStringLiteral("record_file"), recordPath},
        {QStringLiteral("source_project_unchanged"), true},
    }}};
}

QVariantMap PatchActionService::applyAutonomous(const QString &projectId, const QString &projectRootText,
                                                const QStringList &requestedFiles, const QString &purpose,
                                                const QString &patch, const QString &agentRoot) {
    const QString idProject = projectId.trimmed();
    const QString projectRoot = normalizedProjectRoot(projectRootText);
    if (idProject.isEmpty() || projectRoot.isEmpty() || !QFileInfo(projectRoot).isDir())
        return failure(QStringLiteral("自主编辑缺少有效项目标识或目录。"));
    if (patch.toUtf8().size() > 180 * 1024 || patch.contains(QChar::Null)
        || patch.contains(QStringLiteral("GIT binary patch")))
        return failure(QStringLiteral("补丁超过 180 KiB 上限，或包含不支持的二进制内容。"));

    const QVariantMap normalized = normalizePatch(projectRoot, requestedFiles, patch);
    if (!normalized.value(QStringLiteral("ok")).toBool())
        return failure(normalized.value(QStringLiteral("message")).toString());
    const QString normalizedPatchText = normalized.value(QStringLiteral("patch")).toString();

    QStringList files;
    QSet<QString> unique;
    for (const QString &relativeValue : normalized.value(QStringLiteral("files")).toStringList()) {
        const QString relative = QDir::cleanPath(relativeValue.trimmed());
        QString target;
        if (!allowedPatchFile(relative) || !safeRelativePath(projectRoot, relative, &target))
            return failure(QStringLiteral("补丁路径无效：%1").arg(relativeValue));
        const QFileInfo info(target);
        if (info.isSymLink() || (info.exists() && !info.isFile()))
            return failure(QStringLiteral("补丁目标不是可用的常规文件：%1").arg(relative));
        if (unique.contains(relative))
            return failure(QStringLiteral("补丁文件列表存在重复项。"));
        unique.insert(relative);
        files.append(relative);
    }

    const QString patchRoot = runtimePatchRoot(agentRoot);
    if (!QDir().mkpath(patchRoot))
        return failure(QStringLiteral("无法创建自主编辑记录目录。"));
    const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces).remove(QLatin1Char('-')).toLower();
    const QString backupRoot = QDir(patchRoot).filePath(id + QStringLiteral(".backup"));
    const QString stageRoot = QDir(patchRoot).filePath(id + QStringLiteral(".stage"));
    if (!QDir().mkpath(backupRoot) || !QDir().mkpath(stageRoot))
        return failure(QStringLiteral("无法创建自主编辑快照目录。"));

    QString error;
    QJsonObject before;
    bool setupFailed = false;
    for (const QString &relative : files) {
        QString source;
        if (!safeRelativePath(projectRoot, relative, &source)) {
            error = QStringLiteral("补丁路径无效：%1").arg(relative);
            setupFailed = true;
            break;
        }
        const QFileInfo sourceInfo(source);
        if (sourceInfo.isFile()) {
            bool hashOk = false;
            const QString sourceHash = digestFile(source, &hashOk);
            if (!hashOk) {
                error = QStringLiteral("无法读取自主编辑基线：%1").arg(relative);
                setupFailed = true;
                break;
            }
            before.insert(relative, sourceHash);
            if (!copyFile(source, QDir(backupRoot).filePath(relative), &error)
                || !copyFile(source, QDir(stageRoot).filePath(relative), &error)) {
                setupFailed = true;
                break;
            }
        } else {
            before.insert(relative, QString{});
        }
    }
    if (setupFailed || !applyUnifiedDiff(stageRoot, normalizedPatchText, files, &error)) {
        QDir(stageRoot).removeRecursively();
        QDir(backupRoot).removeRecursively();
        return failure(error.isEmpty() ? QStringLiteral("补丁无法应用到临时副本。") : error);
    }

    QJsonObject after;
    for (const QString &relative : files) {
        const QString staged = QDir(stageRoot).filePath(relative);
        bool hashOk = true;
        const QString value = QFileInfo(staged).isFile() ? digestFile(staged, &hashOk) : QString{};
        if (!hashOk) {
            QDir(stageRoot).removeRecursively();
            QDir(backupRoot).removeRecursively();
            return failure(QStringLiteral("无法读取已校验的补丁结果：%1").arg(relative));
        }
        after.insert(relative, value);
    }

    for (const QString &relative : files) {
        QString source;
        safeRelativePath(projectRoot, relative, &source);
        bool hashOk = true;
        const QString actual = QFileInfo(source).isFile() ? digestFile(source, &hashOk) : QString{};
        if (!hashOk || actual != before.value(relative).toString()) {
            QDir(stageRoot).removeRecursively();
            QDir(backupRoot).removeRecursively();
            return failure(QStringLiteral("源文件在自主修改期间发生变化，已放弃写入：%1").arg(relative));
        }
    }

    QStringList applied;
    bool writeFailed = false;
    for (const QString &relative : files) {
        QString target;
        if (!safeRelativePath(projectRoot, relative, &target)) {
            error = QStringLiteral("补丁目标路径在写入前发生变化：%1").arg(relative);
            writeFailed = true;
            break;
        }
        const QString staged = QDir(stageRoot).filePath(relative);
        if (QFileInfo(staged).isFile()) {
            if (!copyFile(staged, target, &error)) {
                writeFailed = true;
                break;
            }
        } else if (QFileInfo::exists(target) && !QFile::remove(target)) {
            error = QStringLiteral("无法删除补丁目标文件：%1").arg(relative);
            writeFailed = true;
            break;
        }
        applied.append(relative);
    }

    auto restore = [&]() {
        bool restored = true;
        for (const QString &relative : std::as_const(applied)) {
            QString target;
            if (!safeRelativePath(projectRoot, relative, &target)) {
                restored = false;
                continue;
            }
            const QString backup = QDir(backupRoot).filePath(relative);
            if (QFileInfo(backup).isFile()) {
                QString restoreError;
                if (!copyFile(backup, target, &restoreError)) {
                    error = restoreError;
                    restored = false;
                }
            } else if (QFileInfo::exists(target) && !QFile::remove(target)) {
                error = QStringLiteral("无法恢复新增文件：%1").arg(relative);
                restored = false;
            }
        }
        return restored;
    };
    if (writeFailed) {
        const bool restored = restore();
        QDir(stageRoot).removeRecursively();
        if (restored)
            QDir(backupRoot).removeRecursively();
        return failure(restored ? error : error + QStringLiteral("；部分文件未能自动恢复，快照保留于 %1").arg(backupRoot));
    }

    QJsonObject afterActual;
    for (const QString &relative : files) {
        QString target;
        safeRelativePath(projectRoot, relative, &target);
        bool hashOk = true;
        const QString value = QFileInfo(target).isFile() ? digestFile(target, &hashOk) : QString{};
        if (!hashOk || value != after.value(relative).toString()) {
            error = QStringLiteral("自主编辑后的文件摘要不匹配：%1").arg(relative);
            const bool restored = restore();
            QDir(stageRoot).removeRecursively();
            if (restored)
                QDir(backupRoot).removeRecursively();
            return failure(restored ? error : error + QStringLiteral("；部分文件未能自动恢复，快照保留于 %1").arg(backupRoot));
        }
        afterActual.insert(relative, value);
    }

    QString storedPatch = patch;
    while (storedPatch.endsWith(QLatin1Char('\n')))
        storedPatch.chop(1);
    storedPatch.append(QLatin1Char('\n'));
    const QByteArray storedBytes = storedPatch.toUtf8();
    const QString diffPath = QDir(patchRoot).filePath(id + QStringLiteral(".diff"));
    const QString recordPath = QDir(patchRoot).filePath(id + QStringLiteral(".json"));
    int added = 0;
    int removed = 0;
    for (const QString &line : storedPatch.split(QLatin1Char('\n'))) {
        if (line.startsWith(QStringLiteral("+++") ) || line.startsWith(QStringLiteral("---")))
            continue;
        if (line.startsWith(QLatin1Char('+'))) ++added;
        else if (line.startsWith(QLatin1Char('-'))) ++removed;
    }
    const QString patchHash = QString::fromLatin1(QCryptographicHash::hash(
        storedBytes, QCryptographicHash::Sha256).toHex());
    const QJsonObject record{
        {QStringLiteral("id"), id},
        {QStringLiteral("project_id"), idProject},
        {QStringLiteral("project_root"), projectRoot},
        {QStringLiteral("files"), QJsonArray::fromStringList(files)},
        {QStringLiteral("purpose"), purpose.trimmed()},
        {QStringLiteral("patch_sha256"), patchHash},
        {QStringLiteral("created_at"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate)},
        {QStringLiteral("status"), QStringLiteral("applied")},
        {QStringLiteral("mode"), QStringLiteral("autonomous")},
        {QStringLiteral("backup_root"), backupRoot},
        {QStringLiteral("base_sha256"), before},
        {QStringLiteral("after_sha256"), afterActual},
        {QStringLiteral("line_stats"), QJsonObject{
            {QStringLiteral("added"), added}, {QStringLiteral("removed"), removed}}},
    };
    if (!saveBytes(diffPath, storedBytes, &error) || !saveRecord(recordPath, record, &error)) {
        QFile::remove(diffPath);
        const bool restored = restore();
        QDir(stageRoot).removeRecursively();
        if (restored)
            QDir(backupRoot).removeRecursively();
        return failure(restored ? error : error + QStringLiteral("；源文件未能自动恢复，快照保留于 %1").arg(backupRoot));
    }
    QDir(stageRoot).removeRecursively();
    return {{QStringLiteral("ok"), true}, {QStringLiteral("result"), QVariantMap{
        {QStringLiteral("edited"), true},
        {QStringLiteral("edit_id"), id},
        {QStringLiteral("status"), QStringLiteral("applied")},
        {QStringLiteral("mode"), QStringLiteral("autonomous")},
        {QStringLiteral("files"), files},
        {QStringLiteral("purpose"), purpose.trimmed()},
        {QStringLiteral("patch_file"), diffPath},
        {QStringLiteral("record_file"), recordPath},
        {QStringLiteral("line_stats"), QVariantMap{
            {QStringLiteral("added"), added}, {QStringLiteral("removed"), removed}}},
        {QStringLiteral("rollback_available"), true},
    }}};
}

QVariantMap PatchActionService::decide(const QString &proposalId, bool approved, const QString &agentRoot,
                                      const QString &isolatedPatchesRoot) {
    const QString id = proposalId.trimmed().toLower();
    if (!validId(id))
        return failure(QStringLiteral("补丁候选标识无效。"));
    const QString root = runtimePatchRoot(agentRoot);
    const QString recordPath = QDir(root).filePath(id + QStringLiteral(".json"));
    const QString diffPath = QDir(root).filePath(id + QStringLiteral(".diff"));
    if (!QFileInfo(recordPath).isFile() || !QFileInfo(diffPath).isFile())
        return failure(QStringLiteral("补丁候选不存在。"));
    QJsonObject record;
    const QVariantMap readError = readRecord(recordPath, &record);
    if (!readError.isEmpty())
        return readError;
    if (record.value(QStringLiteral("id")).toString() != id
        || record.value(QStringLiteral("status")).toString() != QStringLiteral("awaiting_approval"))
        return failure(QStringLiteral("补丁候选已经处理或记录无效。"));
    if (!approved) {
        record.insert(QStringLiteral("status"), QStringLiteral("rejected"));
        record.insert(QStringLiteral("decided_at"), nowUtc());
        QString error;
        if (!saveRecord(recordPath, record, &error))
            return failure(error);
        return {{QStringLiteral("ok"), true}, {QStringLiteral("result"), QVariantMap{
            {QStringLiteral("proposal_id"), id}, {QStringLiteral("status"), QStringLiteral("rejected")},
            {QStringLiteral("source_project_unchanged"), true}}}};
    }

    const QString projectRoot = normalizedProjectRoot(record.value(QStringLiteral("project_root")).toString());
    const QString projectId = record.value(QStringLiteral("project_id")).toString();
    const QJsonArray fileArray = record.value(QStringLiteral("files")).toArray();
    const QJsonObject baseHashes = record.value(QStringLiteral("base_sha256")).toObject();
    if (projectRoot.isEmpty() || projectId.isEmpty() || fileArray.isEmpty()
        || fileArray.size() > 12 || baseHashes.size() != fileArray.size())
        return failure(QStringLiteral("补丁候选缺少可用的源工程快照。"));
    QStringList files;
    for (const QJsonValue &value : fileArray) {
        const QString relative = value.toString();
        QString path;
        if (!allowedPatchFile(relative) || !safeRelativePath(projectRoot, relative, &path))
            return failure(QStringLiteral("补丁路径无效：%1").arg(relative));
        const bool exists = QFileInfo(path).isFile();
        const QString actual = exists ? digestFile(path) : QString{};
        if (actual != baseHashes.value(relative).toString())
            return failure(QStringLiteral("源文件已变化，必须重新生成补丁候选：%1").arg(relative));
        files.append(relative);
    }
    QSet<QString> uniqueFiles;
    for (const QString &file : files)
        uniqueFiles.insert(file);
    if (uniqueFiles.size() != files.size())
        return failure(QStringLiteral("补丁候选包含重复路径。"));
    QFile patchFile(diffPath);
    if (!patchFile.open(QIODevice::ReadOnly))
        return failure(QStringLiteral("补丁候选无法读取。"));
    const QByteArray patchBytes = patchFile.readAll();
    if (patchBytes.size() > 180 * 1024)
        return failure(QStringLiteral("补丁超过 180 KiB 上限。"));
    if (QString::fromLatin1(QCryptographicHash::hash(patchBytes, QCryptographicHash::Sha256).toHex())
        != record.value(QStringLiteral("patch_sha256")).toString())
        return failure(QStringLiteral("补丁内容摘要不一致。"));

    const QString isolatedBase = isolatedPatchesRoot.isEmpty()
        ? QStringLiteral("/media/6/Projects/DFT_agent_terminal_workspaces/approved_patches")
        : isolatedPatchesRoot;
    const QString isolated = QDir(isolatedBase).filePath(id);
    if (QFileInfo::exists(isolated))
        return failure(QStringLiteral("补丁隔离目录已存在，拒绝覆盖。"));
    const QString sourceRoot = QDir(isolated).filePath(QStringLiteral("source"));
    QString error;
    for (const QString &relative : files) {
        QString source;
        if (!safeRelativePath(projectRoot, relative, &source)) {
            QDir(isolated).removeRecursively();
            return failure(QStringLiteral("补丁路径无效：%1").arg(relative));
        }
        if (QFileInfo(source).isFile()
            && !copyFile(source, QDir(sourceRoot).filePath(relative), &error)) {
            QDir(isolated).removeRecursively();
            return failure(error);
        }
    }
    if (!applyUnifiedDiff(sourceRoot, QString::fromUtf8(patchBytes), files, &error)) {
        QDir(isolated).removeRecursively();
        return failure(error);
    }
    QJsonArray isolatedFiles;
    for (const QString &relative : files) {
        const QString path = QDir(sourceRoot).filePath(relative);
        bool ok = false;
        const QString hash = QFileInfo(path).isFile() ? digestFile(path, &ok) : QString{};
        isolatedFiles.append(QJsonObject{{QStringLiteral("source"), path},
            {QStringLiteral("project_file"), relative}, {QStringLiteral("sha256"), hash},
            {QStringLiteral("exists"), QFileInfo(path).isFile()}});
    }
    record.insert(QStringLiteral("status"), QStringLiteral("approved"));
    record.insert(QStringLiteral("decided_at"), nowUtc());
    record.insert(QStringLiteral("isolated_root"), isolated);
    record.insert(QStringLiteral("isolated_files"), isolatedFiles);
    if (!saveRecord(recordPath, record, &error)) {
        QDir(isolated).removeRecursively();
        return failure(error);
    }
    return {{QStringLiteral("ok"), true}, {QStringLiteral("result"), QVariantMap{
        {QStringLiteral("proposal_id"), id}, {QStringLiteral("status"), QStringLiteral("approved")},
        {QStringLiteral("isolated_root"), isolated}, {QStringLiteral("isolated_files"), isolatedFiles.toVariantList()},
        {QStringLiteral("source_project_unchanged"), true}}}};
}

QVariantMap PatchActionService::rollback(const QString &editId, const QString &projectId,
                                         const QString &projectRootText, const QString &agentRoot) {
    const QString id = editId.trimmed().toLower();
    if (!validId(id) || projectId.trimmed().isEmpty())
        return failure(QStringLiteral("编辑记录标识或项目标识无效。"));
    const QString patchRoot = runtimePatchRoot(agentRoot);
    const QString recordPath = QDir(patchRoot).filePath(id + QStringLiteral(".json"));
    QJsonObject record;
    const QVariantMap readError = readRecord(recordPath, &record);
    if (!readError.isEmpty())
        return readError;
    const QString projectRoot = normalizedProjectRoot(projectRootText);
    if (record.value(QStringLiteral("status")).toString() != QStringLiteral("applied"))
        return failure(QStringLiteral("编辑记录当前不可回滚。"));
    if (record.value(QStringLiteral("project_id")).toString() != projectId.trimmed()
        || normalizedProjectRoot(record.value(QStringLiteral("project_root")).toString()) != projectRoot
        || projectRoot.isEmpty())
        return failure(QStringLiteral("编辑记录不属于当前项目。"));
    const QString backupRoot = QFileInfo(record.value(QStringLiteral("backup_root")).toString()).canonicalFilePath();
    QStringList files;
    for (const QJsonValue &value : record.value(QStringLiteral("files")).toArray())
        files.append(value.toString());
    const QJsonObject after = record.value(QStringLiteral("after_sha256")).toObject();
    const QJsonObject before = record.value(QStringLiteral("base_sha256")).toObject();
    if (backupRoot.isEmpty() || files.isEmpty() || after.isEmpty()
        || !pathWithin(patchRoot, backupRoot))
        return failure(QStringLiteral("编辑记录缺少有效的回滚快照。"));
    for (const QString &relative : files) {
        QString target;
        if (!safeRelativePath(projectRoot, relative, &target))
            return failure(QStringLiteral("回滚路径无效：%1").arg(relative));
        const QString current = QFileInfo(target).isFile() ? digestFile(target) : QString{};
        if (current != after.value(relative).toString())
            return failure(QStringLiteral("源文件在编辑后已被其他操作修改，拒绝覆盖回滚。"));
    }
    for (const QString &relative : files) {
        QString target;
        safeRelativePath(projectRoot, relative, &target);
        const QString backup = QDir(backupRoot).filePath(relative);
        if (QFileInfo(backup).isFile() && !QFileInfo(backup).isSymLink()) {
            QString error;
            if (!copyFile(backup, target, &error))
                return failure(error);
        } else if (before.value(relative).toString().isEmpty()) {
            if (QFileInfo::exists(target) && !QFile::remove(target))
                return failure(QStringLiteral("无法移除新建文件以完成回滚：%1").arg(relative));
        } else {
            return failure(QStringLiteral("回滚快照不完整。"));
        }
    }
    record.insert(QStringLiteral("status"), QStringLiteral("rolled_back"));
    record.insert(QStringLiteral("rolled_back_at"), nowUtc());
    QString error;
    if (!saveRecord(recordPath, record, &error))
        return failure(error);
    return {{QStringLiteral("ok"), true}, {QStringLiteral("result"), QVariantMap{
        {QStringLiteral("edited"), false}, {QStringLiteral("edit_id"), id},
        {QStringLiteral("status"), QStringLiteral("rolled_back")}, {QStringLiteral("files"), files}}}};
}
