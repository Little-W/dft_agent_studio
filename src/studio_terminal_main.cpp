#include "studiocliturnservice.h"
#include "studiopaths.h"
#include "studiouserdata.h"
#include "studionativeapi.h"

#include <QCoreApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QPointer>
#include <QRegularExpression>
#include <QSocketNotifier>
#include <QTextStream>
#include <QVariantList>

#include <stdexcept>
#include <utility>
#include <unistd.h>

namespace {

QVariantMap unwrap(const QVariantMap &response) {
    if (!response.value(QStringLiteral("ok")).toBool()) {
        QString message = response.value(QStringLiteral("message")).toString();
        if (message.isEmpty())
            message = response.value(QStringLiteral("error")).toString();
        throw std::runtime_error(message.isEmpty() ? "Studio operation failed." : message.toStdString());
    }
    return response.value(QStringLiteral("result")).toMap();
}

QString displayName(const QVariantMap &item) {
    const QString name = item.value(QStringLiteral("name")).toString().trimmed();
    const QString id = item.value(QStringLiteral("id")).toString().trimmed();
    return name.isEmpty() ? id : name;
}

QString projectPermissionMode(const QVariantMap &project) {
    QString mode = project.value(QStringLiteral("agentPermissionMode")).toString().trimmed().toLower();
    QVariantMap execution = project.value(QStringLiteral("dftExecution")).toMap();
    if (execution.isEmpty())
        execution = project.value(QStringLiteral("metadata")).toMap()
            .value(QStringLiteral("dft_execution")).toMap();
    if (mode.isEmpty())
        mode = execution.value(QStringLiteral("agent_permission_mode")).toString().trimmed().toLower();
    if (mode == QStringLiteral("full") || mode == QStringLiteral("autonomous"))
        return QStringLiteral("full_access");
    if (mode == QStringLiteral("approval") || mode == QStringLiteral("full_access"))
        return mode;
    return QStringLiteral("workspace");
}

QString oneLine(QString value, int max = 160) {
    value = value.simplified();
    if (value.size() > max)
        value = value.left(max - 1) + QChar(0x2026);
    return value;
}

QString eventLabel(const QVariantMap &event) {
    const QString kind = event.value(QStringLiteral("event"), event.value(QStringLiteral("type"))).toString();
    const QString name = event.value(QStringLiteral("name")).toString();
    if (kind == QStringLiteral("tool_started"))
        return QStringLiteral("正在运行工具：%1").arg(name.isEmpty() ? QStringLiteral("tool") : name);
    if (kind == QStringLiteral("tool_finished")) {
        const QVariantMap result = event.value(QStringLiteral("result")).toMap();
        QString error = result.value(QStringLiteral("error")).toString();
        if (error.isEmpty())
            error = result.value(QStringLiteral("message")).toString();
        return error.isEmpty() ? QStringLiteral("工具完成：%1").arg(name)
                               : QStringLiteral("工具失败：%1：%2").arg(name, oneLine(error));
    }
    if (kind == QStringLiteral("provider_reconnecting"))
        return QStringLiteral("API 正在重连（%1/%2）")
            .arg(event.value(QStringLiteral("attempt")).toInt())
            .arg(event.value(QStringLiteral("max_attempts")).toInt());
    if (kind == QStringLiteral("provider_reconnected"))
        return QStringLiteral("API 已恢复连接");
    if (kind == QStringLiteral("user_steering_received"))
        return QStringLiteral("已收到实时引导");
    if (kind == QStringLiteral("response.output_text.delta"))
        return {};
    if (kind == QStringLiteral("response.output_text.done"))
        return {};
    if (kind == QStringLiteral("response.created"))
        return QStringLiteral("正在思考…");
    if (kind == QStringLiteral("response.completed"))
        return {};
    if (kind == QStringLiteral("tool_approval_requested") || kind == QStringLiteral("approval_requested")) {
        const QString id = event.value(QStringLiteral("request_id")).toString();
        return QStringLiteral("需要批准：/approve %1 或 /deny %1").arg(id);
    }
    const QString text = event.value(QStringLiteral("text")).toString();
    return text.isEmpty() ? kind : oneLine(text);
}

QVariant sanitizeSecrets(const QVariant &value) {
    if (value.metaType().id() == QMetaType::QVariantMap) {
        QVariantMap safe;
        const QVariantMap source = value.toMap();
        for (auto it = source.cbegin(); it != source.cend(); ++it) {
            const QString key = it.key().toCaseFolded();
            QString compactKey = key;
            compactKey.remove(QLatin1Char('_'));
            compactKey.remove(QLatin1Char('-'));
            if (key.contains(QStringLiteral("secret")) || key.contains(QStringLiteral("token"))
                || key.contains(QStringLiteral("password")) || key.contains(QStringLiteral("credential"))
                || compactKey.contains(QStringLiteral("apikey"))
                || key.contains(QStringLiteral("authorization")) || key.contains(QStringLiteral("private_key"))) {
                safe.insert(it.key(), QStringLiteral("[已隐藏]"));
            } else {
                safe.insert(it.key(), sanitizeSecrets(it.value()));
            }
        }
        return safe;
    }
    if (value.metaType().id() == QMetaType::QVariantList) {
        QVariantList safe;
        for (const QVariant &item : value.toList())
            safe.append(sanitizeSecrets(item));
        return safe;
    }
    return value;
}

bool parseJsonValueAndReason(const QString &source, QJsonValue *value, QString *reason, QString *error) {
    qsizetype start = 0;
    while (start < source.size() && source.at(start).isSpace())
        ++start;
    if (start == source.size()) {
        *error = QStringLiteral("缺少 JSON 值和修改原因。");
        return false;
    }

    qsizetype end = start;
    const QChar first = source.at(start);
    if (first == QLatin1Char('{') || first == QLatin1Char('[')) {
        int depth = 0;
        bool inString = false;
        bool escaped = false;
        for (; end < source.size(); ++end) {
            const QChar ch = source.at(end);
            if (inString) {
                if (escaped)
                    escaped = false;
                else if (ch == QLatin1Char('\\'))
                    escaped = true;
                else if (ch == QLatin1Char('"'))
                    inString = false;
                continue;
            }
            if (ch == QLatin1Char('"')) {
                inString = true;
            } else if (ch == QLatin1Char('{') || ch == QLatin1Char('[')) {
                ++depth;
            } else if (ch == QLatin1Char('}') || ch == QLatin1Char(']')) {
                if (--depth == 0) {
                    ++end;
                    break;
                }
            }
        }
        if (depth != 0 || inString) {
            *error = QStringLiteral("JSON 对象或数组未闭合；请把 JSON 值完整写在原因之前。");
            return false;
        }
    } else if (first == QLatin1Char('"')) {
        bool escaped = false;
        for (end = start + 1; end < source.size(); ++end) {
            const QChar ch = source.at(end);
            if (escaped)
                escaped = false;
            else if (ch == QLatin1Char('\\'))
                escaped = true;
            else if (ch == QLatin1Char('"')) {
                ++end;
                break;
            }
        }
        if (end > source.size() || (end == source.size() && source.at(end - 1) != QLatin1Char('"'))) {
            *error = QStringLiteral("JSON 字符串未闭合。");
            return false;
        }
    } else {
        end = source.indexOf(QRegularExpression(QStringLiteral("\\s")), start);
        if (end < 0)
            end = source.size();
    }

    const QByteArray wrapped = QByteArrayLiteral("[") + source.mid(start, end - start).toUtf8()
        + QByteArrayLiteral("]");
    QJsonParseError parseError{};
    const QJsonDocument parsed = QJsonDocument::fromJson(wrapped, &parseError);
    if (parseError.error != QJsonParseError::NoError || !parsed.isArray() || parsed.array().size() != 1) {
        *error = QStringLiteral("JSON 值无效：%1").arg(parseError.errorString());
        return false;
    }
    *value = parsed.array().at(0);
    *reason = source.mid(end).trimmed();
    if (reason->isEmpty()) {
        *error = QStringLiteral("必须提供修改原因。");
        return false;
    }
    return true;
}

class Terminal final : public QObject {
public:
    Terminal(QString root, QObject *parent = nullptr)
        : QObject(parent), m_root(std::move(root)), m_out(stdout), m_err(stderr) {}

