# CanvasText - SDF/MSDF Text Rendering Subsystem
cmake_minimum_required(VERSION 3.24)

# Define the library (static or shared based on Canvas configuration)
add_library(CanvasText STATIC
    # Source files
    Font.cpp
    SDFGenerator.cpp
    RectanglePacker.cpp
    
    # Header files (for IDE organization)
    Font.h
    SDFGenerator.h
    RectanglePacker.h
    pch.h
)

# Set target properties
set_target_properties(CanvasText PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
)

# Include directories
target_include_directories(CanvasText 
    PUBLIC 
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

# Preprocessor definitions
target_compile_definitions(CanvasText
    PRIVATE
        $<$<CONFIG:Debug>:_DEBUG>
        $<$<CONFIG:Release>:NDEBUG>
        NOMINMAX
)

# Windows-specific settings
if(WIN32)
    target_compile_definitions(CanvasText PRIVATE
        _WINDOWS
    )
endif()

# Use precompiled headers
target_precompile_headers(CanvasText PRIVATE pch.h)

# Installation
install(TARGETS CanvasText
    LIBRARY DESTINATION ${CANVAS_INSTALL_DEST}/lib
    ARCHIVE DESTINATION ${CANVAS_INSTALL_DEST}/lib
)
