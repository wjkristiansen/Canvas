# CanvasCore - Core library for Canvas 3D Graphics Engine
cmake_minimum_required(VERSION 3.24)

# Define the library
add_library(CanvasCore SHARED
    # Source files
    CanvasCore.cpp
    dllmain.cpp
    Scene.cpp
    Timer.cpp
    SceneGraph.cpp
    RenderQueueManager.cpp
    
    # Header files (for IDE organization)
    ../Inc/CanvasCore.h
    ../Inc/CanvasGfx.h
    ../Inc/CanvasMath.hpp
    ../Inc/CanvasModel.h
    ../Inc/CanvasRender.h
    Camera.h
    Canvas.h
    DataSource.h
    Light.h
    Mesh.h
    Scene.h
    SceneGraph.h
    ScopedUtils.h
    Timeline.h
    Timer.h
    Transform.h
    RenderQueueManager.h
    pch.h
    targetver.h
)

# Set target properties
set_target_properties(CanvasCore PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    OUTPUT_NAME "CanvasCore"
)

# Include directories
target_include_directories(CanvasCore 
    PUBLIC 
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../Inc>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

# Preprocessor definitions
target_compile_definitions(CanvasCore
    PRIVATE
        $<$<CONFIG:Debug>:_DEBUG>
        $<$<CONFIG:Release>:NDEBUG>
        CANVASCORE_EXPORTS
        NOMINMAX
)

# Link with header-only libraries and dependencies
target_link_libraries(CanvasCore PUBLIC
    WIL
    GEM
    QLog
)

# Windows-specific settings
if(WIN32)
    target_compile_definitions(CanvasCore PRIVATE
        _WINDOWS
        _USRDLL
    )
    
    # Link with Windows libraries
    target_link_libraries(CanvasCore PRIVATE
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

# Use precompiled headers
target_precompile_headers(CanvasCore PRIVATE pch.h)

# Add the .def file as a source to use it for exports
target_sources(CanvasCore PRIVATE CanvasCore.def)

# Installation
install(TARGETS CanvasCore
    RUNTIME DESTINATION ${CANVAS_INSTALL_DEST}/bin
    LIBRARY DESTINATION ${CANVAS_INSTALL_DEST}/lib
)

# Install PDB files for debugging
install(FILES $<TARGET_PDB_FILE:CanvasCore> DESTINATION ${CANVAS_INSTALL_DEST}/bin OPTIONAL)





