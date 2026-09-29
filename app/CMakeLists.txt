# ---- lumen_core: everything testable, no QML ------------------------------------------------
qt_add_library(lumen_core STATIC
    src/ipc/jsonlines.h            src/ipc/jsonlines.cpp
    src/workers/workersupervisor.h src/workers/workersupervisor.cpp
    src/workers/pingmeter.h        src/workers/pingmeter.cpp
    src/input/tabletsample.h
    src/input/tableteventfilter.h  src/input/tableteventfilter.cpp
    src/input/stylustilt.h         src/input/stylustilt.cpp
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
    src/ui/holdtips.h              src/ui/holdtips.cpp
)
target_include_directories(lumen_core PUBLIC src)

# ---- Without the Python helpers, WorkerSupervisor serves the pdf helper in-process on PDFium:
#      prebuilt by github.com/bblanchon/pdfium-binaries (MIT). It is the engine QtPdf is built on,
#      and Qt ships no QtPdf for Android.
if(LUMEN_HELPERS)
    target_compile_definitions(lumen_core PUBLIC LUMEN_HAVE_HELPERS)
else()
    set(_pdfium_release https://github.com/bblanchon/pdfium-binaries/releases/download/chromium%2F8066)
    if(ANDROID AND ANDROID_ABI STREQUAL "arm64-v8a")
        set(_pdfium android-arm64 a665e3a9d40fb0024e3959261a400a722a13f7aa6602c59a82c93d44a362c055)
    elseif(ANDROID AND ANDROID_ABI STREQUAL "x86_64")
        set(_pdfium android-x64 44b9444d58f055ab892019aea27423bab74d671a37b5bad84647a91abec82a1d)
    elseif(IOS)
        set(_pdfium ios-device-arm64 0b4c5e0a6a4e9e6102ca832b704a676b1bbd5fb7ae7ba23caeb91bbca305a952)
    elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64")
        set(_pdfium linux-x64 0b43f405477cf2cfc4dbff06905093c3309756c6bca1fb9da99234a2ca97fed2)
    else()
        message(FATAL_ERROR "No PDFium package is set up for this platform; build with -DLUMEN_HELPERS=ON")
    endif()
    list(GET _pdfium 0 _pdfium_package)
    list(GET _pdfium 1 _pdfium_sha256)
    include(FetchContent)
    FetchContent_Declare(lumen_pdfium
        URL ${_pdfium_release}/pdfium-${_pdfium_package}.tgz
        URL_HASH SHA256=${_pdfium_sha256}
        SOURCE_SUBDIR no-cmake-project      # a prebuilt package: fetch it, don't add_subdirectory it
    )
    FetchContent_MakeAvailable(lumen_pdfium)
    set(LUMEN_PDFIUM_LIBRARY ${lumen_pdfium_SOURCE_DIR}/lib/libpdfium${CMAKE_SHARED_LIBRARY_SUFFIX})
    if(IOS)
        # The package names itself ./libpdfium.dylib, which iOS cannot load; the bundle carries it
        # in Frameworks/ and finds it through @rpath (see the lumen target below).
        file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/pdfium)
        file(COPY_FILE ${LUMEN_PDFIUM_LIBRARY} ${CMAKE_BINARY_DIR}/pdfium/libpdfium.dylib)
        set(LUMEN_PDFIUM_LIBRARY ${CMAKE_BINARY_DIR}/pdfium/libpdfium.dylib)
        execute_process(COMMAND install_name_tool -id @rpath/libpdfium.dylib ${LUMEN_PDFIUM_LIBRARY} COMMAND_ERROR_IS_FATAL ANY)
    endif()
    add_library(lumen_pdfium SHARED IMPORTED)
    set_target_properties(lumen_pdfium PROPERTIES
        IMPORTED_LOCATION ${LUMEN_PDFIUM_LIBRARY}
        INTERFACE_INCLUDE_DIRECTORIES ${lumen_pdfium_SOURCE_DIR}/include)
    target_sources(lumen_core PRIVATE
        src/workers/inprocessworker.h
        src/workers/workersupervisor_inprocess.cpp
        src/pdf/pdfengine.h            src/pdf/pdfengine.cpp)
    target_link_libraries(lumen_core PUBLIC lumen_pdfium)
    target_compile_definitions(lumen_core PUBLIC LUMEN_HAVE_PDFIUM)
endif()

# ---- D-Bus: tablet-mode's KWin auto-detect (tabletmode.cpp) is Linux/Plasma-only and optional —
#      without it the manual tablet-mode toggle still works, just with no KWin auto-detect.
if(LINUX)
    set(LUMEN_ENABLE_DBUS_DEFAULT ON)
else()
    set(LUMEN_ENABLE_DBUS_DEFAULT OFF)
endif()
option(LUMEN_ENABLE_DBUS "Build the KWin D-Bus tablet-mode integration (Linux only)" ${LUMEN_ENABLE_DBUS_DEFAULT})
if(LUMEN_ENABLE_DBUS)
    find_package(Qt6 QUIET COMPONENTS DBus)
