# CanvasModelViewer - Model viewing application for Canvas 3D Graphics Engine
cmake_minimum_required(VERSION 3.24)

# Define the executable
add_executable(CanvasModelViewer WIN32
    CanvasModelViewer.cpp
    CanvasModelViewer.h
    Resource.h
    pch.h
    targetver.h
    CanvasModelViewer.rc
)

# Set target properties
set_target_properties(CanvasModelViewer PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    OUTPUT_NAME "CanvasModelViewer"
)

# Include directories
target_include_directories(CanvasModelViewer PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../Inc
    ${CMAKE_CURRENT_SOURCE_DIR}/../ThinWin/Inc
)

# Preprocessor definitions
target_compile_definitions(CanvasModelViewer PRIVATE
    $<$<CONFIG:Debug>:_DEBUG>
    $<$<CONFIG:Release>:NDEBUG>
    NOMINMAX
)

# Windows-specific settings
if(WIN32)
    target_compile_definitions(CanvasModelViewer PRIVATE
        WIN32
        _WINDOWS
    )
    
    # Link with Windows libraries
    target_link_libraries(CanvasModelViewer PRIVATE
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
        comctl32
    )
    
    # Set subsystem to Windows GUI
    set_target_properties(CanvasModelViewer PROPERTIES
        LINK_FLAGS "/SUBSYSTEM:WINDOWS"
    )
endif()

# Link with Canvas libraries
target_link_libraries(CanvasModelViewer PRIVATE
    CanvasCore
    ThinWin
)

# Use precompiled headers
target_precompile_headers(CanvasModelViewer PRIVATE pch.h)

# Installation
install(TARGETS CanvasModelViewer
    RUNTIME DESTINATION ${CANVAS_INSTALL_DEST}/bin
)

# Install PDB files for debugging
install(FILES $<TARGET_PDB_FILE:CanvasModelViewer> DESTINATION ${CANVAS_INSTALL_DEST}/bin OPTIONAL)