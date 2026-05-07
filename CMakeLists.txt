cmake_minimum_required(VERSION 3.27)

project(mdview
    VERSION 0.1.0
    DESCRIPTION "Markdown viewer plugin for Total Commander"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT WIN32)
    message(FATAL_ERROR "mdview is a Windows-only project.")
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")
include(CompilerWarnings)

enable_testing()

find_program(NODE_EXECUTABLE node REQUIRED
    DOC "Node.js 20+ required to build viewer/ bundle")

set(VIEWER_DIR    ${CMAKE_CURRENT_SOURCE_DIR}/viewer)
set(VIEWER_BUNDLE ${VIEWER_DIR}/dist/bundle.js)

add_custom_command(
    OUTPUT ${VIEWER_BUNDLE}
    COMMAND ${NODE_EXECUTABLE} ${VIEWER_DIR}/build.mjs
    WORKING_DIRECTORY ${VIEWER_DIR}
    DEPENDS
        ${VIEWER_DIR}/build.mjs
        ${VIEWER_DIR}/src/app.ts
        ${VIEWER_DIR}/src/markdown.ts
        ${VIEWER_DIR}/src/protocol.ts
        ${VIEWER_DIR}/src/theme.ts
        ${VIEWER_DIR}/package-lock.json
    COMMENT "Bundling viewer/ via esbuild"
    VERBATIM)

add_custom_target(viewer_bundle ALL
    DEPENDS ${VIEWER_BUNDLE})

add_subdirectory(src)
add_subdirectory(tests)
