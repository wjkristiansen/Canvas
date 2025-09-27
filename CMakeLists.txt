cmake_minimum_required(VERSION 3.24)

# Set the project name and version
project(Canvas 
    VERSION 1.0.0
    DESCRIPTION "Canvas 3D Graphics Engine"
    LANGUAGES CXX
)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Set default build type if not specified
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# Configure build types
set(CMAKE_CONFIGURATION_TYPES "Debug;Release;RelWithDebInfo;MinSizeRel" CACHE STRING "" FORCE)

# Include Windows-specific configuration
include(cmake/WindowsConfig.cmake)

# Include directories
include_directories(src/Inc)

# Enable testing
enable_testing()

# Disable WIL tests and packaging to avoid compilation issues
set(WIL_BUILD_TESTS OFF CACHE BOOL "Disable WIL tests")
set(WIL_BUILD_PACKAGING OFF CACHE BOOL "Disable WIL packaging")

# If WIL has its own CMakeLists.txt, include it first
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/wil/CMakeLists.txt")
    add_subdirectory(src/wil EXCLUDE_FROM_ALL)
else()
    # Create our own interface library for WIL (header-only) if WIL doesn't have CMakeLists
    add_library(Canvas_WIL INTERFACE)
    target_include_directories(Canvas_WIL INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src/wil/include>
        $<INSTALL_INTERFACE:include/wil>
    )
    # Create alias for compatibility
    add_library(WIL ALIAS Canvas_WIL)
endif()

# Create interface library for header-only LinkedList submodule
add_library(LinkedList INTERFACE)
target_include_directories(LinkedList INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src/LinkedList/Inc>
    $<INSTALL_INTERFACE:include/LinkedList>
)

# Install LinkedList interface library
install(TARGETS LinkedList
    EXPORT D3D12ResourceUtilsTargets
    INCLUDES DESTINATION include
)

# Install LinkedList headers
install(DIRECTORY src/LinkedList/Inc/
    DESTINATION include/LinkedList
    FILES_MATCHING PATTERN "*.h" PATTERN "*.hpp" PATTERN "*.inl"
)

# Create interface library for header-only GEM submodule
add_library(GEM INTERFACE)
target_include_directories(GEM INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src/GEM/Inc>
    $<INSTALL_INTERFACE:include/GEM>
)

# Install GEM interface library
install(TARGETS GEM
    EXPORT CanvasCoreTargets
    INCLUDES DESTINATION include
)

# Install GEM headers
install(DIRECTORY src/GEM/Inc/
    DESTINATION include/GEM
    FILES_MATCHING PATTERN "*.h" PATTERN "*.hpp" PATTERN "*.inl"
)

# Add subdirectories for submodules first (they may be dependencies)
add_subdirectory(src/QLog)
add_subdirectory(src/ThinWin)
add_subdirectory(src/Allocators)

# Add main project subdirectories
add_subdirectory(src/CanvasCore)
add_subdirectory(src/CanvasGfx12)
add_subdirectory(src/CanvasModel)
add_subdirectory(src/CanvasConsole)
add_subdirectory(src/CanvasModelViewer)
add_subdirectory(src/CanvasUnitTest)
add_subdirectory(src/LinkedList/LinkedListTest)
add_subdirectory(src/HLSL)

# Package configuration
include(CMakePackageConfigHelpers)

# Install include files
install(DIRECTORY src/Inc/
    DESTINATION include
    FILES_MATCHING PATTERN "*.h" PATTERN "*.hpp"
)

# Create package config files
configure_package_config_file(
    "cmake/CanvasConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/CanvasConfig.cmake"
    INSTALL_DESTINATION lib/cmake/Canvas
)

write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/CanvasConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

# Install package config files
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/CanvasConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/CanvasConfigVersion.cmake"
    DESTINATION lib/cmake/Canvas
)