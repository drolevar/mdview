find_package(wil CONFIG REQUIRED)

add_library(mdview_core STATIC
    common/utf.cpp
    common/paths.cpp
    common/version.cpp
    native/plugin_globals.cpp
    native/detect_string.cpp
    platform/win32_window.cpp
    plugin/plugin_window.cpp)

target_include_directories(mdview_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(mdview_core PUBLIC
    WIL::WIL)

mdview_set_warnings(mdview_core)
