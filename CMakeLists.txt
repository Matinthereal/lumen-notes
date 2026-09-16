cmake_minimum_required(VERSION 3.25)
project(lumen VERSION 0.0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Quick Qml Network Test)
qt_standard_project_setup(REQUIRES 6.5)

# Where the Python workers live for a development build (installed builds set LUMEN_WORKERS_DIR).
set(LUMEN_WORKERS_SOURCE_DIR "${CMAKE_SOURCE_DIR}/workers" CACHE PATH "Python workers directory")

add_subdirectory(app)
enable_testing()
add_subdirectory(tests)

# ---- install (Phase 11): `cmake --install build --prefix ~/.local` needs no root and puts the app
#      in the KDE launcher; the RPM spec in packaging/ uses the same rules with /usr.
include(GNUInstallDirs)
install(TARGETS lumen RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(DIRECTORY workers/ DESTINATION ${CMAKE_INSTALL_DATADIR}/lumen/workers PATTERN "__pycache__" EXCLUDE)
install(DIRECTORY prompts/ DESTINATION ${CMAKE_INSTALL_DATADIR}/lumen/prompts)
install(FILES packaging/lumen-notes.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES packaging/lumen-notes.svg DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)
foreach(sz 16 22 32 48 64 128 256 512)
    install(FILES packaging/icons/${sz}x${sz}/lumen-notes.png DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/${sz}x${sz}/apps)
endforeach()
install(FILES docs/USER-GUIDE.md DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/lumen)
