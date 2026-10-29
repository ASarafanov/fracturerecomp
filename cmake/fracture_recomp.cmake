# fracture - build wiring for the recompiled game code.
#
# Takes the place of generated/rexglue.cmake, which `rexglue init` writes for
# a single default.xex in generated/default (and every codegen run rewrites).
# Fracture runs from two releases' default.xex, version 1 and 2. Each one's
# recompiled code, with the hooks written for its addresses, is a DLL of its
# own next to fracture.exe, fracture_<release>.dll (src/game/game_api.h):
# both releases' generated functions are named sub_<guest address>, so they
# cannot share one binary.
#
# A release is built when its codegen input xex/<release>/default.xex exists
# (setup.py copies it there from the disc image); FRACTURE_RELEASES limits
# the list. Release packages carry both.

find_package(rexglue 0.10.0 QUIET CONFIG)
if(NOT rexglue_FOUND)
    message(FATAL_ERROR
        "ReXGlue SDK not found. Run setup.bat, or put the SDK install on "
        "CMAKE_PREFIX_PATH")
endif()
message(STATUS "Found ReXGlue SDK ${REXGLUE_VERSION_STRING} at ${rexglue_DIR}")

set(REXGLUE_RECOMP_DEBUG_INFO "line-tables-only" CACHE STRING
    "Debug info level for generated code: line-tables-only, full, or none")

# Guest SEH scopes emit __try/__except, which traps hardware faults only under
# asynchronous EH. clang++ spells that flag the GNU way.
set(REXGLUE_RECOMP_OPTIONS "")
if(WIN32)
    if(MSVC)
        list(APPEND REXGLUE_RECOMP_OPTIONS /EHa)
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        list(APPEND REXGLUE_RECOMP_OPTIONS -fasync-exceptions)
    endif()
endif()
if(REXGLUE_RECOMP_DEBUG_INFO STREQUAL "none")
    list(APPEND REXGLUE_RECOMP_OPTIONS
        $<$<CXX_COMPILER_ID:Clang,AppleClang,GNU>:-g0>)
elseif(REXGLUE_RECOMP_DEBUG_INFO STREQUAL "line-tables-only")
    list(APPEND REXGLUE_RECOMP_OPTIONS
        $<$<CXX_COMPILER_ID:Clang,AppleClang>:-gline-tables-only>)
endif()

# The options go on the target as well: CMake compiles the header from its own
# cmake_pch.hxx.cxx, which carries none of the source properties, and clang
# rejects a PCH whose EH mode disagrees with the TU including it.
function(rexglue_apply_recomp_settings target_name pch_header)
    target_precompile_headers(${target_name} PRIVATE "${pch_header}")
    target_compile_options(${target_name} PRIVATE ${REXGLUE_RECOMP_OPTIONS})
endfunction()

set(FRACTURE_RELEASES "" CACHE STRING
    "Releases whose game code to build, a list of v1 and v2. Empty: every release whose xex/<release>/default.xex exists")
set(_fracture_releases ${FRACTURE_RELEASES})
if(NOT _fracture_releases)
    foreach(_release v1 v2)
        if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/xex/${_release}/default.xex")
            list(APPEND _fracture_releases ${_release})
        endif()
    endforeach()
endif()
if(NOT _fracture_releases)
    message(FATAL_ERROR
        "Neither xex/v1/default.xex nor xex/v2/default.xex exists. Run setup.bat "
        "with your disc image; it puts the game's default.xex there.")
endif()
message(STATUS "Fracture: game code for ${_fracture_releases}")

# Codegen runs as part of the build, re-running only when an input in codegen.d
# changes; `cmake --build . --target fracture_codegen` runs it for every
# release. The generated sources are only known once it has run, so a fresh
# tree needs configure -> fracture_codegen -> configure -> build.
add_custom_target(fracture_codegen)
set(FRACTURE_GAME_TARGETS "")
foreach(_release ${_fracture_releases})
    set(_manifest "${CMAKE_CURRENT_SOURCE_DIR}/fracture_${_release}_manifest.toml")
    set(_gen_dir "${CMAKE_CURRENT_SOURCE_DIR}/generated/${_release}")
    set(_sources "")
    if(EXISTS "${_gen_dir}/sources.cmake")
        include("${_gen_dir}/sources.cmake")
        set(_sources ${GENERATED_SOURCES})
        unset(GENERATED_SOURCES)
        # Carried on the sources so they hold for whichever target compiles them.
        set_source_files_properties(${_sources}
            PROPERTIES COMPILE_OPTIONS "${REXGLUE_RECOMP_OPTIONS}")
    endif()

    # The stamp comes first: the DEPFILE names it.
    add_custom_command(
        OUTPUT "${_gen_dir}/codegen.build.stamp" ${_sources}
        COMMAND $<TARGET_FILE:rex::rexglue> codegen "${_manifest}"
        DEPFILE "${_gen_dir}/codegen.d"
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Generating recompiled code for default.xex ${_release}"
        VERBATIM
    )
    add_custom_target(fracture_codegen_${_release}
        DEPENDS "${_gen_dir}/codegen.build.stamp")
    add_dependencies(fracture_codegen fracture_codegen_${_release})

    if(_sources)
        add_library(fracture_${_release} SHARED
            ${_sources}
            src/game/game_module.cpp
            src/game/${_release}_hooks.cpp
        )
        target_include_directories(fracture_${_release} PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/src
            ${CMAKE_CURRENT_SOURCE_DIR}/src/game
            ${_gen_dir}
        )
        target_link_libraries(fracture_${_release} PRIVATE rex::runtime)
        rexglue_apply_recomp_settings(fracture_${_release} "${_gen_dir}/fracture_pch.h")
        add_dependencies(fracture_${_release} fracture_codegen_${_release})
        set_target_properties(fracture_${_release} PROPERTIES
            CXX_VISIBILITY_PRESET hidden
        )
        # Next to the host exe, which depends on it.
        rexglue_configure_module_target(fracture_${_release} HOST fracture)
        list(APPEND FRACTURE_GAME_TARGETS fracture_${_release})
    endif()
endforeach()

# Sets <target> up as the SDK's host app (fracture.exe: no recompiled code of
# its own). Usage: fracture_setup_target(<target> [GPU_PLUGINS xenos])
macro(fracture_setup_target target_name)
    target_include_directories(${target_name} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/src
    )
    target_link_libraries(${target_name} PRIVATE rex::runtime)
    rexglue_configure_target(${target_name} ${ARGN})
endmacro()
