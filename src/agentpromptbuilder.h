#pragma once

#include <QString>

class AgentPromptBuilder final {
public:
    static QString baseInstructions();
    static QString mainInstructions();
    static QString studioLanguageInstructions(const QString &language);
    static QString memoryGuidance();
    static QString historyCompactionGuidance();
    static QString compactionHandoffPrompt();
    static QString subagentInstructions(const QString &role, const QString &task);
};
