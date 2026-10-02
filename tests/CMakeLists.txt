qt_add_executable(tst_jsonlines tst_jsonlines.cpp)
target_link_libraries(tst_jsonlines PRIVATE lumen_core Qt6::Test)
add_test(NAME jsonlines COMMAND tst_jsonlines)

# The Python helper processes themselves: only in builds that have them.
if(LUMEN_HELPERS)
    qt_add_executable(tst_worker_ping tst_worker_ping.cpp)
    target_link_libraries(tst_worker_ping PRIVATE lumen_core Qt6::Test)
    add_test(NAME worker_ping COMMAND tst_worker_ping)
endif()

qt_add_executable(tst_ink tst_ink.cpp)
target_link_libraries(tst_ink PRIVATE lumen_core Qt6::Test)
add_test(NAME ink COMMAND tst_ink)

qt_add_executable(tst_storage tst_storage.cpp)
target_link_libraries(tst_storage PRIVATE lumen_core Qt6::Test)
add_test(NAME storage COMMAND tst_storage)

# Pictures in and out by the clipboard. It needs a QGuiApplication for the clipboard, nothing else.
qt_add_executable(tst_images tst_images.cpp)
target_link_libraries(tst_images PRIVATE lumen_core Qt6::Test)
add_test(NAME images COMMAND tst_images)
set_tests_properties(images PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

qt_add_executable(soak_writer soak_writer.cpp)
target_link_libraries(soak_writer PRIVATE lumen_core)
qt_add_executable(tst_soak tst_soak.cpp)
target_link_libraries(tst_soak PRIVATE lumen_core Qt6::Test)
add_dependencies(tst_soak soak_writer)
add_test(NAME soak COMMAND tst_soak)
set_tests_properties(soak PROPERTIES TIMEOUT 300)

qt_add_executable(tst_pdf tst_pdf.cpp)
target_link_libraries(tst_pdf PRIVATE lumen_core Qt6::Test)
add_test(NAME pdf COMMAND tst_pdf)
set_tests_properties(pdf PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 180)

if(LUMEN_HELPERS)
    qt_add_executable(tst_audio tst_audio.cpp)
    target_link_libraries(tst_audio PRIVATE lumen_core Qt6::Test)
    add_test(NAME audio COMMAND tst_audio)
    set_tests_properties(audio PROPERTIES TIMEOUT 900)

    qt_add_executable(tst_ocr tst_ocr.cpp)
    target_link_libraries(tst_ocr PRIVATE lumen_core Qt6::Test)
    add_test(NAME ocr COMMAND tst_ocr)
    set_tests_properties(ocr PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 1800)
endif()

qt_add_executable(tst_fsrs tst_fsrs.cpp)
target_link_libraries(tst_fsrs PRIVATE lumen_core Qt6::Test)
add_test(NAME fsrs COMMAND tst_fsrs)

qt_add_executable(tst_papers tst_papers.cpp)
target_link_libraries(tst_papers PRIVATE lumen_core Qt6::Test)
add_test(NAME papers COMMAND tst_papers)
set_tests_properties(papers PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 300)

# One window per library on a desktop: a second launch hands its files over (main.cpp).
if(LUMEN_HELPERS)
    qt_add_executable(tst_singleinstance tst_singleinstance.cpp)
    target_link_libraries(tst_singleinstance PRIVATE lumen_core Qt6::Test)
    target_compile_definitions(tst_singleinstance PRIVATE LUMEN_APP="$<TARGET_FILE:lumen>")
    add_dependencies(tst_singleinstance lumen)
    add_test(NAME single_instance COMMAND tst_singleinstance)
    set_tests_properties(single_instance PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 180)
endif()

qt_add_executable(tst_canvas tst_canvas.cpp
    ../app/src/canvas/inkcanvas.cpp ../app/src/canvas/inkcanvas_render.cpp)
target_include_directories(tst_canvas PRIVATE ../app/src ../app/src/canvas)
target_link_libraries(tst_canvas PRIVATE lumen_core Qt6::Test Qt6::Quick)
add_test(NAME canvas COMMAND tst_canvas)
set_tests_properties(canvas PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

qt_add_executable(tst_holdtips tst_holdtips.cpp)
target_link_libraries(tst_holdtips PRIVATE lumen_core Qt6::Test Qt6::Quick Qt6::Qml)
add_test(NAME holdtips COMMAND tst_holdtips)
set_tests_properties(holdtips PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# The app itself must start, show the probe, ping the worker and exit cleanly — headless.
add_test(NAME app_smoke COMMAND lumen --smoke)
set_tests_properties(app_smoke PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_FORCE_STDERR_LOGGING=1;LUMEN_DATA_DIR=${CMAKE_BINARY_DIR}/smoke-data" TIMEOUT 60)

# The frontend battle test: taps, holds, deletes and undoes in the real window, and fails on any
# QML warning it provokes.
# It starts from an empty library every run: its checks assume one (a sticky note's colour, the
# split's ratio, what is in the trash), and a library kept from earlier runs failed them.
add_test(NAME app_ui_reset COMMAND ${CMAKE_COMMAND} -E rm -rf ${CMAKE_BINARY_DIR}/uitest-data)
set_tests_properties(app_ui_reset PROPERTIES FIXTURES_SETUP app_ui_library)
# The helper models live outside it and are kept: downloading them again every run left the
# handwriting reader busy with the download when the maths check asked it for work.
add_test(NAME app_ui COMMAND lumen --uitest)
set_tests_properties(app_ui PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_FORCE_STDERR_LOGGING=1;LUMEN_DATA_DIR=${CMAKE_BINARY_DIR}/uitest-data;LUMEN_MODELS_DIR=${CMAKE_BINARY_DIR}/helper-models" TIMEOUT 900
                     FIXTURES_REQUIRED app_ui_library)

# The maths worker: it must get school arithmetic and algebra right, and say so when it cannot.
if(LUMEN_HELPERS)
    qt_add_executable(tst_maths tst_maths.cpp)
    target_link_libraries(tst_maths PRIVATE lumen_core Qt6::Test)
    add_test(NAME maths COMMAND tst_maths)
    set_tests_properties(maths PROPERTIES TIMEOUT 300)
endif()

# Not a test: replays real strokes from a copy of a library to measure pen changes (ADR mynotes-003).
qt_add_executable(pen_replay pen_replay.cpp)
target_link_libraries(pen_replay PRIVATE lumen_core)
