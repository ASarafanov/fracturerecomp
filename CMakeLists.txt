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
    src/fracture_frame_rate.cpp
    src/fracture_graphics.cpp
    src/fracture_hooks.cpp
    src/fracture_installer.cpp
    src/fracture_installer_dialog.cpp
    src/fracture_menu_input.cpp
    src/fracture_settings.cpp
    src/fracture_settings_dialog.cpp
)

if(WIN32)
    add_executable(fracture WIN32 ${FRACTURE_SOURCES})
else()
    add_executable(fracture ${FRACTURE_SOURCES})
endif()

rexglue_setup_target(fracture GPU_PLUGINS xenos)

if(WIN32)
    # dxgi lists the Direct3D 12 adapters for the settings screen; ole32,
    # shell32 and uuid give the install wizard the system file chooser.
    target_link_libraries(fracture PRIVATE dxgi ole32 shell32 uuid)
    # UTF-8 as the process code page, merged into the linker's manifest.
    target_sources(fracture PRIVATE src/fracture.manifest)
endif()
