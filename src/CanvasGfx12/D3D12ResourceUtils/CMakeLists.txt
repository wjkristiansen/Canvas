# D3D12ResourceUtils - DirectX 12 Resource Utilities
cmake_minimum_required(VERSION 3.24)

# Define the library
add_library(D3D12ResourceUtils STATIC
    D3D12ResourceUtils.cpp
    pch.cpp
    pch.h
    framework.h
)

# Set target properties
set_target_properties(D3D12ResourceUtils PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    OUTPUT_NAME "D3D12ResourceUtils"
)

# Include directories
target_include_directories(D3D12ResourceUtils 
    PUBLIC 
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../../Inc>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../d3dx12>
        $<INSTALL_INTERFACE:include>
        $<INSTALL_INTERFACE:include/d3dx12>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

# Preprocessor definitions
target_compile_definitions(D3D12ResourceUtils PRIVATE
    $<$<CONFIG:Debug>:_DEBUG>
    $<$<CONFIG:Release>:NDEBUG>
    NOMINMAX
)

# Link with LinkedList header-only library
target_link_libraries(D3D12ResourceUtils PUBLIC
    LinkedList
)

# Windows-specific settings
if(WIN32)
    target_compile_definitions(D3D12ResourceUtils PRIVATE
        WIN32
        _LIB
    )

    if(TARGET D3D12Agility)
        message(STATUS "D3D12ResourceUtils: Using Agility SDK headers and Windows SDK libraries")
        # Get the include directory from the parent scope
        get_directory_property(AGILITY_INCLUDE_DIR DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/.. DEFINITION AGILITY_INCLUDE_DIR)
        target_include_directories(D3D12ResourceUtils PRIVATE "${AGILITY_INCLUDE_DIR}")
        target_link_libraries(D3D12ResourceUtils PUBLIC d3d12 dxgi d3dcompiler)
    else()
        target_link_libraries(D3D12ResourceUtils PUBLIC d3d12 dxgi d3dcompiler)
    endif()
endif()

# Use precompiled headers
target_precompile_headers(D3D12ResourceUtils PRIVATE pch.h)

# Installation


