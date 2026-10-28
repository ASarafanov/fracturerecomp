# fracture - ReXGlue Recompiled Project
#
# SDK boilerplate lives in generated/rexglue.cmake.

cmake_minimum_required(VERSION 3.25)
project(fracture LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(generated/rexglue.cmake)

# Sources
set(FRACTURE_SOURCES
    src/main.cpp
    src/fracture_hooks.cpp
    src/fracture_settings.cpp
)

if(WIN32)
    add_executable(fracture WIN32 ${FRACTURE_SOURCES})
else()
    add_executable(fracture ${FRACTURE_SOURCES})
endif()

rexglue_setup_target(fracture GPU_PLUGINS xenos)
