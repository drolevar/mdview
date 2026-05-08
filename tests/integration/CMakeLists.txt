find_package(Catch2 3 CONFIG REQUIRED)
find_package(unofficial-nlohmann-json CONFIG QUIET)

add_executable(mdview_integration_tests
    main.cpp
    debug_monitor.cpp
    pump.cpp
    screenshot.cpp
    session.cpp
    summary.cpp
    test_highlight.cpp
    test_lifecycle.cpp
    test_mermaid.cpp
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

add_dependencies(mdview_integration_tests mdview_wlx)

add_test(NAME mdview_integration
         COMMAND mdview_integration_tests
                 --reporter console::out=-::colour-mode=ansi)
set_tests_properties(mdview_integration PROPERTIES
    LABELS "integration"
    ENVIRONMENT "MDVIEW_SMOKE_DIR=${CMAKE_SOURCE_DIR}/tests/smoke")
