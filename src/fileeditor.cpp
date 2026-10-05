#include "fileeditor.h"
#include "studiopaths.h"

#include <QAbstractItemModel>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QUrl>
#include <QVariantMap>
#include <QtConcurrent/QtConcurrentRun>

#include <memory>
#include <vector>

namespace {
constexpr qsizetype MaxIndexedFiles = 12'000;
constexpr qint64 AsyncLoadThresholdBytes = 262'144;
constexpr qint64 MaxLoadedPreviewBytes = 2 * 1'048'576;
constexpr qint64 MaxLogicalLineBytes = 65'536;

bool isHiddenInfrastructureDirectory(const QString &name) {
    static const QSet<QString> names{
        QStringLiteral(".git"),
        QStringLiteral(".hg"),
        QStringLiteral(".svn"),
        QStringLiteral("__pycache__"),
        QStringLiteral("studio_data"),
    };
    return names.contains(name);
}

bool isEditableTextPath(const QString &path) {
    static const QSet<QString> suffixes{
        QStringLiteral("cfg"), QStringLiteral("def"), QStringLiteral("do"),
        QStringLiteral("f"), QStringLiteral("inc"), QStringLiteral("json"),
        QStringLiteral("lef"), QStringLiteral("lib"), QStringLiteral("list"),
        QStringLiteral("log"), QStringLiteral("md"), QStringLiteral("rpt"),
        QStringLiteral("sdc"), QStringLiteral("setup"), QStringLiteral("sv"),
        QStringLiteral("tcl"), QStringLiteral("tf"), QStringLiteral("txt"),
        QStringLiteral("v"), QStringLiteral("vh"), QStringLiteral("yaml"),
        QStringLiteral("yml"),
    };
    const QFileInfo info(path);
    const QString suffix = info.suffix().toLower();
    const QString name = info.fileName().toLower();
    return suffixes.contains(suffix)
        || name == QStringLiteral("makefile")
        || name == QStringLiteral("cmakelists.txt");
}

QString normalizedLocationPath(const QString &path) {
    const QFileInfo info(resolveStudioRecordPath(path.trimmed()));
    const QString canonical = info.canonicalFilePath();
    return QDir::cleanPath(canonical.isEmpty() ? info.absoluteFilePath() : canonical);
}

struct ExplorerRoot {
    QString label;
    QString path;
    QString kind;
    bool searchable = true;
};

struct CompletionEntry {
    QString label;
    QString insertion;
    QString detail;
    QString kind;
};

struct FileLoadResult {
    QString content;
    QString notice;
    QString error;
    int loadedLineCount = 0;
    bool limitedPreview = false;
};

FileLoadResult readTextFile(const QString &path, int maximumLines) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {.error = QObject::tr("无法读取文件：%1").arg(path)};

    QByteArray content;
    content.reserve(static_cast<qsizetype>(qMin(file.size(), MaxLoadedPreviewBytes)));
    int loadedLines = 0;
    QString limitReason;
    while (!file.atEnd() && loadedLines < maximumLines && content.size() < MaxLoadedPreviewBytes) {
        QByteArray line = file.readLine(MaxLogicalLineBytes + 1);
        if (line.isEmpty() && !file.atEnd()) {
            return {.error = QObject::tr("读取文件时发生错误：%1").arg(path)};
        }
        const bool completeLine = line.endsWith('\n') || file.atEnd();
        const qint64 remainingBytes = MaxLoadedPreviewBytes - content.size();
        if (line.size() > remainingBytes) {
            line.truncate(static_cast<qsizetype>(remainingBytes));
            content.append(line);
            limitReason = QObject::tr("文件预览达到 2 MiB 上限");
            break;
        }
        content.append(line);
        ++loadedLines;
        if (!completeLine) {
            limitReason = QObject::tr("文件包含超过 65,536 字节的单行");
            break;
        }
    }

    const bool limited = !file.atEnd();
    if (limited && limitReason.isEmpty()) {
        limitReason = content.size() >= MaxLoadedPreviewBytes
            ? QObject::tr("文件预览达到 2 MiB 上限")
            : QObject::tr("文件超过 %1 行").arg(maximumLines);
    }
    QString notice;
    if (limited) {
        notice = QObject::tr("%1；当前仅载入前 %2 行并以只读方式显示。")
            .arg(limitReason)
            .arg(loadedLines);
    }
    return {
        .content = QString::fromUtf8(content),
        .notice = notice,
        .loadedLineCount = loadedLines,
        .limitedPreview = limited,
    };
}

using CompletionEntries = QVector<CompletionEntry>;

