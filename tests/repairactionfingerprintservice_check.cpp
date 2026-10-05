#include "../src/repairactionfingerprintservice.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <cstdlib>
#include <cstdio>

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    const QVariantMap canonicalFixture{{QStringLiteral("z"), QStringLiteral("雪")},
                                       {QStringLiteral("a"), QVariantList{1, true}}};
    if (RepairActionFingerprintService::canonicalJson(canonicalFixture)
        != QStringLiteral("{\"a\": [1, true], \"z\": \"雪\"}").toUtf8())
        return EXIT_FAILURE;
    if (RepairActionFingerprintService::digest(QByteArray("abc"))
        != QStringLiteral("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"))
        return EXIT_FAILURE;

    const QString root = QStringLiteral("/tmp/fp-root");
    QDir().mkpath(root + QStringLiteral("/src"));
    QFile source(root + QStringLiteral("/src/a.v"));
    if (!source.open(QIODevice::WriteOnly)) {
        std::fprintf(stderr, "could not open fixture: %s\n", qPrintable(source.errorString()));
        return EXIT_FAILURE;
    }
    source.write("x\n");
    source.close();

    const QString relativePatch = QStringLiteral(
        "*** Begin Patch\n*** Update File: src/a.v\n@@\n-x\n+y\n*** End Patch");
    const QString absolutePatch = relativePatch;
    QString absoluteHeader = QStringLiteral("*** Update File: src/a.v");
    QString absoluteReplacement = QStringLiteral("*** Update File: %1/src/a.v").arg(root);
    QString absolutePatchFull = absolutePatch;
    absolutePatchFull.replace(absoluteHeader, absoluteReplacement);

    const QVariantMap relativeArguments{{QStringLiteral("files"), QStringList{QStringLiteral("src/a.v")}},
                                        {QStringLiteral("patch"), relativePatch}};
    const QVariantMap absoluteArguments{{QStringLiteral("files"), QStringList{root + QStringLiteral("/src/a.v")}},
                                        {QStringLiteral("patch"), absolutePatchFull}};
    const QString relativeSignature = RepairActionFingerprintService::actionSignature(
        QStringLiteral("apply_patch"), relativeArguments, root);
    const QString absoluteSignature = RepairActionFingerprintService::actionSignature(
        QStringLiteral("apply_patch"), absoluteArguments, root);
    if (relativeSignature != QStringLiteral("3eaea81967372e6aeb0cdf28e133b12fa91c3c06db40afb3c8135e849471b5a8")
        || absoluteSignature != relativeSignature) {
        std::fprintf(stderr, "patch signature mismatch: %s / %s\n", qPrintable(relativeSignature), qPrintable(absoluteSignature));
        return EXIT_FAILURE;
    }

    const QVariantMap searchArguments{{QStringLiteral("query"), QStringLiteral(" HeLLo  ")},
                                      {QStringLiteral("path"), QStringLiteral(" src/a.v ")},
                                      {QStringLiteral("path_prefix"), QStringLiteral(" src ")}};
    const QString searchSignature = RepairActionFingerprintService::actionSignature(
        QStringLiteral("search_project_text"), searchArguments, root);
    if (searchSignature != QStringLiteral("fa569dae831f79b9bfe7392f5651b1469d7f3ae79a423db82a4d161867608068")) {
        std::fprintf(stderr, "search signature mismatch: %s\n", qPrintable(searchSignature));
        return EXIT_FAILURE;
    }

    const QVariantMap createArguments{{QStringLiteral("path"), QStringLiteral(" x/y ")},
                                      {QStringLiteral("content"), QStringLiteral("abc")}};
    const QString createSignature = RepairActionFingerprintService::actionSignature(
        QStringLiteral("create_file"), createArguments);
    if (createSignature != QStringLiteral("41c3c30cc712e80290ccf9f3b272f7223423718855907acc17cce62e2c4de84b")) {
        std::fprintf(stderr, "create signature mismatch: %s\n", qPrintable(createSignature));
        return EXIT_FAILURE;
    }

    const QString relativeUnifiedDiff = QStringLiteral(
        "--- a/src/a.v\tbefore\n+++ b/src/a.v\tafter\n@@ -1 +1 @@\n-x\n+y\n");
    QString absoluteUnifiedDiff = relativeUnifiedDiff;
    absoluteUnifiedDiff.replace(QStringLiteral("a/src/a.v"), root + QStringLiteral("/src/a.v"));
    absoluteUnifiedDiff.replace(QStringLiteral("b/src/a.v"), root + QStringLiteral("/src/a.v"));
    const QVariantMap relativeUnifiedArguments{{QStringLiteral("files"), QStringList{QStringLiteral("src/a.v")}},
                                               {QStringLiteral("patch"), relativeUnifiedDiff}};
    const QVariantMap absoluteUnifiedArguments{{QStringLiteral("files"), QStringList{root + QStringLiteral("/src/a.v")}},
                                               {QStringLiteral("patch"), absoluteUnifiedDiff}};
    if (RepairActionFingerprintService::actionSignature(QStringLiteral("apply_patch"),
                                                        relativeUnifiedArguments, root)
        != RepairActionFingerprintService::actionSignature(QStringLiteral("apply_patch"),
                                                          absoluteUnifiedArguments, root)) {
        std::fprintf(stderr, "unified diff absolute/relative paths diverged\n");
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
