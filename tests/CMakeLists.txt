find_package(Catch2 3 CONFIG REQUIRED)

add_executable(mdview_tests
    native/test_main.cpp)

target_link_libraries(mdview_tests PRIVATE
    Catch2::Catch2WithMain)

mdview_set_warnings(mdview_tests)

include(Catch)
catch_discover_tests(mdview_tests)
