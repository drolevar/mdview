find_package(Catch2 3 CONFIG REQUIRED)
find_package(unofficial-nlohmann-json CONFIG QUIET)

add_executable(mdview_integration_tests
    main.cpp
    pump.cpp
    screenshot.cpp
    session.cpp
    summary.cpp
    test_audit_late_remap.cpp
    test_embedded_assets.cpp
    test_focus.cpp
    test_highlight.cpp
    test_lifecycle.cpp
    test_markdown_polish.cpp
    test_math.cpp
    test_mermaid.cpp
    test_m10_perf.cpp
    test_precache.cpp
    test_search.cpp
    test_theme.cpp
)

target_include_directories(mdview_integration_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src)

target_link_libraries(mdview_integration_tests PRIVATE
    Catch2::Catch2  # we provide our own wmain; do NOT link Catch2WithMain
    mdview_core
)

if(TARGET nlohmann_json::nlohmann_json)
    target_link_libraries(mdview_integration_tests PRIVATE
        nlohmann_json::nlohmann_json)
endif()

mdview_set_warnings(mdview_integration_tests)

target_precompile_headers(mdview_integration_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/cmake/pch.hpp)

add_dependencies(mdview_integration_tests mdview_wlx)

add_test(NAME mdview_integration
         COMMAND mdview_integration_tests
                 --reporter console::out=-::colour-mode=ansi)
set_tests_properties(mdview_integration PROPERTIES
    LABELS "integration"
    ENVIRONMENT "MDVIEW_SMOKE_DIR=${CMAKE_SOURCE_DIR}/tests/smoke")
