# CanvasFbx - FBX scene importer for Canvas 3D Graphics Engine
cmake_minimum_required(VERSION 3.24)

#---------------------------------------------------------------------------
# ufbx discovery
# Prefer open-source ufbx backend for FBX parsing. If unavailable, target still
# builds in stub mode and importer returns E_NOTIMPL at runtime.
#---------------------------------------------------------------------------
option(CANVAS_FBX_ENABLE "Enable FBX importer backend" ON)

set(CANVASFBX_HAS_UFBX FALSE)

if(CANVAS_FBX_ENABLE)
    include(${CMAKE_SOURCE_DIR}/cmake/FetchUfbx.cmake)
    if(TARGET ufbx)
        set(CANVASFBX_HAS_UFBX TRUE)
        message(STATUS "CanvasFbx: ufbx backend enabled")
    endif()
endif()

if(NOT CANVASFBX_HAS_UFBX)
    message(STATUS "CanvasFbx: Building without ufbx (stub importer only)")
endif()

#---------------------------------------------------------------------------
# Target
#---------------------------------------------------------------------------
add_library(CanvasFbx SHARED
    CanvasFbx.cpp
    dllmain.cpp
    pch.h
    targetver.h
)

set_target_properties(CanvasFbx PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    OUTPUT_NAME "CanvasFbx"
)

target_include_directories(CanvasFbx
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/Inc>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/../Inc
)

target_compile_definitions(CanvasFbx PRIVATE
    $<$<CONFIG:Debug>:_DEBUG>
    $<$<CONFIG:Release>:NDEBUG>
    CANVASFBX_EXPORTS
    NOMINMAX
)

if(WIN32)
    target_compile_definitions(CanvasFbx PRIVATE _WINDOWS _USRDLL)
endif()

#---------------------------------------------------------------------------
# ufbx linkage (when available)
#---------------------------------------------------------------------------
if(CANVASFBX_HAS_UFBX)
    target_compile_definitions(CanvasFbx PRIVATE CANVASFBX_HAS_UFBX=1)
    target_link_libraries(CanvasFbx PRIVATE ufbx)
else()
    target_compile_definitions(CanvasFbx PRIVATE CANVASFBX_HAS_UFBX=0)
endif()

#---------------------------------------------------------------------------
# Canvas dependencies
#---------------------------------------------------------------------------
target_link_libraries(CanvasFbx PRIVATE
    CanvasCore
    GEM
    QLog
)

#---------------------------------------------------------------------------
# Precompiled header
#---------------------------------------------------------------------------
target_precompile_headers(CanvasFbx PRIVATE pch.h)

#---------------------------------------------------------------------------
# Install (matches project convention – co-located in bin/)
#---------------------------------------------------------------------------
install(TARGETS CanvasFbx
    RUNTIME DESTINATION ${CANVAS_INSTALL_DEST}/bin
    LIBRARY DESTINATION ${CANVAS_INSTALL_DEST}/lib
)