endif()
# Gui/Quick can pull in a Qt6::DBus target transitively even when it wasn't requested (Linux QPA
# plugins use it), so gate on the option too — not just on the target existing.
if(LUMEN_ENABLE_DBUS AND TARGET Qt6::DBus)
    target_link_libraries(lumen_core PUBLIC Qt6::DBus)
    target_compile_definitions(lumen_core PUBLIC LUMEN_HAVE_DBUS)
else()
    message(STATUS "Qt6::DBus not built: KWin tablet-mode auto-detect is off, the manual toggle still works")
endif()

# The on-screen keyboard for tablet mode runs in-process (QT_IM_MODULE=qtvirtualkeyboard).
find_package(Qt6 QUIET COMPONENTS VirtualKeyboard)

# ---- SQLite: the system library via pkg-config, or a bundled amalgamation when that isn't
#      available (no pkg-config sqlite3 package on Windows) or when forced with LUMEN_BUNDLED_SQLITE.
option(LUMEN_BUNDLED_SQLITE "Fetch and build the SQLite amalgamation instead of the system library" OFF)
if(LUMEN_MOBILE)
    set(LUMEN_BUNDLED_SQLITE ON)     # pkg-config would find the build machine's own library
endif()
if(NOT LUMEN_BUNDLED_SQLITE)
    find_package(PkgConfig)
    if(PkgConfig_FOUND)
        pkg_check_modules(SQLITE3 IMPORTED_TARGET sqlite3)
    endif()
endif()
if(NOT TARGET PkgConfig::SQLITE3)
    if(NOT LUMEN_BUNDLED_SQLITE)
        message(STATUS "system sqlite3 not found via pkg-config: building the bundled amalgamation instead")
    endif()
    enable_language(C)   # the amalgamation is plain C; the project itself only enables CXX above
    include(FetchContent)
    FetchContent_Declare(lumen_sqlite3
        URL https://www.sqlite.org/2026/sqlite-amalgamation-3530400.zip
        URL_HASH SHA3_256=628a44cfe82c66aed1ccbbe85a562d2e33ebe64b3288981ed76285612227934e
    )
    FetchContent_MakeAvailable(lumen_sqlite3)
    add_library(lumen_bundled_sqlite3 STATIC ${lumen_sqlite3_SOURCE_DIR}/sqlite3.c)
    target_include_directories(lumen_bundled_sqlite3 PUBLIC ${lumen_sqlite3_SOURCE_DIR})
    # FTS5 is the schema's search index (schema.cpp); WAL is a runtime PRAGMA, not a build flag.
    target_compile_definitions(lumen_bundled_sqlite3 PUBLIC SQLITE_ENABLE_FTS5 SQLITE_THREADSAFE=1)
    if(UNIX)
        target_link_libraries(lumen_bundled_sqlite3 PUBLIC ${CMAKE_DL_LIBS})
    endif()
    add_library(PkgConfig::SQLITE3 ALIAS lumen_bundled_sqlite3)
endif()

target_link_libraries(lumen_core PUBLIC Qt6::Core Qt6::Gui Qt6::Network Qt6::Quick PkgConfig::SQLITE3)
target_compile_definitions(lumen_core PUBLIC
    LUMEN_PROMPTS_SOURCE_DIR="${CMAKE_SOURCE_DIR}/prompts"
    LUMEN_WORKERS_SOURCE_DIR="${LUMEN_WORKERS_SOURCE_DIR}"
    LUMEN_VERSION="${PROJECT_VERSION}"
)

# ---- lumen: the executable + its QML module ---------------------------------------------------
qt_add_executable(lumen main.cpp)
qt_add_resources(lumen "appicon" PREFIX "/icons" BASE "${CMAKE_SOURCE_DIR}/packaging" FILES "${CMAKE_SOURCE_DIR}/packaging/lumen-notes.svg")
# Lumen's typefaces (ADR mynotes-003, packaging/make-ui-fonts.sh): Figtree for the interface,
# Newsreader for titles. SIL OFL 1.1; the licences sit beside them.
qt_add_resources(lumen "uifonts" PREFIX "/fonts" BASE "${CMAKE_SOURCE_DIR}/packaging/fonts" FILES
    "${CMAKE_SOURCE_DIR}/packaging/fonts/Figtree-Regular.ttf" "${CMAKE_SOURCE_DIR}/packaging/fonts/Figtree-Medium.ttf"
    "${CMAKE_SOURCE_DIR}/packaging/fonts/Figtree-SemiBold.ttf" "${CMAKE_SOURCE_DIR}/packaging/fonts/Figtree-Bold.ttf"
    "${CMAKE_SOURCE_DIR}/packaging/fonts/Newsreader-Medium.ttf")
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
        qml/Elevation.qml
        qml/Card.qml
        qml/PopIn.qml
        qml/PopOut.qml
        qml/LButton.qml
        qml/LSwitch.qml
        qml/LSlider.qml
        qml/LField.qml
        qml/PenPopover.qml
        qml/LibraryPage.qml
        qml/LCombo.qml
        qml/Keyboard.qml
        qml/ImageLayer.qml
        qml/ShapeLayer.qml
        qml/StyleBar.qml
        qml/ObjectMenu.qml
        qml/TrashPage.qml
        qml/ShortcutsPage.qml
        qml/PageBrowser.qml
        qml/StickyNotes.qml
        qml/NewPageChooser.qml
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
        qml/HoldTip.qml
        qml/ProgressChip.qml
        qml/ServicesSection.qml
    SOURCES
        src/input/penprobeitem.h src/input/penprobeitem.cpp
        src/input/keyinjector.h src/input/keyinjector.cpp
        src/canvas/inkcanvas.h src/canvas/inkcanvas.cpp src/canvas/inkcanvas_render.cpp
        src/ui/uitest.h src/ui/uitest.cpp
        src/ui/splitbinder.h src/ui/splitbinder.cpp
        src/ui/platform.h src/ui/platform.cpp
        src/ui/theme.h src/ui/theme.cpp
        src/text/documentformatter.h src/text/documentformatter.cpp
)
target_include_directories(lumen PRIVATE src src/input src/canvas src/text)
target_link_libraries(lumen PRIVATE lumen_core Qt6::Quick Qt6::Qml)

