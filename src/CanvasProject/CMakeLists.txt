# CanvasProject - reads project manifests and resolves package names
cmake_minimum_required(VERSION 3.24)

# A static library: its public API exposes STL types, so it is linked into each binary that uses
# it rather than shared across a DLL boundary.
add_library(CanvasProject STATIC
    ProjectManifest.cpp
    ../Inc/CanvasProject.h
)

set_target_properties(CanvasProject PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    OUTPUT_NAME "CanvasProject"
)

target_include_directories(CanvasProject
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../Inc>
)

target_compile_definitions(CanvasProject PRIVATE
    $<$<CONFIG:Debug>:_DEBUG>
    $<$<CONFIG:Release>:NDEBUG>
    NOMINMAX
)

target_link_libraries(CanvasProject
    PUBLIC
        CanvasPkg
        GEM
    PRIVATE
        CanvasJsonText
)

install(TARGETS CanvasProject
    ARCHIVE DESTINATION ${CANVAS_INSTALL_DEST}/lib
)
