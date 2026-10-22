cmake_minimum_required(VERSION 3.24)

# Ensure MSVC runtime selection is controlled by CMAKE_MSVC_RUNTIME_LIBRARY.
cmake_policy(SET CMP0091 NEW)

# Set the project name and version
project(Canvas 
    VERSION 1.0.0
    DESCRIPTION "Canvas 3D Graphics Engine"
    LANGUAGES C CXX     # C: ufbx is built from ufbx.c
)

if(MSVC AND PROJECT_IS_TOP_LEVEL)
    # Root-level authoritative CRT mode: /MDd for Debug, /MD for all other configs.
    # A parent project that includes Canvas with add_subdirectory() owns the CRT choice, and
    # Canvas targets inherit it so every object in the link agrees.
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL" CACHE STRING "MSVC runtime library" FORCE)
endif()

# Function to determine architecture name from CMake variables
function(get_architecture_name OUT_VAR_NAME)
    # Use CMAKE_VS_PLATFORM_NAME for Visual Studio generators (most reliable for target architecture)
    # Fall back to CMAKE_SYSTEM_PROCESSOR for other generators
    if(CMAKE_VS_PLATFORM_NAME)
        # Visual Studio generator provides the target platform name directly
        set(ARCH_NAME "${CMAKE_VS_PLATFORM_NAME}")
    else()
        # For other generators, use CMAKE_SYSTEM_PROCESSOR (target processor type)
        # Note: This is the target architecture, not host architecture when cross-compiling
        if(CMAKE_SYSTEM_PROCESSOR MATCHES "AMD64|x86_64")
            set(ARCH_NAME "x64")
        elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "ARM64|aarch64")
            set(ARCH_NAME "ARM64")
        elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "ARM|arm")
            set(ARCH_NAME "ARM")
        elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "i[3-6]86|x86")
            set(ARCH_NAME "x86")
        else()
            set(ARCH_NAME "${CMAKE_SYSTEM_PROCESSOR}")
        endif()
    endif()
    set(${OUT_VAR_NAME} "${ARCH_NAME}" PARENT_SCOPE)
endfunction()

# Determine architecture
get_architecture_name(ARCH_NAME)

# Set default install prefix to local install directory with architecture and config.
# A parent project that includes Canvas with add_subdirectory() owns the install prefix.
if(PROJECT_IS_TOP_LEVEL AND CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
    set(CMAKE_INSTALL_PREFIX "${Canvas_SOURCE_DIR}/install" CACHE PATH "Install prefix" FORCE)
endif()

# Samples and tests build by default only when Canvas is the top-level project.
option(CANVAS_BUILD_SAMPLES "Build the Canvas sample applications" ${PROJECT_IS_TOP_LEVEL})
option(CANVAS_BUILD_TESTS "Build the Canvas unit tests" ${PROJECT_IS_TOP_LEVEL})

# Set up architecture/config-aware destination for runtime installation
set(CANVAS_INSTALL_DEST "${ARCH_NAME}/$<CONFIG>")

# Set C++ standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Set default build type if not specified. A parent project that includes Canvas with
# add_subdirectory() owns the build type, so Canvas targets build in the same configuration
# as the parent's (and stage into the same runtime directory).
if(PROJECT_IS_TOP_LEVEL AND NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# Configure build types. A parent project that includes Canvas with add_subdirectory() owns
# its configuration list, which may include custom configurations.
if(PROJECT_IS_TOP_LEVEL)
    set(CMAKE_CONFIGURATION_TYPES "Debug;Release;RelWithDebInfo;MinSizeRel" CACHE STRING "" FORCE)
endif()

# Include Windows-specific configuration
include(cmake/WindowsConfig.cmake)

# Set up shared runtime image directory (all binaries + data in one place)
include(cmake/RuntimeStaging.cmake)

# Include directories
include_directories(src/Inc)

# Enable testing
enable_testing()

# Test options
option(ALLOCATORS_BUILD_TESTS "Build Allocators unit tests" OFF)

# If WIL has its own CMakeLists.txt, include it first
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/deps/wil/CMakeLists.txt")
    # Disable WIL tests and packaging to avoid compilation issues. These are normal variables so
    # they stay out of a parent project's cache; WIL's minimum CMake version predates option()
    # honoring normal variables (CMP0077), so enable that while it is added.
    set(WIL_BUILD_TESTS OFF)
    set(WIL_BUILD_PACKAGING OFF)
    set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)
    add_subdirectory(deps/wil EXCLUDE_FROM_ALL)
    unset(CMAKE_POLICY_DEFAULT_CMP0077)
else()
    # Create our own interface library for WIL (header-only) if WIL doesn't have CMakeLists
    add_library(Canvas_WIL INTERFACE)
    target_include_directories(Canvas_WIL INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/deps/wil/include>
        $<INSTALL_INTERFACE:include/wil>
    )
    # Create alias for compatibility
    add_library(WIL ALIAS Canvas_WIL)
endif()

# Create interface library for header-only GEM submodule
add_library(GEM INTERFACE)
target_include_directories(GEM INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/deps/GEM/Inc>
    $<INSTALL_INTERFACE:include/GEM>
)
target_compile_features(GEM INTERFACE cxx_std_20)

# Header-only nlohmann/json (single-include build)
add_library(NlohmannJson INTERFACE)
target_include_directories(NlohmannJson INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/deps/json/single_include>
)

# Add subdirectories for submodules first (they may be dependencies)
add_subdirectory(deps/QLog)
add_subdirectory(deps/ThinWin)
add_subdirectory(deps/InCommand)
add_subdirectory(src/CanvasPlatformWin32)
# Allocators is header-only, no CMakeLists.txt needed
# Create interface library for header-only Allocators submodule
add_library(Allocators INTERFACE)
target_include_directories(Allocators INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/deps/Allocators/Inc>
    $<INSTALL_INTERFACE:include/Allocators>
)

# Add main project subdirectories
add_subdirectory(src/Common)
add_subdirectory(src/CanvasCore)
add_subdirectory(src/RectanglePacker)
add_subdirectory(src/CanvasText)
add_subdirectory(src/CanvasGfx12)
add_subdirectory(src/CanvasFbx)
if(CANVAS_BUILD_SAMPLES)
    add_subdirectory(src/CanvasConsole)
    add_subdirectory(src/CanvasModelViewer)
    add_subdirectory(src/CanvasTerrainViewer)
    add_subdirectory(src/CanvasLightGarden)
endif()
if(CANVAS_BUILD_TESTS)
    add_subdirectory(src/CanvasUnitTest)
endif()
# Allocators tests - conditionally include based on option
if(ALLOCATORS_BUILD_TESTS)
    add_subdirectory(deps/Allocators/AllocatorsTest)
endif()
add_subdirectory(src/HLSL)
