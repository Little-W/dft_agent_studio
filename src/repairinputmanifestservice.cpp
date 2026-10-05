#include "repairinputmanifestservice.h"

#include "repairactionfingerprintservice.h"

#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QMutex>
#include <QMutexLocker>
#include <QHash>
#include <QSet>
#include <QVariantList>

#include <algorithm>

#ifdef Q_OS_LINUX
#include <sys/stat.h>
#endif

namespace {
const QSet<QString> kSourceSuffixes{
    QStringLiteral(".v"), QStringLiteral(".sv"), QStringLiteral(".vh"), QStringLiteral(".svh"),
    QStringLiteral(".vhd"), QStringLiteral(".vhdl"), QStringLiteral(".f"), QStringLiteral(".sdc"),
    QStringLiteral(".tcl"), QStringLiteral(".xdc"), QStringLiteral(".lib"), QStringLiteral(".db"),
    QStringLiteral(".lef"), QStringLiteral(".json"), QStringLiteral(".yaml"), QStringLiteral(".yml"),
    QStringLiteral(".cfg"), QStringLiteral(".sh"), QStringLiteral(".py"), QStringLiteral(".toml"),
    QStringLiteral(".filelist"), QStringLiteral(".lst"), QStringLiteral(".vf")};
const QSet<QString> kSkippedDirectories{
    QStringLiteral(".git"), QStringLiteral("artifacts"), QStringLiteral("studio_data"),
    QStringLiteral("__pycache__"), QStringLiteral(".venv"), QStringLiteral("venv"),
    QStringLiteral("node_modules"), QStringLiteral(".pytest_cache"), QStringLiteral("build"),
    QStringLiteral("dist")};
const QSet<QString> kIgnoredConfigKeys{
    QStringLiteral("name"), QStringLiteral("description"), QStringLiteral("displayName"),
    QStringLiteral("goal"), QStringLiteral("notes"), QStringLiteral("updated_at"),
    QStringLiteral("created_at"), QStringLiteral("status"), QStringLiteral("progress"),
    QStringLiteral("approvedAccessPaths"), QStringLiteral("multiAgentEnabled"),
    QStringLiteral("disabledCapabilities")};
const QStringList kPathHints{
    QStringLiteral("path"), QStringLiteral("file"), QStringLiteral("dir"), QStringLiteral("root"),
    QStringLiteral("workspace"), QStringLiteral("script"), QStringLiteral("document"),
    QStringLiteral("dofile"), QStringLiteral("library"), QStringLiteral("report"),
    QStringLiteral("include")};
struct CachedFileHash {
    qint64 size = -1;
    qint64 modifiedStamp = -1;
    qint64 changedStamp = -1;
    qint64 inode = -1;
    QString digest;
};
QMutex &fileHashMutex() {
    static QMutex mutex;
    return mutex;
}
QHash<QString, CachedFileHash> &fileHashCache() {
    static QHash<QString, CachedFileHash> cache;
    return cache;
}

QVariant effectiveConfig(const QVariant &value) {
    if (value.metaType().id() == QMetaType::QVariantMap) {
        QVariantMap result;
        const QVariantMap source = value.toMap();
        for (auto it = source.cbegin(); it != source.cend(); ++it) {
            const QString key = it.key();
            const QString folded = key.toCaseFolded();
            if (key.startsWith(QLatin1Char('_')) || folded.startsWith(QStringLiteral("model"))
                || folded.startsWith(QStringLiteral("agent")) || folded.startsWith(QStringLiteral("ui"))
                || kIgnoredConfigKeys.contains(key))
                continue;
            result.insert(key, effectiveConfig(it.value()));
        }
        return result;
    }
    if (value.metaType().id() == QMetaType::QVariantList) {
        QVariantList result;
        for (const QVariant &item : value.toList())
            result.append(effectiveConfig(item));
        return result;
    }
    return value;
}

bool sourceFile(const QFileInfo &info) {
    return info.isFile() && !info.isSymLink()
        && (kSourceSuffixes.contains(QLatin1Char('.') + info.suffix().toLower())
            || info.fileName().compare(QStringLiteral("filelist.txt"), Qt::CaseInsensitive) == 0
            || info.fileName().compare(QStringLiteral("makefile"), Qt::CaseInsensitive) == 0
            || info.fileName().compare(QStringLiteral("bender.lock"), Qt::CaseInsensitive) == 0);
}

QString normalizedPath(QString path, const QString &root) {
    path = path.trimmed();
    if (path == QStringLiteral("~"))
        path = QDir::homePath();
    else if (path.startsWith(QStringLiteral("~/")))
        path = QDir::home().filePath(path.mid(2));
    if (!QFileInfo(path).isAbsolute())
        path = QDir(root).filePath(path);
    const QString canonical = QFileInfo(path).canonicalFilePath();
    return QDir::cleanPath(canonical.isEmpty() ? QFileInfo(path).absoluteFilePath() : canonical);
}

void collectExplicitPaths(const QVariant &value, const QString &key, const QString &root,
                          QSet<QString> *paths) {
    if (value.metaType().id() == QMetaType::QVariantMap) {
        const QVariantMap map = value.toMap();
        for (auto it = map.cbegin(); it != map.cend(); ++it)
            collectExplicitPaths(it.value(), it.key(), root, paths);
        return;
    }
    if (value.metaType().id() == QMetaType::QVariantList) {
        for (const QVariant &item : value.toList())
            collectExplicitPaths(item, key, root, paths);
        return;
    }
    const QString foldedKey = key.toCaseFolded().replace(QLatin1Char('-'), QLatin1Char('_'));
    if (!std::any_of(kPathHints.cbegin(), kPathHints.cend(), [&foldedKey](const QString &hint) {
            return foldedKey.contains(hint);
        }))
        return;
    const QString text = value.toString().trimmed();
    if (text.isEmpty() || text.contains(QLatin1Char('\n')))
        return;
    const QString path = normalizedPath(text, root);
    if (sourceFile(QFileInfo(path)) || foldedKey.contains(QStringLiteral("filelist")))
        paths->insert(path);
}

QString sha256File(const QString &path, QString *error) {
    const QFileInfo info(path);
    CachedFileHash signature;
    signature.size = info.size();
#ifdef Q_OS_LINUX
    struct stat fileStatus {};
    const QByteArray encodedPath = QFile::encodeName(path);
    if (::stat(encodedPath.constData(), &fileStatus) == 0) {
        signature.modifiedStamp = static_cast<qint64>(fileStatus.st_mtim.tv_sec) * 1'000'000'000LL
            + fileStatus.st_mtim.tv_nsec;
        signature.changedStamp = static_cast<qint64>(fileStatus.st_ctim.tv_sec) * 1'000'000'000LL
            + fileStatus.st_ctim.tv_nsec;
        signature.inode = static_cast<qint64>(fileStatus.st_ino);
    }
#else
    signature.modifiedStamp = info.lastModified().toMSecsSinceEpoch();
    signature.changedStamp = info.metadataChangeTime().toMSecsSinceEpoch();
#endif
    {
        QMutexLocker locker(&fileHashMutex());
        const auto cached = fileHashCache().constFind(path);
        if (cached != fileHashCache().cend() && cached->size == signature.size
            && cached->modifiedStamp == signature.modifiedStamp
            && cached->changedStamp == signature.changedStamp
            && cached->inode == signature.inode)
            return cached->digest;
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error)
            *error = file.errorString();
        return {};
    }
    QCryptographicHash hash(QCryptographicHash::Sha256);
    while (!file.atEnd()) {
        const QByteArray block = file.read(1024 * 1024);
        if (block.isEmpty() && file.error() != QFileDevice::NoError) {
            if (error)
                *error = file.errorString();
            return {};
        }
        hash.addData(block);
    }
    const QString digest = QString::fromLatin1(hash.result().toHex());
    {
        QMutexLocker locker(&fileHashMutex());
        signature.digest = digest;
        fileHashCache().insert(path, signature);
        while (fileHashCache().size() > 100'000)
            fileHashCache().erase(fileHashCache().begin());
    }
    return digest;
}

void scanProject(const QString &directory, int depth, QSet<QString> *paths, QStringList *missing) {
    if (depth > 128)
        return;
    QDir dir(directory);
    const QFileInfoList entries = dir.entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden,
                                                     QDir::Name);
    for (const QFileInfo &entry : entries) {
        if (entry.isSymLink())
            continue;
        if (entry.isDir()) {
            const QString name = entry.fileName();
            if (kSkippedDirectories.contains(name) || name.startsWith(QStringLiteral(".dft-")))
                continue;
            scanProject(entry.absoluteFilePath(), depth + 1, paths, missing);
            if (paths->size() > 20'000) {
                missing->append(QStringLiteral("input_manifest_limit_exceeded: declare a narrower project root"));
                return;
            }
        } else if (sourceFile(entry)) {
            paths->insert(entry.canonicalFilePath());
            if (paths->size() > 20'000) {
                missing->append(QStringLiteral("input_manifest_limit_exceeded: declare a narrower project root"));
                return;
            }
        }
    }
}