    bool start() {
        try {
            const QVariantMap projectResult = unwrap(StudioNativeApi::dispatch(
                QStringLiteral("project/list"), {}, m_root));
            m_projects = projectResult.value(QStringLiteral("projects")).toList();
            if (m_projects.isEmpty())
                throw std::runtime_error("Studio 没有已配置的项目。");

            const QVariantMap modelResult = unwrap(StudioNativeApi::dispatch(
                QStringLiteral("model/list"), {}, m_root));
            m_models = modelResult.value(QStringLiteral("models")).toList();
            m_activeModelId = modelResult.value(QStringLiteral("active_model_id")).toString();
            for (const QVariant &value : std::as_const(m_models)) {
                const QVariantMap model = value.toMap();
                if (model.value(QStringLiteral("id")).toString() == m_activeModelId) {
                    m_model = model;
                    break;
                }
            }
            if (m_model.isEmpty() && !m_models.isEmpty())
                m_model = m_models.constFirst().toMap();
            m_project = m_projects.constFirst().toMap();
            m_permissionMode = projectPermissionMode(m_project);
        } catch (const std::exception &error) {
            m_err << "启动失败：" << error.what() << Qt::endl;
            return false;
        }

        m_out << "DFT Agent Studio · 原生终端\n"
              << "项目：" << displayName(m_project)
              << "  模型：" << m_model.value(QStringLiteral("id")).toString() << "\n"
              << "权限模式：" << m_permissionMode << "  输入任务开始，/help 查看命令。API 密钥不会显示。" << Qt::endl;
        printPrompt();
        m_notifier = new QSocketNotifier(STDIN_FILENO, QSocketNotifier::Read, this);
        connect(m_notifier, &QSocketNotifier::activated, this,
            [this](QSocketDescriptor, QSocketNotifier::Type) { readInput(); });
        return true;
    }

private:
    struct Turn {
        QPointer<StudioCliTurnService> service;
        QString projectId;
        QString modelId;
    };

