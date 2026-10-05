cmake_minimum_required(VERSION 3.21)

project(DftAgentStudio VERSION 0.1.0 LANGUAGES CXX)

set(DFT_LLAMA_SERVER_BINARY "" CACHE FILEPATH
    "Optional standalone llama-server binary to bundle under libexec/dft-agent-studio")
set(DFT_LLAMA_FINETUNE_BINARY "" CACHE FILEPATH
    "Optional standalone llama-finetune binary to install as dft-model-finetune")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Concurrent Core Gui Network Qml Quick QuickControls2 Widgets)
find_package(SQLite3 REQUIRED)
find_package(ZLIB REQUIRED)
find_package(yaml-cpp REQUIRED)
find_package(KF6Parts QUIET)

if(TARGET SQLite3::SQLite3)
    set(DFT_SQLITE3_TARGET SQLite3::SQLite3)
else()
    set(DFT_SQLITE3_TARGET SQLite::SQLite3)
endif()

if(TARGET yaml-cpp::yaml-cpp)
    set(DFT_YAML_CPP_TARGET yaml-cpp::yaml-cpp)
elseif(TARGET yaml-cpp)
    set(DFT_YAML_CPP_TARGET yaml-cpp)
else()
    message(FATAL_ERROR "yaml-cpp was found but did not define a supported CMake target")
endif()

qt_standard_project_setup(REQUIRES 6.5)
if(QT_KNOWN_POLICY_QTP0004)
    qt_policy(SET QTP0004 NEW)
endif()

qt_add_executable(dft-agent-studio
    src/main.cpp
    src/latexmathimageprovider.cpp
    src/latexmathimageprovider.h
    src/studiopaths.h
    src/studiouserdata.cpp
    src/studiouserdata.h
    src/projectmodel.cpp
    src/projectmodel.h
    src/projectconfigimporter.cpp
    src/projectconfigimporter.h
    src/projectinspectionservice.cpp
    src/projectinspectionservice.h
    src/modelcatalog.cpp
    src/modelcatalog.h
    src/capabilitymodel.cpp
    src/capabilitymodel.h
    src/agentcontroller.cpp
    src/agentcontroller.h
    src/agentpromptbuilder.cpp
    src/agentpromptbuilder.h
    src/agenttoolclient.cpp
    src/agenttoolclient.h
    src/agenttoolservice.cpp
    src/agenttoolservice.h
    src/configureddftflowservice.cpp
    src/configureddftflowservice.h
    src/sourcecompatibilityservice.cpp
    src/sourcecompatibilityservice.h
    src/edajobservice.cpp
    src/edajobservice.h
    src/fanatpgservice.cpp
    src/fanatpgservice.h
    src/dftevidenceservice.cpp
    src/dftevidenceservice.h
    src/dftreportevidenceservice.cpp
    src/dftreportevidenceservice.h
    src/responsesroundclient.cpp
    src/responsesroundclient.h
    src/responsestooltranscript.cpp
    src/responsestooltranscript.h
    src/responsestooltranscriptservice.cpp
    src/responsestooltranscriptservice.h
    src/responsesturnrunner.cpp
    src/responsesturnrunner.h
    src/workspacedatabase.cpp
    src/workspacedatabase.h
    src/workspacecatalog.cpp
    src/workspacecatalog.h
    src/sessioncatalog.cpp
    src/sessioncatalog.h
    src/patchactionservice.cpp
    src/patchactionservice.h
    src/repairactionfingerprintservice.cpp
    src/repairactionfingerprintservice.h
    src/repairissueservice.cpp
    src/repairissueservice.h
    src/repairinputmanifestservice.cpp
    src/repairinputmanifestservice.h
    src/repairrunevidenceservice.cpp
    src/repairrunevidenceservice.h
    src/repairruntimestateservice.cpp
    src/repairruntimestateservice.h
    src/localmodelruntimeservice.cpp
    src/localmodelruntimeservice.h
    src/pathpermissionservice.cpp
    src/pathpermissionservice.h
    src/nativememoryservice.cpp
    src/nativememoryservice.h
    src/agentepisodeservice.cpp
    src/agentepisodeservice.h
    src/nativetoolregistryservice.cpp
    src/nativetoolregistryservice.h
    src/nativesubagentservice.cpp
    src/nativesubagentservice.h
    src/processoutputservice.cpp
    src/processoutputservice.h
    src/studiotoolservice.cpp
    src/studiotoolservice.h
    src/runreportservice.cpp
    src/runreportservice.h
    src/studionativeapi.cpp
    src/studionativeapi.h
    src/studiostorageservice.cpp
    src/studiostorageservice.h
    src/studiocliturnservice.cpp
    src/studiocliturnservice.h
    src/studiouserdata.cpp
    src/studiouserdata.h
    src/languagesettings.cpp
    src/languagesettings.h
    src/konsoleterminalitem.cpp
    src/konsoleterminalitem.h
    src/fileeditor.cpp
    src/fileeditor.h
    src/syntaxhighlighter.cpp
    src/syntaxhighlighter.h
    src/wheelzoomitem.cpp
    src/wheelzoomitem.h
    src/wheelscrollanimator.cpp
    src/wheelscrollanimator.h
    src/chatdisplaymodel.cpp
    src/chatdisplaymodel.h
)