CompletionEntries svCompletionEntries() {
    return {
        {QStringLiteral("always_ff"), QStringLiteral("always_ff @(posedge clk) begin\n    \nend"), QStringLiteral("sequential block"), QStringLiteral("keyword")},
        {QStringLiteral("always_comb"), QStringLiteral("always_comb begin\n    \nend"), QStringLiteral("combinational block"), QStringLiteral("keyword")},
        {QStringLiteral("assign"), QStringLiteral("assign signal = value;"), QStringLiteral("continuous assignment"), QStringLiteral("keyword")},
        {QStringLiteral("case"), QStringLiteral("case (selector)\n    default: begin\n    end\nendcase"), QStringLiteral("case statement"), QStringLiteral("keyword")},
        {QStringLiteral("endmodule"), QStringLiteral("endmodule"), QStringLiteral("module terminator"), QStringLiteral("keyword")},
        {QStringLiteral("endinterface"), QStringLiteral("endinterface"), QStringLiteral("interface terminator"), QStringLiteral("keyword")},
        {QStringLiteral("endpackage"), QStringLiteral("endpackage"), QStringLiteral("package terminator"), QStringLiteral("keyword")},
        {QStringLiteral("for"), QStringLiteral("for (int index = 0; index < count; index++) begin\n    \nend"), QStringLiteral("for loop"), QStringLiteral("keyword")},
        {QStringLiteral("function"), QStringLiteral("function automatic logic name;\n    \nendfunction"), QStringLiteral("function declaration"), QStringLiteral("keyword")},
        {QStringLiteral("generate"), QStringLiteral("generate\n    \nendgenerate"), QStringLiteral("generate block"), QStringLiteral("keyword")},
        {QStringLiteral("input"), QStringLiteral("input logic signal"), QStringLiteral("input declaration"), QStringLiteral("keyword")},
        {QStringLiteral("logic"), QStringLiteral("logic signal"), QStringLiteral("four-state variable"), QStringLiteral("keyword")},
        {QStringLiteral("module"), QStringLiteral("module module_name (\n    input logic clk,\n    input logic rst_n\n);\n\nendmodule"), QStringLiteral("module declaration"), QStringLiteral("keyword")},
        {QStringLiteral("output"), QStringLiteral("output logic signal"), QStringLiteral("output declaration"), QStringLiteral("keyword")},
        {QStringLiteral("parameter"), QStringLiteral("parameter int WIDTH = 32"), QStringLiteral("parameter declaration"), QStringLiteral("keyword")},
        {QStringLiteral("typedef"), QStringLiteral("typedef enum logic [1:0] {IDLE, BUSY, DONE} state_t;"), QStringLiteral("type declaration"), QStringLiteral("keyword")},
        {QStringLiteral("wire"), QStringLiteral("wire signal"), QStringLiteral("net declaration"), QStringLiteral("keyword")},
        {QStringLiteral("`default_nettype"), QStringLiteral("`default_nettype none"), QStringLiteral("preprocessor directive"), QStringLiteral("directive")},
        {QStringLiteral("`timescale"), QStringLiteral("`timescale 1ns/1ps"), QStringLiteral("preprocessor directive"), QStringLiteral("directive")},
    };
}

CompletionEntries tclCompletionEntries(bool constraintsOnly) {
    CompletionEntries entries{
        {QStringLiteral("create_clock"), QStringLiteral("create_clock -name clk -period 10.0 [get_ports {clk}]"), QStringLiteral("clock constraint"), QStringLiteral("function")},
        {QStringLiteral("create_generated_clock"), QStringLiteral("create_generated_clock -name clk_div2 -source [get_ports {clk}] -divide_by 2 [get_pins {u_div/Q}]"), QStringLiteral("generated clock"), QStringLiteral("function")},
        {QStringLiteral("set_clock_uncertainty"), QStringLiteral("set_clock_uncertainty 0.1 [get_clocks {clk}]"), QStringLiteral("clock uncertainty"), QStringLiteral("function")},
        {QStringLiteral("set_false_path"), QStringLiteral("set_false_path -from [get_ports {reset_n}]"), QStringLiteral("timing exception"), QStringLiteral("function")},
        {QStringLiteral("set_input_delay"), QStringLiteral("set_input_delay 1.0 -clock [get_clocks {clk}] [all_inputs]"), QStringLiteral("input delay"), QStringLiteral("function")},
        {QStringLiteral("set_output_delay"), QStringLiteral("set_output_delay 1.0 -clock [get_clocks {clk}] [all_outputs]"), QStringLiteral("output delay"), QStringLiteral("function")},
        {QStringLiteral("set_max_transition"), QStringLiteral("set_max_transition 0.2 [current_design]"), QStringLiteral("transition limit"), QStringLiteral("function")},
        {QStringLiteral("set_multicycle_path"), QStringLiteral("set_multicycle_path 2 -setup -from [get_registers {*} ] -to [get_registers {*} ]"), QStringLiteral("multicycle exception"), QStringLiteral("function")},
    };
    if (constraintsOnly)
        return entries;

    entries += CompletionEntries{
        {QStringLiteral("analyze"), QStringLiteral("analyze -format sverilog -f input/rtl.f"), QStringLiteral("read RTL files"), QStringLiteral("function")},
        {QStringLiteral("compile"), QStringLiteral("compile -map_effort low -area_effort low -power_effort none"), QStringLiteral("synthesis command"), QStringLiteral("function")},
        {QStringLiteral("create_test_protocol"), QStringLiteral("create_test_protocol"), QStringLiteral("DFT protocol"), QStringLiteral("function")},
        {QStringLiteral("dft_drc"), QStringLiteral("dft_drc -coverage_estimate"), QStringLiteral("DFT rule check"), QStringLiteral("function")},
        {QStringLiteral("elab"), QStringLiteral("elab top_module"), QStringLiteral("elaborate design"), QStringLiteral("function")},
        {QStringLiteral("insert_dft"), QStringLiteral("insert_dft"), QStringLiteral("insert scan logic"), QStringLiteral("function")},
        {QStringLiteral("link"), QStringLiteral("link"), QStringLiteral("link design"), QStringLiteral("function")},
        {QStringLiteral("preview_dft"), QStringLiteral("preview_dft -show all"), QStringLiteral("preview scan insertion"), QStringLiteral("function")},
        {QStringLiteral("report_dft"), QStringLiteral("report_dft -summary"), QStringLiteral("DFT report"), QStringLiteral("function")},
        {QStringLiteral("report_scan_path"), QStringLiteral("report_scan_path -view existing_dft"), QStringLiteral("scan path report"), QStringLiteral("function")},
        {QStringLiteral("set_dft_configuration"), QStringLiteral("set_dft_configuration -scan enable"), QStringLiteral("DFT configuration"), QStringLiteral("function")},
        {QStringLiteral("set_dft_signal"), QStringLiteral("set_dft_signal -view existing_dft -type ScanClock -timing {45 55} -port {clk}"), QStringLiteral("DFT signal"), QStringLiteral("function")},
        {QStringLiteral("set_search_path"), QStringLiteral("set search_path [list . /path/to/library]"), QStringLiteral("search path"), QStringLiteral("function")},
        {QStringLiteral("set_target_library"), QStringLiteral("set target_library [list cells.db]"), QStringLiteral("target library"), QStringLiteral("function")},
    };
    return entries;
}

