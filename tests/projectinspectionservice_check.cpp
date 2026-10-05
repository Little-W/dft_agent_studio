#include "projectinspectionservice.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QVariantList>

#include <cstdio>

namespace {
bool require(bool condition, const char *message) {
    if (!condition)
        std::fprintf(stderr, "project inspection check failed: %s\n", message);
    return condition;
}

bool containsCandidate(const QVariantList &candidates, const QString &name, bool configured) {
    for (const QVariant &value : candidates) {
        const QVariantMap candidate = value.toMap();
        if (candidate.value(QStringLiteral("name")).toString() == name)
            return candidate.value(QStringLiteral("configured")).toBool() == configured;
    }
    return false;
}
}

int main() {
    QTemporaryDir temporary;
    if (!temporary.isValid())
        return 1;
    const QString source = QDir(temporary.path()).filePath(QStringLiteral("rtl/i2c_master_top.v"));
    if (!QDir().mkpath(QFileInfo(source).absolutePath()))
        return 1;
    QFile rtl(source);
    if (!rtl.open(QIODevice::WriteOnly | QIODevice::Text))
        return 1;
    rtl.write(R"(module i2c_master_top (
    input wb_clk_i,
    input wb_rst_i, // synchronous active high reset
    input arst_i,
    input enable
);
always @(posedge wb_clk_i) begin
  if (wb_rst_i) begin end
end
always @(posedge wb_clk_i or posedge arst_i) begin
  if (arst_i) begin end
end
endmodule
)" );
    rtl.close();

    QVariantMap project{
        {QStringLiteral("root"), temporary.path()},
        {QStringLiteral("rtl_root"), QDir(temporary.path()).filePath(QStringLiteral("rtl"))},
        {QStringLiteral("top"), QStringLiteral("i2c_master_top")},
        {QStringLiteral("metadata"), QVariantMap{
            {QStringLiteral("dft_execution"), QVariantMap{
                {QStringLiteral("clock"), QStringLiteral("wb_clk_i")},
                {QStringLiteral("reset"), QStringLiteral("arst_i")},
                {QStringLiteral("source_files"), QVariantList{QStringLiteral("rtl/i2c_master_top.v")}}}}}}};
    const QVariantMap result = ProjectInspectionService::inspect(project);
    const QVariantList resetCandidates = result.value(QStringLiteral("reset_candidates")).toList();
    const QVariantList missingResetCandidates = result.value(QStringLiteral("unconfigured_reset_candidates")).toList();
    const QVariantList clockCandidates = result.value(QStringLiteral("clock_candidates")).toList();
    return require(result.value(QStringLiteral("available")).toBool(), "inspection is available")
        && require(result.value(QStringLiteral("configured_top_found")).toBool(), "configured top exists in RTL")
        && require(containsCandidate(clockCandidates, QStringLiteral("wb_clk_i"), true), "configured clock input is found")
        && require(containsCandidate(resetCandidates, QStringLiteral("arst_i"), true), "configured asynchronous reset is found")
        && require(containsCandidate(resetCandidates, QStringLiteral("wb_rst_i"), false), "unconfigured synchronous reset input is found")
        && require(containsCandidate(missingResetCandidates, QStringLiteral("wb_rst_i"), false), "missing reset is surfaced separately")
        ? 0 : 1;
}
