find_package(Catch2 3 CONFIG REQUIRED)

add_executable(mdview_tests
    native/test_main.cpp
    native/utf_tests.cpp
    native/paths_tests.cpp)

target_link_libraries(mdview_tests PRIVATE
    mdview_core
    Catch2::Catch2WithMain)

mdview_set_warnings(mdview_tests)

include(Catch)
catch_discover_tests(mdview_tests)