CompletionEntries completionEntries(const QString &suffix) {
    if (suffix == QStringLiteral("sv") || suffix == QStringLiteral("v") || suffix == QStringLiteral("vh"))
        return svCompletionEntries();
    if (suffix == QStringLiteral("sdc"))
        return tclCompletionEntries(true);
    if (suffix == QStringLiteral("tcl"))
        return tclCompletionEntries(false);
    if (suffix == QStringLiteral("f")) {
        return {
            {QStringLiteral("+incdir+"), QStringLiteral("+incdir+./rtl"), QStringLiteral("include directory"), QStringLiteral("directive")},
            {QStringLiteral("RTL source"), QStringLiteral("./rtl/top.sv"), QStringLiteral("SystemVerilog source"), QStringLiteral("file")},
        };
    }
    return {};
}

int completionTokenStart(const QString &text, int cursorPosition) {
    const int cursor = qBound(0, cursorPosition, text.size());
    const int lineStart = text.lastIndexOf(u'\n', cursor - 1) + 1;
    int tokenStart = cursor;
    while (tokenStart > lineStart) {
        const QChar character = text.at(tokenStart - 1);
        if (!character.isLetterOrNumber() && character != u'_' && character != u'$' && character != u'`' && character != u'-')
            break;
        --tokenStart;
    }
    return tokenStart;
}

QString completionPrefix(const QString &text, int cursorPosition) {
    const int cursor = qBound(0, cursorPosition, text.size());
    return text.mid(completionTokenStart(text, cursor), cursor - completionTokenStart(text, cursor));
}

QString stripCodeFence(QString completion) {
    completion.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    completion.replace(u'\r', u'\n');
    const QString trimmed = completion.trimmed();
    if (!trimmed.startsWith(QStringLiteral("```")))
        return completion;
    completion = trimmed;
    const int firstNewline = completion.indexOf(u'\n');
    if (firstNewline >= 0)
        completion = completion.mid(firstNewline + 1);
    const int closingFence = completion.lastIndexOf(QStringLiteral("```"));
    if (closingFence >= 0)
        completion.truncate(closingFence);
    return completion;
}

QString normalizeAgentCompletion(QString completion, const QString &text, int cursorPosition) {
    completion = stripCodeFence(std::move(completion));
    const QString openingTag = QStringLiteral("<COMPLETION>");
    const QString closingTag = QStringLiteral("</COMPLETION>");
    if (completion.startsWith(openingTag))
        completion.remove(0, openingTag.size());
    const int closingTagOffset = completion.indexOf(closingTag);
    if (closingTagOffset >= 0)
        completion.truncate(closingTagOffset);
    completion.remove(QChar::Null);

    const int cursor = qBound(0, cursorPosition, text.size());
    const int lineStart = text.lastIndexOf(u'\n', cursor - 1) + 1;
    const QString linePrefix = text.mid(lineStart, cursor - lineStart);
    if (!linePrefix.isEmpty() && completion.startsWith(linePrefix))
        completion.remove(0, linePrefix.size());
    const QString token = completionPrefix(text, cursor);
    if (!token.isEmpty() && completion.startsWith(token, Qt::CaseInsensitive))
        completion.remove(0, token.size());

    constexpr int maxCompletionCharacters = 1'200;
    constexpr int maxCompletionLines = 16;
    if (completion.size() > maxCompletionCharacters)
        completion.truncate(maxCompletionCharacters);
    int lineBreaks = 0;
    for (int index = 0; index < completion.size(); ++index) {
        if (completion.at(index) == u'\n' && ++lineBreaks >= maxCompletionLines) {
            completion.truncate(index);
            break;
        }
    }
    return completion;
}

void collectIndexedFiles(
    const QString &root,
    QSet<QString> &seen,
    QStringList &files
) {
    QStringList pending{root};
    while (!pending.isEmpty() && files.size() < MaxIndexedFiles) {
        const QDir directory(pending.takeLast());
        const QFileInfoList entries = directory.entryInfoList(
            QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System,
            QDir::DirsFirst | QDir::Name | QDir::IgnoreCase
        );
        for (const QFileInfo &entry : entries) {
            if (entry.isDir()) {
                if (!isHiddenInfrastructureDirectory(entry.fileName())
                    && entry.fileName() != QStringLiteral("node_modules"))
                    pending.append(entry.absoluteFilePath());
                continue;
            }
            if (!isEditableTextPath(entry.filePath()))
                continue;
            const QString canonical = entry.canonicalFilePath();
            if (!canonical.isEmpty() && !seen.contains(canonical)) {
                seen.insert(canonical);
                files.append(canonical);
                if (files.size() >= MaxIndexedFiles)
                    break;
            }
        }
    }
}
}

