# CanvasPkg - reads and writes CanvasPkg package files
cmake_minimum_required(VERSION 3.24)

# A static library: its public API exposes STL types, and it links no Canvas library, so a
# program that writes or reads packages can use it without the engine.
add_library(CanvasPkg STATIC
    CpkgFile.cpp
    CpkgFile.h
    CpkgFormat.h
    CpkgReader.cpp
    CpkgWriter.cpp
    ../Inc/CanvasPkg.h
)

set_target_properties(CanvasPkg PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    OUTPUT_NAME "CanvasPkg"
)

target_include_directories(CanvasPkg
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../Inc>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_definitions(CanvasPkg PRIVATE
    $<$<CONFIG:Debug>:_DEBUG>
    $<$<CONFIG:Release>:NDEBUG>
    NOMINMAX
)

target_link_libraries(CanvasPkg PUBLIC GEM)

install(TARGETS CanvasPkg
    ARCHIVE DESTINATION ${CANVAS_INSTALL_DEST}/lib
)
