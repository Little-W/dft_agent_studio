#include "scrolltraceanalyzerservice.h"

#include <QJsonArray>
#include <QJsonObject>

#include <cstdio>

namespace {
bool require(bool condition, const char *message) {
    if (!condition)
        std::fprintf(stderr, "FAIL: %s\n", message);
    return condition;
}

QJsonObject validTrace() {
    QJsonArray events;
    for (int i = 0; i < 5; ++i)
        events.append(QJsonObject{{QStringLiteral("t_ms"), i * 10.0},
                                  {QStringLiteral("accepted"), true}});
    QJsonArray content;
    QJsonArray frames;
    QJsonArray animation;
    for (int i = 0; i <= 30; ++i) {
        const double time = i * 1000.0 / 165.0;
        content.append(QJsonObject{{QStringLiteral("t_ms"), time},
                                   {QStringLiteral("y"), i * 2.0}});
        frames.append(time);
        animation.append(time);
    }
    return {{QStringLiteral("events"), events}, {QStringLiteral("content"), content},
            {QStringLiteral("frame_swapped_ms"), frames}, {QStringLiteral("after_animating_ms"), animation}};
}
} // namespace

int main() {
    bool ok = true;
    const ScrollTraceAnalysis valid = ScrollTraceAnalyzerService::analyze(validTrace());
    ok &= require(valid.error.isEmpty(), "valid trace is parsed");
    ok &= require(valid.passed, "165 Hz trace passes all acceptance checks");
    ok &= require(valid.metrics.contains(QStringLiteral("frame_hz=165.0")), "frame cadence is reported");

    QJsonObject malformed = validTrace();
    QJsonArray malformedContent;
    malformedContent.append(QJsonObject{{QStringLiteral("t_ms"), QStringLiteral("bad")}});
    malformed.insert(QStringLiteral("content"), malformedContent);
    const ScrollTraceAnalysis invalid = ScrollTraceAnalyzerService::analyze(malformed);
    ok &= require(!invalid.error.isEmpty() && !invalid.passed, "malformed sample is rejected explicitly");

    const ScrollTraceAnalysis badRate = ScrollTraceAnalyzerService::analyze(validTrace(), 0.0, 1.0);
    ok &= require(!badRate.error.isEmpty(), "invalid target rate is rejected");
    return ok ? 0 : 1;
}
