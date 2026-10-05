#include "scrolltraceanalyzerservice.h"

#include <QJsonArray>
#include <QJsonValue>

#include <algorithm>
#include <cmath>
#include <limits>

namespace {
struct Point {
    double time = 0.0;
    double value = 0.0;
};

bool readNumber(const QJsonObject &object, const QString &key, double *value) {
    const QJsonValue field = object.value(key);
    if (!field.isDouble() || !std::isfinite(field.toDouble()))
        return false;
    *value = field.toDouble();
    return true;
}

QVector<double> intervals(const QVector<double> &samples) {
    QVector<double> result;
    result.reserve(qMax(0, samples.size() - 1));
    for (qsizetype i = 1; i < samples.size(); ++i) {
        const double delta = samples.at(i) - samples.at(i - 1);
        if (delta > 0.0)
            result.append(delta);
    }
    return result;
}

double percentile(QVector<double> values, double fraction) {
    if (values.isEmpty())
        return std::numeric_limits<double>::quiet_NaN();
    std::sort(values.begin(), values.end());
    const double position = (values.size() - 1) * fraction;
    const qsizetype lower = static_cast<qsizetype>(std::floor(position));
    const qsizetype upper = static_cast<qsizetype>(std::ceil(position));
    if (lower == upper)
        return values.at(lower);
    return values.at(lower) + (values.at(upper) - values.at(lower)) * (position - lower);
}

double mean(const QVector<double> &values) {
    if (values.isEmpty())
        return 0.0;
    double sum = 0.0;
    for (double value : values)
        sum += value;
    return sum / values.size();
}

QString number(double value, int precision = 2) {
    return std::isfinite(value) ? QString::number(value, 'f', precision) : QStringLiteral("nan");
}

void addCheck(ScrollTraceAnalysis *result, bool passed, const QString &description) {
    result->checks.append(QStringLiteral("%1: %2")
                              .arg(passed ? QStringLiteral("PASS") : QStringLiteral("FAIL"), description));
    result->passed = result->passed && passed;
}
} // namespace

