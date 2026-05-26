add_library(CanvasPlatformWin32 STATIC
    AppWindow.cpp
    AppWindow.h
    RawInput.cpp
    RawInput.h
    pch.h
    ../Inc/CanvasPlatformWin32.h)

set_target_properties(CanvasPlatformWin32 PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON)

target_include_directories(CanvasPlatformWin32
    PUBLIC  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../Inc>
            $<INSTALL_INTERFACE:include>
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_compile_definitions(CanvasPlatformWin32 PRIVATE
    $<$<CONFIG:Debug>:_DEBUG>
    $<$<CONFIG:Release>:NDEBUG>
    NOMINMAX
    _WINDOWS)

target_link_libraries(CanvasPlatformWin32
    PUBLIC  GEM
    PRIVATE ThinWin user32 kernel32)

target_precompile_headers(CanvasPlatformWin32 PRIVATE pch.h)

install(TARGETS CanvasPlatformWin32
    ARCHIVE DESTINATION ${CANVAS_INSTALL_DEST}/lib)
