cmake_minimum_required(VERSION 3.25)
# SPDX-License-Identifier: GPL-3.0-or-later
project(lumen VERSION 0.2.1 LANGUAGES CXX
    DESCRIPTION "Pen, touch and keyboard notes for Qt/QML")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(IOS)
    enable_language(OBJCXX)     # app/src/ui/platform_ios.mm: Apple Pencil, the share sheet, Files
endif()
find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Quick Qml Network Test)
qt_standard_project_setup(REQUIRES 6.5)

# Where the Python workers live for a development build (installed builds set LUMEN_WORKERS_DIR).
set(LUMEN_WORKERS_SOURCE_DIR "${CMAKE_SOURCE_DIR}/workers" CACHE PATH "Python workers directory")

# Android and iOS (ADR mynotes-002): no Python helpers -- iOS cannot start processes at all -- so
# the PDF helper runs in-process on PDFium and the other helpers' features are hidden. A desktop
# build can do the same with -DLUMEN_HELPERS=OFF, which is how the mobile paths are tested here.
if(ANDROID OR IOS)
    set(LUMEN_MOBILE ON)
else()
    set(LUMEN_MOBILE OFF)
endif()
if(LUMEN_MOBILE)
    set(LUMEN_HELPERS_DEFAULT OFF)
else()
    set(LUMEN_HELPERS_DEFAULT ON)
endif()
option(LUMEN_HELPERS "Run the PDF/audio/OCR/maths/AI features in Python helper processes" ${LUMEN_HELPERS_DEFAULT})

add_subdirectory(app)
if(NOT LUMEN_MOBILE)
    enable_testing()
    add_subdirectory(tests)
endif()

# ---- install (Phase 11): `cmake --install build --prefix ~/.local` needs no root and puts the app
#      in the KDE launcher; the AppImage and the RPM spec in packaging/ use the same rules with /usr.
#      Windows installs are flat (no bin/ + share/ split -- see workersDir()/promptsDir() in
#      workersupervisor.cpp/main.cpp, which look next to applicationDirPath() there instead of
#      one level up from it); the Inno Setup script in packaging/windows/ builds on this layout.
include(GNUInstallDirs)
if(LUMEN_MOBILE)
    # packaged by androiddeployqt / Xcode, not installed
elseif(WIN32)
    install(TARGETS lumen RUNTIME DESTINATION .)
    install(DIRECTORY workers/ DESTINATION workers PATTERN "__pycache__" EXCLUDE)
    install(DIRECTORY prompts/ DESTINATION prompts)
    install(FILES docs/USER-GUIDE.md DESTINATION .)
else()
    install(TARGETS lumen RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    install(DIRECTORY workers/ DESTINATION ${CMAKE_INSTALL_DATADIR}/lumen/workers PATTERN "__pycache__" EXCLUDE)
    install(DIRECTORY prompts/ DESTINATION ${CMAKE_INSTALL_DATADIR}/lumen/prompts)
    install(FILES packaging/lumen-notes.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
    install(FILES packaging/lumen-notes.svg DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)
    foreach(sz 16 22 32 48 64 128 256 512)
        install(FILES packaging/icons/${sz}x${sz}/lumen-notes.png DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/${sz}x${sz}/apps)
    endforeach()
    install(FILES docs/USER-GUIDE.md DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/lumen)
endif()