class FileTreeModel final : public QAbstractItemModel {
public:
    enum Role {
        DisplayNameRole = Qt::UserRole + 1,
        PathRole,
        EntryTypeRole,
        RootKindRole,
        EditableRole,
        AvailableRole,
    };

    explicit FileTreeModel(QObject *parent = nullptr)
        : QAbstractItemModel(parent) {
    }

    QModelIndex index(int row, int column, const QModelIndex &parent = {}) const override {
        if (column != 0 || row < 0)
            return {};
        Node *parentNode = nodeForIndex(parent);
        if (!parentNode || row >= static_cast<int>(parentNode->children.size()))
            return {};
        return createIndex(row, column, parentNode->children.at(row).get());
    }

    QModelIndex parent(const QModelIndex &child) const override {
        if (!child.isValid())
            return {};
        const Node *node = static_cast<const Node *>(child.internalPointer());
        const Node *parentNode = node ? node->parent : nullptr;
        if (!parentNode || parentNode == &m_root)
            return {};
        const Node *grandparent = parentNode->parent;
        if (!grandparent)
            return {};
        for (int row = 0; row < static_cast<int>(grandparent->children.size()); ++row) {
            if (grandparent->children.at(row).get() == parentNode)
                return createIndex(row, 0, const_cast<Node *>(parentNode));
        }
        return {};
    }

    int rowCount(const QModelIndex &parent = {}) const override {
        if (parent.column() > 0)
            return 0;
        const Node *node = nodeForIndex(parent);
        return node ? static_cast<int>(node->children.size()) : 0;
    }

    int columnCount(const QModelIndex & = {}) const override { return 1; }

    QVariant data(const QModelIndex &index, int role) const override {
        if (!index.isValid())
            return {};
        const Node *node = static_cast<const Node *>(index.internalPointer());
        if (!node)
            return {};
        switch (role) {
        case Qt::DisplayRole:
        case DisplayNameRole: return node->name;
        case PathRole: return node->path;
        case EntryTypeRole: return node->isRoot ? QStringLiteral("root")
            : node->isDirectory ? QStringLiteral("directory") : QStringLiteral("file");
        case RootKindRole: return node->rootKind;
        case EditableRole: return !node->isDirectory && isEditableTextPath(node->path);
        case AvailableRole: return node->available;
        default: return {};
        }
    }

    QHash<int, QByteArray> roleNames() const override {
        return {
            {DisplayNameRole, "displayName"},
            {PathRole, "entryPath"},
            {EntryTypeRole, "entryType"},
            {RootKindRole, "rootKind"},
            {EditableRole, "editable"},
            {AvailableRole, "available"},
        };
    }

    bool hasChildren(const QModelIndex &parent = {}) const override {
        const Node *node = nodeForIndex(parent);
        if (!node)
            return false;
        if (node == &m_root)
            return !node->children.empty();
        return node->isDirectory && node->available && (!node->loaded || !node->children.empty());
    }

    bool canFetchMore(const QModelIndex &parent) const override {
        const Node *node = nodeForIndex(parent);
        return node && node != &m_root && node->isDirectory && node->available && !node->loaded;
    }

    void fetchMore(const QModelIndex &parent) override {
        Node *node = nodeForIndex(parent);
        if (!node || node->loaded)
            return;
        auto children = readChildren(node);
        node->loaded = true;
        if (children.empty()) {
            emit dataChanged(parent, parent);
            return;
        }
        beginInsertRows(parent, 0, static_cast<int>(children.size()) - 1);
        node->children = std::move(children);
        endInsertRows();
    }

    void setRoots(const QVector<ExplorerRoot> &roots) {
        beginResetModel();
        m_roots = roots;
        m_root.children.clear();
        for (const ExplorerRoot &root : roots) {
            auto node = std::make_unique<Node>();
            node->name = root.label;
            node->path = root.path;
            node->rootKind = root.kind;
            node->isDirectory = true;
            node->isRoot = true;
            node->available = QFileInfo(root.path).isDir();
            node->parent = &m_root;
            if (node->available) {
                node->children = readChildren(node.get());
                node->loaded = true;
            }
            m_root.children.push_back(std::move(node));
        }
        endResetModel();
    }

    void refresh() { setRoots(m_roots); }

private:
    struct Node {
        QString name;
        QString path;
        QString rootKind;
        bool isDirectory = true;
        bool isRoot = false;
        bool available = true;
        bool loaded = false;
        Node *parent = nullptr;
        std::vector<std::unique_ptr<Node>> children;
    };

    Node *nodeForIndex(const QModelIndex &index) const {
        return index.isValid() ? static_cast<Node *>(index.internalPointer()) : const_cast<Node *>(&m_root);
    }

