# CanvasUnitTest - Unit tests for Canvas 3D Graphics Engine
cmake_minimum_required(VERSION 3.24)

# This project uses Microsoft CppUnitTestFramework

# Define the test library (Microsoft Test Framework requires DLL)
add_library(CanvasUnitTest SHARED
    CanvasCollectionsTest.cpp
    CanvasInterfacesTest.cpp
    CanvasMathTest.cpp
    D3D12ResourceUtilsTest.cpp
    stdafx.cpp
    stdafx.h
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
    ${CMAKE_CURRENT_SOURCE_DIR}/../CanvasGfx12/d3dx12
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
    
    # Link with Windows libraries
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
        d3d12
        dxgi
        d3dcompiler
    )
endif()

# Link with Canvas libraries
target_link_libraries(CanvasUnitTest PRIVATE
    CanvasCore
    CanvasModel
    D3D12ResourceUtils
)

# Use Microsoft CppUnitTestFramework
target_compile_definitions(CanvasUnitTest PRIVATE
    _WINDLL
)

# Link with Microsoft Test Framework
find_library(MS_UNITTEST_FRAMEWORK
    NAMES Microsoft.VisualStudio.TestTools.CppUnitTestFramework
    PATHS
        "$ENV{VCInstallDir}Auxiliary/VS/UnitTest/lib"
        "$ENV{VSINSTALLDIR}VC/Auxiliary/VS/UnitTest/lib"
    PATH_SUFFIXES x64
    REQUIRED
)

target_link_libraries(CanvasUnitTest PRIVATE ${MS_UNITTEST_FRAMEWORK})

# Use precompiled headers
target_precompile_headers(CanvasUnitTest PRIVATE stdafx.h)

# Register tests with CTest using vstest.console.exe for Microsoft Test Framework
enable_testing()

# For Microsoft Test Framework, find vstest.console.exe using more robust methods
find_program(VSTEST_CONSOLE
    NAMES vstest.console.exe
    HINTS
        # Try environment variables first (most reliable)
        "$ENV{VSINSTALLDIR}Common7/IDE/CommonExtensions/Microsoft/TestWindow"
        "$ENV{DevEnvDir}CommonExtensions/Microsoft/TestWindow"
    PATHS
        # Use vswhere.exe to find Visual Studio installations dynamically
        [HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\VisualStudio\\SxS\\VS7;15.0]/Common7/IDE/CommonExtensions/Microsoft/TestWindow
        [HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\VisualStudio\\SxS\\VS7;16.0]/Common7/IDE/CommonExtensions/Microsoft/TestWindow  
        [HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\VisualStudio\\SxS\\VS7;17.0]/Common7/IDE/CommonExtensions/Microsoft/TestWindow
    PATH_SUFFIXES
        Common7/IDE/CommonExtensions/Microsoft/TestWindow
    DOC "Visual Studio Test Console executable"
)

# If not found, try using vswhere.exe to locate Visual Studio
if(NOT VSTEST_CONSOLE AND WIN32)
    find_program(VSWHERE_EXE
        NAMES vswhere.exe
        PATHS
            "$ENV{ProgramFiles(x86)}/Microsoft Visual Studio/Installer"
            "${CMAKE_CURRENT_SOURCE_DIR}/tools"
        DOC "Visual Studio Locator tool"
    )
    
    if(VSWHERE_EXE)
        execute_process(
            COMMAND ${VSWHERE_EXE} -latest -property installationPath
            OUTPUT_VARIABLE VS_INSTALL_PATH
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
        )
        
        if(VS_INSTALL_PATH)
            find_program(VSTEST_CONSOLE
                NAMES vstest.console.exe
                PATHS "${VS_INSTALL_PATH}/Common7/IDE/CommonExtensions/Microsoft/TestWindow"
                NO_DEFAULT_PATH
            )
        endif()
    endif()
endif()

if(VSTEST_CONSOLE)
    add_test(NAME CanvasUnitTests 
        COMMAND ${VSTEST_CONSOLE} $<TARGET_FILE:CanvasUnitTest> "--logger:console;verbosity=normal"
        WORKING_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}
    )
    message(STATUS "Found VSTest Console: ${VSTEST_CONSOLE}")
    
    # Set test properties for better integration
    set_tests_properties(CanvasUnitTests PROPERTIES
        TIMEOUT 120
        LABELS "UnitTest;Microsoft"
    )
else()
    message(WARNING "VSTest Console not found - CanvasUnitTest will not be runnable via CTest")
    message(STATUS "You can still run the tests manually using Visual Studio Test Explorer")
    message(STATUS "Or run: vstest.console.exe ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/CanvasUnitTest.dll")
endif()

# Installation
install(TARGETS CanvasUnitTest
    RUNTIME DESTINATION bin
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
)