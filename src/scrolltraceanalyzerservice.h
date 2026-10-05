#pragma once

#include <QJsonObject>
#include <QStringList>

struct ScrollTraceAnalysis final {
    QStringList checks;
    QString metrics;
    QString error;
    bool passed = false;
};

class ScrollTraceAnalyzerService final {
public:
    static ScrollTraceAnalysis analyze(const QJsonObject &trace, double targetHz = 165.0,
                                       double minimumFrameRatio = 1.0);
};
