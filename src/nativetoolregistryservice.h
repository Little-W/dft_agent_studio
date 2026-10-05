#pragma once

#include <QMap>
#include <QMutex>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

#include <functional>
#include <optional>

class NativeToolRegistryService final {
public:
    using ToolHandler = std::function<QVariantMap(const QVariantMap &)>;
    using EventCallback = std::function<void(const QVariantMap &)>;
    using BeforeExecute = std::function<std::optional<QVariantMap>(const QVariantMap &)>;
    using AfterExecute = std::function<QVariantMap(const QVariantMap &, const QVariantMap &)>;

    bool registerTool(const QString &name, const QString &description,
                      const QVariantMap &parameters, QString *error = nullptr,
                      const QString &risk = QStringLiteral("read"),
                      bool requiresApproval = false, ToolHandler handler = {});

    void setEventCallback(EventCallback callback);
    void setExecutionCallbacks(BeforeExecute before, AfterExecute after);
    QVariantMap request(const QString &name, const QVariantMap &arguments,
                        const QString &reason = {}, const QString &providerCallId = {},
                        std::optional<bool> requiresApprovalOverride = std::nullopt);
    QVariantList pendingRequests() const;
    QVariantMap decide(const QString &requestId, bool approved);
    QVariantMap execute(const QString &requestId);
    QVariantMap completedResults() const;
    QVariantList completedCalls() const;

    QVariantList responsesToolDefinitions() const;

    static QVariantMap responsesStrictSchema(const QVariantMap &schema);
    static QVariantMap normalizedArguments(const QVariantMap &arguments,
                                           const QVariantMap &schema);
    static bool validateArguments(const QVariantMap &arguments,
                                  const QVariantMap &schema,
                                  QString *error = nullptr);

private:
    struct ToolDefinition {
        QString description;
        QVariantMap parameters;
        QString risk;
        bool requiresApproval = false;
        ToolHandler handler;
    };

    void emitEvent(const QVariantMap &event) const;

    QMap<QString, ToolDefinition> m_tools;
    QMap<QString, QVariantMap> m_requests;
    QMap<QString, QVariantMap> m_results;
    EventCallback m_eventCallback;
    BeforeExecute m_beforeExecute;
    AfterExecute m_afterExecute;
    mutable QMutex m_mutex;
};
