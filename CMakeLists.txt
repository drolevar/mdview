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

add_subdirectory(src)
add_subdirectory(tests)