ScrollTraceAnalysis ScrollTraceAnalyzerService::analyze(const QJsonObject &trace, double targetHz,
                                                        double minimumFrameRatio) {
    ScrollTraceAnalysis result;
    if (!std::isfinite(targetHz) || targetHz <= 0.0
        || !std::isfinite(minimumFrameRatio) || minimumFrameRatio <= 0.0) {
        result.error = QStringLiteral("Target refresh rate and minimum frame ratio must be finite and positive.");
        return result;
    }

    const QJsonArray events = trace.value(QStringLiteral("events")).toArray();
    const QJsonArray content = trace.value(QStringLiteral("content")).toArray();
    const QJsonArray frames = trace.value(QStringLiteral("frame_swapped_ms")).toArray();
    const QJsonArray animation = trace.value(QStringLiteral("after_animating_ms")).toArray();
    QVector<double> eventTimes;
    QVector<Point> points;
    QVector<double> frameSamples;
    QVector<double> animationSamples;

    for (const QJsonValue &value : events) {
        double time = 0.0;
        if (!value.isObject() || !readNumber(value.toObject(), QStringLiteral("t_ms"), &time)) {
            result.error = QStringLiteral("Each wheel event must contain a finite numeric t_ms.");
            return result;
        }
        eventTimes.append(time);
    }
    for (const QJsonValue &value : content) {
        Point point;
        if (!value.isObject() || !readNumber(value.toObject(), QStringLiteral("t_ms"), &point.time)
            || !readNumber(value.toObject(), QStringLiteral("y"), &point.value)) {
            result.error = QStringLiteral("Each content sample must contain finite numeric t_ms and y fields.");
            return result;
        }
        points.append(point);
    }
    auto readTimes = [&result](const QJsonArray &array, QVector<double> *output, const QString &label) {
        for (const QJsonValue &value : array) {
            if (!value.isDouble() || !std::isfinite(value.toDouble())) {
                result.error = QStringLiteral("%1 must contain only finite numeric timestamps.").arg(label);
                return false;
            }
            output->append(value.toDouble());
        }
        return true;
    };
    if (!readTimes(frames, &frameSamples, QStringLiteral("frame_swapped_ms"))
        || !readTimes(animation, &animationSamples, QStringLiteral("after_animating_ms")))
        return result;

    if (events.isEmpty())
        result.checks.append(QStringLiteral("FAIL: no wheel events were captured"));
    if (content.isEmpty())
        result.checks.append(QStringLiteral("FAIL: no contentY changes were captured"));
    if (frames.isEmpty())
        result.checks.append(QStringLiteral("FAIL: no frameSwapped samples were captured"));
    result.passed = events.size() == 5 && !content.isEmpty() && !frames.isEmpty();

    qsizetype firstMotion = -1;
    for (qsizetype i = 1; i < points.size(); ++i) {
        if (std::abs(points.at(i).value - points.at(i - 1).value) > 0.05) {
            firstMotion = i;
            break;
        }
    }
    const double latency = firstMotion >= 0 && !eventTimes.isEmpty()
        ? points.at(firstMotion).time - eventTimes.first()
        : std::numeric_limits<double>::quiet_NaN();
    QVector<double> contentTimes;
    contentTimes.reserve(points.size());
    for (const Point &point : points)
        contentTimes.append(point.time);

    const double activeEnd = !points.isEmpty() ? points.last().time
        : (!eventTimes.isEmpty() ? eventTimes.last() : 0.0);
    auto activeSamples = [&eventTimes, activeEnd](const QVector<double> &samples) {
        QVector<double> selected;
        if (eventTimes.isEmpty())
            return selected;
        for (double time : samples) {
            if (time >= eventTimes.first() - 1.0 && time <= activeEnd + 12.0)
                selected.append(time);
        }
        return selected;
    };
    const QVector<double> frameDeltas = intervals(activeSamples(frameSamples));
    const QVector<double> contentDeltas = intervals(contentTimes);
    const QVector<double> animationDeltas = intervals(activeSamples(animationSamples));
    int direction = 0;
    int reversals = 0;
    for (qsizetype i = 1; i < points.size(); ++i) {
        const double delta = points.at(i).value - points.at(i - 1).value;
        if (std::abs(delta) <= 0.05)
            continue;
        const int nextDirection = delta > 0.0 ? 1 : -1;
        if (direction && nextDirection != direction)
            ++reversals;
        direction = nextDirection;
    }
    const double netDistance = points.size() > 1 ? points.last().value - points.first().value : 0.0;
    const double framePeriod = 1000.0 / targetHz;
    const double frameHz = frameDeltas.isEmpty() ? 0.0 : 1000.0 / mean(frameDeltas);
    const double contentHz = contentDeltas.isEmpty() ? 0.0 : 1000.0 / mean(contentDeltas);
    double maximumFrameDelta = std::numeric_limits<double>::infinity();
    if (!frameDeltas.isEmpty())
        maximumFrameDelta = -std::numeric_limits<double>::infinity();
    for (double delta : frameDeltas)
        maximumFrameDelta = qMax(maximumFrameDelta, delta);
    const double frameP95 = percentile(frameDeltas, 0.95);
    const double contentP95 = percentile(contentDeltas, 0.95);
    const double requiredHz = targetHz * minimumFrameRatio;
    int acceptedCount = 0;
    for (const QJsonValue &value : events)
        acceptedCount += value.toObject().value(QStringLiteral("accepted")).toBool() ? 1 : 0;

    addCheck(&result, events.size() == 5,
             QStringLiteral("wheel events delivered (%1/5 expected)").arg(events.size()));
    addCheck(&result, points.size() > 2 && std::abs(netDistance) > 1.0,
             QStringLiteral("scroll content moved (%1px net)").arg(number(netDistance, 1)));
    addCheck(&result, frameHz >= requiredHz,
             QStringLiteral("presentation cadence %1Hz >= %2Hz").arg(number(frameHz, 1), number(requiredHz, 1)));
    addCheck(&result, frameP95 <= framePeriod * 1.25,
             QStringLiteral("frame interval p95 %1ms <= %2ms").arg(number(frameP95), number(framePeriod * 1.25)));
    addCheck(&result, maximumFrameDelta <= framePeriod * 2.0,
             QStringLiteral("no presentation gap exceeds two frames (%1ms)").arg(number(maximumFrameDelta)));
    addCheck(&result, contentP95 <= framePeriod * 1.25,
             QStringLiteral("content interval p95 %1ms <= %2ms").arg(number(contentP95), number(framePeriod * 1.25)));
    addCheck(&result, latency <= framePeriod * 2.0,
             QStringLiteral("first input-to-motion latency %1ms <= %2ms").arg(number(latency), number(framePeriod * 2.0)));
    addCheck(&result, reversals == 0,
             QStringLiteral("no content direction reversals (%1)").arg(reversals));

    result.metrics = QStringLiteral(
        "metrics: frame_hz=%1 frame_dt_ms[p50=%2 p95=%3 max=%4] "
        "content_hz=%5 content_dt_ms[p50=%6 p95=%7 max=%8] "
        "afterAnimating_hz=%9 input_to_motion_ms=%10 distance_px=%11 reversals=%12 "
        "wheel_event_accepted_flags=%13/%14")
        .arg(number(frameHz, 1), number(percentile(frameDeltas, 0.50)), number(frameP95), number(maximumFrameDelta))
        .arg(number(contentHz, 1), number(percentile(contentDeltas, 0.50)), number(contentP95),
             number(contentDeltas.isEmpty() ? std::numeric_limits<double>::quiet_NaN()
                                            : *std::max_element(contentDeltas.cbegin(), contentDeltas.cend())))
        .arg(number(animationDeltas.isEmpty() ? 0.0 : 1000.0 / mean(animationDeltas), 1), number(latency),
             number(netDistance, 1))
        .arg(reversals).arg(acceptedCount).arg(events.size());
    return result;
}
