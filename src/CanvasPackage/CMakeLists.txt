# CanvasPackage - binary asset package I/O library
cmake_minimum_required(VERSION 3.24)

add_library(CanvasPackage STATIC
    CanvasPackage.cpp
    CpkgStream.cpp
    CpkgStream.h
    CpkgBlobTypes.h
    CpkgLog.h
    CpkgSink.cpp
    CpkgSink.h
    CpkgSource.cpp
    CpkgSource.h
    CpkgIO.cpp
    CpkgIO.h
    pch.h
    targetver.h
)

set_target_properties(CanvasPackage PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    OUTPUT_NAME "CanvasPackage"
)

target_include_directories(CanvasPackage
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/Inc>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/../Inc
)

target_link_libraries(CanvasPackage
    PUBLIC
        GEM
)

target_compile_definitions(CanvasPackage PRIVATE
    $<$<CONFIG:Debug>:_DEBUG>
    $<$<CONFIG:Release>:NDEBUG>
    NOMINMAX
)

target_precompile_headers(CanvasPackage PRIVATE pch.h)

install(TARGETS CanvasPackage
    ARCHIVE DESTINATION ${CANVAS_INSTALL_DEST}/lib
)