qt_add_qml_module(dft-agent-studio
    URI DftAgentStudio
    VERSION 1.0
    QML_FILES
        qml/Main.qml
        qml/components/FlatIcon.qml
        qml/components/ActivityShimmer.qml
        qml/components/CodeEditor.qml
        qml/components/ContextUsageIndicator.qml
        qml/components/FileExplorerItem.qml
        qml/components/FileTreeItem.qml
        qml/components/GoalDictionary.qml
        qml/components/HoverSurface.qml
        qml/components/PathField.qml
        qml/components/ReportMarkdownView.qml
        qml/components/SidebarItem.qml
        qml/components/SectionCard.qml
        qml/components/SettingsDialog.qml
        qml/components/ChatDialog.qml
        qml/components/ChatMenuItem.qml
        qml/components/SettingsLauncher.qml
        qml/components/ScrollableTextArea.qml
        qml/components/ScrollableTextField.qml
        qml/components/SmoothListView.qml
        qml/components/SmoothHorizontalListView.qml
        qml/components/SmoothScrollView.qml
        qml/components/StatusPill.qml
        qml/components/StyledCheckBox.qml
        qml/components/StyledComboBox.qml
        qml/components/StyledTextArea.qml
        qml/components/StyledTextField.qml
        qml/components/TerminalLogView.qml
        qml/components/ThemedToolTip.qml
        qml/components/ToolbarButton.qml
        qml/components/WheelScroll.js
)

qt_add_resources(dft-agent-studio "dft_misans_font"
    PREFIX "/fonts"
    BASE "${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts"
    FILES "${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/MiSans VF.ttf"
)

set_source_files_properties(assets/icons/dft-agent-studio.png PROPERTIES
    QT_RESOURCE_ALIAS dft-agent-studio.png
)

qt_add_resources(dft-agent-studio "dft_app_icon"
    PREFIX "/icons"
    FILES "${CMAKE_CURRENT_SOURCE_DIR}/assets/icons/dft-agent-studio.png"
)

target_link_libraries(dft-agent-studio PRIVATE Qt6::Concurrent Qt6::Core Qt6::Gui Qt6::Network Qt6::Qml Qt6::Quick Qt6::QuickControls2 Qt6::Widgets ${DFT_SQLITE3_TARGET} ZLIB::ZLIB ${DFT_YAML_CPP_TARGET})
target_compile_definitions(dft-agent-studio PRIVATE DFT_AGENT_STUDIO_VERSION="${PROJECT_VERSION}")

