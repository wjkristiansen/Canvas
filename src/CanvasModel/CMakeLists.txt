# CanvasModel - Model handling library for Canvas 3D Graphics Engine
cmake_minimum_required(VERSION 3.24)

# Define the library (static library based on vcxproj analysis)
add_library(CanvasModel STATIC
    CanvasModel.cpp
    pch.cpp
    pch.h
    framework.h
)

# Set target properties
set_target_properties(CanvasModel PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    OUTPUT_NAME "CanvasModel"
)

# Include directories
target_include_directories(CanvasModel 
    PUBLIC 
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../Inc>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

# Preprocessor definitions
target_compile_definitions(CanvasModel PRIVATE
    $<$<CONFIG:Debug>:_DEBUG>
    $<$<CONFIG:Release>:NDEBUG>
    NOMINMAX
)

# Windows-specific settings
if(WIN32)
    target_compile_definitions(CanvasModel PRIVATE
        WIN32
        _LIB
    )
endif()

# Link with CanvasCore
target_link_libraries(CanvasModel PUBLIC
    CanvasCore
)

# Use precompiled headers
target_precompile_headers(CanvasModel PRIVATE pch.h)


