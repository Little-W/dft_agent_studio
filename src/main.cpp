#include "agentcontroller.h"
#include "studiopaths.h"
#include "studiouserdata.h"
#include "chatdisplaymodel.h"
#include "capabilitymodel.h"
#include "fileeditor.h"
#include "languagesettings.h"
#include "latexmathimageprovider.h"
#include "konsoleterminalitem.h"
#include "modelcatalog.h"
#include "projectconfigimporter.h"
#include "projectmodel.h"
#include "syntaxhighlighter.h"
#include "wheelzoomitem.h"
#include "wheelscrollanimator.h"

#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QIcon>
#include <QApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QFont>
#include <QFontDatabase>
#include <QMessageBox>
#include <QLibraryInfo>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlError>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QQuickItem>
#include <QScreen>
#include <QSurfaceFormat>
#include <QTimer>
#include <QVariant>
#include <QWheelEvent>
#include <QMutex>
#include <QMutexLocker>
#include <QPointer>
#include <qqml.h>

#include <cstdio>
#include <functional>
#include <atomic>

class PropertySignalSampler final : public QObject {
    Q_OBJECT
public:
    std::function<void()> callback;

public slots:
    void sample() { if (callback) callback(); }
};

int main(int argc, char *argv[]) {
    // The UI uses its own FlatIcon glyphs. Select an installed fallback theme
    // so KDE's platform integration does not probe a missing AdwaitaLegacy.
    qputenv("QT_ICON_THEME", QByteArrayLiteral("hicolor"));
    qputenv("KDE_ICON_THEME", QByteArrayLiteral("hicolor"));
    qputenv("QT_QPA_PLATFORMTHEME", QByteArrayLiteral("generic"));
    if (!qEnvironmentVariableIsSet("QSG_RENDER_LOOP"))
        qputenv("QSG_RENDER_LOOP", QByteArrayLiteral("threaded"));
    if (!qEnvironmentVariableIsSet("QSG_USE_SIMPLE_ANIMATION_DRIVER")
        && qEnvironmentVariable("QT_QUICK_BACKEND") != QStringLiteral("software"))
        qputenv("QSG_USE_SIMPLE_ANIMATION_DRIVER", QByteArrayLiteral("1"));
    QSurfaceFormat surfaceFormat = QSurfaceFormat::defaultFormat();
    surfaceFormat.setSwapInterval(1);
    QSurfaceFormat::setDefaultFormat(surfaceFormat);
    QApplication application(argc, argv);
    QIcon::setThemeName(QStringLiteral("hicolor"));
    QCoreApplication::setOrganizationName("DFT Agent Studio");
    QCoreApplication::setApplicationName("DFT Agent Studio");
    QCoreApplication::setApplicationVersion(QStringLiteral(DFT_AGENT_STUDIO_VERSION));
    application.setWindowIcon(QIcon(QStringLiteral(":/icons/dft-agent-studio.png")));
    const int misansFontId = QFontDatabase::addApplicationFont(QStringLiteral(":/fonts/MiSans VF.ttf"));
    if (misansFontId >= 0) {
        const QStringList families = QFontDatabase::applicationFontFamilies(misansFontId);
        if (!families.isEmpty()) {
            QFont uiFont(families.constFirst());
            uiFont.setStyleHint(QFont::SansSerif);
            uiFont.setStyleStrategy(QFont::PreferAntialias);
            application.setFont(uiFont);
        }
    } else {
        std::fprintf(stderr, "Could not load the embedded MiSans font; using the system default.\n");
    }
    QQuickStyle::setStyle("Basic");
    qmlRegisterType<SyntaxHighlighter>("DftAgentStudio", 1, 0, "SyntaxHighlighter");
    qmlRegisterType<KonsoleTerminalItem>("DftAgentStudio", 1, 0, "KonsoleTerminal");
    qmlRegisterType<WheelZoomItem>("DftAgentStudio", 1, 0, "WheelZoomItem");
    qmlRegisterType<WheelScrollAnimator>("DftAgentStudio", 1, 0, "WheelScrollAnimator");

    QCommandLineParser parser;
    parser.setApplicationDescription("A local, evidence-reviewed DFT Agent control center");
    parser.addHelpOption();
    const QCommandLineOption workingDirectoryOption("agent-root", "DFT Agent project root used for project files and runtime configuration.", "path");
    const QCommandLineOption storageInfoOption("storage-info", "Print resolved storage paths and counts without starting the Studio window.");
    const QCommandLineOption demoOption("demo", "Use simulated Agent events without starting a live agent run.");
    const QCommandLineOption demoRunOption("demo-run", "Start the simulated Agent after the QML window is ready.");
    const QCommandLineOption selectProjectOption("select-project", "Select a project without starting the Agent; intended for visual QA.", "id");
    const QCommandLineOption runProjectOption("run-project", "Select and start a project through the GUI controller.", "id");
    const QCommandLineOption pageOption("page", "Open a UI page by index (0-12); intended for visual QA.", "index");
    const QCommandLineOption sidebarCollapsedOption("sidebar-collapsed", "Start with the sidebar collapsed; intended for visual QA.");
    const QCommandLineOption sidebarProjectsCollapsedOption("sidebar-projects-collapsed", "Start with the left sidebar project entries collapsed; intended for visual QA.");
    const QCommandLineOption relatedFilesCollapsedOption("related-files-collapsed", "Start with the related-files bar collapsed; intended for visual QA.");
    const QCommandLineOption expandFlowStagesOption("expand-flow-stages", "Expand all flow stages; intended for visual QA.");
    const QCommandLineOption showContextUsageOption("show-context-usage", "Open the context usage details; intended for visual QA.");
    const QCommandLineOption detailedModeOption("detailed-mode", "Open the live IC tool output sidebar; intended for visual QA.");
    const QCommandLineOption terminalInputOption("terminal-input", "Send text to the embedded terminal after startup; intended for visual QA.", "text");
    const QCommandLineOption openFileOption("open-file", "Open a project file in the editor; intended for visual QA.", "path");
    const QCommandLineOption chatPromptOption("chat-prompt", "Submit a Chat prompt after startup; intended for acceptance QA.", "text");
    const QCommandLineOption captureOption("capture", "Capture the rendered Qt Quick window to a PNG file, then exit.", "path");
    const QCommandLineOption captureDelayOption("capture-delay", "Milliseconds to wait before a --capture image is taken.", "milliseconds", "1000");
    const QCommandLineOption chatScrollTestOption("chat-scroll-test", "Scroll Chat to the history boundary for nested Wayland acceptance QA.");
    const QCommandLineOption profileFramesOption("profile-frames", "Log four one-second Qt Quick frame-swap samples, then exit.");
    const QCommandLineOption scrollTraceOption("scroll-trace", "Send a mouse-wheel burst over Chat and save input/content/frame timestamps as JSON; intended for virtual-display QA.", "path");
    const QCommandLineOption scrollTargetOption("scroll-target", "Object name of a scrollable Flickable used by --scroll-trace.", "objectName", "codexChatActivityList");
    const QCommandLineOption qaOpenDialogOption("qa-open-dialog", "Open a named dialog after startup; intended for UI QA.", "objectName");
    parser.addOption(storageInfoOption);
    parser.addOption(workingDirectoryOption);
    parser.addOption(demoOption);
    parser.addOption(demoRunOption);
    parser.addOption(selectProjectOption);
    parser.addOption(runProjectOption);
    parser.addOption(pageOption);
    parser.addOption(sidebarCollapsedOption);
    parser.addOption(sidebarProjectsCollapsedOption);
    parser.addOption(relatedFilesCollapsedOption);
    parser.addOption(expandFlowStagesOption);
    parser.addOption(showContextUsageOption);
    parser.addOption(detailedModeOption);
    parser.addOption(terminalInputOption);
    parser.addOption(openFileOption);
    parser.addOption(chatPromptOption);
    parser.addOption(captureOption);
    parser.addOption(captureDelayOption);
    parser.addOption(chatScrollTestOption);
    parser.addOption(profileFramesOption);
    parser.addOption(scrollTraceOption);
    parser.addOption(scrollTargetOption);
    parser.addOption(qaOpenDialogOption);
    parser.process(application);

    try {
    const QString requestedRoot = parser.isSet(workingDirectoryOption)
        ? parser.value(workingDirectoryOption) : studioFindAgentRoot();
    const QString canonicalAgentRoot = QFileInfo(requestedRoot).canonicalFilePath();
    const QString agentRoot = canonicalAgentRoot.isEmpty()
        ? QFileInfo(requestedRoot).absoluteFilePath() : canonicalAgentRoot;
    application.setProperty("dftAgentRoot", agentRoot);
    QString dataInitializationError;
    if (!initializeStudioUserDataRoot(agentRoot, &dataInitializationError)) {
        std::fprintf(stderr, "Cannot initialize Studio user data: %s\n", dataInitializationError.toUtf8().constData());
        return 2;
    }
    const QString dataRoot = studioDataRoot(agentRoot);
    const QString expectedDataRoot = QDir::home().filePath(QStringLiteral(".dft_agent_studio"));
    if (QDir::cleanPath(dataRoot) != QDir::cleanPath(expectedDataRoot)) {
        std::fprintf(stderr, "Studio data root must be ~/.dft_agent_studio.\n");
        return 2;
    }
    const QString projectRegistryPath = studioConfigPath(QStringLiteral("projects.json"), agentRoot);
    const QString modelCatalogPath = studioConfigPath(QStringLiteral("models.json"), agentRoot);
    const QString capabilitiesPath = studioConfigPath(QStringLiteral("gui_capabilities.json"), agentRoot);
    const QString uiSettingsPath = studioUiSettingsPath(agentRoot);
    QDir().mkpath(QFileInfo(uiSettingsPath).absolutePath());
    // Child workers inherit only the canonical Studio paths.
    qputenv("DFT_STUDIO_DATA_DIR", dataRoot.toUtf8());
    qputenv("DFT_STUDIO_PROJECTS_FILE", projectRegistryPath.toUtf8());
    qputenv("DFT_STUDIO_MODELS_FILE", modelCatalogPath.toUtf8());
    qputenv("DFT_STUDIO_CAPABILITIES_FILE", capabilitiesPath.toUtf8());
    QString configError;
    const int projectCount = studioValidateCatalogue(projectRegistryPath, QStringLiteral("projects"), &configError);
    const int modelCount = projectCount < 0 ? -1
        : studioValidateCatalogue(modelCatalogPath, QStringLiteral("models"), &configError);
    int capabilitiesCount = 0;
    if (projectCount >= 0 && modelCount >= 0 && QFileInfo::exists(capabilitiesPath))
        capabilitiesCount = studioValidateCatalogue(capabilitiesPath, QStringLiteral("disabled"), &configError);
    const bool hiddenHistory = projectCount == 0 && studioHasSessionRecords(dataRoot);
    if (hiddenHistory)
        configError = QStringLiteral("检测到会话正文，但当前项目注册表为空。需要恢复原项目 ID/目录关联，不能据此判定历史被删除。");
    if (projectCount < 0 || modelCount < 0 || capabilitiesCount < 0 || hiddenHistory) {
        const QString message = QStringLiteral(
            "%1\n\nStudio 数据目录：%2\n项目注册表：%3\n模型配置：%4\n"
            "当前版本只读取 ~/.dft_agent_studio/config，不会回退到项目目录或旧环境变量。\n"
            "请确认迁移后的配置文件已经存在于上述固定路径。")
                .arg(configError, dataRoot, projectRegistryPath, modelCatalogPath);
        std::fprintf(stderr, "%s\n", message.toUtf8().constData());
        if (!parser.isSet(storageInfoOption))
            QMessageBox::critical(nullptr, QStringLiteral("Studio 存储读取失败"), message);
        return 2;
    }
    const QJsonObject storageInfo{{"agent_root", agentRoot}, {"data_root", dataRoot},
        {"projects_file", projectRegistryPath}, {"models_file", modelCatalogPath},
        {"capabilities_file", capabilitiesPath}, {"projects", projectCount}, {"models", modelCount},
        {"ui_settings_file", uiSettingsPath}, {"ui_settings_file_exists", QFileInfo::exists(uiSettingsPath)}};
    if (parser.isSet(storageInfoOption)) {
        std::printf("%s\n", QJsonDocument(storageInfo).toJson(QJsonDocument::Indented).constData());
        return 0;
    }
    std::fprintf(stderr, "DFT Studio storage: %s\n", QJsonDocument(storageInfo).toJson(QJsonDocument::Compact).constData());
    ProjectModel projectModel(projectRegistryPath);
    ModelCatalog modelCatalog(modelCatalogPath);
    CapabilityModel capabilityModel(capabilitiesPath);
    AgentController agentController(agentRoot);
    ChatDisplayModel chatDisplayModel;
    agentController.setModelCatalogPath(modelCatalogPath);
    ProjectConfigImporter projectConfigImporter(agentRoot);
    projectConfigImporter.setModelCatalogPath(modelCatalogPath);
    const auto synchronizeContextSettings = [&modelCatalog, &agentController] {
        const auto model = modelCatalog.modelAt(modelCatalog.activeModelIndex());
        agentController.configureContextUsage(
            model.value(QStringLiteral("contextWindow"), 65'536).toInt(),
            model.value(QStringLiteral("inputContextTokens"), 16'384).toInt(),
            model.value(QStringLiteral("maximumNewTokens"), 4'096).toInt()
        );
    };
    QObject::connect(
        &modelCatalog,
        &ModelCatalog::catalogChanged,
        &agentController,
        synchronizeContextSettings
    );
    synchronizeContextSettings();
    LanguageSettings languageSettings;
    FileEditor fileEditor;
    agentController.setDemoMode(parser.isSet(demoOption) || parser.isSet(demoRunOption) || agentController.demoMode());

    QQmlApplicationEngine engine;
    engine.addImageProvider(QStringLiteral("latex"), new LatexMathImageProvider);
    QObject::connect(&engine, &QQmlApplicationEngine::warnings, [](const QList<QQmlError> &warnings) {
        for (const auto &warning : warnings)
            std::fprintf(stderr, "QML warning: %s\\n", qPrintable(warning.toString()));
    });
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, [](const QUrl &url) {
        std::fprintf(stderr, "Failed to create QML root object: %s\\n", qPrintable(url.toString()));
    });
    engine.rootContext()->setContextProperty("projectModel", &projectModel);
    engine.rootContext()->setContextProperty("modelCatalog", &modelCatalog);
    engine.rootContext()->setContextProperty("capabilityModel", &capabilityModel);
    engine.rootContext()->setContextProperty("agentController", &agentController);
    engine.rootContext()->setContextProperty("chatDisplayModel", &chatDisplayModel);
    engine.rootContext()->setContextProperty("projectConfigImporter", &projectConfigImporter);
    engine.rootContext()->setContextProperty("languageSettings", &languageSettings);
    engine.rootContext()->setContextProperty("fileEditor", &fileEditor);
    engine.rootContext()->setContextProperty("qtRuntimeVersion", QLibraryInfo::version().toString());
    engine.loadFromModule("DftAgentStudio", "Main");
    if (engine.rootObjects().isEmpty())
        return 1;
    QObject *rootObject = engine.rootObjects().constFirst();
    if (parser.isSet(qaOpenDialogOption) && !parser.isSet(scrollTraceOption)) {
        const QString dialogName = parser.value(qaOpenDialogOption);
        QTimer::singleShot(900, rootObject, [rootObject, dialogName] {
            QVariant opened;
            if (!QMetaObject::invokeMethod(rootObject, "qaOpenDialog", Qt::DirectConnection,
                    Q_RETURN_ARG(QVariant, opened), Q_ARG(QVariant, dialogName))
                || !opened.toBool())
                std::fprintf(stderr, "UI QA could not open dialog=%s.\n", qPrintable(dialogName));
        });
    }
    if (parser.isSet(scrollTraceOption)) {
        auto *quickWindow = qobject_cast<QQuickWindow *>(rootObject);
        const QString tracePath = parser.value(scrollTraceOption);
        const QString scrollTargetName = parser.value(scrollTargetOption);
        const QString dialogName = parser.value(qaOpenDialogOption);
        if (!quickWindow) {
            std::fprintf(stderr, "Could not start scroll trace without a QQuickWindow.\n");
        } else {
            if (!dialogName.isEmpty()) {
                QTimer::singleShot(1200, rootObject, [rootObject, dialogName] {
                    QVariant opened;
                    if (!QMetaObject::invokeMethod(rootObject, "qaOpenDialog", Qt::DirectConnection,
                            Q_RETURN_ARG(QVariant, opened), Q_ARG(QVariant, dialogName))
                        || !opened.toBool())
                        std::fprintf(stderr, "Scroll QA could not open dialog=%s.\n", qPrintable(dialogName));
                });
            }
            if (scrollTargetName == QStringLiteral("codexChatActivityList")) {
                QTimer::singleShot(5000, rootObject, [rootObject] {
                    const QVariantList entries = rootObject->property("codexChatDisplayEntries").toList();
                    for (const QVariant &entry : entries) {
                        const QVariantMap item = entry.toMap();
                        if (item.value(QStringLiteral("kind")).toString() != QStringLiteral("activity_group")
                            && item.value(QStringLiteral("kind")).toString() != QStringLiteral("tool_group"))
                            continue;
                        QVariant key;
                        if (QMetaObject::invokeMethod(rootObject, "activityGroupKey",
                                Q_RETURN_ARG(QVariant, key), Q_ARG(QVariant, entry)))
                            rootObject->setProperty("codexExpandedActivityGroupKey", key);
                        break;
                    }
                });
                QTimer::singleShot(5300, rootObject, [rootObject] {
                    auto *list = rootObject->findChild<QQuickItem *>(QStringLiteral("codexChatActivityList"));
                    if (list)
                        list->setProperty("contentY", list->property("originY"));
                });
            }
            auto *elapsed = new QElapsedTimer;
            elapsed->start();
            auto *trace = new QObject(&application);
            auto *active = new std::atomic_bool(false);
            trace->setProperty("eventIndex", 0);
            auto *events = new QJsonArray;
            auto *contentSamples = new QJsonArray;
            auto *animationSamples = new QJsonArray;
            auto *frameSamples = new QJsonArray;
            auto *samplesMutex = new QMutex;
            auto *scrollItem = new QPointer<QQuickItem>;
            QTimer::singleShot(5700, &application,
                [quickWindow, trace, active, elapsed, events, contentSamples, animationSamples,
                 frameSamples, samplesMutex, scrollItem, tracePath, scrollTargetName] {
                    *scrollItem = quickWindow->findChild<QQuickItem *>(scrollTargetName);
                    if (!*scrollItem) {
                        std::fprintf(stderr, "Scroll trace could not find objectName=%s.\n", qPrintable(scrollTargetName));
                        QCoreApplication::exit(2);
                        return;
                    }
                    auto *item = scrollItem->data();
                    if (!item->isVisible() || item->width() <= 0.0 || item->height() <= 0.0) {
                        std::fprintf(stderr, "Scroll trace target %s is not visible or has no viewport; refusing invalid QA data.\n",
                            qPrintable(scrollTargetName));
                        QCoreApplication::exit(2);
                        return;
                    }
                    if (!item->property("contentY").isValid()) {
                        std::fprintf(stderr, "Scroll trace target %s has no vertical Flickable contentY.\n", qPrintable(scrollTargetName));
                        QCoreApplication::exit(2);
                        return;
                    }
                    const qreal minimumY = item->property("originY").toReal();
                    const qreal maximumY = qMax(minimumY,
                        minimumY + item->property("contentHeight").toReal() - item->height());
                    item->setProperty("contentY", minimumY);
                    std::fprintf(stderr, "scroll trace target: list=%dx%d content_height=%.1f range=%.1f refresh_hz=%.2f\n",
                        qRound(item->width()), qRound(item->height()), item->property("contentHeight").toReal(),
                        maximumY - minimumY, quickWindow->screen() ? quickWindow->screen()->refreshRate() : 0.0);
                    active->store(true, std::memory_order_relaxed);
                    auto *contentSampler = new PropertySignalSampler;
                    contentSampler->callback = [item, active, elapsed, contentSamples] {
                        if (!active->load(std::memory_order_relaxed)) return;
                        contentSamples->append(QJsonObject{{"t_ms", elapsed->elapsed()},
                            {"y", item->property("contentY").toReal()}});
                    };
                    QObject::connect(item, SIGNAL(contentYChanged()), contentSampler, SLOT(sample()));
                    QObject::connect(quickWindow, &QQuickWindow::afterAnimating, trace,
                        [active, elapsed, animationSamples, samplesMutex] {
                            if (active->load(std::memory_order_relaxed)) {
                                QMutexLocker lock(samplesMutex);
                                animationSamples->append(elapsed->elapsed());
                            }
                        }, Qt::DirectConnection);
                    QObject::connect(quickWindow, &QQuickWindow::frameSwapped, trace,
                        [active, elapsed, frameSamples, samplesMutex] {
                            if (!active->load(std::memory_order_relaxed)) return;
                            QMutexLocker lock(samplesMutex);
                            frameSamples->append(elapsed->elapsed());
                        }, Qt::DirectConnection);

                    const QPointF local(item->width() * 0.08, item->height() * 0.5);
                    const QPointF position = item->mapToItem(quickWindow->contentItem(), local);
                    const QPointF global = quickWindow->mapToGlobal(position.toPoint());
                    auto *sendTimer = new QTimer(trace);
                    sendTimer->setInterval(80);
                    QObject::connect(sendTimer, &QTimer::timeout, trace,
                        [quickWindow, trace, active, elapsed, events, sendTimer, position, global,
                         contentSamples, animationSamples, frameSamples, samplesMutex, tracePath] {
                            const int index = trace->property("eventIndex").toInt();
                            if (index >= 5) {
                                sendTimer->stop();
                                QTimer::singleShot(900, trace, [quickWindow, trace, active, elapsed, events,
                                    contentSamples, animationSamples, frameSamples, samplesMutex,
                                    tracePath] {
                                        active->store(false, std::memory_order_relaxed);
                                        QJsonArray framesCopy;
                                        QJsonArray animationsCopy;
                                        {
                                            QMutexLocker lock(samplesMutex);
                                            framesCopy = *frameSamples;
                                            animationsCopy = *animationSamples;
                                        }
                                        QJsonObject report{
                                            {"schema", "dft-studio-scroll-trace-v1"},
                                            {"refresh_hz", quickWindow->screen() ? quickWindow->screen()->refreshRate() : 0.0},
                                            {"events", *events},
                                            {"content", *contentSamples},
                                            {"after_animating_ms", animationsCopy},
                                            {"frame_swapped_ms", framesCopy}
                                        };
                                        QFile output(tracePath);
                                        if (!output.open(QIODevice::WriteOnly | QIODevice::Truncate)
                                            || output.write(QJsonDocument(report).toJson(QJsonDocument::Indented)) < 0) {
                                            std::fprintf(stderr, "Could not write scroll trace: %s\n", qPrintable(tracePath));
                                            QCoreApplication::exit(2);
                                            return;
                                        }
                                        std::fprintf(stderr, "scroll trace saved: %s events=%d content_samples=%d frame_samples=%d\n",
                                            qPrintable(tracePath), events->size(), contentSamples->size(), framesCopy.size());
                                        QCoreApplication::quit();
                                    });
                                return;
                            }
                            QWheelEvent event(position, global, QPoint(), QPoint(0, -120), Qt::NoButton,
                                Qt::NoModifier, Qt::NoScrollPhase, false);
                            event.setAccepted(false);
                            QCoreApplication::sendEvent(quickWindow, &event);
                            events->append(QJsonObject{{"t_ms", elapsed->elapsed()}, {"index", index},
                                {"accepted", event.isAccepted()}, {"delta_y", -120}});
                            trace->setProperty("eventIndex", index + 1);
                        });
                    sendTimer->start();
                });
        }
    }
    if (parser.isSet(profileFramesOption)) {
        auto *quickWindow = qobject_cast<QQuickWindow *>(rootObject);
        if (!quickWindow || !rootObject->setProperty("qaFrameProfile", true)) {
            std::fprintf(stderr, "Could not enable the Qt Quick frame profile.\n");
        } else {
            rootObject->setProperty("qaFrameProfile", false);
            auto *profileTimer = new QTimer(&application);
            profileTimer->setInterval(1000);
            profileTimer->setProperty("frameCount", 0);
            profileTimer->setProperty("sampleCount", 0);
            auto *profileClock = new QElapsedTimer;
            QObject::connect(quickWindow, &QQuickWindow::frameSwapped, profileTimer, [profileTimer] {
                profileTimer->setProperty("frameCount", profileTimer->property("frameCount").toInt() + 1);
            });
            QObject::connect(profileTimer, &QTimer::timeout, &application,
                [rootObject, quickWindow, profileTimer, profileClock] {
                    const int sampleCount = profileTimer->property("sampleCount").toInt();
                    const qint64 now = profileClock->elapsed();
                    const qint64 previous = profileTimer->property("previousSampleMs").toLongLong();
                    const qint64 duration = qMax<qint64>(1, now - previous);
                    const int animationTicks = rootObject->property("qaAnimationTickCount").toInt();
                    std::fprintf(stderr, "Qt Quick frame profile [%d/4]: swaps=%.1fHz animation_ticks=%.1fHz interval=%lldms screen=%s@%.2fHz\n",
                        sampleCount + 1,
                        1000.0 * profileTimer->property("frameCount").toInt() / duration,
                        1000.0 * animationTicks / duration, static_cast<long long>(duration),
                        quickWindow->screen() ? qPrintable(quickWindow->screen()->name()) : "none",
                        quickWindow->screen() ? quickWindow->screen()->refreshRate() : 0.0);
                    profileTimer->setProperty("frameCount", 0);
                    rootObject->setProperty("qaAnimationTickCount", 0);
                    profileTimer->setProperty("sampleCount", sampleCount + 1);
                    profileTimer->setProperty("previousSampleMs", now);
                    if (sampleCount + 1 >= 4) {
                        rootObject->setProperty("qaFrameProfile", false);
                        profileTimer->stop();
                        QCoreApplication::quit();
                    }
                });
            QTimer::singleShot(2000, profileTimer, [rootObject, profileTimer, profileClock] {
                rootObject->setProperty("qaAnimationTickCount", 0);
                rootObject->setProperty("qaFrameProfile", true);
                profileTimer->setProperty("frameCount", 0);
                profileClock->start();
                profileTimer->setProperty("previousSampleMs", 0);
                profileTimer->start();
            });
        }
    }
    if (parser.isSet(pageOption)) {
        bool pageIsValid = false;
        const int requestedPage = parser.value(pageOption).toInt(&pageIsValid);
        if (!pageIsValid || requestedPage < 0 || requestedPage > 12)
            std::fprintf(stderr, "--page must be an integer between 0 and 11.\\n");
        else if (!rootObject->setProperty("selectedPage", requestedPage))
            std::fprintf(stderr, "Could not select requested page.\\n");
    }
    if (parser.isSet(sidebarCollapsedOption) && !rootObject->setProperty("sidebarExpanded", false))
        std::fprintf(stderr, "Could not collapse the sidebar.\\n");
    if (parser.isSet(sidebarProjectsCollapsedOption) && !rootObject->setProperty("sidebarProjectListExpanded", false))
        std::fprintf(stderr, "Could not collapse the left sidebar projects.\\n");
    if (parser.isSet(relatedFilesCollapsedOption) && !rootObject->setProperty("relatedFilesExpanded", false))
        std::fprintf(stderr, "Could not collapse the related-files bar.\\n");
    if (parser.isSet(expandFlowStagesOption)) {
        const QStringList stageKeys{"readProject", "executor", "synthesis", "scan", "mbist", "atpg", "lbist", "report"};
        if (!rootObject->setProperty("expandedFlowStageKeys", QVariant::fromValue(stageKeys)))
            std::fprintf(stderr, "Could not expand the flow stages.\\n");
    }
    if (parser.isSet(showContextUsageOption)) {
        QTimer::singleShot(250, rootObject, [rootObject] {
            if (!QMetaObject::invokeMethod(rootObject, "openContextUsageDetails"))
                std::fprintf(stderr, "Could not open the context usage details.\n");
        });
    }
    if (parser.isSet(detailedModeOption))
        agentController.setDetailedMode(true);
    if (parser.isSet(selectProjectOption)) {
        const QString requestedId = parser.value(selectProjectOption);
        int projectIndex = -1;
        for (int index = 0; index < projectModel.rowCount(); ++index) {
            if (projectModel.projectAt(index).value("id").toString() == requestedId) {
                projectIndex = index;
                break;
            }
        }
        if (projectIndex < 0) {
            std::fprintf(stderr, "Unknown project id for --select-project: %s\\n", qPrintable(requestedId));
            return 2;
        }
        projectModel.setCurrentIndex(projectIndex);
        if (!parser.isSet(pageOption) && !rootObject->setProperty("selectedPage", 1))
            std::fprintf(stderr, "Could not select the Project Settings page.\\n");
    }
    if (parser.isSet(demoRunOption)) {
        if (!parser.isSet(pageOption) && !rootObject->setProperty("selectedPage", 2))
            std::fprintf(stderr, "Could not select the Flow monitor for demo mode.\\n");
        agentController.run(projectModel.currentProject(), capabilityModel.disabledIds());
    }
    if (parser.isSet(runProjectOption)) {
        const QString requestedId = parser.value(runProjectOption);
        int projectIndex = -1;
        for (int index = 0; index < projectModel.rowCount(); ++index) {
            if (projectModel.projectAt(index).value("id").toString() == requestedId) {
                projectIndex = index;
                break;
            }
        }
        if (projectIndex < 0) {
            std::fprintf(stderr, "Unknown project id for --run-project: %s\\n", qPrintable(requestedId));
            return 2;
        }
        projectModel.setCurrentIndex(projectIndex);
        if (!parser.isSet(pageOption) && !rootObject->setProperty("selectedPage", 2))
            std::fprintf(stderr, "Could not select the Flow monitor for the requested project.\\n");
        agentController.run(projectModel.currentProject(), capabilityModel.disabledIds());
    }
    if (parser.isSet(terminalInputOption)) {
        const QString input = parser.value(terminalInputOption);
        QTimer::singleShot(1200, rootObject, [rootObject, input] {
            auto *terminal = rootObject->findChild<KonsoleTerminalItem *>(QStringLiteral("embeddedTerminal"));
            if (!terminal || !terminal->sendText(input + QLatin1Char('\n')))
                std::fprintf(stderr, "Could not send input to the embedded terminal.\n");
        });
    }
    if (parser.isSet(openFileOption)) {
        const QString path = parser.value(openFileOption);
        QTimer::singleShot(300, rootObject, [&fileEditor, path] {
            if (!fileEditor.selectFile(path))
                std::fprintf(stderr, "Could not select requested editor file: %s\n", qPrintable(path));
        });
    }
    if (parser.isSet(chatPromptOption)) {
        const QString prompt = parser.value(chatPromptOption);
        QTimer::singleShot(1400, rootObject, [rootObject, prompt] {
            if (!rootObject->setProperty("pendingChatPrompt", prompt)
                || !QMetaObject::invokeMethod(rootObject, "submitAgentPrompt", Q_ARG(QVariant, false)))
                std::fprintf(stderr, "Could not submit the requested Chat prompt.\n");
        });
    }
    if (parser.isSet(chatScrollTestOption)) {
        QTimer::singleShot(60000, rootObject, [rootObject] {
            const bool invoked = QMetaObject::invokeMethod(rootObject, "runChatScrollAcceptance");
            std::fprintf(stderr, "Chat scroll acceptance hook invoked: %s\n", invoked ? "yes" : "no");
            if (!invoked)
                std::fprintf(stderr, "Could not run the Chat scroll acceptance hook.\n");
        });
    }
    if (parser.isSet(captureOption)) {
        const QString capturePath = parser.value(captureOption);
        const int delay = qMax(0, parser.value(captureDelayOption).toInt());
        QTimer::singleShot(delay, &application, [rootObject, capturePath] {
            auto *quickWindow = qobject_cast<QQuickWindow *>(rootObject);
            if (!quickWindow || !quickWindow->grabWindow().save(capturePath))
                std::fprintf(stderr, "Could not capture Qt Quick window: %s\\n", qPrintable(capturePath));
            QCoreApplication::quit();
        });
    }
    return application.exec();
    } catch (const std::exception &error) {
        std::fprintf(stderr, "Studio storage startup failed: %s\n", error.what());
        if (!parser.isSet(storageInfoOption))
            QMessageBox::critical(nullptr, QStringLiteral("Studio 启动失败"), QString::fromUtf8(error.what()));
        return 2;
    }
}
#include "main.moc"