    std::vector<std::unique_ptr<Node>> readChildren(Node *parentNode) const {
        std::vector<std::unique_ptr<Node>> children;
        QDir directory(parentNode->path);
        if (!directory.exists())
            return children;
        const QFileInfoList entries = directory.entryInfoList(
            QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System,
            QDir::DirsFirst | QDir::Name | QDir::IgnoreCase
        );
        children.reserve(entries.size());
        for (const QFileInfo &entry : entries) {
            if (entry.isDir() && isHiddenInfrastructureDirectory(entry.fileName()))
                continue;
            auto child = std::make_unique<Node>();
            child->name = entry.fileName();
            child->path = entry.absoluteFilePath();
            child->rootKind = parentNode->rootKind;
            child->isDirectory = entry.isDir();
            child->available = entry.exists();
            child->parent = parentNode;
            children.push_back(std::move(child));
        }
        return children;
    }

    Node m_root;
    QVector<ExplorerRoot> m_roots;
};

FileEditor::FileEditor(QObject *parent)
    : QObject(parent),
      m_treeModel(new FileTreeModel(this)) {
}

QString FileEditor::filePath() const { return m_filePath; }
QString FileEditor::language() const { return languageForPath(m_filePath); }
QString FileEditor::content() const { return m_content; }
bool FileEditor::loading() const { return m_loading; }
bool FileEditor::limitedPreview() const { return m_limitedPreview; }
QString FileEditor::loadNotice() const { return m_loadNotice; }
int FileEditor::loadedLineCount() const { return m_loadedLineCount; }
int FileEditor::maximumLoadedLines() const { return m_maximumLoadedLines; }
QStringList FileEditor::editableFiles() const { return m_editableFiles; }
QStringList FileEditor::relatedFiles() const { return m_relatedFiles; }
QAbstractItemModel *FileEditor::treeModel() const { return m_treeModel; }
int FileEditor::treeRootCount() const { return m_treeModel->rowCount(); }
bool FileEditor::dirty() const { return m_dirty; }
QString FileEditor::error() const { return m_error; }
bool FileEditor::agentCompletionRunning() const { return m_agentCompletionRunning; }
QString FileEditor::agentCompletion() const { return m_agentCompletion; }
int FileEditor::agentCompletionCursor() const { return m_agentCompletionCursor; }
QString FileEditor::agentCompletionError() const { return m_agentCompletionError; }

void FileEditor::setMaximumLoadedLines(int value) {
    const int normalized = qBound(1'000, value, 100'000);
    if (m_maximumLoadedLines == normalized)
        return;
    m_maximumLoadedLines = normalized;
    emit maximumLoadedLinesChanged();
    if (!m_filePath.isEmpty() && !m_dirty)
        openFile(m_filePath);
}

void FileEditor::setProjectRoots(const QString &projectRoot, const QString &rtlRoot) {
    QVariantList locations;
    if (!projectRoot.trimmed().isEmpty()) {
        locations.append(QVariantMap{
            {QStringLiteral("label"), tr("原始项目")},
            {QStringLiteral("path"), projectRoot},
            {QStringLiteral("kind"), QStringLiteral("project")},
            {QStringLiteral("searchable"), true},
        });
    }
    if (!rtlRoot.trimmed().isEmpty()) {
        locations.append(QVariantMap{
            {QStringLiteral("label"), QStringLiteral("RTL")},
            {QStringLiteral("path"), rtlRoot},
            {QStringLiteral("kind"), QStringLiteral("rtl")},
            {QStringLiteral("searchable"), true},
        });
    }
    setProjectLocations(locations);
}

void FileEditor::setProjectLocations(const QVariantList &locations) {
    QVector<ExplorerRoot> roots;
    QStringList allowedRoots;
    QStringList indexRoots;
    QStringList signatures;
    QSet<QString> seen;
    for (const QVariant &value : locations) {
        const QVariantMap location = value.toMap();
        const QString rawPath = location.value(QStringLiteral("path")).toString().trimmed();
        if (rawPath.isEmpty())
            continue;
        const QString path = normalizedLocationPath(rawPath);
        if (path.isEmpty() || seen.contains(path))
            continue;
        seen.insert(path);
        const QString kind = location.value(QStringLiteral("kind"), QStringLiteral("folder")).toString();
        const QString fallbackLabel = QFileInfo(path).fileName().isEmpty() ? path : QFileInfo(path).fileName();
        const QString label = location.value(QStringLiteral("label"), fallbackLabel).toString();
        const bool searchable = location.value(QStringLiteral("searchable"), true).toBool();
        roots.append({label, path, kind, searchable});
        allowedRoots.append(path);
        if (searchable && QFileInfo(path).isDir())
            indexRoots.append(path);
        signatures.append(QStringLiteral("%1\x1f%2\x1f%3\x1f%4")
            .arg(label, path, kind, searchable ? QStringLiteral("1") : QStringLiteral("0")));
    }
    const QString signature = signatures.join(u'\x1e');
    if (m_rootSignature == signature)
        return;

    cancelAgentCompletion();
    ++m_loadRequestId;
    const bool loadWasRunning = m_loading;
    m_loading = false;
    m_rootSignature = signature;
    m_roots = allowedRoots;
    m_indexRoots = indexRoots;
    QString fileToReload;
    if (loadWasRunning && !m_filePath.isEmpty() && isAllowedFile(m_filePath))
        fileToReload = m_filePath;
    if (!m_filePath.isEmpty() && !isAllowedFile(m_filePath)) {
        m_filePath.clear();
        m_content.clear();
        m_loadNotice.clear();
        m_loading = false;
        m_limitedPreview = false;
        m_loadedLineCount = 0;
        m_dirty = false;
        m_relatedFiles.clear();
        emit filePathChanged();
        emit contentChanged();
        emit loadStateChanged();
        emit dirtyChanged();
        emit relatedFilesChanged();
    }
    if (loadWasRunning)
        emit loadStateChanged();
    m_treeModel->setRoots(roots);
    emit treeRootsChanged();
    refreshFileIndex();
    if (!fileToReload.isEmpty())
        openFile(fileToReload);
}

