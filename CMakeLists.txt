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
set(VIEWER_STAMP  ${VIEWER_DIR}/dist/.viewer.stamp)

add_custom_command(
    OUTPUT ${VIEWER_STAMP}
    COMMAND ${CMAKE_COMMAND} -E env
        "NODE_ENV=$<IF:$<CONFIG:Release>,production,development>"
        ${NODE_EXECUTABLE} ${VIEWER_DIR}/build.mjs
    WORKING_DIRECTORY ${VIEWER_DIR}
    DEPENDS
        ${VIEWER_DIR}/build.mjs
        ${VIEWER_DIR}/src/app.ts
        ${VIEWER_DIR}/src/markdown.ts
        ${VIEWER_DIR}/src/protocol.ts
        ${VIEWER_DIR}/src/theme.ts
        ${VIEWER_DIR}/src/highlight.ts
        ${VIEWER_DIR}/src/mermaid-chunk.ts
        ${VIEWER_DIR}/src/math-rules.ts
        ${VIEWER_DIR}/src/math-chunk.ts
        ${VIEWER_DIR}/styles/katex-overrides.css
        ${VIEWER_DIR}/src/summary.ts
        ${VIEWER_DIR}/src/declarations.d.ts
        ${VIEWER_DIR}/package-lock.json
    COMMENT "Bundling viewer/ via esbuild (ESM + splitting)"
    VERBATIM)

add_custom_target(viewer_bundle ALL
    DEPENDS ${VIEWER_STAMP})

# Pre-stage the viewer asset tree (everything we'd otherwise ship
# loose next to mdview.wlx64) into a single clean directory so the
# M8 resource generator can glob a single root. Mirrors the layout
# in src/CMakeLists.txt's viewer_stage step but runs BEFORE
# mdview_wlx is built, so the generator output can be linked into it.
set(VIEWER_PRESTAGE_ROOT
    "${CMAKE_BINARY_DIR}/generated/viewer-tree")
set(VIEWER_PRESTAGE_STAMP
    "${CMAKE_BINARY_DIR}/generated/viewer-tree.stamp")
add_custom_command(
    OUTPUT ${VIEWER_PRESTAGE_STAMP}
    COMMAND ${CMAKE_COMMAND} -E rm -rf ${VIEWER_PRESTAGE_ROOT}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${VIEWER_PRESTAGE_ROOT}
    COMMAND ${CMAKE_COMMAND} -E copy
        ${VIEWER_DIR}/index.html
        ${VIEWER_DIR}/styles.css
        ${VIEWER_PRESTAGE_ROOT}
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${VIEWER_DIR}/styles
        ${VIEWER_PRESTAGE_ROOT}/styles
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${VIEWER_DIR}/dist
        ${VIEWER_PRESTAGE_ROOT}/dist
    COMMAND ${CMAKE_COMMAND} -E touch ${VIEWER_PRESTAGE_STAMP}
    DEPENDS
        ${VIEWER_STAMP}
        ${VIEWER_DIR}/index.html
        ${VIEWER_DIR}/styles.css
        ${VIEWER_DIR}/styles/hljs-light.css
        ${VIEWER_DIR}/styles/hljs-dark.css
        ${VIEWER_DIR}/styles/katex-overrides.css
    COMMENT "Pre-staging viewer assets for embedded RC generation"
    VERBATIM)
add_custom_target(viewer_prestage DEPENDS ${VIEWER_PRESTAGE_STAMP})

# Generate viewer_assets.rc + viewer_assets.gen.cpp from the
# pre-staged tree. Re-runs whenever any pre-staged file changes
# (custom_command DEPENDS-on-stamp), which is in turn refreshed
# whenever ${VIEWER_STAMP} bumps (viewer_bundle did an esbuild).
set(GEN_DIR  "${CMAKE_BINARY_DIR}/generated")
set(GEN_RC   "${GEN_DIR}/viewer_assets.rc")
set(GEN_CPP  "${GEN_DIR}/viewer_assets.gen.cpp")
add_custom_command(
    OUTPUT ${GEN_RC} ${GEN_CPP}
    COMMAND ${CMAKE_COMMAND}
            -DASSET_DIR=${VIEWER_PRESTAGE_ROOT}
            -DOUTPUT_RC=${GEN_RC}
            -DOUTPUT_CPP=${GEN_CPP}
            -P ${CMAKE_SOURCE_DIR}/cmake/GenerateViewerResources.cmake
    DEPENDS ${VIEWER_PRESTAGE_STAMP}
            ${CMAKE_SOURCE_DIR}/cmake/GenerateViewerResources.cmake
    COMMENT "Generating viewer_assets.rc + .gen.cpp from pre-staged tree"
    VERBATIM)
add_custom_target(viewer_assets_gen
    DEPENDS ${GEN_RC} ${GEN_CPP})

add_subdirectory(src)
add_subdirectory(tests)
add_subdirectory(tools/precache_probe)
add_subdirectory(tools/dpi_probe)
