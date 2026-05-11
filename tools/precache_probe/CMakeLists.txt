find_package(wil CONFIG REQUIRED)
find_package(unofficial-webview2 CONFIG REQUIRED)

add_executable(precache_probe WIN32
    main.cpp)

# wmain entry point for a /SUBSYSTEM:WINDOWS exe is wWinMainCRTStartup
# by default; we want a console-attached wmain instead. Use the
# console subsystem (so stdout/stderr are usable) but keep a Unicode
# wmain entry.
set_target_properties(precache_probe PROPERTIES
    WIN32_EXECUTABLE FALSE)

target_link_libraries(precache_probe PRIVATE
    WIL::WIL
    unofficial::webview2::webview2)

target_compile_definitions(precache_probe PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    WIL_LOCK_EXCEPTION_MODE)

mdview_set_warnings(precache_probe)

# Stage probe.html next to the binary so SetVirtualHostNameToFolderMapping
# can serve it from the executable directory.
add_custom_command(TARGET precache_probe POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_CURRENT_SOURCE_DIR}/probe.html
        $<TARGET_FILE_DIR:precache_probe>/probe.html
    COMMENT "Staging probe.html next to precache_probe.exe"
    VERBATIM)