    QString m_root;
    QTextStream m_out;
    QTextStream m_err;
    QSocketNotifier *m_notifier = nullptr;
    QByteArray m_input;
    QVariantList m_projects;
    QVariantList m_models;
    QVariantMap m_project;
    QVariantMap m_model;
    QString m_activeModelId;
    QString m_sessionId;
    QString m_permissionMode = QStringLiteral("workspace");
    bool m_newSession = true;
    QString m_newSessionName;
    bool m_multiAgent = false;
    QHash<QString, Turn> m_turns;

    void printPrompt() {
        const QString project = displayName(m_project);
        const QString session = m_sessionId.isEmpty() ? QStringLiteral("新会话") : m_sessionId.left(8);
        m_out << "\n[" << project << " · " << session << " · " << m_permissionMode << "] > " << Qt::flush;
    }

    void readInput() {
        char bytes[4096];
        const ssize_t count = ::read(STDIN_FILENO, bytes, sizeof(bytes));
        if (count <= 0) {
            m_notifier->setEnabled(false);
            if (m_turns.isEmpty())
                QCoreApplication::quit();
            return;
        }
        m_input.append(bytes, static_cast<qsizetype>(count));
        while (true) {
            const qsizetype newline = m_input.indexOf('\n');
            if (newline < 0)
                break;
            const QString line = QString::fromUtf8(m_input.left(newline)).trimmed();
            m_input.remove(0, newline + 1);
            if (!line.isEmpty())
                handleLine(line);
            printPrompt();
        }
    }

    QStringList splitCommand(const QString &line) const {
        static const QRegularExpression token(QStringLiteral("(?:\\\"([^\\\"]*)\\\"|'([^']*)'|(\\S+))"));
        QStringList result;
        auto matches = token.globalMatch(line);
        while (matches.hasNext()) {
            const auto match = matches.next();
            result.append(!match.captured(1).isNull() ? match.captured(1)
                : !match.captured(2).isNull() ? match.captured(2) : match.captured(3));
        }
        return result;
    }

    void handleLine(const QString &line) {
        if (!line.startsWith(QLatin1Char('/'))) {
            if (m_sessionId.isEmpty() || !activeTurn(m_sessionId))
                startTurn(line);
            else
                steer(m_sessionId, line);
            return;
        }
        const QStringList parts = splitCommand(line);
        const QString command = parts.value(0).mid(1).toLower();
        const QString argument = line.section(QRegularExpression(QStringLiteral("\\s+")), 1).trimmed();
        try {
            if (command == QStringLiteral("help") || command == QStringLiteral("?")) {
                help();
            } else if (command == QStringLiteral("exit") || command == QStringLiteral("quit")) {
                if (m_turns.isEmpty())
                    QCoreApplication::quit();
                else
                    m_out << "仍有后台任务运行；使用 /stop 停止当前任务，或继续切换查看。" << Qt::endl;
            } else if (command == QStringLiteral("projects")) {
                listProjects();
            } else if (command == QStringLiteral("project")) {
                selectProject(parts.value(1));
            } else if (command == QStringLiteral("project-show")) {
                showProject(parts.value(1, QStringLiteral("current")));
            } else if (command == QStringLiteral("project-set")) {
                setProject(line);
            } else if (command == QStringLiteral("models")) {
                listModels();
            } else if (command == QStringLiteral("model")) {
                selectModel(parts.value(1));
            } else if (command == QStringLiteral("sessions") || command == QStringLiteral("session")) {
                if (parts.size() == 1)
                    listSessions();
                else
                    selectSession(parts.at(1));
            } else if (command == QStringLiteral("new")) {
                m_sessionId.clear();
                m_newSession = true;
                m_newSessionName = parts.mid(1).join(QLatin1Char(' ')).left(120);
                m_out << "下一条任务将在“" << displayName(m_project) << "”中新建会话"
                      << (m_newSessionName.isEmpty() ? QString{} : QStringLiteral("：%1").arg(m_newSessionName))
                      << "。" << Qt::endl;
            } else if (command == QStringLiteral("archive")) {
                archiveSession(parts.value(1, m_sessionId));
            } else if (command == QStringLiteral("delete")) {
                deleteSession(parts.value(1, m_sessionId));
            } else if (command == QStringLiteral("steer")) {
                steer(m_sessionId, argument.mid(parts.value(0).size()).trimmed());
            } else if (command == QStringLiteral("pause")) {
                withCurrentTurn([](StudioCliTurnService *turn) { turn->pause(); }, QStringLiteral("已请求暂停"));
            } else if (command == QStringLiteral("resume")) {
                withCurrentTurn([](StudioCliTurnService *turn) { turn->resume(); }, QStringLiteral("已请求恢复"));
            } else if (command == QStringLiteral("stop")) {
                withCurrentTurn([](StudioCliTurnService *turn) { turn->cancel(); }, QStringLiteral("已请求停止"));
            } else if (command == QStringLiteral("approve") || command == QStringLiteral("deny")) {
                decide(parts.value(1), command == QStringLiteral("approve"));
            } else if (command == QStringLiteral("multi-agent")) {
                m_multiAgent = argument.isEmpty() ? !m_multiAgent
                    : argument == QStringLiteral("on") || argument == QStringLiteral("true");
                m_out << "多智能体模式：" << (m_multiAgent ? "开启" : "关闭") << Qt::endl;
            } else if (command == QStringLiteral("permission")) {
                const QString mode = parts.value(1).trimmed().toLower();
                if (mode.isEmpty()) {
                    m_out << "当前权限模式：" << m_permissionMode << Qt::endl;
                } else if (mode == QStringLiteral("workspace") || mode == QStringLiteral("approval")
                           || mode == QStringLiteral("full_access")) {
                    m_permissionMode = mode;
                    m_out << "后续新运行使用权限模式：" << m_permissionMode << Qt::endl;
                } else {
                    throw std::runtime_error("权限模式只能是 workspace、approval 或 full_access。");
                }
            } else if (command == QStringLiteral("status")) {
                status();
            } else {
                m_out << "未知命令：" << command << "。输入 /help 查看可用命令。" << Qt::endl;
            }
        } catch (const std::exception &error) {
            m_err << "错误：" << error.what() << Qt::endl;
        }
    }

