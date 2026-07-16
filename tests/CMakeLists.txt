find_package(GTest REQUIRED)
find_package(nlohmann_json REQUIRED)

add_executable(filename_parser_test filename_parser_test.cpp)
target_link_libraries(filename_parser_test
    PRIVATE
        synaxis_core
        GTest::gtest
        GTest::gtest_main
        nlohmann_json::nlohmann_json
)
target_compile_definitions(filename_parser_test PRIVATE
    FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
)

include(GoogleTest)
gtest_discover_tests(filename_parser_test)

add_executable(media_library_test media_library_test.cpp)
target_link_libraries(media_library_test
    PRIVATE
        synaxis_core
        GTest::gtest
        GTest::gtest_main
)
target_compile_definitions(media_library_test PRIVATE
    FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
)
gtest_discover_tests(media_library_test)
