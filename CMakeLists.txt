cmake_minimum_required(VERSION 3.20)
project(Synaxis VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# The GUI is optional so that a CLI-only build doesn't drag in Qt. synaxis_core
# stays Qt-free either way: only synaxis_gui links Qt, which is what keeps the
# CLI buildable on a machine without it.
option(SYNAXIS_BUILD_GUI "Build the Qt Quick GUI (synaxis_gui)" ON)
option(SYNAXIS_BUILD_TESTS "Build unit tests (synaxis_tests)" ON)

find_package(nlohmann_json REQUIRED)
find_package(Threads REQUIRED)

find_package(PkgConfig REQUIRED)
pkg_check_modules(MPV REQUIRED IMPORTED_TARGET mpv)
pkg_check_modules(VLC REQUIRED IMPORTED_TARGET libvlc)

add_library(synaxis_core
    src/artwork.cpp
    src/artwork_frame.cpp
    src/filename_parser.cpp
    src/media_library.cpp
    src/player.cpp
    src/player_mpv.cpp
    src/player_vlc.cpp
    src/watch_state.cpp
)
target_include_directories(synaxis_core PUBLIC include)
target_link_libraries(synaxis_core
    PUBLIC nlohmann_json::nlohmann_json Threads::Threads
    PRIVATE PkgConfig::MPV PkgConfig::VLC
)

add_executable(synaxis_cli src/main.cpp)
target_link_libraries(synaxis_cli PRIVATE synaxis_core)
target_compile_definitions(synaxis_cli PRIVATE SYNAXIS_VERSION="${PROJECT_VERSION}")

if(SYNAXIS_BUILD_TESTS)
    find_package(GTest REQUIRED)
    include(GoogleTest)
    enable_testing()

    add_executable(synaxis_tests
        tests/artwork_test.cpp
        tests/filename_parser_test.cpp
        tests/media_library_test.cpp
        tests/watch_state_test.cpp
    )
    target_link_libraries(synaxis_tests PRIVATE synaxis_core GTest::gmock GTest::gtest_main)

    # The artwork tests need real video to decode. Rather than committing
    # binaries (tests/movies is gitignored for that reason), generate exactly
    # the clips the tests need with ffmpeg. Each one targets a specific
    # behaviour of the frame provider's blank-frame heuristic, which a stray
    # sample file can't be relied on to exercise.
    #
    # ffmpeg is optional: without it the fixture-dependent tests skip, and
    # everything that doesn't decode video still runs.
    find_program(FFMPEG_EXECUTABLE ffmpeg)
    if(FFMPEG_EXECUTABLE)
        set(SYNAXIS_FIXTURE_DIR "${CMAKE_CURRENT_BINARY_DIR}/test_fixtures")

        # 1280x720 sources, so the provider's scale-to-640 filter is genuinely
        # exercised rather than being a no-op on an already-small frame.
        set(_lavfi_colorful    "testsrc=size=1280x720:rate=10:duration=10")
        set(_lavfi_black       "color=c=black:s=1280x720:r=10:d=10")
        set(_lavfi_black_first "color=c=black:s=1280x720:r=10:d=5")
        set(_lavfi_test_second "testsrc=size=1280x720:rate=10:duration=5")

        # Ordinary video: a real frame at the very first offset tried.
        add_custom_command(
            OUTPUT "${SYNAXIS_FIXTURE_DIR}/colorful.mp4"
            COMMAND ${CMAKE_COMMAND} -E make_directory "${SYNAXIS_FIXTURE_DIR}"
            COMMAND ${FFMPEG_EXECUTABLE} -y -loglevel error
                    -f lavfi -i "${_lavfi_colorful}"
                    -c:v libx264 -pix_fmt yuv420p "${SYNAXIS_FIXTURE_DIR}/colorful.mp4"
            COMMENT "Generating test fixture: colorful.mp4"
            VERBATIM)

        # Black for its first half: the 10% and 35% grabs land on black and only
        # the 60% one finds a picture. This is the regression that motivated the
        # retry in the first place.
        add_custom_command(
            OUTPUT "${SYNAXIS_FIXTURE_DIR}/black_opening.mp4"
            COMMAND ${CMAKE_COMMAND} -E make_directory "${SYNAXIS_FIXTURE_DIR}"
            COMMAND ${FFMPEG_EXECUTABLE} -y -loglevel error
                    -f lavfi -i "${_lavfi_black_first}"
                    -f lavfi -i "${_lavfi_test_second}"
                    -filter_complex "[0:v][1:v]concat=n=2:v=1[v]" -map "[v]"
                    -c:v libx264 -pix_fmt yuv420p "${SYNAXIS_FIXTURE_DIR}/black_opening.mp4"
            COMMENT "Generating test fixture: black_opening.mp4"
            VERBATIM)

        # Black throughout: no offset can help, and the provider must still
        # return its best rather than declining a playable file.
        add_custom_command(
            OUTPUT "${SYNAXIS_FIXTURE_DIR}/all_black.mp4"
            COMMAND ${CMAKE_COMMAND} -E make_directory "${SYNAXIS_FIXTURE_DIR}"
            COMMAND ${FFMPEG_EXECUTABLE} -y -loglevel error
                    -f lavfi -i "${_lavfi_black}"
                    -c:v libx264 -pix_fmt yuv420p "${SYNAXIS_FIXTURE_DIR}/all_black.mp4"
            COMMENT "Generating test fixture: all_black.mp4"
            VERBATIM)

        add_custom_target(synaxis_test_fixtures DEPENDS
            "${SYNAXIS_FIXTURE_DIR}/colorful.mp4"
            "${SYNAXIS_FIXTURE_DIR}/black_opening.mp4"
            "${SYNAXIS_FIXTURE_DIR}/all_black.mp4")
        add_dependencies(synaxis_tests synaxis_test_fixtures)

        target_compile_definitions(synaxis_tests PRIVATE
            SYNAXIS_FIXTURE_DIR="${SYNAXIS_FIXTURE_DIR}")
    else()
        message(STATUS
            "ffmpeg not found: artwork tests that decode video will skip. "
            "Install ffmpeg to run the full suite.")
    endif()

    gtest_discover_tests(synaxis_tests)
endif()

if(SYNAXIS_BUILD_GUI)
    # 6.5 is the floor for qt_standard_project_setup(REQUIRES) and for
    # loadFromModule(), both of which this target uses. QuickDialogs2 backs
    # the settings page's directory picker (QtQuick.Dialogs' FolderDialog).
    find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Network Quick QuickDialogs2)
    qt_standard_project_setup(REQUIRES 6.5)

    qt_add_executable(synaxis_gui
        src/gui/main.cpp
        src/gui/app_config.cpp
        src/gui/library_controller.cpp
        src/gui/mpv_item.cpp
        src/gui/shelf_model.cpp
        src/gui/tmdb_provider.cpp
    )

    # Theme is a QML singleton, which the module has to be told about: the
    # `pragma Singleton` in the file alone isn't enough for the module's
    # generated qmldir to declare it.
    set_source_files_properties(src/gui/qml/Theme.qml
        PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

    # QML_FILES are compiled into the binary rather than loaded from disk, so
    # the GUI runs from the build tree without an install step or a QML import
    # path pointing back at the source.
    # The QML module's version is its own API version, not the application's —
    # tying it to PROJECT_VERSION put the module at major 0, which is an odd
    # corner for QML's import resolution and isn't worth exercising to express
    # "the app is 0.1.0". main.cpp registers the Library singleton against the
    # same 1.0.
    qt_add_qml_module(synaxis_gui
        URI Synaxis
        VERSION 1.0
        QML_FILES
            src/gui/qml/Main.qml
            src/gui/qml/Hero.qml
            src/gui/qml/Shelf.qml
            src/gui/qml/Tile.qml
            src/gui/qml/Theme.qml
            src/gui/qml/PlayerOverlay.qml
            src/gui/qml/SettingsPage.qml
        SOURCES
            src/gui/app_config.hpp
            src/gui/library_controller.hpp
            src/gui/mpv_item.hpp
            src/gui/shelf_model.hpp
            src/gui/tmdb_provider.hpp
    )

    # qt_add_qml_module's generated registration code includes declaring headers
    # as <mpv_item.hpp>, angle brackets and all, so their directory has to be on
    # the include path. Without this the include silently no-ops behind its
    # __has_include guard and the QML_ELEMENT types fail to compile as undeclared.
    target_include_directories(synaxis_gui PRIVATE src/gui)

    target_link_libraries(synaxis_gui PRIVATE
        synaxis_core Qt6::Network Qt6::Quick Qt6::QuickDialogs2)
    target_compile_definitions(synaxis_gui PRIVATE SYNAXIS_VERSION="${PROJECT_VERSION}")
endif()
