# HLSL - Shader compilation for Canvas 3D Graphics Engine
cmake_minimum_required(VERSION 3.24)

# This is a custom target for HLSL shader compilation
# Note: CMake doesn't have built-in HLSL support, so we'll create custom commands

# Find the DirectX Shader Compiler (dxc.exe) or FXC compiler
find_program(DXC_EXECUTABLE 
    NAMES dxc.exe
    PATHS 
        "C:/Program Files (x86)/Windows Kits/10/bin/*/x64"
        "C:/Program Files/Windows Kits/10/bin/*/x64"
    PATH_SUFFIXES bin
)

find_program(FXC_EXECUTABLE 
    NAMES fxc.exe
    PATHS 
        "C:/Program Files (x86)/Windows Kits/10/bin/*/x64"
        "C:/Program Files/Windows Kits/10/bin/*/x64"
    PATH_SUFFIXES bin
)

if(NOT DXC_EXECUTABLE AND NOT FXC_EXECUTABLE)
    message(WARNING "Neither DXC nor FXC compiler found. Shader compilation will be skipped.")
    # Create dummy target
    add_custom_target(HLSL)
    return()
endif()

# Choose compiler (prefer DXC over FXC)
if(DXC_EXECUTABLE)
    set(SHADER_COMPILER ${DXC_EXECUTABLE})
    set(COMPILER_TYPE "DXC")
    message(STATUS "Using DirectX Shader Compiler (DXC): ${SHADER_COMPILER}")
else()
    set(SHADER_COMPILER ${FXC_EXECUTABLE})
    set(COMPILER_TYPE "FXC")
    message(STATUS "Using FXC Shader Compiler: ${SHADER_COMPILER}")
endif()

# Define shader output directory
set(SHADER_OUTPUT_DIR ${CMAKE_BINARY_DIR}/shaders)
file(MAKE_DIRECTORY ${SHADER_OUTPUT_DIR})

# Shader compilation function
function(add_hlsl_shader SHADER_FILE PROFILE ENTRY_POINT)
    get_filename_component(SHADER_NAME ${SHADER_FILE} NAME_WE)
    set(OUTPUT_FILE ${SHADER_OUTPUT_DIR}/${SHADER_NAME}.cso)
    
    if(COMPILER_TYPE STREQUAL "DXC")
        add_custom_command(
            OUTPUT ${OUTPUT_FILE}
            COMMAND ${SHADER_COMPILER}
                -T ${PROFILE}
                -E ${ENTRY_POINT}
                -Fo ${OUTPUT_FILE}
                -I ${CMAKE_CURRENT_SOURCE_DIR}
                ${CMAKE_CURRENT_SOURCE_DIR}/${SHADER_FILE}
            DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/${SHADER_FILE}
                    ${CMAKE_CURRENT_SOURCE_DIR}/Common.hlsli
            COMMENT "Compiling HLSL shader ${SHADER_FILE} with DXC"
            VERBATIM
        )
    else()
        add_custom_command(
            OUTPUT ${OUTPUT_FILE}
            COMMAND ${SHADER_COMPILER}
                /T ${PROFILE}
                /E ${ENTRY_POINT}
                /Fo ${OUTPUT_FILE}
                /I ${CMAKE_CURRENT_SOURCE_DIR}
                ${CMAKE_CURRENT_SOURCE_DIR}/${SHADER_FILE}
            DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/${SHADER_FILE}
                    ${CMAKE_CURRENT_SOURCE_DIR}/Common.hlsli
            COMMENT "Compiling HLSL shader ${SHADER_FILE} with FXC"
            VERBATIM
        )
    endif()
    
    list(APPEND SHADER_OUTPUTS ${OUTPUT_FILE})
    set(SHADER_OUTPUTS ${SHADER_OUTPUTS} PARENT_SCOPE)
endfunction()

# Compile shaders
set(SHADER_OUTPUTS "")

# Vertex Shader
add_hlsl_shader("VSPrimary.hlsl" "vs_5_0" "VSPrimary")

# Pixel Shader  
add_hlsl_shader("PSPrimary.hlsl" "ps_5_0" "PSPrimary")

# Create the HLSL target
add_custom_target(HLSL ALL
    DEPENDS ${SHADER_OUTPUTS}
    COMMENT "Building HLSL shaders"
)

# Copy header files
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/Primary.h
    ${CMAKE_BINARY_DIR}/include/Primary.h
    COPYONLY
)

# Installation
install(FILES ${SHADER_OUTPUTS}
    DESTINATION shaders
    OPTIONAL
)

install(FILES 
    Primary.h
    Common.hlsli
    DESTINATION include/shaders
)