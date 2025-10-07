# WindowsConfig.cmake - Windows-specific configuration for Canvas 3D Graphics Engine
# This file configures Windows-specific build settings, libraries, and compiler options

# Only configure if we're building on Windows
if(NOT WIN32)
    return()
endif()

message(STATUS "Configuring Windows-specific build settings...")

# =============================================================================
# Windows SDK and Platform Configuration
# =============================================================================

# Ensure we have a minimum Windows version (Windows 10 for DirectX 12 support)
if(NOT DEFINED CMAKE_SYSTEM_VERSION)
    set(CMAKE_SYSTEM_VERSION "10.0" CACHE STRING "Windows SDK version" FORCE)
endif()

# Set minimum Windows API version for DirectX 12 support (Windows 10)
add_compile_definitions(
    _WIN32_WINNT=0x0A00  # Windows 10
    WINVER=0x0A00        # Windows 10
    NTDDI_VERSION=NTDDI_WIN10
)

# =============================================================================
# DirectX 12 and Graphics Libraries
# =============================================================================

# Find DirectX 12 libraries (required for graphics engine)
find_library(D3D12_LIB d3d12 REQUIRED)
find_library(DXGI_LIB dxgi REQUIRED)
find_library(D3DCOMPILER_LIB d3dcompiler REQUIRED)

if(D3D12_LIB AND DXGI_LIB AND D3DCOMPILER_LIB)
    message(STATUS "Found DirectX 12 libraries:")
    message(STATUS "  d3d12: ${D3D12_LIB}")
    message(STATUS "  dxgi: ${DXGI_LIB}")
    message(STATUS "  d3dcompiler: ${D3DCOMPILER_LIB}")
else()
    message(FATAL_ERROR "DirectX 12 libraries not found. Please install Windows SDK.")
endif()

# =============================================================================
# Standard Windows System Libraries
# =============================================================================

# Define standard Windows system libraries used throughout the project
set(CANVAS_WINDOWS_SYSTEM_LIBS
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

# Define DirectX libraries
set(CANVAS_DIRECTX_LIBS
    d3d12
    dxgi
    d3dcompiler
)

# =============================================================================
# Compiler and Linker Configuration
# =============================================================================

# Visual Studio specific configurations
if(MSVC)
    message(STATUS "Configuring MSVC compiler settings...")
    
    # Enable parallel compilation
    add_compile_options(/MP)
    
    # Set warning level
    add_compile_options(/W4 /WX)
    
    # Disable specific warnings that are common in graphics programming
    add_compile_options(
        /wd4005  # macro redefinition (common with Windows headers)
        /wd4996  # deprecated function warnings
    )
    
    # Enable function-level linking and COMDAT folding for Release builds
    add_compile_options($<$<CONFIG:Release>:/Gy>)
    add_link_options($<$<CONFIG:Release>:/OPT:REF>)
    add_link_options($<$<CONFIG:Release>:/OPT:ICF>)
    
    # Enable debug information in all builds
    add_compile_options(/Zi)
    add_link_options(/DEBUG)
    
    # Set runtime library to Multi-threaded DLL
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
endif()

# =============================================================================
# Common Windows Preprocessor Definitions
# =============================================================================

# Standard Windows definitions used across the project
add_compile_definitions(
    WIN32
    _WINDOWS
    NOMINMAX                # Prevent Windows.h from defining min/max macros
    WIN32_LEAN_AND_MEAN    # Exclude rarely used parts of Windows headers
    VC_EXTRALEAN           # Exclude even more from Windows headers
)

# Configuration-specific definitions
add_compile_definitions(
    $<$<CONFIG:Debug>:_DEBUG>
    $<$<CONFIG:Release>:NDEBUG>
)

# =============================================================================
# Platform-specific Features
# =============================================================================

# Enable Windows-specific features for libraries
if(BUILD_SHARED_LIBS OR CMAKE_BUILD_TYPE STREQUAL "Debug")
    # Enable export of all symbols for shared libraries on Windows
    set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS ON CACHE BOOL "Export all symbols on Windows" FORCE)
endif()

# =============================================================================
# Helper Functions
# =============================================================================

# Function to add Windows-specific settings to a target
function(canvas_configure_windows_target target_name)
    if(NOT WIN32)
        return()
    endif()
    
    # Add Windows system libraries
    target_link_libraries(${target_name} PRIVATE ${CANVAS_WINDOWS_SYSTEM_LIBS})
    
    # Set subsystem based on target type
    get_target_property(target_type ${target_name} TYPE)
    if(target_type STREQUAL "EXECUTABLE")
        get_target_property(win32_executable ${target_name} WIN32_EXECUTABLE)
        if(win32_executable)
            set_target_properties(${target_name} PROPERTIES
                LINK_FLAGS "/SUBSYSTEM:WINDOWS"
            )
        else()
            set_target_properties(${target_name} PROPERTIES
                LINK_FLAGS "/SUBSYSTEM:CONSOLE"
            )
        endif()
    endif()
endfunction()

# Function to add DirectX libraries to a graphics target
function(canvas_configure_directx_target target_name)
    if(NOT WIN32)
        return()
    endif()
    
    target_link_libraries(${target_name} PRIVATE ${CANVAS_DIRECTX_LIBS})
endfunction()

# =============================================================================
# Information Display
# =============================================================================

message(STATUS "Windows configuration completed:")
message(STATUS "  Windows SDK Version: ${CMAKE_SYSTEM_VERSION}")
message(STATUS "  Target Architecture: ${CMAKE_VS_PLATFORM_NAME}")
message(STATUS "  MSVC Runtime: ${CMAKE_MSVC_RUNTIME_LIBRARY}")
message(STATUS "  Export All Symbols: ${CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS}")