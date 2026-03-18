# GpuTask - GPU Task Graph for D3D12 Enhanced Barriers
cmake_minimum_required(VERSION 3.24)

# Define the library
add_library(GpuTask STATIC
    GpuTask.cpp
    pch.cpp
    pch.h
)

# Set target properties
set_target_properties(GpuTask PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    OUTPUT_NAME "GpuTask"
)

# Include directories
target_include_directories(GpuTask 
    PUBLIC 
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../../Inc>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

# Preprocessor definitions
target_compile_definitions(GpuTask PRIVATE
    $<$<CONFIG:Debug>:_DEBUG>
    $<$<CONFIG:Release>:NDEBUG>
    NOMINMAX
)

# Windows-specific settings
if(WIN32)
    target_compile_definitions(GpuTask PRIVATE
        WIN32
        _LIB
    )

    if(TARGET D3D12Agility)
        message(STATUS "GpuTask: Using Agility SDK headers and Windows SDK libraries")
        target_link_libraries(GpuTask PUBLIC D3D12Agility)
    else()
        target_link_libraries(GpuTask PUBLIC d3d12 dxgi d3dcompiler)
    endif()
endif()

# Use precompiled headers
target_precompile_headers(GpuTask PRIVATE pch.h)
