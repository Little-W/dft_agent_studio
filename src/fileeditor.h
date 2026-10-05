#pragma once

#include <QObject>
#include <QNetworkAccessManager>
#include <QStringList>
#include <QVariantList>

class QAbstractItemModel;
class QNetworkReply;
class FileTreeModel;

class FileEditor final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString filePath READ filePath NOTIFY filePathChanged)
    Q_PROPERTY(QString language READ language NOTIFY filePathChanged)
    Q_PROPERTY(QString content READ content NOTIFY contentChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY loadStateChanged)
    Q_PROPERTY(bool limitedPreview READ limitedPreview NOTIFY loadStateChanged)
    Q_PROPERTY(QString loadNotice READ loadNotice NOTIFY loadStateChanged)
    Q_PROPERTY(int loadedLineCount READ loadedLineCount NOTIFY loadStateChanged)
    Q_PROPERTY(int maximumLoadedLines READ maximumLoadedLines WRITE setMaximumLoadedLines NOTIFY maximumLoadedLinesChanged)
    Q_PROPERTY(QStringList editableFiles READ editableFiles NOTIFY editableFilesChanged)
    Q_PROPERTY(QStringList relatedFiles READ relatedFiles NOTIFY relatedFilesChanged)
    Q_PROPERTY(QAbstractItemModel *treeModel READ treeModel CONSTANT)
    Q_PROPERTY(int treeRootCount READ treeRootCount NOTIFY treeRootsChanged)
    Q_PROPERTY(bool dirty READ dirty NOTIFY dirtyChanged)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)
    Q_PROPERTY(bool agentCompletionRunning READ agentCompletionRunning NOTIFY agentCompletionRunningChanged)
    Q_PROPERTY(QString agentCompletion READ agentCompletion NOTIFY agentCompletionChanged)
    Q_PROPERTY(int agentCompletionCursor READ agentCompletionCursor NOTIFY agentCompletionChanged)
    Q_PROPERTY(QString agentCompletionError READ agentCompletionError NOTIFY agentCompletionErrorChanged)

public:
    explicit FileEditor(QObject *parent = nullptr);

    QString filePath() const;
    QString language() const;
    QString content() const;
    bool loading() const;
    bool limitedPreview() const;
    QString loadNotice() const;
    int loadedLineCount() const;
    int maximumLoadedLines() const;
    void setMaximumLoadedLines(int value);
    QStringList editableFiles() const;
    QStringList relatedFiles() const;
    QAbstractItemModel *treeModel() const;
    int treeRootCount() const;
    bool dirty() const;
    QString error() const;
    bool agentCompletionRunning() const;
    QString agentCompletion() const;
    int agentCompletionCursor() const;
    QString agentCompletionError() const;

    Q_INVOKABLE void setProjectRoots(const QString &projectRoot, const QString &rtlRoot);
    Q_INVOKABLE void setProjectLocations(const QVariantList &locations);
    Q_INVOKABLE void refreshFiles();
    Q_INVOKABLE bool selectFile(const QString &path);
    Q_INVOKABLE bool openFile(const QString &path);
    Q_INVOKABLE QString fileName(const QString &path) const;
    Q_INVOKABLE QString relativePath(const QString &path) const;
    Q_INVOKABLE QString languageForFile(const QString &path) const;
    Q_INVOKABLE bool isEditableFile(const QString &path) const;
    Q_INVOKABLE void setContent(const QString &content);
    Q_INVOKABLE bool save();
    Q_INVOKABLE QString suggest(const QString &text, int cursorPosition) const;
    Q_INVOKABLE QVariantList completionItems(const QString &text, int cursorPosition) const;
    Q_INVOKABLE void requestAgentCompletion(
        const QString &modelId,
        const QString &apiBase,
        int contextWindow,
        const QString &text,
        int cursorPosition,
        bool explicitRequest
    );
    Q_INVOKABLE void cancelAgentCompletion();

signals:
    void filePathChanged();
    void contentChanged();
    void loadStateChanged();
    void maximumLoadedLinesChanged();
    void editableFilesChanged();
    void relatedFilesChanged();
    void treeRootsChanged();
    void dirtyChanged();
    void errorChanged();
    void agentCompletionRunningChanged();
    void agentCompletionChanged();
    void agentCompletionErrorChanged();

private:
    bool isAllowedFile(const QString &path) const;
    bool isAllowedSuffix(const QString &path) const;
    static QString languageForPath(const QString &path);
    void setError(const QString &message);
    void setAgentCompletion(const QString &completion);
    void setAgentCompletionError(const QString &message);
    bool canRequestAgentCompletion(const QString &text, int cursorPosition) const;
    void refreshFileIndex();
    void updateRelatedFiles();
    void applyLoadedFile(
        quint64 requestId,
        const QString &canonicalPath,
        const QString &content,
        bool limitedPreview,
        int loadedLineCount,
        const QString &notice,
        const QString &error
    );

    QStringList m_roots;
    QStringList m_indexRoots;
    QString m_rootSignature;
    QStringList m_editableFiles;
    QStringList m_relatedFiles;
    QString m_filePath;
    QString m_content;
    QString m_loadNotice;
    QString m_error;
    bool m_loading = false;
    bool m_limitedPreview = false;
    int m_loadedLineCount = 0;
    int m_maximumLoadedLines = 10'000;
    quint64 m_loadRequestId = 0;
    bool m_dirty = false;
    QNetworkAccessManager m_network;
    QNetworkReply *m_agentCompletionReply = nullptr;
    bool m_agentCompletionRunning = false;
    QString m_agentCompletion;
    int m_agentCompletionCursor = -1;
    QString m_agentCompletionError;
    quint64 m_agentCompletionRequestId = 0;
    FileTreeModel *m_treeModel = nullptr;
};
