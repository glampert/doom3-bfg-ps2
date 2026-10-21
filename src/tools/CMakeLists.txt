# ================================================================================================
# File: CMakeLists.txt
# Brief: The C++ host tools (resource_unpack, asset_reexport) and their shared asset library, built
# with the host's own C++20 compiler on macOS, Linux or Windows (MSVC). One definition of their
# flags serves `make`, CI and every host.
# This source code is released under the GNU GPL-3.0-or-later license.
# ================================================================================================
#
# `make resource-unpack` and `make tools` drive this project on macOS and Linux. Anywhere else,
# from the repository root:
#
#   cmake -S src/tools -B build/tools
#   cmake --build build/tools --config Release
#
# The binaries land in the build directory (in a per-config folder under Visual Studio or Xcode).

cmake_minimum_required(VERSION 3.24)
project(d3bfg_tools LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Optimized unless the caller asks otherwise: a single-config generator given no build type
# would not optimize at all.
get_property(IS_MULTI_CONFIG GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
if(NOT IS_MULTI_CONFIG AND NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Release, RelWithDebInfo, Debug or MinSizeRel." FORCE)
endif()

# MSVC links its C/C++ runtime statically (/MT), so each .exe runs on its own, without the
# Visual C++ Redistributable installed. Set before any target is made.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# The binaries go to the build directory itself, unless the caller names another place (the
# Makefile does: build/tools/, with this build tree under it).
if(NOT CMAKE_RUNTIME_OUTPUT_DIRECTORY)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
endif()

# ------------------------------------------------------------------------------------------------
# Language restrictions and warnings
# ------------------------------------------------------------------------------------------------

# GCC and Clang hold the tools to the target's language restrictions and to the strict warning
# set the host tests use (HOST_WARNFLAGS in the Makefile), as errors, plus -Wpedantic, because
# MSVC builds them too and has none of the GNU extensions. GCC adds the logic and duplication
# checks it applies to the EE build. MSVC gets /W4 and keeps its default exception and RTTI
# settings; the sources use neither, and the other compilers enforce that. To get past a
# warning a newer compiler adds, configure with --compile-no-warning-as-error.
# Floating-point contraction is off on every compiler (MSVC's /fp:precise already keeps it off),
# so converted assets come out byte-identical whichever host cooks them.
if(MSVC)
    set(TOOL_OPTIONS /W4 /fp:precise)
else()
    set(TOOL_OPTIONS
        -ffp-contract=off
        -fno-exceptions -fno-rtti -fno-threadsafe-statics
        -Wall -Wextra -Wpedantic
        -Wshadow -Wdouble-promotion -Wconversion -Wsign-conversion
        -Wformat=2 -Wno-format-nonliteral -Wundef -Wpointer-arith
        -Wcast-align -Wwrite-strings -Wredundant-decls -Wnull-dereference
        -Wnon-virtual-dtor -Woverloaded-virtual -Wvla)
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        list(APPEND TOOL_OPTIONS -Wlogical-op -Wduplicated-cond -Wduplicated-branches)
    endif()
endif()

function(apply_tool_options name)
    target_compile_options(${name} PRIVATE ${TOOL_OPTIONS})
    set_target_properties(${name} PROPERTIES COMPILE_WARNING_AS_ERROR ON)
    if(MSVC)
        # The portable C runtime calls (fopen) rather than the _s variants.
        target_compile_definitions(${name} PRIVATE _CRT_SECURE_NO_WARNINGS)
    endif()
endfunction()

function(add_tool name)
    add_executable(${name} ${ARGN})
    apply_tool_options(${name})
    target_link_libraries(${name} PRIVATE assets)
endfunction()

# ------------------------------------------------------------------------------------------------
# Shared asset library
# ------------------------------------------------------------------------------------------------

# Retail containers, image decoding and the GS texture conversion every re-export tool shares.
# Sources include it as "assets/<file>.h" from this directory.
add_library(assets STATIC
    assets/bimage.cpp
    assets/gs_texture.cpp
    assets/gstx.cpp
    assets/image.cpp
    assets/json.cpp
    assets/palette.cpp
    assets/png.cpp
    assets/rect_pack.cpp
    assets/resources.cpp
    assets/rle.cpp
    assets/texture_convert.cpp)
apply_tool_options(assets)
target_include_directories(assets PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# ------------------------------------------------------------------------------------------------
# Tools
# ------------------------------------------------------------------------------------------------

add_tool(resource_unpack resource_unpack/main.cpp)
add_tool(asset_reexport asset_reexport/main.cpp)
