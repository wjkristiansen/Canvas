# cpkg - the command-line tool that builds package sources and projects
cmake_minimum_required(VERSION 3.24)

# Everything but main, so the unit tests can run cpkg's commands in-process.
add_library(CpkgTool STATIC
    Build.cpp
    BundleLayout.cpp
    Cli.cpp
    CpkgTool.h
    Diagnostics.cpp
    Diagnostics.h
    Dump.cpp
    FileSystem.cpp
    FileSystem.h
    Format.cpp
    PackageWrite.cpp
    Project.cpp
    Project.h
    Source.h
    SourceLoad.cpp
    SourceValidate.cpp
    Stages.h
    Translate.cpp
    Unpack.cpp
    pch.h
)

set_target_properties(CpkgTool PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
)

target_include_directories(CpkgTool PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
)

target_compile_definitions(CpkgTool PRIVATE
    $<$<CONFIG:Debug>:_DEBUG>
    $<$<CONFIG:Release>:NDEBUG>
    NOMINMAX
    CPKG_VERSION="${Canvas_VERSION}"
)

target_link_libraries(CpkgTool
    PUBLIC
        CanvasJsonText
        CanvasPkg
        CanvasProject
    PRIVATE
        InCommandLib
        InCommandHeaders
)

target_precompile_headers(CpkgTool PRIVATE pch.h)

add_executable(cpkg Main.cpp)

set_target_properties(cpkg PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
)

target_compile_definitions(cpkg PRIVATE
    $<$<CONFIG:Debug>:_DEBUG>
    $<$<CONFIG:Release>:NDEBUG>
    NOMINMAX
    WIN32_LEAN_AND_MEAN
)

target_link_libraries(cpkg PRIVATE
    CanvasUtf8Manifest
    CpkgTool
)

install(TARGETS cpkg
    RUNTIME DESTINATION ${CANVAS_INSTALL_DEST}/bin
)

install(FILES $<TARGET_PDB_FILE:cpkg> DESTINATION ${CANVAS_INSTALL_DEST}/bin OPTIONAL)