# ---- Android and iPad packaging (ADR mynotes-002). The app id is permanent: an update only
#      installs over an app with the same id and signing key.
set(LUMEN_APP_ID io.github.matinthereal.lumen)
math(EXPR LUMEN_VERSION_CODE "${PROJECT_VERSION_MAJOR} * 10000 + ${PROJECT_VERSION_MINOR} * 100 + ${PROJECT_VERSION_PATCH}")
if(ANDROID)
    set_target_properties(lumen PROPERTIES
        QT_ANDROID_PACKAGE_NAME ${LUMEN_APP_ID}
        QT_ANDROID_APP_NAME Lumen
        QT_ANDROID_APP_ICON @mipmap/ic_launcher
        QT_ANDROID_PACKAGE_SOURCE_DIR ${CMAKE_SOURCE_DIR}/packaging/android
        QT_ANDROID_VERSION_NAME ${PROJECT_VERSION}
        QT_ANDROID_VERSION_CODE ${LUMEN_VERSION_CODE}
        QT_ANDROID_MIN_SDK_VERSION 28
        QT_ANDROID_TARGET_SDK_VERSION 35)
    set_property(TARGET lumen APPEND PROPERTY QT_ANDROID_EXTRA_LIBS ${LUMEN_PDFIUM_LIBRARY})
    qt_add_resources(lumen "fonts" PREFIX "/fonts" BASE "${CMAKE_SOURCE_DIR}/packaging/fonts"
                     FILES "${CMAKE_SOURCE_DIR}/packaging/fonts/symbols.ttf")
elseif(IOS)
    target_sources(lumen PRIVATE src/ui/platform_ios.mm)
    set_source_files_properties(src/ui/platform_ios.mm PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
    target_link_libraries(lumen PRIVATE "-framework UIKit")
    set(asset_catalog ${CMAKE_SOURCE_DIR}/packaging/ios/Assets.xcassets)
    target_sources(lumen PRIVATE ${asset_catalog})
    set_source_files_properties(${asset_catalog} PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
    set_target_properties(lumen PROPERTIES
        OUTPUT_NAME Lumen
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_INFO_PLIST ${CMAKE_SOURCE_DIR}/packaging/ios/Info.plist.in
        MACOSX_BUNDLE_GUI_IDENTIFIER ${LUMEN_APP_ID}
        MACOSX_BUNDLE_BUNDLE_NAME Lumen
        MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION}
        MACOSX_BUNDLE_BUNDLE_VERSION ${LUMEN_VERSION_CODE}
        XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER ${LUMEN_APP_ID}
        XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY "2"                   # iPad
        XCODE_ATTRIBUTE_ASSETCATALOG_COMPILER_APPICON_NAME AppIcon
        XCODE_ATTRIBUTE_LD_RUNPATH_SEARCH_PATHS "@executable_path/Frameworks"
        XCODE_EMBED_FRAMEWORKS ${LUMEN_PDFIUM_LIBRARY}
        XCODE_EMBED_FRAMEWORKS_CODE_SIGN_ON_COPY ON)
endif()

# --uitest drives the real window. Synthesising *touch* needs QtTest's helper (positions on a
# QEventPoint are otherwise private API), so it is opt-in: on for development builds, off for
# packaging, where the binary must not link libQt6Test.
if(LUMEN_MOBILE)
    set(LUMEN_UITEST_TOUCH_DEFAULT OFF)
else()
    set(LUMEN_UITEST_TOUCH_DEFAULT ON)
endif()
option(LUMEN_UITEST_TOUCH "Link QtTest so --uitest can synthesise finger input" ${LUMEN_UITEST_TOUCH_DEFAULT})
if(LUMEN_UITEST_TOUCH)
    find_package(Qt6 REQUIRED COMPONENTS Test)
    target_link_libraries(lumen PRIVATE Qt6::Test)
    target_compile_definitions(lumen PRIVATE LUMEN_HAVE_QTEST)
endif()
