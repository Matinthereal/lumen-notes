# ---- lumen_core: everything testable, no QML ------------------------------------------------
qt_add_library(lumen_core STATIC
    src/ipc/jsonlines.h            src/ipc/jsonlines.cpp
    src/workers/workersupervisor.h src/workers/workersupervisor.cpp
    src/workers/pingmeter.h        src/workers/pingmeter.cpp
    src/input/tabletsample.h
    src/input/tableteventfilter.h  src/input/tableteventfilter.cpp
    src/ink/inktypes.h             src/ink/geometry.h
    src/ink/tessellate.h           src/ink/tessellate.cpp
    src/ink/inkdocument.h          src/ink/inkdocument.cpp
    src/ink/undostack.h            src/ink/undostack.cpp
    src/ink/onefilter.h
    src/ink/shapes.h               src/ink/shapes.cpp
    src/storage/paths.h            src/storage/paths.cpp
    src/storage/database.h         src/storage/database.cpp
    src/storage/schema.h           src/storage/schema.cpp
    src/storage/strokecodec.h      src/storage/strokecodec.cpp
    src/storage/journal.h          src/storage/journal.cpp
    src/storage/pagestore.h        src/storage/pagestore.cpp
    src/storage/library.h          src/storage/library.cpp
    src/storage/backup.h           src/storage/backup.cpp
    src/storage/notebookfile.h     src/storage/notebookfile.cpp
    src/storage/attachments.h      src/storage/attachments.cpp
    src/pdf/pdfservice.h           src/pdf/pdfservice.cpp
    src/ui/themeiconprovider.h     src/ui/themeiconprovider.cpp
    src/ui/lateximageprovider.h    src/ui/lateximageprovider.cpp
    src/text/textblocks.h          src/text/textblocks.cpp
    src/media/images.h             src/media/images.cpp
    src/media/shapes.h             src/media/shapes.cpp
    src/maths/mathsservice.h       src/maths/mathsservice.cpp
    src/audio/audioservice.h       src/audio/audioservice.cpp
    src/ai/claudeservice.h         src/ai/claudeservice.cpp
    src/cards/cards.h              src/cards/cards.cpp
    src/cards/fsrs.h               src/cards/fsrs.cpp
    src/ocr/linesegment.h          src/ocr/linesegment.cpp
    src/ocr/ocrservice.h           src/ocr/ocrservice.cpp
    src/papers/papersservice.h     src/papers/papersservice.cpp
    src/ui/tabletmode.h            src/ui/tabletmode.cpp
    src/ui/backuptool.h            src/ui/backuptool.cpp
    src/ui/notebooktool.h          src/ui/notebooktool.cpp
    src/ui/thumbnails.h            src/ui/thumbnails.cpp
)
target_include_directories(lumen_core PUBLIC src)
find_package(Qt6 REQUIRED COMPONENTS DBus)
# The on-screen keyboard for tablet mode runs in-process (QT_IM_MODULE=qtvirtualkeyboard).
find_package(Qt6 QUIET COMPONENTS VirtualKeyboard)
find_package(PkgConfig REQUIRED)
pkg_check_modules(SQLITE3 REQUIRED IMPORTED_TARGET sqlite3)
target_link_libraries(lumen_core PUBLIC Qt6::Core Qt6::Gui Qt6::Network Qt6::Quick Qt6::DBus PkgConfig::SQLITE3)
target_compile_definitions(lumen_core PUBLIC
    LUMEN_PROMPTS_SOURCE_DIR="${CMAKE_SOURCE_DIR}/prompts"
    LUMEN_WORKERS_SOURCE_DIR="${LUMEN_WORKERS_SOURCE_DIR}"
    LUMEN_VERSION="${PROJECT_VERSION}"
)

# ---- lumen: the executable + its QML module ---------------------------------------------------
qt_add_executable(lumen main.cpp)
qt_add_resources(lumen "appicon" PREFIX "/icons" BASE "${CMAKE_SOURCE_DIR}/packaging" FILES "${CMAKE_SOURCE_DIR}/packaging/lumen-notes.svg")
set_source_files_properties(qml/Ui.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)
qt_add_qml_module(lumen
    URI Lumen
    VERSION 1.0
    QML_FILES
        qml/Ui.qml
        qml/Rail.qml
        qml/SelectionFrame.qml
        qml/ColourPicker.qml
        qml/TypedPage.qml
        qml/ActionSheet.qml
        qml/ConfirmSheet.qml
        qml/TranscriptPanel.qml
        qml/Main.qml
        qml/Toolbar.qml
        qml/Sidebar.qml
        qml/Icon.qml
        qml/Keyboard.qml
        qml/ImageLayer.qml
        qml/ShapeLayer.qml
        qml/StyleBar.qml
        qml/ObjectMenu.qml
        qml/TrashPage.qml
        qml/ShortcutsPage.qml
        qml/PageBrowser.qml
        qml/TextLayer.qml
        qml/SearchPalette.qml
        qml/AudioPanel.qml
        qml/AiPanel.qml
        qml/OcrPanel.qml
        qml/LatexPopup.qml
        qml/ReviewPage.qml
        qml/CardsPanel.qml
        qml/PapersPanel.qml
        qml/PaperBar.qml
        qml/DashboardPage.qml
        qml/SettingsPage.qml
        qml/Onboarding.qml
        qml/ProbePage.qml
        qml/Readout.qml
        qml/LinkPicker.qml
        qml/BacklinkRow.qml
        qml/PagePanel.qml
        qml/TagChip.qml
        qml/SplitPane.qml
        qml/PagePicker.qml
        qml/LaserOverlay.qml
    SOURCES
        src/input/penprobeitem.h src/input/penprobeitem.cpp
        src/input/keyinjector.h src/input/keyinjector.cpp
        src/canvas/inkcanvas.h src/canvas/inkcanvas.cpp src/canvas/inkcanvas_render.cpp
        src/ui/uitest.h src/ui/uitest.cpp
        src/ui/splitbinder.h src/ui/splitbinder.cpp
        src/text/documentformatter.h src/text/documentformatter.cpp
)
target_include_directories(lumen PRIVATE src src/input src/canvas src/text)
target_link_libraries(lumen PRIVATE lumen_core Qt6::Quick Qt6::Qml)

# --uitest drives the real window. Synthesising *touch* needs QtTest's helper (positions on a
# QEventPoint are otherwise private API), so it is opt-in: on for development builds, off for
# packaging, where the binary must not link libQt6Test.
option(LUMEN_UITEST_TOUCH "Link QtTest so --uitest can synthesise finger input" ON)
if(LUMEN_UITEST_TOUCH)
    find_package(Qt6 REQUIRED COMPONENTS Test)
    target_link_libraries(lumen PRIVATE Qt6::Test)
    target_compile_definitions(lumen PRIVATE LUMEN_HAVE_QTEST)
endif()
