#pragma once

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>
#include <QVariant>
#include <stdexcept>

// Keep the environment name/default aligned with the storage migration layer.
// The GUI exports its resolved absolute root to any launched worker process.
inline QString studioAgentRoot() {
    const auto app = QCoreApplication::instance();
    const QString value = app ? app->property("dftAgentRoot").toString() : QString{};
    return value.isEmpty() ? QDir::currentPath() : value;
}

inline QString studioAbsolutePath(QString value, const QString &agentRoot) {
    if (value == QStringLiteral("~")) value = QDir::homePath();
    else if (value.startsWith(QStringLiteral("~/"))) value = QDir::home().filePath(value.mid(2));
    return QDir::cleanPath(QFileInfo(value).isAbsolute() ? value : QDir(agentRoot).absoluteFilePath(value));
}

inline QString studioDataRoot(const QString &agentRoot = studioAgentRoot()) {
#ifdef DFT_AGENT_STUDIO_TESTING
    const QString absolute = QDir(agentRoot).absoluteFilePath(QStringLiteral("studio_data"));
#else
    Q_UNUSED(agentRoot);
    const QString absolute = QDir::home().filePath(QStringLiteral(".dft_agent_studio"));
#endif
    const QString canonical = QFileInfo(absolute).canonicalFilePath();
    return canonical.isEmpty() ? QDir::cleanPath(absolute) : canonical;
}

// Prefer the executable's checkout over the desktop launcher's arbitrary CWD.
// Explicit --agent-root remains authoritative.
inline QString studioFindAgentRoot() {
    const QString applicationRoot = QCoreApplication::applicationDirPath();
    QDir directory(applicationRoot);
    for (int index = 0; index < 8; ++index) {
        if (QFileInfo(directory.filePath("CMakeLists.txt")).isFile()
            && QFileInfo(directory.filePath("src/main.cpp")).isFile())
            return directory.absolutePath();
        if (!directory.cdUp()) break;
    }
    QDir standaloneRoot(applicationRoot);
    if (standaloneRoot.dirName() == QStringLiteral("build") || standaloneRoot.dirName().startsWith(QStringLiteral("build-")))
        standaloneRoot.cdUp();
    return standaloneRoot.absolutePath();
}

inline QString resolveStudioRecordPath(const QString &value, const QString &agentRoot = studioAgentRoot()) {
    QString raw = QDir::fromNativeSeparators(value);
    if (raw.startsWith(QStringLiteral("~/"))) raw = QDir::home().filePath(raw.mid(2));
    if (raw.split(QLatin1Char('/')).contains(QStringLiteral(".."))) return value;
    const QString targetRoot = studioDataRoot(agentRoot);
    QStringList roots{QDir::cleanPath(QFileInfo(agentRoot).absoluteFilePath())};
    QFile aliases(QDir(targetRoot).filePath(QStringLiteral("_migration/path_aliases.json")));
    if (aliases.open(QIODevice::ReadOnly)) {
        for (const auto item : QJsonDocument::fromJson(aliases.readAll()).object().value("legacy_roots").toArray()) {
            if (item.isString()) roots.append(QDir::fromNativeSeparators(item.toString()));
        }
    }
    QStringList candidates{raw.startsWith(QStringLiteral("./")) ? raw.mid(2) : raw};
    for (QString prefix : roots) {
        while (prefix.endsWith(QLatin1Char('/'))) prefix.chop(1);
        if (raw.startsWith(prefix + QLatin1Char('/'))) candidates.append(raw.mid(prefix.size() + 1));
    }
    const QStringList stores{
        "agent_runtime", "memory", "runs", "episodes", "iterations", "logs",
        "gui", "gui_runs", "runtime", "review.jsonl", "audit.jsonl"
    };
    for (const QString &candidate : candidates) {
        QString relative;
        for (const QString &store : stores) {
            const QString old = QStringLiteral("artifacts/") + store;
            if (candidate == old || candidate.startsWith(old + QLatin1Char('/'))) {
                relative = store + candidate.mid(old.size());
                break;
            }
        }
        for (const QString &name : {QStringLiteral("projects.json"), QStringLiteral("models.json"), QStringLiteral("gui_capabilities.json")}) {
            if (candidate == QStringLiteral("data/") + name) relative = QStringLiteral("config/") + name;
        }
        if (candidate == QStringLiteral("data/feedback.jsonl")) relative = QStringLiteral("feedback.jsonl");
        if (candidate.startsWith(QStringLiteral("artifacts/gui-")) && candidate.endsWith(QStringLiteral(".png"))
            && !candidate.mid(10).contains(QLatin1Char('/')))
            relative = QStringLiteral("gui/screenshots/") + candidate.mid(10);
        if (!relative.isEmpty()) {
            const QString out = QDir(targetRoot).filePath(relative);
            const QString canonicalRoot = QFileInfo(targetRoot).canonicalFilePath();
            const QString canonical = QFileInfo(out).canonicalFilePath();
            if (!canonicalRoot.isEmpty() && !canonical.isEmpty()
                && canonical.startsWith(canonicalRoot + QDir::separator())) return out;
        }
    }
    return value;
}