void FileEditor::refreshFiles() {
    refreshFileIndex();
    m_treeModel->refresh();
    emit treeRootsChanged();
}

void FileEditor::refreshFileIndex() {
    QStringList files;
    QSet<QString> seen;
    for (const QString &root : m_indexRoots)
        collectIndexedFiles(root, seen, files);
    files.sort(Qt::CaseInsensitive);
    if (m_editableFiles != files) {
        m_editableFiles = files;
        emit editableFilesChanged();
    }
    updateRelatedFiles();
}

bool FileEditor::selectFile(const QString &path) {
    if (!isAllowedFile(path)) {
        setError(tr("只能打开当前项目中的受支持文本文件。"));
        return false;
    }
    const QString canonical = QFileInfo(path).canonicalFilePath();
    if (canonical.isEmpty()) {
        setError(tr("无法确定文件位置：%1").arg(path));
        return false;
    }
    cancelAgentCompletion();
    ++m_loadRequestId;
    if (m_filePath != canonical) {
        m_filePath = canonical;
        emit filePathChanged();
    }
    if (!m_content.isEmpty()) {
        m_content.clear();
        emit contentChanged();
    }
    if (m_dirty) {
        m_dirty = false;
        emit dirtyChanged();
    }
    m_loading = false;
    m_limitedPreview = false;
    m_loadNotice.clear();
    m_loadedLineCount = 0;
    setError({});
    updateRelatedFiles();
    emit loadStateChanged();
    return true;
}

bool FileEditor::openFile(const QString &path) {
    if (!selectFile(path))
        return false;
    const QString canonical = m_filePath;
    const quint64 requestId = m_loadRequestId;
    const int maximumLines = m_maximumLoadedLines;
    if (QFileInfo(canonical).size() < AsyncLoadThresholdBytes) {
        const FileLoadResult result = readTextFile(canonical, maximumLines);
        applyLoadedFile(
            requestId,
            canonical,
            result.content,
            result.limitedPreview,
            result.loadedLineCount,
            result.notice,
            result.error
        );
        return result.error.isEmpty();
    }

    m_loading = true;
    emit loadStateChanged();
    auto *watcher = new QFutureWatcher<FileLoadResult>(this);
    connect(watcher, &QFutureWatcher<FileLoadResult>::finished, this, [this, watcher, requestId, canonical] {
        const FileLoadResult result = watcher->result();
        watcher->deleteLater();
        applyLoadedFile(
            requestId,
            canonical,
            result.content,
            result.limitedPreview,
            result.loadedLineCount,
            result.notice,
            result.error
        );
    });
    watcher->setFuture(QtConcurrent::run([canonical, maximumLines] {
        return readTextFile(canonical, maximumLines);
    }));
    return true;
}

void FileEditor::applyLoadedFile(
    quint64 requestId,
    const QString &canonicalPath,
    const QString &content,
    bool limitedPreview,
    int loadedLineCount,
    const QString &notice,
    const QString &error
) {
    if (requestId != m_loadRequestId || canonicalPath != m_filePath)
        return;
    m_loading = false;
    if (!error.isEmpty()) {
        if (!m_content.isEmpty()) {
            m_content.clear();
            emit contentChanged();
        }
        m_limitedPreview = false;
        m_loadNotice.clear();
        m_loadedLineCount = 0;
        setError(error);
        emit loadStateChanged();
        return;
    }
    if (m_content != content) {
        m_content = content;
        emit contentChanged();
    }
    m_limitedPreview = limitedPreview;
    m_loadNotice = notice;
    m_loadedLineCount = loadedLineCount;
    setError({});
    emit loadStateChanged();
}

QString FileEditor::fileName(const QString &path) const {
    return QFileInfo(path).fileName();
}

QString FileEditor::relativePath(const QString &path) const {
    const QString canonical = QFileInfo(path).canonicalFilePath();
    if (canonical.isEmpty())
        return path;
    for (const QString &root : m_roots) {
        if (canonical.startsWith(root + QDir::separator()))
            return QDir(root).relativeFilePath(canonical);
    }
    return canonical;
}

QString FileEditor::languageForFile(const QString &path) const {
    return languageForPath(path);
}

bool FileEditor::isEditableFile(const QString &path) const {
    const QFileInfo info(path);
    return info.isFile() && isAllowedSuffix(path);
}

void FileEditor::setContent(const QString &content) {
    if (m_loading || m_limitedPreview)
        return;
    if (m_content == content)
        return;
    cancelAgentCompletion();
    m_content = content;
    emit contentChanged();
    if (!m_dirty) {
        m_dirty = true;
        emit dirtyChanged();
    }
}

bool FileEditor::save() {
    if (m_filePath.isEmpty() || !isAllowedFile(m_filePath)) {
        setError(tr("请先打开当前项目中的文件。"));
        return false;
    }
    if (m_loading || m_limitedPreview) {
        setError(tr("当前文件只载入了部分内容，不能保存。"));
        return false;
    }
    QSaveFile file(m_filePath);
    if (!file.open(QIODevice::WriteOnly) || file.write(m_content.toUtf8()) < 0 || !file.commit()) {
        setError(tr("保存失败：%1").arg(m_filePath));
        return false;
    }
    if (m_dirty) {
        m_dirty = false;
        emit dirtyChanged();
    }
    setError({});
    return true;
}

