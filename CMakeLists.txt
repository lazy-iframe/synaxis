cmake_minimum_required(VERSION 3.20)
project(Synaxis VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(nlohmann_json REQUIRED)

find_package(PkgConfig REQUIRED)
pkg_check_modules(MPV REQUIRED IMPORTED_TARGET mpv)

add_library(synaxis_core
    src/filename_parser.cpp
    src/media_library.cpp
    src/player.cpp
)
target_include_directories(synaxis_core PUBLIC include)
target_link_libraries(synaxis_core PUBLIC nlohmann_json::nlohmann_json PkgConfig::MPV)

add_executable(synaxis_cli src/main.cpp)
target_link_libraries(synaxis_cli PRIVATE synaxis_core)
target_compile_definitions(synaxis_cli PRIVATE SYNAXIS_VERSION="${PROJECT_VERSION}")

enable_testing()
add_subdirectory(tests)
