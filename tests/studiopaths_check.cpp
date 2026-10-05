#include "studiopaths.h"
#include <QCoreApplication>
#include <QFile>
#include <QTemporaryDir>
#include <cstdio>

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    QTemporaryDir temp;
    if (!temp.isValid()) return 1;
    const QString root = temp.path();
    const QString data = QDir(root).filePath("studio_data");
    qputenv("DFT_STUDIO_DATA_DIR", QByteArrayLiteral("studio_data0"));
    qputenv("DFT_STUDIO_PROJECTS_FILE", QByteArrayLiteral("/tmp/not-used-projects.json"));
    if (studioDataRoot(root) != data) return 2;
    if (studioConfigPath("projects.json", root) != QDir(data).filePath("config/projects.json")) return 3;
    if (studioConfigPath("models.json", root) != QDir(data).filePath("config/models.json")) return 4;
    if (studioConfigPath("gui_capabilities.json", root) != QDir(data).filePath("config/gui_capabilities.json")) return 5;
    if (studioUiSettingsPath(root) != QDir(data).filePath("config/ui_settings.ini")) return 6;
    QFile binding(QDir(root).filePath(".dft-studio-storage.json"));
    if (!binding.open(QIODevice::WriteOnly)) return 7;
    binding.write(R"({"schema_version":1,"data_root":"studio_data0"})");
    binding.close();
    if (studioDataRoot(root) != data) return 8;
    QDir().mkpath(QDir(data).filePath("config"));
    QFile projects(studioConfigPath("projects.json", root));
    if (!projects.open(QIODevice::WriteOnly)) return 9;
    projects.write(R"({"projects":[{"id":"p1","root":"/external/rtl"}]})");
    projects.close();
    QString validationError;
    if (studioValidateCatalogue(projects.fileName(), "projects", &validationError) != 1) return 10;
    const QString target = QDir(data).filePath("agent_runtime/patches/a.diff");
    QDir().mkpath(QFileInfo(target).absolutePath());
    QFile patch(target);
    if (!patch.open(QIODevice::WriteOnly) || patch.write("test diff\n") <= 0) return 11;
    patch.close();
    const QString old = QDir(root).filePath("artifacts/agent_runtime/patches/a.diff");
    if (resolveStudioRecordPath(old, root) != target) return 12;
    std::puts("DFT Studio single-root storage checks passed.");
    return 0;
}