QString FileEditor::suggest(const QString &text, int cursorPosition) const {
    const QVariantList items = completionItems(text, cursorPosition);
    return items.isEmpty() ? QString() : items.constFirst().toMap().value(QStringLiteral("insertText")).toString();
}

QVariantList FileEditor::completionItems(const QString &text, int cursorPosition) const {
    QVariantList items;
    if (m_filePath.isEmpty())
        return items;

    const int cursor = qBound(0, cursorPosition, text.size());
    const QString prefix = completionPrefix(text, cursor);
    const QString suffix = QFileInfo(m_filePath).suffix().toLower();
    CompletionEntries entries = completionEntries(suffix);
    QSet<QString> labels;

    const auto appendEntry = [&items, &labels, &prefix](const CompletionEntry &entry) {
        if (items.size() >= 12)
            return;
        const bool labelMatches = prefix.isEmpty() || entry.label.startsWith(prefix, Qt::CaseInsensitive);
        const bool insertionMatches = prefix.isEmpty() || entry.insertion.startsWith(prefix, Qt::CaseInsensitive);
        if ((!labelMatches && !insertionMatches) || labels.contains(entry.label))
            return;
        QString insertText = entry.insertion;
        if (!prefix.isEmpty() && insertText.startsWith(prefix, Qt::CaseInsensitive))
            insertText.remove(0, prefix.size());
        labels.insert(entry.label);
        items.append(QVariantMap{
            {QStringLiteral("label"), entry.label},
            {QStringLiteral("insertText"), insertText},
            {QStringLiteral("detail"), entry.detail},
            {QStringLiteral("kind"), entry.kind},
        });
    };

    for (const CompletionEntry &entry : entries)
        appendEntry(entry);

    if (prefix.size() >= 2) {
        static const QRegularExpression WordPattern(QStringLiteral(R"(\b[A-Za-z_][A-Za-z0-9_$]*\b)"));
        auto match = WordPattern.globalMatch(text);
        while (match.hasNext() && items.size() < 12) {
            const QString word = match.next().captured();
            if (word.size() > prefix.size() && word.startsWith(prefix, Qt::CaseInsensitive))
                appendEntry({word, word, QStringLiteral("current document"), QStringLiteral("text")});
        }
    }
    return items;
}

bool FileEditor::canRequestAgentCompletion(const QString &text, int cursorPosition) const {
    if (m_filePath.isEmpty())
        return false;
    const int cursor = qBound(0, cursorPosition, text.size());
    const int lineEndOffset = text.indexOf(u'\n', cursor);
    const int lineEnd = lineEndOffset < 0 ? text.size() : lineEndOffset;
    if (!text.mid(cursor, lineEnd - cursor).trimmed().isEmpty())
        return false;
    const QString prefix = completionPrefix(text, cursor);
    return prefix.size() >= 2 || prefix == QStringLiteral("`");
}

