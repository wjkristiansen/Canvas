# CanvasFbx - FBX integration library for Canvas 3D Graphics Engine
cmake_minimum_required(VERSION 3.24)

# Define the library
add_library(CanvasFbx SHARED
    CanvasFbx.cpp
    dllmain.cpp
    pch.h
    targetver.h
)

# Set target properties
set_target_properties(CanvasFbx PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    WINDOWS_EXPORT_ALL_SYMBOLS ON
    OUTPUT_NAME "CanvasFbx"
)

# Include directories
target_include_directories(CanvasFbx 
    PUBLIC 
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/Inc>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/../Inc
)

# Preprocessor definitions
target_compile_definitions(CanvasFbx PRIVATE
    $<$<CONFIG:Debug>:_DEBUG>
    $<$<CONFIG:Release>:NDEBUG>
    CANVASFBX_EXPORTS
    NOMINMAX
)

# Windows-specific settings
if(WIN32)
    target_compile_definitions(CanvasFbx PRIVATE
        _WINDOWS
        _USRDLL
    )
    
    # Link with Windows libraries
    target_link_libraries(CanvasFbx PRIVATE
        kernel32
        user32
        gdi32
        winspool
        comdlg32
        advapi32
        shell32
        ole32
        oleaut32
        uuid
        odbc32
        odbccp32
    )
endif()

# Link with CanvasCore
target_link_libraries(CanvasFbx PRIVATE
    CanvasCore
)

# Use precompiled headers
target_precompile_headers(CanvasFbx PRIVATE pch.h)

# Note: FBX SDK integration would need to be added here when available
# find_package(FBX REQUIRED)
# target_link_libraries(CanvasFbx PRIVATE FBX::FBX)

# Installation
install(TARGETS CanvasFbx
    EXPORT CanvasFbxTargets
    RUNTIME DESTINATION bin
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    INCLUDES DESTINATION include
)

# Install headers if they exist in Inc directory
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Inc")
    install(DIRECTORY Inc/
        DESTINATION include/CanvasFbx
        FILES_MATCHING PATTERN "*.h" PATTERN "*.hpp"
    )
endif()

install(EXPORT CanvasFbxTargets
    FILE CanvasFbxTargets.cmake
    NAMESPACE Canvas::
    DESTINATION lib/cmake/Canvas
)