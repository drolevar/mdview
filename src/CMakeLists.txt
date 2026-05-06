find_package(wil CONFIG REQUIRED)

add_library(mdview_core STATIC
    common/utf.cpp
    common/paths.cpp)

target_include_directories(mdview_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(mdview_core PUBLIC
    WIL::WIL)

mdview_set_warnings(mdview_core)