QStringList filelistCandidates(const QString &raw, const QString &root, const QString &parent) {
    QString value = raw.trimmed();
    if (value.size() >= 2 && value.startsWith(QLatin1Char('"')) && value.endsWith(QLatin1Char('"')))
        value = value.mid(1, value.size() - 2);
    const QFileInfo requested(value);
    if (requested.isAbsolute())
        return {QDir::cleanPath(value)};
    return {QDir(root).filePath(value), QDir(parent).filePath(value)};
}

void expandFilelist(const QString &path, const QString &root, QSet<QString> *paths,
                    QStringList *missing, QSet<QString> *visited) {
    const QString canonical = QFileInfo(path).canonicalFilePath();
    if (canonical.isEmpty() || visited->contains(canonical))
        return;
    visited->insert(canonical);
    if (visited->size() > 256) {
        missing->append(QStringLiteral("filelist_recursion_limit_exceeded:") + canonical);
        return;
    }
    QFile file(canonical);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        missing->append(canonical);
        return;
    }
    const QStringList lines = QString::fromUtf8(file.readAll()).split(QLatin1Char('\n'));
    for (QString line : lines) {
        line = line.section(QStringLiteral("//"), 0, 0).section(QLatin1Char('#'), 0, 0).trimmed();
        if (line.isEmpty())
            continue;
        if (line.startsWith(QStringLiteral("+incdir+"))) {
            const QStringList folders = line.mid(8).split(QLatin1Char('+'), Qt::SkipEmptyParts);
            for (const QString &folder : folders) {
                QStringList candidates = filelistCandidates(folder, root, QFileInfo(canonical).absolutePath());
                const auto directory = std::find_if(candidates.cbegin(), candidates.cend(), [](const QString &candidate) {
                    return QFileInfo(candidate).isDir() && !QFileInfo(candidate).isSymLink();
                });
                if (directory == candidates.cend()) {
                    missing->append(QStringLiteral("include-directory:") + folder);
                    continue;
                }
                QDirIterator iterator(*directory, {QStringLiteral("*.vh"), QStringLiteral("*.svh")},
                                      QDir::Files | QDir::NoSymLinks, QDirIterator::Subdirectories);
                while (iterator.hasNext()) {
                    const QString header = QFileInfo(iterator.next()).canonicalFilePath();
                    if (!header.isEmpty())
                        paths->insert(header);
                }
            }
            continue;
        }
        bool nested = false;
        QString includePath;
        if (line.startsWith(QStringLiteral("-f ")) || line.startsWith(QStringLiteral("-F "))) {
            nested = true;
            includePath = line.mid(3).trimmed();
        } else if (!line.startsWith(QLatin1Char('+')) && !line.startsWith(QLatin1Char('-'))) {
            includePath = line;
        } else {
            continue;
        }
        const QStringList candidates = filelistCandidates(includePath, root, QFileInfo(canonical).absolutePath());
        const auto candidate = std::find_if(candidates.cbegin(), candidates.cend(), [](const QString &value) {
            return QFileInfo(value).isFile() && !QFileInfo(value).isSymLink();
        });
        if (candidate == candidates.cend()) {
            missing->append(QStringLiteral("filelist:") + canonical + QLatin1Char(':') + includePath);
            continue;
        }
        const QString included = QFileInfo(*candidate).canonicalFilePath();
        if (included.isEmpty())
            continue;
        paths->insert(included);
        if (nested)
            expandFilelist(included, root, paths, missing, visited);
    }
}

