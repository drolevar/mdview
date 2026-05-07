find_package(Catch2 3 CONFIG REQUIRED)

add_executable(mdview_tests
    native/test_main.cpp
    native/utf_tests.cpp
    native/paths_tests.cpp
    native/version_tests.cpp
    native/plugin_globals_tests.cpp
    native/detect_string_tests.cpp
    native/test_event_revoker.cpp
    native/test_init_error.cpp
    native/test_renderer_protocol.cpp
    native/test_viewer_paths.cpp
    native/test_win32_window.cpp)

target_link_libraries(mdview_tests PRIVATE
    mdview_core
    Catch2::Catch2WithMain)

mdview_set_warnings(mdview_tests)

include(Catch)
catch_discover_tests(mdview_tests)
