# RectanglePacker - 2D bin-packing allocator
cmake_minimum_required(VERSION 3.24)

add_library(RectanglePacker STATIC
    RectanglePacker.cpp
    RectanglePacker.h
)

set_target_properties(RectanglePacker PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
)

target_include_directories(RectanglePacker
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<INSTALL_INTERFACE:include>
)

install(TARGETS RectanglePacker
    ARCHIVE DESTINATION ${CANVAS_INSTALL_DEST}/lib
)
