include(TcmdPlugin)

find_package(wil CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(unofficial-webview2 CONFIG REQUIRED)

add_library(mdview_core STATIC
    common/utf.cpp
    common/paths.cpp
    common/version.cpp
    native/plugin_globals.cpp
    native/debug_log.cpp
    native/detect_string.cpp
    native/event_revoker.cpp
    native/init_error.cpp
    native/renderer_protocol.cpp
    native/viewer_paths.cpp
    native/webview2_environment.cpp
    native/webview2_host.cpp
    platform/win32_window.cpp
    plugin/plugin_window.cpp
    plugin/fallback_window.cpp)

target_include_directories(mdview_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(mdview_core PUBLIC
    WIL::WIL
    nlohmann_json::nlohmann_json
    unofficial::webview2::webview2)

target_compile_definitions(mdview_core PUBLIC
    WIL_LOCK_EXCEPTION_MODE)

mdview_set_warnings(mdview_core)

add_library(mdview_wlx SHARED
    plugin/dll_main.cpp
    plugin/wlx_exports.cpp
    plugin/wlx_exports.def)

target_include_directories(mdview_wlx PRIVATE
    ${CMAKE_SOURCE_DIR}/external/totalcmd-wlx-sdk/src)

target_link_libraries(mdview_wlx PRIVATE
    mdview_core)

mdview_set_warnings(mdview_wlx)
mdview_configure_wlx_plugin(mdview_wlx)