inline QString studioConfigPath(const QString &name, const QString &agentRoot = studioAgentRoot()) {
    const QStringList names{QStringLiteral("projects.json"), QStringLiteral("models.json"),
                            QStringLiteral("gui_capabilities.json")};
    if (!names.contains(name))
        throw std::runtime_error("Unknown Studio configuration filename.");
    return QDir(studioDataRoot(agentRoot)).filePath(QStringLiteral("config/") + name);
}

inline QString studioUiSettingsPath(const QString &agentRoot = studioAgentRoot()) {
    return QDir(studioDataRoot(agentRoot)).filePath(QStringLiteral("config/ui_settings.ini"));
}

inline int studioValidateCatalogue(const QString &path, const QString &key, QString *error) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        *error = QStringLiteral("无法读取 %1：%2").arg(key, path);
        return -1;
    }
    if (file.size() > 32 * 1024 * 1024) {
        *error = QStringLiteral("配置文件超过安全读取上限：%1").arg(path);
        return -1;
    }
    QJsonParseError jsonError;
    const auto document = QJsonDocument::fromJson(file.readAll(), &jsonError);
    if (jsonError.error != QJsonParseError::NoError || !document.isObject() || !document.object().value(key).isArray()) {
        *error = QStringLiteral("配置文件格式无效或缺少 %1 数组：%2").arg(key, path);
        return -1;
    }
    const auto entries = document.object().value(key).toArray();
    if (key == QStringLiteral("projects")) {
        QStringList identifiers;
        for (const auto &entry : entries) {
            const auto row = entry.toObject();
            const QString id = row.value("id").toString().trimmed();
            if (!entry.isObject() || id.isEmpty() || identifiers.contains(id)
                || row.value("root").toString().trimmed().isEmpty()) {
                *error = QStringLiteral("项目配置存在无效记录、重复 ID 或缺少 root：%1").arg(path);
                return -1;
            }
            identifiers.append(id);
        }
    } else if (key == QStringLiteral("models")) {
        // Keep startup compatible with ModelCatalog::load(): legacy catalogues
        // may contain duplicate IDs and were historically loaded verbatim.
        // Uniqueness is still enforced by ModelCatalog::validate() for future
        // add/edit operations; startup recovery must not discard old settings.
        for (const auto &entry : entries) {
            if (!entry.isObject() || entry.toObject().value("id").toString().trimmed().isEmpty()) {
                *error = QStringLiteral("模型配置存在无效记录或空 ID：%1").arg(path);
                return -1;
            }
        }
    }
    return entries.size();
}


inline bool studioHasSessionRecords(const QString &dataRoot) {
    const QString directory = QDir(dataRoot).filePath("agent_runtime/threads");
    QDirIterator iterator(directory, {QStringLiteral("*.thread.bin"), QStringLiteral("session.json")},
                          QDir::Files, QDirIterator::Subdirectories);
    return iterator.hasNext();
}