add_executable(dft-training-data
    src/training_dataset_main.cpp
    src/trainingdatasetservice.cpp
    src/trainingdatasetservice.h
)
target_link_libraries(dft-training-data PRIVATE Qt6::Core)
target_include_directories(dft-training-data PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_definitions(dft-training-data PRIVATE DFT_AGENT_STUDIO_VERSION="${PROJECT_VERSION}")
set_target_properties(dft-training-data PROPERTIES INSTALL_RPATH "$ORIGIN/../lib")
install(TARGETS dft-training-data RUNTIME DESTINATION bin)

add_executable(dft-training-shards
    src/training_shards_main.cpp
    src/trainingshardingservice.cpp
    src/trainingshardingservice.h
    src/trainingpolicies.h
)
target_link_libraries(dft-training-shards PRIVATE Qt6::Core Qt6::Network)
target_include_directories(dft-training-shards PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_definitions(dft-training-shards PRIVATE DFT_AGENT_STUDIO_VERSION="${PROJECT_VERSION}")
set_target_properties(dft-training-shards PROPERTIES INSTALL_RPATH "$ORIGIN/../lib")
install(TARGETS dft-training-shards RUNTIME DESTINATION bin)

add_executable(dft-training-preflight
    src/training_preflight_main.cpp
    src/trainingshardingservice.cpp
    src/trainingshardingservice.h
    src/trainingpolicies.h
)
target_link_libraries(dft-training-preflight PRIVATE Qt6::Core Qt6::Network)
target_include_directories(dft-training-preflight PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_definitions(dft-training-preflight PRIVATE DFT_AGENT_STUDIO_VERSION="${PROJECT_VERSION}")
set_target_properties(dft-training-preflight PROPERTIES INSTALL_RPATH "$ORIGIN/../lib")
install(TARGETS dft-training-preflight RUNTIME DESTINATION bin)

add_executable(dft-evaluation-data
    src/evaluation_dataset_main.cpp
    src/evaluationdatasetservice.cpp
    src/evaluationdatasetservice.h
    src/trainingpolicies.h
)
target_link_libraries(dft-evaluation-data PRIVATE Qt6::Core)
target_include_directories(dft-evaluation-data PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_definitions(dft-evaluation-data PRIVATE DFT_AGENT_STUDIO_VERSION="${PROJECT_VERSION}")
set_target_properties(dft-evaluation-data PROPERTIES INSTALL_RPATH "$ORIGIN/../lib")
install(TARGETS dft-evaluation-data RUNTIME DESTINATION bin)

add_executable(dft-preference-data
    src/preference_dataset_main.cpp
    src/preferencedatasetservice.cpp
    src/preferencedatasetservice.h
)
target_link_libraries(dft-preference-data PRIVATE Qt6::Core)
target_include_directories(dft-preference-data PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_definitions(dft-preference-data PRIVATE DFT_AGENT_STUDIO_VERSION="${PROJECT_VERSION}")
set_target_properties(dft-preference-data PROPERTIES INSTALL_RPATH "$ORIGIN/../lib")
install(TARGETS dft-preference-data RUNTIME DESTINATION bin)

add_executable(dft-agent-feedback
    src/agent_feedback_main.cpp
    src/agentepisodeservice.cpp
    src/agentepisodeservice.h
    src/nativememoryservice.cpp
    src/nativememoryservice.h
    src/studiopaths.h
)
target_link_libraries(dft-agent-feedback PRIVATE Qt6::Core ${DFT_SQLITE3_TARGET})
target_include_directories(dft-agent-feedback PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_definitions(dft-agent-feedback PRIVATE DFT_AGENT_STUDIO_VERSION="${PROJECT_VERSION}")
set_target_properties(dft-agent-feedback PROPERTIES INSTALL_RPATH "$ORIGIN/../lib")
install(TARGETS dft-agent-feedback RUNTIME DESTINATION bin)

add_executable(dft-dataset-quality
    src/dataset_quality_main.cpp
    src/datasetqualityservice.cpp
    src/datasetqualityservice.h
)
target_link_libraries(dft-dataset-quality PRIVATE Qt6::Core)
target_include_directories(dft-dataset-quality PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_definitions(dft-dataset-quality PRIVATE DFT_AGENT_STUDIO_VERSION="${PROJECT_VERSION}")
set_target_properties(dft-dataset-quality PROPERTIES INSTALL_RPATH "$ORIGIN/../lib")
install(TARGETS dft-dataset-quality RUNTIME DESTINATION bin)

add_executable(dft-scroll-trace-analyzer
    src/scroll_trace_analyzer_main.cpp
    src/scrolltraceanalyzerservice.cpp
    src/scrolltraceanalyzerservice.h
)
target_link_libraries(dft-scroll-trace-analyzer PRIVATE Qt6::Core)
target_include_directories(dft-scroll-trace-analyzer PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_definitions(dft-scroll-trace-analyzer PRIVATE DFT_AGENT_STUDIO_VERSION="${PROJECT_VERSION}")
set_target_properties(dft-scroll-trace-analyzer PROPERTIES INSTALL_RPATH "$ORIGIN/../lib")
install(TARGETS dft-scroll-trace-analyzer RUNTIME DESTINATION bin)

add_executable(dft-session-audit
    src/session_audit_main.cpp
    src/sessionauditservice.cpp
    src/sessionauditservice.h
)
target_link_libraries(dft-session-audit PRIVATE Qt6::Core ZLIB::ZLIB)
target_include_directories(dft-session-audit PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_definitions(dft-session-audit PRIVATE DFT_AGENT_STUDIO_VERSION="${PROJECT_VERSION}")
set_target_properties(dft-session-audit PROPERTIES INSTALL_RPATH "$ORIGIN/../lib")
install(TARGETS dft-session-audit RUNTIME DESTINATION bin)

if(KF6Parts_FOUND)
    target_compile_definitions(dft-agent-studio PRIVATE DFT_AGENT_HAS_KONSOLE_PART=1)
    target_link_libraries(dft-agent-studio PRIVATE KF6::Parts)
else()
    target_compile_definitions(dft-agent-studio PRIVATE DFT_AGENT_HAS_KONSOLE_PART=0)
    message(STATUS "KF6 KParts was not found; the project terminal will use an external terminal application")
endif()

set(DFT_STUDIO_CLI_TURN_SOURCES
    src/studiocliturnservice.cpp
    src/studiocliturnservice.h
    src/studiouserdata.cpp
    src/studiouserdata.h
    src/agentpromptbuilder.cpp
    src/agentpromptbuilder.h
    src/agenttoolservice.cpp
    src/agenttoolservice.h
    src/projectinspectionservice.cpp
    src/projectinspectionservice.h
    src/configureddftflowservice.cpp
    src/configureddftflowservice.h
    src/sourcecompatibilityservice.cpp
    src/sourcecompatibilityservice.h
    src/edajobservice.cpp
    src/edajobservice.h
    src/fanatpgservice.cpp
    src/fanatpgservice.h
    src/dftevidenceservice.cpp
    src/dftevidenceservice.h
    src/dftreportevidenceservice.cpp
    src/dftreportevidenceservice.h
    src/responsesroundclient.cpp
    src/responsesroundclient.h
    src/responsestooltranscript.cpp
    src/responsestooltranscript.h
    src/responsestooltranscriptservice.cpp
    src/responsestooltranscriptservice.h
    src/responsesturnrunner.cpp
    src/responsesturnrunner.h
    src/workspacedatabase.cpp
    src/workspacedatabase.h
    src/workspacecatalog.cpp
    src/workspacecatalog.h
    src/sessioncatalog.cpp
    src/sessioncatalog.h
    src/patchactionservice.cpp
    src/patchactionservice.h
    src/repairactionfingerprintservice.cpp
    src/repairactionfingerprintservice.h
    src/repairissueservice.cpp
    src/repairissueservice.h
    src/repairinputmanifestservice.cpp
    src/repairinputmanifestservice.h
    src/repairrunevidenceservice.cpp
    src/repairrunevidenceservice.h
    src/repairruntimestateservice.cpp
    src/repairruntimestateservice.h
    src/localmodelruntimeservice.cpp
    src/localmodelruntimeservice.h
    src/pathpermissionservice.cpp
    src/pathpermissionservice.h
    src/nativememoryservice.cpp
    src/nativememoryservice.h
    src/nativetoolregistryservice.cpp
    src/nativetoolregistryservice.h
    src/nativesubagentservice.cpp
    src/nativesubagentservice.h
    src/processoutputservice.cpp
    src/processoutputservice.h
    src/studiotoolservice.cpp
    src/studiotoolservice.h
    src/runreportservice.cpp
    src/runreportservice.h
    src/studiostorageservice.cpp
    src/studiostorageservice.h
)

add_library(dft-studio-cli-runtime STATIC
    ${DFT_STUDIO_CLI_TURN_SOURCES}
    src/studionativeapi.cpp
    src/studionativeapi.h
)
    target_link_libraries(dft-studio-cli-runtime PUBLIC Qt6::Core Qt6::Gui Qt6::Network ${DFT_SQLITE3_TARGET} ZLIB::ZLIB ${DFT_YAML_CPP_TARGET})
target_include_directories(dft-studio-cli-runtime PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)

include(CTest)

if(BUILD_TESTING)
    add_executable(dft-studio-user-data-test
        tests/studiouserdata_check.cpp
        src/studiouserdata.cpp
        src/studiouserdata.h
        src/studiopaths.h
    )
    target_link_libraries(dft-studio-user-data-test PRIVATE Qt6::Core ${DFT_SQLITE3_TARGET})
    target_include_directories(dft-studio-user-data-test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    add_test(NAME dft-studio-user-data COMMAND dft-studio-user-data-test)

    add_executable(dft-agent-studio-storage-check tests/studiopaths_check.cpp src/studiopaths.h)
    target_link_libraries(dft-agent-studio-storage-check PRIVATE Qt6::Core)
    target_include_directories(dft-agent-studio-storage-check PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    add_test(NAME dft-agent-studio-storage COMMAND dft-agent-studio-storage-check)

    add_executable(dft-repair-issue-check
        tests/repairissueservice_check.cpp
        src/repairissueservice.cpp
        src/repairissueservice.h
    )
    target_link_libraries(dft-repair-issue-check PRIVATE Qt6::Core)
    target_include_directories(dft-repair-issue-check PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    add_test(NAME dft-repair-issue COMMAND dft-repair-issue-check)

    add_executable(dft-repair-action-fingerprint-check
        tests/repairactionfingerprintservice_check.cpp
        src/repairactionfingerprintservice.cpp
        src/repairactionfingerprintservice.h
    )
    target_link_libraries(dft-repair-action-fingerprint-check PRIVATE Qt6::Core)
    target_include_directories(dft-repair-action-fingerprint-check PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    add_test(NAME dft-repair-action-fingerprint COMMAND dft-repair-action-fingerprint-check)

    add_executable(dft-repair-run-evidence-check
        tests/repair_run_evidence_check.cpp
        src/repairrunevidenceservice.cpp
        src/repairrunevidenceservice.h
    )
    target_link_libraries(dft-repair-run-evidence-check PRIVATE Qt6::Core)
    target_include_directories(dft-repair-run-evidence-check PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    add_test(NAME dft-repair-run-evidence COMMAND dft-repair-run-evidence-check)

    add_executable(dft-repair-runtime-state-check
        tests/repairruntimestateservice_check.cpp
        src/repairruntimestateservice.cpp
        src/repairruntimestateservice.h
        src/repairissueservice.cpp
        src/repairissueservice.h
        src/repairactionfingerprintservice.cpp
        src/repairactionfingerprintservice.h
        src/repairrunevidenceservice.cpp
        src/repairrunevidenceservice.h
    )
    target_link_libraries(dft-repair-runtime-state-check PRIVATE Qt6::Core)
    target_include_directories(dft-repair-runtime-state-check PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    add_test(NAME dft-repair-runtime-state COMMAND dft-repair-runtime-state-check)

    add_executable(dft-repair-input-manifest-check
        tests/repairinputmanifestservice_check.cpp
        src/repairinputmanifestservice.cpp
        src/repairinputmanifestservice.h
        src/repairactionfingerprintservice.cpp
        src/repairactionfingerprintservice.h
    )
    target_link_libraries(dft-repair-input-manifest-check PRIVATE Qt6::Core)
    target_include_directories(dft-repair-input-manifest-check PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    add_test(NAME dft-repair-input-manifest COMMAND dft-repair-input-manifest-check)

    add_executable(dft-path-permission-check
        tests/pathpermissionservice_check.cpp
        src/pathpermissionservice.cpp
        src/pathpermissionservice.h
    )
    target_link_libraries(dft-path-permission-check PRIVATE Qt6::Core)
    target_include_directories(dft-path-permission-check PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    add_test(NAME dft-path-permission COMMAND dft-path-permission-check)

    add_executable(dft-native-memory-check
        tests/nativememoryservice_check.cpp
        src/nativememoryservice.cpp
        src/nativememoryservice.h
    )
    target_link_libraries(dft-native-memory-check PRIVATE Qt6::Core ${DFT_SQLITE3_TARGET})
    target_include_directories(dft-native-memory-check PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    add_test(NAME dft-native-memory COMMAND dft-native-memory-check)

    add_executable(dft-training-dataset-check
        tests/trainingdatasetservice_check.cpp
        src/trainingdatasetservice.cpp
        src/trainingdatasetservice.h
    )
    target_link_libraries(dft-training-dataset-check PRIVATE Qt6::Core)
    target_include_directories(dft-training-dataset-check PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    add_test(NAME dft-training-dataset COMMAND dft-training-dataset-check)
    add_test(NAME dft-training-cli-version COMMAND dft-training-data --version)

    add_executable(dft-training-shards-check
        tests/trainingshardingservice_check.cpp
        src/trainingshardingservice.cpp
        src/trainingshardingservice.h
        src/trainingpolicies.h
    )
    target_link_libraries(dft-training-shards-check PRIVATE Qt6::Core Qt6::Network)
    target_include_directories(dft-training-shards-check PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    add_test(NAME dft-training-shards COMMAND dft-training-shards-check)
    add_test(NAME dft-training-shards-cli-version COMMAND dft-training-shards --version)

    add_test(NAME dft-training-preflight-cli-version COMMAND dft-training-preflight --version)

    add_executable(dft-evaluation-dataset-check
        tests/evaluationdatasetservice_check.cpp
        src/evaluationdatasetservice.cpp
        src/evaluationdatasetservice.h
        src/trainingpolicies.h
    )
    target_link_libraries(dft-evaluation-dataset-check PRIVATE Qt6::Core)
    target_include_directories(dft-evaluation-dataset-check PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    add_test(NAME dft-evaluation-dataset COMMAND dft-evaluation-dataset-check)
    add_test(NAME dft-evaluation-cli-version COMMAND dft-evaluation-data --version)

    add_executable(dft-preference-dataset-check
        tests/preferencedatasetservice_check.cpp
        src/preferencedatasetservice.cpp
        src/preferencedatasetservice.h
    )
    target_link_libraries(dft-preference-dataset-check PRIVATE Qt6::Core)
    target_include_directories(dft-preference-dataset-check PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    add_test(NAME dft-preference-dataset COMMAND dft-preference-dataset-check)
    add_test(NAME dft-preference-cli-version COMMAND dft-preference-data --version)

    add_executable(dft-agent-episode-check
        tests/agentepisodeservice_check.cpp
        src/agentepisodeservice.cpp
        src/agentepisodeservice.h
        src/nativememoryservice.cpp
        src/nativememoryservice.h
    )
    target_link_libraries(dft-agent-episode-check PRIVATE Qt6::Core ${DFT_SQLITE3_TARGET})
    target_include_directories(dft-agent-episode-check PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    add_test(NAME dft-agent-episode-feedback COMMAND dft-agent-episode-check)
    add_test(NAME dft-agent-feedback-cli-version COMMAND dft-agent-feedback --version)

    add_executable(dft-dataset-quality-check
        tests/datasetqualityservice_check.cpp
        src/datasetqualityservice.cpp
        src/datasetqualityservice.h
    )
    target_link_libraries(dft-dataset-quality-check PRIVATE Qt6::Core)
    target_include_directories(dft-dataset-quality-check PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    add_test(NAME dft-dataset-quality COMMAND dft-dataset-quality-check)
    add_test(NAME dft-dataset-quality-cli-version COMMAND dft-dataset-quality --version)

    add_executable(dft-scroll-trace-analyzer-check
        tests/scrolltraceanalyzerservice_check.cpp
        src/scrolltraceanalyzerservice.cpp
        src/scrolltraceanalyzerservice.h
    )
    target_link_libraries(dft-scroll-trace-analyzer-check PRIVATE Qt6::Core)
    target_include_directories(dft-scroll-trace-analyzer-check PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    add_test(NAME dft-scroll-trace-analyzer COMMAND dft-scroll-trace-analyzer-check)
    add_test(NAME dft-scroll-trace-analyzer-cli-version COMMAND dft-scroll-trace-analyzer --version)

    add_executable(dft-session-audit-check
        tests/sessionauditservice_check.cpp
        src/sessionauditservice.cpp
        src/sessionauditservice.h
    )
    target_link_libraries(dft-session-audit-check PRIVATE Qt6::Core ZLIB::ZLIB)
    target_include_directories(dft-session-audit-check PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    add_test(NAME dft-session-audit COMMAND dft-session-audit-check)
    add_test(NAME dft-session-audit-cli-version COMMAND dft-session-audit --version)

    add_executable(dft-native-tool-registry-check
        tests/nativetoolregistryservice_check.cpp
        src/nativetoolregistryservice.cpp
        src/nativetoolregistryservice.h
    )
    target_link_libraries(dft-native-tool-registry-check PRIVATE Qt6::Core)
    target_include_directories(dft-native-tool-registry-check PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_compile_options(dft-native-tool-registry-check PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang>:-UNDEBUG>
        $<$<CXX_COMPILER_ID:MSVC>:/UNDEBUG>
    )
    add_test(NAME dft-native-tool-registry COMMAND dft-native-tool-registry-check)

    add_executable(dft-project-inspection-check
        tests/projectinspectionservice_check.cpp
        src/projectinspectionservice.cpp
        src/projectinspectionservice.h
    )
    target_link_libraries(dft-project-inspection-check PRIVATE Qt6::Core)
    target_include_directories(dft-project-inspection-check PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    add_test(NAME dft-project-inspection COMMAND dft-project-inspection-check)

    add_executable(dft-local-model-runtime-check
        tests/localmodelruntimeservice_check.cpp
        src/localmodelruntimeservice.cpp
        src/localmodelruntimeservice.h
    )
    target_link_libraries(dft-local-model-runtime-check PRIVATE Qt6::Core)
    target_include_directories(dft-local-model-runtime-check PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    add_test(NAME dft-local-model-runtime COMMAND dft-local-model-runtime-check)

    add_executable(dft-native-subagent-check
        tests/nativesubagentservice_check.cpp
        src/nativesubagentservice.cpp
        src/nativesubagentservice.h
        src/responsesturnrunner.cpp
        src/responsesturnrunner.h
        src/responsesroundclient.cpp
        src/responsesroundclient.h
        src/responsestooltranscript.cpp
        src/responsestooltranscript.h
        src/agentpromptbuilder.cpp
        src/agentpromptbuilder.h
        src/sessioncatalog.cpp
        src/sessioncatalog.h
        src/pathpermissionservice.cpp
        src/pathpermissionservice.h
        src/workspacedatabase.cpp
        src/workspacedatabase.h
        src/workspacecatalog.cpp
        src/workspacecatalog.h
        src/responsestooltranscriptservice.cpp
        src/responsestooltranscriptservice.h
        src/runreportservice.cpp
        src/runreportservice.h
        src/studiotoolservice.cpp
        src/studiotoolservice.h
    )
    target_link_libraries(dft-native-subagent-check PRIVATE Qt6::Core Qt6::Gui Qt6::Network ${DFT_SQLITE3_TARGET} ZLIB::ZLIB ${DFT_YAML_CPP_TARGET})
    target_include_directories(dft-native-subagent-check PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_compile_definitions(dft-native-subagent-check PRIVATE NATIVE_SUBAGENT_SERVICE_STANDALONE)
    add_test(NAME dft-native-subagent COMMAND dft-native-subagent-check)

    add_executable(dft-dft-report-evidence-check
        tests/dftreportevidenceservice_check.cpp
        src/dftreportevidenceservice.cpp
        src/dftreportevidenceservice.h
    )
    target_link_libraries(dft-dft-report-evidence-check PRIVATE Qt6::Core)
    target_include_directories(dft-dft-report-evidence-check PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    add_test(NAME dft-dft-report-evidence COMMAND dft-dft-report-evidence-check)

    add_executable(dft-configured-dft-flow-check
        tests/configureddftflowservice_check.cpp
        src/configureddftflowservice.cpp
        src/configureddftflowservice.h
        src/sourcecompatibilityservice.cpp
        src/sourcecompatibilityservice.h
        src/edajobservice.cpp
        src/edajobservice.h
        src/fanatpgservice.cpp
        src/fanatpgservice.h
        src/processoutputservice.cpp
        src/processoutputservice.h
        src/dftevidenceservice.cpp
        src/dftevidenceservice.h
        src/dftreportevidenceservice.cpp
        src/dftreportevidenceservice.h
    )
    target_link_libraries(dft-configured-dft-flow-check PRIVATE Qt6::Core ${DFT_YAML_CPP_TARGET})
    target_include_directories(dft-configured-dft-flow-check PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    add_test(NAME dft-configured-dft-flow COMMAND dft-configured-dft-flow-check)

    add_executable(dft-fan-atpg-check
        tests/fanatpgservice_check.cpp
        src/fanatpgservice.cpp
        src/fanatpgservice.h
        src/processoutputservice.cpp
        src/processoutputservice.h
    )
    target_link_libraries(dft-fan-atpg-check PRIVATE Qt6::Core)
    target_include_directories(dft-fan-atpg-check PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    add_test(NAME dft-fan-atpg COMMAND dft-fan-atpg-check)

    add_executable(dft-studio-storage-api-check
        tests/studionativeapi_check.cpp
        src/studionativeapi.cpp
        src/studionativeapi.h
        src/studiostorageservice.cpp
        src/studiostorageservice.h
        src/sessioncatalog.cpp
        src/sessioncatalog.h
        src/runreportservice.cpp
        src/runreportservice.h
        src/pathpermissionservice.cpp
        src/pathpermissionservice.h
        src/studiotoolservice.cpp
        src/studiotoolservice.h
        src/configureddftflowservice.cpp
        src/configureddftflowservice.h
        src/sourcecompatibilityservice.cpp
        src/sourcecompatibilityservice.h
        src/edajobservice.cpp
        src/edajobservice.h
        src/fanatpgservice.cpp
        src/fanatpgservice.h
        src/processoutputservice.cpp
        src/processoutputservice.h
        src/dftevidenceservice.cpp
        src/dftevidenceservice.h
        src/dftreportevidenceservice.cpp
        src/dftreportevidenceservice.h
    )
    target_link_libraries(dft-studio-storage-api-check PRIVATE Qt6::Core Qt6::Gui Qt6::Network ${DFT_SQLITE3_TARGET} ZLIB::ZLIB ${DFT_YAML_CPP_TARGET})
    target_include_directories(dft-studio-storage-api-check PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    add_test(NAME dft-studio-storage-api COMMAND dft-studio-storage-api-check)

    add_executable(dft-studio-storage-path-check
        tests/studiostorageservice_check.cpp
        src/studiostorageservice.cpp
        src/studiostorageservice.h
    )
    target_link_libraries(dft-studio-storage-path-check PRIVATE Qt6::Core Qt6::Gui)
    target_include_directories(dft-studio-storage-path-check PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    add_test(NAME dft-studio-storage-path COMMAND dft-studio-storage-path-check)

    add_executable(dft-process-output-check
        tests/processoutputservice_check.cpp
        src/processoutputservice.cpp
        src/processoutputservice.h
    )
    target_link_libraries(dft-process-output-check PRIVATE Qt6::Core)
    target_include_directories(dft-process-output-check PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    add_test(NAME dft-process-output COMMAND dft-process-output-check)

    add_executable(dft-agent-studio-cli-turn-check
        tests/studiocliturnservice_check.cpp
    )
    target_link_libraries(dft-agent-studio-cli-turn-check PRIVATE dft-studio-cli-runtime)
    add_test(NAME dft-studio-cli-turn COMMAND dft-agent-studio-cli-turn-check)

    qt_add_executable(dft-agent-studio-backend-check
        tests/backend_check.cpp
        src/latexmathimageprovider.cpp
        src/latexmathimageprovider.h
        src/projectmodel.cpp
        src/projectmodel.h
        src/projectconfigimporter.cpp
        src/projectconfigimporter.h
        src/modelcatalog.cpp
        src/modelcatalog.h
        src/chatdisplaymodel.cpp
        src/chatdisplaymodel.h
        src/capabilitymodel.cpp
        src/capabilitymodel.h
        src/agentcontroller.cpp
        src/agentcontroller.h
        src/agentepisodeservice.cpp
        src/agentepisodeservice.h
        src/agentpromptbuilder.cpp
        src/agentpromptbuilder.h
        src/agenttoolclient.cpp
        src/agenttoolclient.h
        src/agenttoolservice.cpp
        src/agenttoolservice.h
        src/projectinspectionservice.cpp
        src/projectinspectionservice.h
        src/configureddftflowservice.cpp
        src/configureddftflowservice.h
        src/sourcecompatibilityservice.cpp
        src/sourcecompatibilityservice.h
        src/edajobservice.cpp
        src/edajobservice.h
        src/fanatpgservice.cpp
        src/fanatpgservice.h
        tests/agenttoolservice_runtime_check.cpp
        src/dftevidenceservice.cpp
        src/dftevidenceservice.h
        src/dftreportevidenceservice.cpp
        src/dftreportevidenceservice.h
        src/responsesroundclient.cpp
        src/responsesroundclient.h
        src/responsestooltranscript.cpp
        src/responsestooltranscript.h
        src/responsestooltranscriptservice.cpp
        src/responsestooltranscriptservice.h
        src/responsesturnrunner.cpp
        src/responsesturnrunner.h
        src/localmodelruntimeservice.cpp
        src/localmodelruntimeservice.h
        src/workspacedatabase.cpp
        src/workspacedatabase.h
        src/workspacecatalog.cpp
        src/workspacecatalog.h
        src/sessioncatalog.cpp
        src/sessioncatalog.h
        src/patchactionservice.cpp
        src/patchactionservice.h
        src/repairactionfingerprintservice.cpp
        src/repairactionfingerprintservice.h
        src/repairissueservice.cpp
        src/repairissueservice.h
        src/repairinputmanifestservice.cpp
        src/repairinputmanifestservice.h
        src/repairruntimestateservice.cpp
        src/repairruntimestateservice.h
        src/repairrunevidenceservice.cpp
        src/repairrunevidenceservice.h
        src/pathpermissionservice.cpp
        src/pathpermissionservice.h
        src/nativememoryservice.cpp
        src/nativememoryservice.h
        src/agentepisodeservice.cpp
        src/agentepisodeservice.h
        src/nativetoolregistryservice.cpp
        src/nativetoolregistryservice.h
        src/nativesubagentservice.cpp
        src/nativesubagentservice.h
        src/processoutputservice.cpp
        src/processoutputservice.h
        src/studiotoolservice.cpp
        src/studiotoolservice.h
        src/runreportservice.cpp
        src/runreportservice.h
        src/studionativeapi.cpp
        src/studionativeapi.h
        src/studiostorageservice.cpp
        src/studiostorageservice.h
        src/languagesettings.cpp
        src/languagesettings.h
        src/fileeditor.cpp
        src/fileeditor.h
        src/syntaxhighlighter.cpp
        src/syntaxhighlighter.h
    )

    target_link_libraries(dft-agent-studio-backend-check PRIVATE Qt6::Concurrent Qt6::Core Qt6::Gui Qt6::Network Qt6::Qml Qt6::Quick ${DFT_SQLITE3_TARGET} ZLIB::ZLIB ${DFT_YAML_CPP_TARGET})
    target_include_directories(dft-agent-studio-backend-check PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

    add_test(NAME dft-agent-studio-backend COMMAND dft-agent-studio-backend-check)
    add_test(NAME dft-agent-tool-runtime COMMAND dft-agent-studio-backend-check --agent-tool-runtime)
    add_test(NAME dft-agent-studio-live-steering COMMAND dft-agent-studio-backend-check --live-steering)
    set_tests_properties(dft-agent-studio-backend dft-agent-tool-runtime dft-agent-studio-live-steering
        PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

    add_executable(dft-responses-tool-transcript-compaction-check
        tests/responsestooltranscript_compaction_check.cpp
        src/responsestooltranscript.cpp
        src/responsestooltranscript.h
    )
    target_link_libraries(dft-responses-tool-transcript-compaction-check PRIVATE Qt6::Core)
    target_include_directories(dft-responses-tool-transcript-compaction-check PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    add_test(NAME dft-responses-tool-transcript-compaction
        COMMAND dft-responses-tool-transcript-compaction-check)

    qt_add_executable(dft-agent-studio-latex-visual-check
        tests/latex_visual_check.cpp
        src/latexmathimageprovider.cpp
        src/latexmathimageprovider.h
    )
    target_link_libraries(dft-agent-studio-latex-visual-check PRIVATE Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick)
    target_include_directories(dft-agent-studio-latex-visual-check PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    add_test(NAME dft-agent-studio-latex-visual COMMAND dft-agent-studio-latex-visual-check)
    set_tests_properties(dft-agent-studio-latex-visual PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

    add_executable(dft-agent-studio-wheel-check
        tests/wheelzoomitem_check.cpp
        src/wheelzoomitem.cpp
        src/wheelzoomitem.h
    )
    target_link_libraries(
        dft-agent-studio-wheel-check
        PRIVATE Qt6::Core Qt6::Gui Qt6::Quick
    )
    target_include_directories(
        dft-agent-studio-wheel-check
        PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
    )
    add_test(NAME dft-agent-studio-wheel COMMAND dft-agent-studio-wheel-check)
    set_tests_properties(
        dft-agent-studio-wheel
        PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    )

    qt_add_executable(dft-agent-studio-scroll-check
        tests/wheelscrollanimator_check.cpp
        src/wheelscrollanimator.cpp
        src/wheelscrollanimator.h
    )
    target_link_libraries(dft-agent-studio-scroll-check PRIVATE Qt6::Core)
    target_include_directories(dft-agent-studio-scroll-check PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    add_test(NAME dft-agent-studio-scroll COMMAND dft-agent-studio-scroll-check)

    find_program(QML_TEST_RUNNER NAMES qmltestrunner6 qmltestrunner)
    if(QML_TEST_RUNNER)
        add_test(
            NAME dft-agent-studio-qml
            COMMAND ${QML_TEST_RUNNER}
                -input ${CMAKE_CURRENT_SOURCE_DIR}/tests/qml
                -import ${CMAKE_CURRENT_BINARY_DIR}
        )
        set_tests_properties(
            dft-agent-studio-qml
            PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
        )
    endif()
endif()

add_executable(dft-agent-studio-storage
    src/studio_storage_cli_main.cpp
    src/studiopaths.h
    src/studiouserdata.cpp
    src/studiouserdata.h
    src/studionativeapi.cpp
    src/studionativeapi.h
    src/studiostorageservice.cpp
    src/studiostorageservice.h
    src/sessioncatalog.cpp
    src/sessioncatalog.h
    src/runreportservice.cpp
    src/runreportservice.h
    src/pathpermissionservice.cpp
    src/pathpermissionservice.h
    src/studiotoolservice.cpp
    src/studiotoolservice.h
    src/configureddftflowservice.cpp
    src/configureddftflowservice.h
    src/edajobservice.cpp
    src/edajobservice.h
    src/fanatpgservice.cpp
    src/fanatpgservice.h
    src/processoutputservice.cpp
    src/processoutputservice.h
    src/sourcecompatibilityservice.cpp
    src/sourcecompatibilityservice.h
    src/dftevidenceservice.cpp
    src/dftevidenceservice.h
    src/dftreportevidenceservice.cpp
    src/dftreportevidenceservice.h
)
target_link_libraries(dft-agent-studio-storage PRIVATE Qt6::Core Qt6::Gui Qt6::Network ${DFT_SQLITE3_TARGET} ZLIB::ZLIB ${DFT_YAML_CPP_TARGET})
target_include_directories(dft-agent-studio-storage PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
install(TARGETS dft-agent-studio-storage RUNTIME DESTINATION bin)

add_executable(dft-agent-studio-agent src/studio_agent_cli_main.cpp)
target_link_libraries(dft-agent-studio-agent PRIVATE dft-studio-cli-runtime)
install(TARGETS dft-agent-studio-agent RUNTIME DESTINATION bin)

add_executable(dft-agent-studio-server src/studio_server_main.cpp)
target_link_libraries(dft-agent-studio-server PRIVATE dft-studio-cli-runtime)
target_compile_definitions(dft-agent-studio-server PRIVATE DFT_AGENT_STUDIO_VERSION="${PROJECT_VERSION}")
install(TARGETS dft-agent-studio-server RUNTIME DESTINATION bin)
if(BUILD_TESTING)
    add_executable(dft-agent-studio-server-check tests/studio_server_check.cpp)
    target_link_libraries(dft-agent-studio-server-check PRIVATE Qt6::Core)
    target_compile_definitions(dft-agent-studio-server-check PRIVATE
        DFT_STUDIO_SERVER_PATH="$<TARGET_FILE:dft-agent-studio-server>")
    add_dependencies(dft-agent-studio-server-check dft-agent-studio-server)
    add_test(NAME dft-studio-app-server COMMAND dft-agent-studio-server-check)
endif()

add_executable(dft-studio src/studio_terminal_main.cpp)
target_link_libraries(dft-studio PRIVATE dft-studio-cli-runtime)
target_compile_definitions(dft-studio PRIVATE DFT_AGENT_STUDIO_VERSION="${PROJECT_VERSION}")
install(TARGETS dft-studio RUNTIME DESTINATION bin)
if(BUILD_TESTING)
    add_dependencies(dft-agent-studio-cli-turn-check dft-agent-studio-agent)
    target_compile_definitions(dft-agent-studio-cli-turn-check PRIVATE
        DFT_STUDIO_AGENT_CLI_PATH="$<TARGET_FILE:dft-agent-studio-agent>")
endif()

install(TARGETS dft-agent-studio
    BUNDLE DESTINATION .
    RUNTIME DESTINATION bin
)
if(DFT_LLAMA_SERVER_BINARY)
    if(NOT EXISTS "${DFT_LLAMA_SERVER_BINARY}" OR IS_DIRECTORY "${DFT_LLAMA_SERVER_BINARY}")
        message(FATAL_ERROR "DFT_LLAMA_SERVER_BINARY must point to a built llama-server executable")
    endif()
    install(PROGRAMS "${DFT_LLAMA_SERVER_BINARY}"
        DESTINATION libexec/dft-agent-studio
        RENAME llama-server
    )
endif()
if(DFT_LLAMA_FINETUNE_BINARY)
    if(NOT EXISTS "${DFT_LLAMA_FINETUNE_BINARY}" OR IS_DIRECTORY "${DFT_LLAMA_FINETUNE_BINARY}")
        message(FATAL_ERROR "DFT_LLAMA_FINETUNE_BINARY must point to a built llama-finetune executable")
    endif()
    install(PROGRAMS "${DFT_LLAMA_FINETUNE_BINARY}"
        DESTINATION bin
        RENAME dft-model-finetune
    )
endif()
install(FILES packaging/dft-agent-studio.desktop
    DESTINATION share/applications
)
install(FILES assets/icons/dft-agent-studio.png
    DESTINATION share/icons/hicolor/512x512/apps
)

qt_generate_deploy_qml_app_script(
    TARGET dft-agent-studio
    OUTPUT_SCRIPT dft_agent_studio_deploy_script
    NO_TRANSLATIONS
)
configure_file(
    cmake/deploy_qt_runtime.cmake.in
    "${CMAKE_CURRENT_BINARY_DIR}/deploy_qt_runtime.cmake"
    @ONLY
)
install(SCRIPT "${CMAKE_CURRENT_BINARY_DIR}/deploy_qt_runtime.cmake")

set(CPACK_PACKAGE_NAME "dft-agent-studio")
set(CPACK_PACKAGE_VENDOR "Little-W")
set(CPACK_PACKAGE_CONTACT "Little-W")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "DFT Agent Studio")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
set(CPACK_DEBIAN_PACKAGE_MAINTAINER "Little-W")
set(CPACK_DEBIAN_PACKAGE_SECTION "utils")
set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE "amd64")
set(CPACK_DEBIAN_PACKAGE_DEPENDS "libsqlite3-0, zlib1g, libyaml-cpp0.7, libgl1, libxkbcommon0")
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
set(CPACK_RPM_PACKAGE_LICENSE "MIT")
set(CPACK_RPM_PACKAGE_GROUP "Applications/Engineering")
set(CPACK_RPM_PACKAGE_REQUIRES "sqlite-libs, zlib, yaml-cpp")
include(CPack)

get_property(_dft_studio_targets DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)
foreach(_target IN LISTS _dft_studio_targets)
    if(_target MATCHES "-check$")
        target_compile_definitions(${_target} PRIVATE DFT_AGENT_STUDIO_TESTING=1)
    endif()
endforeach()