    void help() {
        m_out << "命令：\n"
              << "  /projects                 列出项目\n"
              << "  /project <id|序号>         切换项目（后台任务继续运行）\n"
              << "  /project-show [id|current] 查看项目设置（敏感字段自动隐藏）\n"
              << "  /project-set <id|current> <field> <JSON> <reason> 修改项目设置\n"
              << "  /models                   列出模型\n"
              << "  /model <id|序号>           选择模型\n"
              << "  /sessions                 列出当前项目的会话\n"
              << "  /session <id>             切换会话\n"
              << "  /new [名称]               新建会话（首条消息发送时创建）\n"
              << "  /archive [id]             归档会话\n"
              << "  /delete [id]              永久删除会话\n"
              << "  /multi-agent [on|off]     切换多智能体模式\n"
              << "  /permission [模式]         查看或设置 workspace、approval、full_access\n"
              << "  /steer <消息>              实时引导当前运行任务\n"
              << "  /pause | /resume | /stop  控制当前会话的运行任务\n"
              << "  /approve <request-id>     批准权限请求\n"
              << "  /deny <request-id>        拒绝权限请求\n"
              << "  /status | /help | /exit\n"
              << "普通文本直接作为任务发送；任务运行中普通文本会作为实时引导。" << Qt::endl;
    }

    static int selectedIndex(const QString &selector, const QVariantList &items) {
        bool numeric = false;
        const int index = selector.toInt(&numeric) - 1;
        if (numeric && index >= 0 && index < items.size())
            return index;
        for (qsizetype i = 0; i < items.size(); ++i) {
            if (items.at(i).toMap().value(QStringLiteral("id")).toString() == selector)
                return static_cast<int>(i);
        }
        return -1;
    }

    void listProjects() {
        for (qsizetype i = 0; i < m_projects.size(); ++i) {
            const QVariantMap item = m_projects.at(i).toMap();
            m_out << (i + 1) << ". " << displayName(item) << "  ["
                  << item.value(QStringLiteral("id")).toString() << "]  "
                  << item.value(QStringLiteral("root")).toString();
            if (!item.value(QStringLiteral("exists")).toBool())
                m_out << "  (目录不可用)";
            m_out << Qt::endl;
        }
    }

    void selectProject(const QString &selector) {
        const int index = selectedIndex(selector, m_projects);
        if (index < 0)
            throw std::runtime_error("项目不存在；用 /projects 查看项目 ID 与序号。");
        m_project = m_projects.at(index).toMap();
        m_permissionMode = projectPermissionMode(m_project);
        m_sessionId.clear();
        m_newSession = true;
        m_newSessionName.clear();
        m_out << "当前项目：" << displayName(m_project) << Qt::endl;
    }

    QVariantMap projectBySelector(const QString &selector) const {
        if (selector.isEmpty() || selector == QStringLiteral("current"))
            return m_project;
        const int index = selectedIndex(selector, m_projects);
        if (index < 0)
            throw std::runtime_error("项目不存在；用 /projects 查看项目 ID 与序号。");
        return m_projects.at(index).toMap();
    }

    void showProject(const QString &selector) {
        const QVariantMap selected = projectBySelector(selector);
        const QString projectId = selected.value(QStringLiteral("id")).toString();
        const QVariantMap result = unwrap(StudioNativeApi::dispatch(QStringLiteral("project/read"),
            {{QStringLiteral("project_id"), projectId}}, m_root));
        const QVariantMap project = result.value(QStringLiteral("project")).toMap();
        if (project.isEmpty())
            throw std::runtime_error("原生 Studio API 未返回项目配置。");
        const QVariant safe = sanitizeSecrets(project);
        m_out << displayName(project) << "  [" << projectId << "]\n"
              << QJsonDocument::fromVariant(safe).toJson(QJsonDocument::Indented) << Qt::flush;
    }

