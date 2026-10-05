#include "repairinputmanifestservice.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include <cstdio>

namespace {
bool writeFile(const QString &path, const QByteArray &contents) {
    if (!QDir().mkpath(QFileInfo(path).absolutePath()))
        return false;
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(contents) == contents.size();
}

bool require(bool condition, const char *message) {
    if (!condition)
        std::fprintf(stderr, "FAIL: %s\n", message);
    return condition;
}
} // namespace

int main() {
    QTemporaryDir temporary;
    if (!temporary.isValid())
        return 2;
    const QString root = temporary.filePath(QStringLiteral("project"));
    const QString outside = temporary.filePath(QStringLiteral("shared/rtl.sv"));
    const QString filelist = QDir(root).filePath(QStringLiteral("rtl/top.f"));
    const QString nestedFilelist = QDir(root).filePath(QStringLiteral("rtl/nested.f"));
    const QString header = QDir(root).filePath(QStringLiteral("include/defines.svh"));
    bool ok = true;
    ok &= require(writeFile(QDir(root).filePath(QStringLiteral("rtl/top.sv")), "module top; endmodule\n"),
                  "write project RTL source");
    ok &= require(writeFile(filelist, "-f nested.f\n+incdir+../include\n../../shared/rtl.sv\n"),
                  "write root filelist");
    ok &= require(writeFile(nestedFilelist, "top.sv\n"), "write nested filelist");
    ok &= require(writeFile(header, "`define WIDTH 8\n"), "write include header");
    ok &= require(writeFile(outside, "module shared; endmodule\n"), "write external RTL");
    ok &= require(writeFile(QDir(root).filePath(QStringLiteral("studio_data/private.json")), "{}\n"),
                  "write ignored Studio state");
    if (!ok)
        return 2;

    QVariantMap project{
        {QStringLiteral("root"), root},
        {QStringLiteral("name"), QStringLiteral("presentation only")},
        {QStringLiteral("goal"), QStringLiteral("A descriptive goal must not become a path")},
        {QStringLiteral("metadata"), QVariantMap{{QStringLiteral("dft_execution"),
            QVariantMap{{QStringLiteral("rtl_filelist"), filelist}}}}},
    };
    const QVariantMap first = RepairInputManifestService::snapshot(project, {}, root);
    ok &= require(first.value(QStringLiteral("complete")).toBool(), "complete input manifest is produced");
    const QVariantList manifest = first.value(QStringLiteral("manifest")).toList();
    auto containsPath = [&manifest](const QString &path) {
        for (const QVariant &entry : manifest) {
            const QVariantList pair = entry.toList();
            if (!pair.isEmpty() && pair.first().toString() == QFileInfo(path).canonicalFilePath())
                return true;
        }
        return false;
    };
    ok &= require(containsPath(filelist) && containsPath(nestedFilelist), "nested filelists enter the manifest");
    ok &= require(containsPath(outside) && containsPath(header), "external RTL and include headers enter the manifest");

    const QVariantMap forced = RepairInputManifestService::snapshot(project,
        {{QStringLiteral("force_rerun"), true}, {QStringLiteral("verification_reason"), QStringLiteral("reproducibility")}}, root);
    ok &= require(first.value(QStringLiteral("base")) == forced.value(QStringLiteral("base"))
                  && first.value(QStringLiteral("key")) == forced.value(QStringLiteral("key")),
                  "force and verification labels do not change source fingerprints");

    writeFile(outside, "module shared; wire changed; endmodule\n");
    const QVariantMap changed = RepairInputManifestService::snapshot(project, {}, root);
    ok &= require(first.value(QStringLiteral("base")) != changed.value(QStringLiteral("base")),
                  "changing external source bytes invalidates the baseline");
    writeFile(outside, "module shared; endmodule\n");
    writeFile(QDir(root).filePath(QStringLiteral("studio_data/private.json")), "{\"not_input\":true}\n");
    const QVariantMap onlyStudioStateChanged = RepairInputManifestService::snapshot(project, {}, root);
    ok &= require(first.value(QStringLiteral("base")) == onlyStudioStateChanged.value(QStringLiteral("base")),
                  "Studio runtime data does not invalidate circuit fingerprints");

    project.insert(QStringLiteral("repairInputPaths"), QVariantList{temporary.filePath(QStringLiteral("missing.v"))});
    const QVariantMap incomplete = RepairInputManifestService::snapshot(project, {}, root);
    ok &= require(!incomplete.value(QStringLiteral("complete")).toBool()
                  && !incomplete.value(QStringLiteral("missing")).toList().isEmpty(),
                  "missing declared input fails closed");
    return ok ? 0 : 1;
}
