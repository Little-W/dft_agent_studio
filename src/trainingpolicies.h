#pragma once

#include <QSet>
#include <QString>

inline const QSet<QString> &supportedTrainingPolicies() {
    static const QSet<QString> policies{
        QStringLiteral("supervisor_feedback_v1"),
        QStringLiteral("role_reinforcement_v1"),
        QStringLiteral("workflow_execution_v1"),
        QStringLiteral("dft_tool_reasoning_v1"),
        QStringLiteral("iteration_problem_solving_v1"),
        QStringLiteral("workspace_agent_v1"),
        QStringLiteral("tool_trajectory_v1"),
        QStringLiteral("workspace_trajectory_v1"),
        QStringLiteral("tcl_config_import_v1"),
    };
    return policies;
}