    void setProject(const QString &line) {
        static const QRegularExpression commandPattern(
            QStringLiteral("^/project-set\\s+(\\S+)\\s+(\\S+)\\s+(.+)$"));
        const auto match = commandPattern.match(line);
        if (!match.hasMatch())
            throw std::runtime_error("用法：/project-set <id|current> <field> <JSON> <reason>");
        const QString selector = match.captured(1);
        const QString field = match.captured(2);
        QJsonValue value;
        QString reason;
        QString parseError;
        if (!parseJsonValueAndReason(match.captured(3), &value, &reason, &parseError))
            throw std::runtime_error(parseError.toStdString());

        const QStringList path = field.split(QLatin1Char('.'), Qt::KeepEmptyParts);
        if (path.isEmpty() || path.contains(QString{}))
            throw std::runtime_error("设置字段路径无效。");
        static const QSet<QString> allowed{
            QStringLiteral("name"), QStringLiteral("kind"), QStringLiteral("root"),
            QStringLiteral("rtl_root"), QStringLiteral("top"), QStringLiteral("goal"),
            QStringLiteral("notes"), QStringLiteral("flow_profile"), QStringLiteral("minimum_coverage"),
            QStringLiteral("maximum_dft_drc_violations"), QStringLiteral("library_dir"),
            QStringLiteral("library_file"), QStringLiteral("library_profile"),
            QStringLiteral("metadata"), QStringLiteral("dft_execution"),
            QStringLiteral("flow_modules"), QStringLiteral("related_documents")};
        if (!allowed.contains(path.constFirst()))
            throw std::runtime_error(QStringLiteral("不允许修改的项目字段：%1").arg(path.constFirst()).toStdString());
        if (path.size() > 1 && path.constFirst() != QStringLiteral("metadata")
            && path.constFirst() != QStringLiteral("dft_execution")
            && path.constFirst() != QStringLiteral("flow_modules"))
            throw std::runtime_error("该设置只接受完整字段值，不能使用子字段路径。");

        QVariantMap changes;
        QVariant nestedValue = value.toVariant();
        for (qsizetype index = path.size() - 1; index > 0; --index)
            nestedValue = QVariantMap{{path.at(index), nestedValue}};
        if (path.constFirst() == QStringLiteral("metadata")) {
            changes.insert(QStringLiteral("metadata"), path.size() == 1 ? value.toVariant() : nestedValue);
        } else if (path.constFirst() == QStringLiteral("dft_execution")
                   || path.constFirst() == QStringLiteral("flow_modules")) {
            changes.insert(path.constFirst(), path.size() == 1 ? value.toVariant() : nestedValue);
        } else {
            changes.insert(path.constFirst(), value.toVariant());
        }

        QVariantMap selected = projectBySelector(selector);
        if (selected.isEmpty())
            throw std::runtime_error("请先用 /project 选择一个项目。");
        // An explicit project-set command is a user-authored configuration change.
        selected.insert(QStringLiteral("agentPermissionMode"), QStringLiteral("autonomous"));
        const QVariantMap response = unwrap(StudioNativeApi::dispatch(QStringLiteral("project/update"),
            {{QStringLiteral("project"), selected},
             {QStringLiteral("project_id"), selected.value(QStringLiteral("id"))},
             {QStringLiteral("changes"), changes}, {QStringLiteral("reason"), reason}}, m_root));
        const QVariantMap updatedProject = response.value(QStringLiteral("project")).toMap();
        if (!updatedProject.isEmpty()) {
            for (qsizetype index = 0; index < m_projects.size(); ++index) {
                QVariantMap entry = m_projects.at(index).toMap();
                if (entry.value(QStringLiteral("id")).toString() == updatedProject.value(QStringLiteral("id")).toString()) {
                    m_projects[index] = updatedProject;
                    if (m_project.value(QStringLiteral("id")).toString() == updatedProject.value(QStringLiteral("id")).toString())
                        m_project = updatedProject;
                    break;
                }
            }
        }
        const QVariantList fields = response.value(QStringLiteral("changed_fields")).toList();
        m_out << (response.value(QStringLiteral("updated")).toBool() ? "已更新 " : "未发生变化：")
              << selected.value(QStringLiteral("id")).toString() << " · "
              << (fields.isEmpty() ? field : [&fields] {
                    QStringList names;
                    for (const QVariant &item : fields)
                        names.append(item.toString());
                    return names.join(QStringLiteral(", "));
                 }()) << Qt::endl;
    }

