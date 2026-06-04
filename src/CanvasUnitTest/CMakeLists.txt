# CanvasUnitTest - Unit tests for Canvas 3D Graphics Engine (GoogleTest)
cmake_minimum_required(VERSION 3.24)

include(FetchContent)

# Fetch GoogleTest (same version used by sibling deps to avoid duplicate fetches)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(googletest)

# Define the test executable (GoogleTest)
add_executable(CanvasUnitTest
    CanvasInterfacesTest.cpp
    CanvasMathTest.cpp
    CanvasTextTest.cpp
    D3D12ResourceUtilsTest.cpp
    GpuTaskGraphTest.cpp
    BVHTest.cpp
    LightBVHTest.cpp
    pch.h
    targetver.h
)

# Set target properties
set_target_properties(CanvasUnitTest PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    OUTPUT_NAME "CanvasUnitTest"
)

# Include directories
target_include_directories(CanvasUnitTest PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../Inc
    ${CMAKE_CURRENT_SOURCE_DIR}/../CanvasText
    ${CMAKE_CURRENT_SOURCE_DIR}/../CanvasCore
    ${CMAKE_CURRENT_SOURCE_DIR}/../RectanglePacker
    ${CMAKE_CURRENT_SOURCE_DIR}/../CanvasGfx12/Lib
    ${CMAKE_CURRENT_SOURCE_DIR}/../CanvasGfx12/D3D12ResourceUtils
)

# Preprocessor definitions
target_compile_definitions(CanvasUnitTest PRIVATE
    $<$<CONFIG:Debug>:_DEBUG>
    $<$<CONFIG:Release>:NDEBUG>
    NOMINMAX
)

# Windows-specific settings
if(WIN32)
    target_compile_definitions(CanvasUnitTest PRIVATE
        WIN32
        _CONSOLE
    )

    # Link with Windows libraries (excluding d3d12/dxgi/d3dcompiler - inherited from D3D12ResourceUtils)
    target_link_libraries(CanvasUnitTest PRIVATE
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

# Link with Canvas libraries
target_link_libraries(CanvasUnitTest PRIVATE
    CanvasCore
    CanvasText
    RectanglePacker
    D3D12ResourceUtils
    GTest::gtest
    GTest::gtest_main
)

# Use precompiled headers
target_precompile_headers(CanvasUnitTest PRIVATE pch.h)

# Register tests with CTest via GoogleTest discovery.
enable_testing()
include(GoogleTest)
gtest_discover_tests(CanvasUnitTest
    WORKING_DIRECTORY "${CANVAS_RUNTIME_DIR}"
    PROPERTIES
        TIMEOUT 300
        LABELS "UnitTest"
)

# Installation
install(TARGETS CanvasUnitTest
    RUNTIME DESTINATION ${CANVAS_INSTALL_DEST}/bin
)

# Install PDB files for debugging
install(FILES $<TARGET_PDB_FILE:CanvasUnitTest> DESTINATION ${CANVAS_INSTALL_DEST}/bin OPTIONAL)
