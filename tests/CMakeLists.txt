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
    native/test_module_pin.cpp
    native/test_precache_manager.cpp
    native/test_renderer_protocol.cpp
    native/test_viewer_host.cpp
    native/test_viewer_paths.cpp
    native/test_win32_window.cpp
    native/test_encoding.cpp
    native/test_document_loader.cpp
    native/test_asset_router.cpp
    native/test_embedded_assets.cpp
    native/test_embedded_resource_stream.cpp
    native/test_externalize.cpp
    native/test_theme.cpp
    native/test_plugin_window_commands.cpp
    native/test_listclose_reentrancy.cpp
    native/test_summary_v2.cpp
    native/test_log_bridge.cpp
    # Compiled into the unit-test exe too so test_summary_v2 can
    # exercise the parser without spinning up an integration session.
    integration/summary.cpp)

target_include_directories(mdview_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(mdview_tests PRIVATE
    mdview_core
    Catch2::Catch2WithMain)

mdview_set_warnings(mdview_tests)

target_precompile_headers(mdview_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/cmake/pch.hpp)

include(Catch)
catch_discover_tests(mdview_tests)

option(MDVIEW_BUILD_INTEGRATION_TESTS
       "Build the WLX integration test executable"
       ON)

if(MDVIEW_BUILD_INTEGRATION_TESTS)
    add_subdirectory(integration)
endif()