    void listModels() {
        for (qsizetype i = 0; i < m_models.size(); ++i) {
            const QVariantMap model = m_models.at(i).toMap();
            const QString id = model.value(QStringLiteral("id")).toString();
            m_out << (i + 1) << ". " << id;
            const QString label = model.value(QStringLiteral("label")).toString();
            if (!label.isEmpty() && label != id)
                m_out << " · " << label;
            if (id == m_activeModelId)
                m_out << "  [Studio 默认]";
            if (id == m_model.value(QStringLiteral("id")).toString())
                m_out << "  [当前选择]";
            m_out << Qt::endl;
        }
    }

    void selectModel(const QString &selector) {
        const int index = selectedIndex(selector, m_models);
        if (index < 0)
            throw std::runtime_error("模型不存在；用 /models 查看模型 ID 与序号。");
        const QString id = m_models.at(index).toMap().value(QStringLiteral("id")).toString();
        const QVariantMap response = unwrap(StudioNativeApi::dispatch(
            QStringLiteral("model/run-config"), {{QStringLiteral("model_id"), id}}, m_root));
        m_model = response.value(QStringLiteral("model")).toMap();
        m_out << "当前模型：" << id << Qt::endl;
    }

    void listSessions() {
        const QVariantMap response = unwrap(StudioNativeApi::dispatch(
            QStringLiteral("session/list"), {{QStringLiteral("project"), m_project}}, m_root));
        const QVariantList sessions = response.value(QStringLiteral("sessions")).toList();
        if (sessions.isEmpty()) {
            m_out << "当前项目没有会话。" << Qt::endl;
            return;
        }
        for (qsizetype i = 0; i < sessions.size(); ++i) {
            const QVariantMap item = sessions.at(i).toMap();
            m_out << (i + 1) << ". "
                  << (item.value(QStringLiteral("archived")).toBool() ? "[归档] " : "")
                  << displayName(item) << "  [" << item.value(QStringLiteral("id")).toString() << "]"
                  << "  · " << item.value(QStringLiteral("turn_count")).toInt() << " 轮";
            if (!item.value(QStringLiteral("preview")).toString().isEmpty())
                m_out << "\n   " << oneLine(item.value(QStringLiteral("preview")).toString());
            m_out << Qt::endl;
        }
    }

    QVariantMap lookupSession(const QString &id) {
        const QVariantMap result = unwrap(StudioNativeApi::dispatch(
            QStringLiteral("session/lookup"), {{QStringLiteral("thread_id"), id}}, m_root));
        return result.value(QStringLiteral("thread")).toMap();
    }

    void selectSession(const QString &id) {
        if (id.isEmpty())
            throw std::runtime_error("请提供会话 ID。");
        const QVariantMap summary = lookupSession(id);
        const QString projectId = summary.value(QStringLiteral("project_id")).toString();
        int index = selectedIndex(projectId, m_projects);
        if (index < 0)
            throw std::runtime_error("会话所属项目不在当前项目目录中。");
        m_project = m_projects.at(index).toMap();
        const QVariantMap loaded = unwrap(StudioNativeApi::dispatch(QStringLiteral("session/read"),
            {{QStringLiteral("project"), m_project}, {QStringLiteral("thread_id"), id}}, m_root));
        m_sessionId = id;
        const QVariantMap thread = loaded.value(QStringLiteral("thread")).toMap();
        const QString savedPermission = thread.value(QStringLiteral("settings_snapshot")).toMap()
            .value(QStringLiteral("permission_mode")).toString().trimmed().toLower();
        if (savedPermission == QStringLiteral("workspace") || savedPermission == QStringLiteral("approval")
            || savedPermission == QStringLiteral("full_access"))
            m_permissionMode = savedPermission;
        m_newSession = false;
        m_newSessionName.clear();
        m_out << "已切换会话：" << displayName(loaded.value(QStringLiteral("thread")).toMap())
              << "  [" << id << "]\n";
        const QVariantList conversation = loaded.value(QStringLiteral("conversation")).toList();
        const qsizetype first = qMax<qsizetype>(0, conversation.size() - 12);
        for (qsizetype i = first; i < conversation.size(); ++i) {
            const QVariantMap message = conversation.at(i).toMap();
            const QString role = message.value(QStringLiteral("role")).toString();
            const QString text = message.value(QStringLiteral("text"), message.value(QStringLiteral("content"))).toString();
            if (!text.isEmpty())
                m_out << (role == QStringLiteral("user") ? "你：\n" : "Agent：\n") << text << "\n";
        }
    }

    void archiveSession(const QString &id) {
        if (id.isEmpty())
            throw std::runtime_error("当前没有选中的会话。");
        const QVariantMap owner = lookupSession(id);
        const QVariantMap project = projectFor(owner.value(QStringLiteral("project_id")).toString());
        unwrap(StudioNativeApi::dispatch(QStringLiteral("session/archive"),
            {{QStringLiteral("project"), project}, {QStringLiteral("thread_id"), id},
             {QStringLiteral("archived"), true}}, m_root));
        m_out << "已归档会话 " << id << Qt::endl;
    }

