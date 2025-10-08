# CanvasGfx12 Library - DirectX 12 Graphics implementation
cmake_minimum_required(VERSION 3.24)

# Define the library
add_library(CanvasGfx12 SHARED
    CanvasGfx12.cpp
    Device12.cpp
    GraphicsContext12.cpp
    Surface12.cpp
    SwapChain12.cpp
    dllmain.cpp
    pch.h
    targetver.h
)

# Set target properties
set_target_properties(CanvasGfx12 PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    OUTPUT_NAME "CanvasGfx12"
)

# Include directories
target_include_directories(CanvasGfx12 
    PUBLIC 
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../../Inc>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

# Preprocessor definitions
target_compile_definitions(CanvasGfx12 PRIVATE
    $<$<CONFIG:Debug>:_DEBUG>
    $<$<CONFIG:Release>:NDEBUG>
    CANVASGFX12_EXPORTS
    NOMINMAX
)

# Windows-specific settings
if(WIN32)
    target_compile_definitions(CanvasGfx12 PRIVATE
        _WINDOWS
        _USRDLL
    )

    if(TARGET D3D12Agility)
        message(STATUS "CanvasGfx12: Linking against Agility SDK")
        target_link_libraries(CanvasGfx12 PRIVATE
            D3D12Agility
            kernel32 user32 gdi32 winspool comdlg32 advapi32 shell32 ole32 oleaut32 uuid odbc32 odbccp32
        )
    else()
        target_link_libraries(CanvasGfx12 PRIVATE
            d3d12 dxgi d3dcompiler
            kernel32 user32 gdi32 winspool comdlg32 advapi32 shell32 ole32 oleaut32 uuid odbc32 odbccp32
        )
    endif()
endif()

# Link with other Canvas libraries
target_link_libraries(CanvasGfx12 PRIVATE
    CanvasCore
    D3D12ResourceUtils
    GEM
)

# Use precompiled headers
target_precompile_headers(CanvasGfx12 PRIVATE pch.h)

# Installation
install(TARGETS CanvasGfx12
    RUNTIME DESTINATION ${CANVAS_INSTALL_DEST}/bin
    LIBRARY DESTINATION ${CANVAS_INSTALL_DEST}/lib
)

# Install PDB files for debugging
install(FILES $<TARGET_PDB_FILE:CanvasGfx12> DESTINATION ${CANVAS_INSTALL_DEST}/bin OPTIONAL)



# Add post-build command to copy Agility SDK DLLs to output directory for development
set(AGILITY_BIN_PATH "${CMAKE_BINARY_DIR}/_deps/AgilityD3D12/extracted/build/native/bin/x64")
if(WIN32 AND EXISTS "${AGILITY_BIN_PATH}/D3D12Core.dll")
    # Runtime deployment layout recommended by Microsoft: D3D12/<Version>/D3D12Core.dll etc.
    set(AGILITY_RUNTIME_DIR $<TARGET_FILE_DIR:CanvasGfx12>/D3D12/1.616.1)
    add_custom_command(TARGET CanvasGfx12 POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${AGILITY_RUNTIME_DIR}"
        COMMAND ${CMAKE_COMMAND} -E echo "[AgilitySDK] Copying runtime DLLs to ${AGILITY_RUNTIME_DIR}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${AGILITY_BIN_PATH}/D3D12Core.dll" "${AGILITY_RUNTIME_DIR}/D3D12Core.dll"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${AGILITY_BIN_PATH}/d3d12SDKLayers.dll" "${AGILITY_RUNTIME_DIR}/d3d12SDKLayers.dll"
        VERBATIM
    )
endif()

# Install D3D12 Agility SDK runtime DLLs if they exist
if(WIN32 AND EXISTS "${AGILITY_BIN_PATH}/D3D12Core.dll")
    message(STATUS "Adding D3D12 Agility SDK DLLs to install manifest")
    install(FILES 
        "${AGILITY_BIN_PATH}/D3D12Core.dll"
        "${AGILITY_BIN_PATH}/d3d12SDKLayers.dll"
        DESTINATION ${CANVAS_INSTALL_DEST}/bin/D3D12
    )
endif()