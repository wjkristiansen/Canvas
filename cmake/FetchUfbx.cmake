# FetchUfbx.cmake
#
# Resolve open-source ufbx sources and create a static `ufbx` target when found.
#
# Resolution order:
#   1) UFBX_ROOT cache/path provided by caller
#   2) Repository checkout at deps/ufbx
#
# On success: defines target `ufbx` and sets UFBX_ROOT cache variable.
# On failure: emits STATUS/WARNING and returns without target.
#
# NOTE: This project intentionally does NOT auto-download ufbx at configure
# time. Dependencies are expected to be managed via git submodules.

include_guard(GLOBAL)

set(_UFBX_ROOT_CANDIDATE "")

if(DEFINED UFBX_ROOT AND EXISTS "${UFBX_ROOT}/ufbx.h" AND EXISTS "${UFBX_ROOT}/ufbx.c")
    set(_UFBX_ROOT_CANDIDATE "${UFBX_ROOT}")
elseif(EXISTS "${CMAKE_SOURCE_DIR}/deps/ufbx/ufbx.h" AND EXISTS "${CMAKE_SOURCE_DIR}/deps/ufbx/ufbx.c")
    set(_UFBX_ROOT_CANDIDATE "${CMAKE_SOURCE_DIR}/deps/ufbx")
endif()

if(NOT _UFBX_ROOT_CANDIDATE)
    message(STATUS "Canvas: ufbx source not found; CanvasFbx will build in stub mode.")
    message(STATUS "  Expected submodule location: ${CMAKE_SOURCE_DIR}/deps/ufbx")
    message(STATUS "  Initialize submodules: git submodule update --init --recursive")
    message(STATUS "  Provide -DUFBX_ROOT=<path containing ufbx.h and ufbx.c>")
    return()
endif()

set(UFBX_ROOT "${_UFBX_ROOT_CANDIDATE}" CACHE PATH "Path containing ufbx.h and ufbx.c" FORCE)
message(STATUS "Canvas: Using ufbx sources at ${UFBX_ROOT}")

if(NOT TARGET ufbx)
    add_library(ufbx STATIC "${UFBX_ROOT}/ufbx.c")
    target_include_directories(ufbx PUBLIC "${UFBX_ROOT}")
    target_compile_definitions(ufbx PRIVATE UFBX_REAL_IS_FLOAT=1)
    set_target_properties(ufbx PROPERTIES
        C_STANDARD 99
        C_STANDARD_REQUIRED ON
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL"
    )
endif()
