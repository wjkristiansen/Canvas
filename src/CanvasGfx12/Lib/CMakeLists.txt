# CanvasGfx12 Library - DirectX 12 Graphics implementation
cmake_minimum_required(VERSION 3.24)

# Define the library
add_library(CanvasGfx12 SHARED
    CanvasGfx12.cpp
    Instance12.cpp
    Device12.cpp
    GraphicsContext12.cpp
    Surface12.cpp
    SwapChain12.cpp
    dllmain.cpp
    stdafx.cpp
    stdafx.h
    targetver.h
)

# Set target properties
set_target_properties(CanvasGfx12 PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    OUTPUT_NAME "CanvasGfx12"
)

# Use explicit DEF file for exports instead of automatic generation
set_target_properties(CanvasGfx12 PROPERTIES
    LINK_FLAGS "/DEF:${CMAKE_CURRENT_SOURCE_DIR}/CanvasGfx12.def"
)

# Include directories
target_include_directories(CanvasGfx12 
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
target_compile_definitions(CanvasGfx12 PRIVATE
    $<$<CONFIG:Debug>:_DEBUG>
    $<$<CONFIG:Release>:NDEBUG>
    CANVASGFX12_EXPORTS
    NOMINMAX
)

# Windows-specific settings
if(WIN32)
    target_compile_definitions(CanvasGfx12 PRIVATE
        _WINDOWS
        _USRDLL
    )
    
    if(TARGET D3D12Agility)
        message(STATUS "CanvasGfx12: Linking against Agility SDK")
        target_link_libraries(CanvasGfx12 PRIVATE
            D3D12Agility
            kernel32 user32 gdi32 winspool comdlg32 advapi32 shell32 ole32 oleaut32 uuid odbc32 odbccp32
        )
    else()
    target_link_libraries(CanvasGfx12 PRIVATE
            d3d12 dxgi d3dcompiler
            kernel32 user32 gdi32 winspool comdlg32 advapi32 shell32 ole32 oleaut32 uuid odbc32 odbccp32
    )
endif()
endif()

# Link with other Canvas libraries
target_link_libraries(CanvasGfx12 PRIVATE
    CanvasCore
    D3D12ResourceUtils
)

# Use precompiled headers
target_precompile_headers(CanvasGfx12 PRIVATE stdafx.h)

# Installation
install(TARGETS CanvasGfx12
    EXPORT CanvasGfx12Targets
    RUNTIME DESTINATION bin
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    INCLUDES DESTINATION include
)

install(EXPORT CanvasGfx12Targets
    FILE CanvasGfx12Targets.cmake
    NAMESPACE Canvas::
    DESTINATION lib/cmake/Canvas
)