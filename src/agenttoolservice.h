#pragma once

#include <QString>
#include <QVariantList>
#include <QVariantMap>

#include "responsesturnrunner.h"

#include <functional>

class AgentToolService final {
public:
    using ToolEventCallback = std::function<void(const QVariantMap &)>;
    using ToolCompletionCallback = std::function<void(const QVariantMap &)>;

    static bool supports(const QString &action);
    static QVariantMap dispatch(const QString &action, const QVariantMap &project,
                                const QVariantMap &arguments, const QString &agentRoot);
    static QVariantList nativeToolCatalog(const QVariantMap &project,
                                          const QStringList &disabledTools = {});
    static QVariantMap initializeToolRuntime(const QString &runtimeId,
                                             const QVariantMap &project,
                                             const QString &agentRoot,
                                             const QStringList &disabledTools = {},
                                             ToolEventCallback eventCallback = {});
    static QVariantMap initializeToolRuntime(const QString &runtimeId,
                                             const QVariantMap &project,
                                             const QVariantList &definitions,
                                             const QString &agentRoot,
                                             ToolEventCallback eventCallback = {});
    static QVariantMap configureSubagentRuntime(const QString &runtimeId,
                                                const QString &parentSessionId,
                                                const ResponsesTurnRunner::Request &turnRequest);
    static QVariantList responsesToolDefinitions(const QString &runtimeId);
    static QVariantMap requestTool(const QString &runtimeId, const QString &name,
                                   const QVariantMap &arguments,
                                   const QString &reason = {},
                                   const QString &providerCallId = {});
    static QVariantMap decideTool(const QString &runtimeId, const QString &requestId, bool approved);
    static QVariantMap executeTool(const QString &runtimeId, const QString &requestId);
    static void dispatchToolAsync(const QString &runtimeId, const QString &name,
                                  const QVariantMap &arguments, const QString &providerCallId,
                                  ToolCompletionCallback completion);
    static QVariantList pendingToolRequests(const QString &runtimeId);
    static QVariantMap completedToolResults(const QString &runtimeId);
    static QVariantList completedToolCalls(const QString &runtimeId);
    static bool closeToolRuntime(const QString &runtimeId);
};