    void deleteSession(const QString &id) {
        if (id.isEmpty())
            throw std::runtime_error("请提供会话 ID，或先选中会话。");
        if (activeTurn(id))
            throw std::runtime_error("该会话仍有后台任务；停止任务后再删除。");
        const QVariantMap owner = lookupSession(id);
        const QVariantMap project = projectFor(owner.value(QStringLiteral("project_id")).toString());
        unwrap(StudioNativeApi::dispatch(QStringLiteral("session/delete"),
            {{QStringLiteral("project"), project}, {QStringLiteral("thread_id"), id}}, m_root));
        if (m_sessionId == id) {
            m_sessionId.clear();
            m_newSession = true;
        }
        m_out << "已永久删除会话 " << id << Qt::endl;
    }

    QVariantMap projectFor(const QString &id) const {
        const int index = selectedIndex(id, m_projects);
        if (index < 0)
            throw std::runtime_error("会话所属项目不可用。");
        return m_projects.at(index).toMap();
    }

    StudioCliTurnService *activeTurn(const QString &sessionId) const {
        const auto it = m_turns.constFind(sessionId);
        return it == m_turns.cend() || !it->service ? nullptr : it->service.data();
    }

    void startTurn(const QString &goal) {
        if (m_project.isEmpty() || m_model.isEmpty())
            throw std::runtime_error("需要先选择项目和模型。");
        auto *service = new StudioCliTurnService(this);
        StudioCliTurnService::Request request;
        request.project = m_project;
        request.agentRoot = m_root;
        request.sessionId = m_sessionId;
        request.createNewSession = m_newSession || m_sessionId.isEmpty();
        request.sessionName = request.createNewSession
            ? (m_newSessionName.isEmpty() ? goal.left(80) : m_newSessionName) : QString{};
        request.goal = goal;
        request.permissionMode = m_permissionMode;
        request.multiAgent = m_multiAgent;
        request.baseUrl = m_model.value(QStringLiteral("api_base")).toString();
        request.apiKeyFile = m_model.value(QStringLiteral("api_key_file")).toString();
        request.model = m_model.value(QStringLiteral("id")).toString();
        request.reasoningEffort = m_model.value(QStringLiteral("reasoning_effort"), QStringLiteral("medium")).toString();
        request.contextWindow = m_model.value(QStringLiteral("context_window"), 65'536).toInt();
        request.effectiveContextPercent = m_model.value(QStringLiteral("effective_context_percent"), 92).toInt();
        request.maximumOutputTokens = m_model.value(QStringLiteral("maximum_new_tokens"), 8'192).toInt();
        request.maximumToolRounds = 500;
        request.timeoutMs = m_model.value(QStringLiteral("timeout_ms"), 1'800'000).toInt();
        request.reconnectMaxAttempts = m_model.value(QStringLiteral("reconnect_max_attempts"), 10).toInt();
        request.reconnectDelayMs = qRound(m_model.value(QStringLiteral("reconnect_delay_seconds"), 1.0).toDouble() * 1'000.0);
        const auto setting = [this](const QString &key, const QVariant &fallback) {
            const QVariant value = m_model.value(key, fallback);
            return QJsonValue::fromVariant(value);
        };
        request.samplingOptions = QJsonObject{
            {QStringLiteral("temperature"), setting(QStringLiteral("temperature"), 0.6)},
            {QStringLiteral("top_k"), setting(QStringLiteral("top_k"), 40)},
            {QStringLiteral("top_p"), setting(QStringLiteral("top_p"), 0.9)},
            {QStringLiteral("min_p"), setting(QStringLiteral("min_p"), 0.05)},
            {QStringLiteral("repeat_penalty"), setting(QStringLiteral("repeat_penalty"), 1.1)},
            {QStringLiteral("repeat_last_n"), setting(QStringLiteral("repeat_last_n"), 256)},
            {QStringLiteral("dry_multiplier"), setting(QStringLiteral("dry_multiplier"), 0.5)},
            {QStringLiteral("presence_penalty"), setting(QStringLiteral("presence_penalty"), 0.0)},
            {QStringLiteral("frequency_penalty"), setting(QStringLiteral("frequency_penalty"), 0.0)},
        };
        connect(service, &StudioCliTurnService::activity, this,
            [this](const QString &sessionId, const QVariantMap &event) { showActivity(sessionId, event); });
        connect(service, &StudioCliTurnService::completed, this,
            [this, service](const QString &sessionId, const QVariantMap &result) {
                m_out << "\n[" << sessionId.left(8) << "] Agent 完成";
                const QString answer = result.value(QStringLiteral("answer")).toString();
                if (!answer.isEmpty())
                    m_out << "\n" << answer;
                m_out << Qt::endl;
                m_turns.remove(sessionId);
                service->deleteLater();
            });
        connect(service, &StudioCliTurnService::failed, this,
            [this, service](const QString &sessionId, const QVariantMap &error) {
                QString message = error.value(QStringLiteral("message")).toString();
                if (message.isEmpty())
                    message = error.value(QStringLiteral("error")).toString();
                m_err << "\n[" << sessionId.left(8) << "] Agent 失败：" << message << Qt::endl;
                m_turns.remove(sessionId);
                service->deleteLater();
            });
        const QVariantMap started = service->start(request);
        if (!started.value(QStringLiteral("ok")).toBool()) {
            service->deleteLater();
            throw std::runtime_error(started.value(QStringLiteral("message")).toString().toStdString());
        }
        m_sessionId = started.value(QStringLiteral("session_id")).toString();
        m_newSession = false;
        m_turns.insert(m_sessionId, Turn{service,
            m_project.value(QStringLiteral("id")).toString(), request.model});
        m_out << "\n已启动任务 · 会话 " << m_sessionId << " · 模型 " << request.model << Qt::endl;
    }

    void showActivity(const QString &sessionId, const QVariantMap &event) {
        const QString kind = event.value(QStringLiteral("event"), event.value(QStringLiteral("type"))).toString();
        if (kind == QStringLiteral("response.output_text.delta")) {
            const QString delta = event.value(QStringLiteral("delta")).toString();
            if (!delta.isEmpty())
                m_out << delta << Qt::flush;
            return;
        }
        const QString label = eventLabel(event);
        if (!label.isEmpty())
            m_out << "\n[" << sessionId.left(8) << "] " << label << Qt::endl;
    }

    void steer(const QString &sessionId, const QString &message) {
        if (message.trimmed().isEmpty())
            throw std::runtime_error("请提供引导内容。");
        StudioCliTurnService *turn = activeTurn(sessionId);
        if (!turn)
            throw std::runtime_error("当前会话没有正在运行的任务。");
        if (!turn->steer(message))
            throw std::runtime_error("引导未被当前任务接受。");
        m_out << "已发送实时引导。" << Qt::endl;
    }

    template<typename Operation>
    void withCurrentTurn(Operation operation, const QString &message) {
        StudioCliTurnService *turn = activeTurn(m_sessionId);
        if (!turn)
            throw std::runtime_error("当前会话没有正在运行的任务。");
        operation(turn);
        m_out << message << "。" << Qt::endl;
    }

    void decide(const QString &requestId, bool approved) {
        if (requestId.isEmpty())
            throw std::runtime_error("请提供权限请求 ID。");
        StudioCliTurnService *turn = activeTurn(m_sessionId);
        if (!turn)
            throw std::runtime_error("当前会话没有正在运行的任务。");
        const QVariantMap result = turn->decideTool(requestId, approved);
        if (!result.value(QStringLiteral("ok")).toBool())
            throw std::runtime_error(result.value(QStringLiteral("message")).toString().toStdString());
        m_out << (approved ? "已批准 " : "已拒绝 ") << requestId << Qt::endl;
    }

    void status() {
        m_out << "项目：" << displayName(m_project) << "\n"
              << "模型：" << m_model.value(QStringLiteral("id")).toString() << "\n"
              << "会话：" << (m_sessionId.isEmpty() ? QStringLiteral("新会话") : m_sessionId) << "\n"
              << "多智能体：" << (m_multiAgent ? "开启" : "关闭") << "\n"
              << "后台任务：" << m_turns.size() << Qt::endl;
        for (auto it = m_turns.cbegin(); it != m_turns.cend(); ++it)
            m_out << "  " << it.key() << (it->service && it->service->running() ? " · 运行中" : " · 停止中") << Qt::endl;
    }
};

}

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("dft-studio"));
#ifndef DFT_AGENT_STUDIO_VERSION
#define DFT_AGENT_STUDIO_VERSION "0.1.0"
#endif
    QCoreApplication::setApplicationVersion(QStringLiteral(DFT_AGENT_STUDIO_VERSION));
    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("DFT Agent Studio native interactive CLI"));
    parser.addHelpOption();
    parser.addVersionOption();
    const QCommandLineOption rootOption(QStringList{QStringLiteral("agent-root")},
        QStringLiteral("Studio checkout root."), QStringLiteral("path"));
    parser.addOption(rootOption);
    parser.addPositionalArgument(QStringLiteral("command"),
        QStringLiteral("Optional command: chat (default) or server via run-dft-studio.sh."),
        QStringLiteral("[chat]"));
    parser.process(app);
    if (parser.isSet(QStringLiteral("version"))) {
        QTextStream(stdout) << QCoreApplication::applicationName() << ' '
                            << QStringLiteral(DFT_AGENT_STUDIO_VERSION) << Qt::endl;
        return 0;
    }
    if (!parser.positionalArguments().isEmpty()
        && parser.positionalArguments().constFirst() != QStringLiteral("chat")) {
        QTextStream(stderr) << "Unknown command. Use `dft-studio chat` or `./run-dft-studio.sh server --stdio`.\n";
        return 2;
    }
    QString root = studioFindAgentRoot();
    if (parser.isSet(rootOption))
        root = QDir(parser.value(rootOption)).absolutePath();
    QString dataError;
    if (!initializeStudioUserDataRoot(root, &dataError)) {
        QTextStream(stderr) << dataError << Qt::endl;
        return 2;
    }
    Terminal terminal(root);
    if (!terminal.start())
        return 2;
    return app.exec();
}