void FileEditor::requestAgentCompletion(
    const QString &modelId,
    const QString &apiBase,
    int contextWindow,
    const QString &text,
    int cursorPosition,
    bool explicitRequest
) {
    cancelAgentCompletion();
    if (m_loading || m_limitedPreview)
        return;
    if (!canRequestAgentCompletion(text, cursorPosition))
        return;
    const quint64 requestId = ++m_agentCompletionRequestId;
    const int cursor = qBound(0, cursorPosition, text.size());
    const QString fallback = suggest(text, cursorPosition);
    const QUrl base(apiBase.trimmed());
    if (m_filePath.isEmpty() || modelId.trimmed().isEmpty() || !base.isValid() || base.scheme().isEmpty()) {
        if (explicitRequest) {
            m_agentCompletionCursor = cursor;
            setAgentCompletion(fallback);
            setAgentCompletionError(tr("本地模型配置不可用，已提供语言补全。"));
        }
        return;
    }

    QUrl endpoint(base);
    QString path = endpoint.path();
    if (!path.endsWith(u'/'))
        path.append(u'/');
    if (!path.endsWith(QStringLiteral("v1/")))
        path.append(QStringLiteral("v1/"));
    path.append(QStringLiteral("chat/completions"));
    endpoint.setPath(path);

    const int contextCharacters = qBound(4'096, contextWindow * 2, 20'000);
    const int prefixCharacters = qMin(cursor, contextCharacters * 3 / 4);
    const int suffixCharacters = qMin(text.size() - cursor, contextCharacters - prefixCharacters);
    const QString prefix = text.mid(cursor - prefixCharacters, prefixCharacters);
    const QString suffix = text.mid(cursor, suffixCharacters);
    const QString language = QFileInfo(m_filePath).suffix().toUpper();
    const QString prompt = QStringLiteral(
        "File: %1\nLanguage: %2\n\n"
        "Insert at <CURSOR>. Return only the text to insert there. Never repeat text inside <PREFIX> or <SUFFIX>. "
        "Do not use Markdown, explanations, shell commands, or file writes. Keep the result below 16 lines and compatible with DFT source syntax.\n\n"
        "<PREFIX>\n%3\n<CURSOR>\n<SUFFIX>\n%4\n</SUFFIX>"
    ).arg(m_filePath, language, prefix, suffix);
    const QJsonObject payload{
        {"model", modelId.trimmed()},
        {"messages", QJsonArray{
            QJsonObject{{"role", "system"}, {"content", "You are an inline completion provider for SystemVerilog, Tcl, and SDC. Return insertion text only."}},
            QJsonObject{{"role", "user"}, {"content", prompt}},
        }},
        {"temperature", 0.05},
        {"max_tokens", 192},
        {"stream", false},
        {"think", false},
        {"stop", QJsonArray{QStringLiteral("</COMPLETION>"), QStringLiteral("<|END_COMPLETION|>")}},
        {"options", QJsonObject{{"num_ctx", qBound(2048, contextWindow, 262144)}}},
    };
    QNetworkRequest request(endpoint);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setRawHeader("Authorization", "Bearer local-llama-cpp");
    m_agentCompletionRunning = true;
    emit agentCompletionRunningChanged();
    setAgentCompletion({});
    setAgentCompletionError({});
    QNetworkReply *reply = m_network.post(request, QJsonDocument(payload).toJson(QJsonDocument::Compact));
    m_agentCompletionReply = reply;
    const QString requestedPath = m_filePath;
    connect(reply, &QNetworkReply::finished, this, [this, reply, requestId, requestedPath, text, cursor, fallback, explicitRequest] {
        if (requestId != m_agentCompletionRequestId || reply != m_agentCompletionReply) {
            reply->deleteLater();
            return;
        }
        m_agentCompletionReply = nullptr;
        m_agentCompletionRunning = false;
        emit agentCompletionRunningChanged();
        const QByteArray response = reply->readAll();
        const QString networkError = reply->error() == QNetworkReply::NoError ? QString() : reply->errorString();
        reply->deleteLater();
        if (m_filePath != requestedPath || m_content != text)
            return;
        const auto document = QJsonDocument::fromJson(response);
        const auto choices = document.object().value("choices").toArray();
        QString completion;
        if (!choices.isEmpty())
            completion = normalizeAgentCompletion(choices.first().toObject().value("message").toObject().value("content").toString(), text, cursor);
        if (!networkError.isEmpty() || completion.trimmed().isEmpty()) {
            if (explicitRequest) {
                m_agentCompletionCursor = cursor;
                setAgentCompletion(fallback);
                setAgentCompletionError(networkError.isEmpty()
                    ? tr("本地模型没有返回可插入文本，已提供语言补全。")
                    : tr("本地模型请求失败：%1；已提供语言补全。").arg(networkError));
            }
            return;
        }
        m_agentCompletionCursor = cursor;
        setAgentCompletion(completion);
        setAgentCompletionError({});
    });
}

void FileEditor::cancelAgentCompletion() {
    ++m_agentCompletionRequestId;
    if (m_agentCompletionReply) {
        QNetworkReply *reply = m_agentCompletionReply;
        m_agentCompletionReply = nullptr;
        reply->abort();
    }
    if (m_agentCompletionRunning) {
        m_agentCompletionRunning = false;
        emit agentCompletionRunningChanged();
    }
    m_agentCompletionCursor = -1;
    setAgentCompletion({});
    setAgentCompletionError({});
}

bool FileEditor::isAllowedFile(const QString &path) const {
    const QFileInfo info(path);
    if (!info.isFile() || !isAllowedSuffix(path))
        return false;
    const QString canonical = info.canonicalFilePath();
    for (const QString &root : m_roots) {
        if (canonical == root || canonical.startsWith(root + QDir::separator()))
            return true;
    }
    return false;
}

bool FileEditor::isAllowedSuffix(const QString &path) const {
    return isEditableTextPath(path);
}

QString FileEditor::languageForPath(const QString &path) {
    const QString suffix = QFileInfo(path).suffix().toLower();
    if (suffix == QStringLiteral("sv"))
        return QStringLiteral("systemverilog");
    if (suffix == QStringLiteral("v") || suffix == QStringLiteral("vh"))
        return QStringLiteral("verilog");
    if (suffix == QStringLiteral("tcl") || suffix == QStringLiteral("sdc"))
        return suffix;
    if (suffix == QStringLiteral("f"))
        return QStringLiteral("filelist");
    if (suffix == QStringLiteral("lib") || suffix == QStringLiteral("lef") || suffix == QStringLiteral("tf"))
        return QStringLiteral("technology");
    return QStringLiteral("config");
}

void FileEditor::setError(const QString &message) {
    if (m_error == message)
        return;
    m_error = message;
    emit errorChanged();
}

void FileEditor::setAgentCompletion(const QString &completion) {
    if (m_agentCompletion == completion)
        return;
    m_agentCompletion = completion;
    emit agentCompletionChanged();
}

void FileEditor::setAgentCompletionError(const QString &message) {
    if (m_agentCompletionError == message)
        return;
    m_agentCompletionError = message;
    emit agentCompletionErrorChanged();
}

void FileEditor::updateRelatedFiles() {
    QStringList related;
    if (!m_filePath.isEmpty()) {
        const QString stem = QFileInfo(m_filePath).completeBaseName();
        for (const QString &candidate : m_editableFiles) {
            if (candidate != m_filePath && QFileInfo(candidate).completeBaseName() == stem)
                related.append(candidate);
        }
    }
    if (m_relatedFiles != related) {
        m_relatedFiles = related;
        emit relatedFilesChanged();
    }
}
