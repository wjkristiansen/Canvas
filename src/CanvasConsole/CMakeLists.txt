# CanvasConsole - Console application for Canvas 3D Graphics Engine
cmake_minimum_required(VERSION 3.24)

# Define the executable
add_executable(CanvasConsole
    CanvasConsole.cpp
    pch.cpp
    pch.h
)

# Set target properties
set_target_properties(CanvasConsole PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    OUTPUT_NAME "CanvasConsole"
)

# Include directories
target_include_directories(CanvasConsole PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../Inc
)

# Preprocessor definitions
target_compile_definitions(CanvasConsole PRIVATE
    $<$<CONFIG:Debug>:_DEBUG>
    $<$<CONFIG:Release>:NDEBUG>
    _CONSOLE
)

# Windows-specific settings
if(WIN32)
    target_compile_definitions(CanvasConsole PRIVATE
        WIN32
        _WINDOWS
    )
    
    # Link with Windows libraries
    target_link_libraries(CanvasConsole PRIVATE
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
    
    # Set subsystem to console
    set_target_properties(CanvasConsole PROPERTIES
        LINK_FLAGS "/SUBSYSTEM:CONSOLE"
    )
endif()

# Link with CanvasCore library
target_link_libraries(CanvasConsole PRIVATE
    CanvasCore
)

# Use precompiled headers
target_precompile_headers(CanvasConsole PRIVATE pch.h)

# Installation
install(TARGETS CanvasConsole
    RUNTIME DESTINATION ${CANVAS_INSTALL_DEST}/bin
)

# Install PDB files for debugging
install(FILES $<TARGET_PDB_FILE:CanvasConsole> DESTINATION ${CANVAS_INSTALL_DEST}/bin OPTIONAL)