QVariantMap effectiveOptions(const QVariantMap &arguments) {
    QVariantMap options;
    static const QSet<QString> ignored{
        QStringLiteral("force_rerun"), QStringLiteral("verification_reason")};
    for (auto it = arguments.cbegin(); it != arguments.cend(); ++it) {
        if (!ignored.contains(it.key()) && it.value().isValid() && !it.value().isNull())
            options.insert(it.key(), it.value());
    }
    return options;
}
} // namespace

QVariantMap RepairInputManifestService::snapshot(const QVariantMap &project,
                                                const QVariantMap &toolArguments,
                                                const QString &agentRoot) {
    QString root = project.value(QStringLiteral("root")).toString().trimmed();
    if (root.isEmpty())
        root = agentRoot;
    root = normalizedPath(root, agentRoot);
    QVariantMap effective = effectiveConfig(project).toMap();
    QSet<QString> paths;
    QStringList missing;
    const QFileInfo rootInfo(root);
    if (!rootInfo.isDir() || rootInfo.isSymLink()) {
        missing.append(root);
    } else {
        scanProject(root, 0, &paths, &missing);
    }
    collectExplicitPaths(effective, {}, root, &paths);
    for (const QVariant &value : project.value(QStringLiteral("repairInputPaths")).toList()) {
        const QString path = normalizedPath(value.toString(), root);
        const QFileInfo info(path);
        if (info.isFile() && !info.isSymLink())
            paths.insert(path);
        else
            missing.append(path);
    }

    QSet<QString> visitedFilelists;
    const QStringList initialPaths = paths.values();
    for (const QString &path : initialPaths) {
        const QFileInfo info(path);
        if (info.suffix().compare(QStringLiteral("f"), Qt::CaseInsensitive) == 0
            || info.fileName().contains(QStringLiteral("filelist"), Qt::CaseInsensitive))
            expandFilelist(path, root, &paths, &missing, &visitedFilelists);
    }

    QStringList sortedPaths = paths.values();
    std::sort(sortedPaths.begin(), sortedPaths.end());
    QVariantList manifest;
    manifest.reserve(sortedPaths.size());
    for (const QString &path : sortedPaths) {
        QString error;
        const QString digest = sha256File(path, &error);
        if (digest.isEmpty()) {
            missing.append(path + QStringLiteral(": ") + error);
            continue;
        }
        manifest.append(QVariant::fromValue(QVariantList{path, digest}));
    }

    const QVariantMap options = effectiveOptions(toolArguments);
    const QString base = RepairActionFingerprintService::digest(QVariantMap{
        {QStringLiteral("inputs"), manifest},
        {QStringLiteral("config"), effective}});
    const QString key = RepairActionFingerprintService::digest(QVariantMap{
        {QStringLiteral("base"), base}, {QStringLiteral("options"), options}});
    return {{QStringLiteral("base"), base}, {QStringLiteral("key"), key},
            {QStringLiteral("project_root"), root},
            {QStringLiteral("options"), options}, {QStringLiteral("manifest"), manifest},
            {QStringLiteral("complete"), missing.isEmpty()}, {QStringLiteral("missing"), missing}};
}
