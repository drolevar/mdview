# Standalone feasibility probe: its own vcpkg manifest, NOT
# add_subdirectory'd by the main build and not part of the product.
# Configured directly via build-spike.ps1 with a *-windows-static
# triplet + MSVC_RUNTIME_LIBRARY=MultiThreaded so the C/C++ runtime
# AND the WebView2 loader link statically (no VC++ redist, no
# WebView2Loader.dll) - the deployment shape it exists to validate.
cmake_minimum_required(VERSION 3.27)
cmake_policy(SET CMP0091 NEW)   # MSVC_RUNTIME_LIBRARY abstraction

project(win7_spike CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(wil CONFIG REQUIRED)
find_package(unofficial-webview2 CONFIG REQUIRED)

add_executable(win7_spike WIN32 main.cpp)

set_target_properties(win7_spike PROPERTIES
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

target_link_libraries(win7_spike PRIVATE
    WIL::WIL
    unofficial::webview2::webview2)

target_compile_definitions(win7_spike PRIVATE
    UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)

if(MSVC)
    target_compile_options(win7_spike PRIVATE /W4 /permissive- /EHsc /utf-8)
endif()
