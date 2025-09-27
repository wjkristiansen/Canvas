# CanvasGfx12 - DirectX 12 Graphics implementation for Canvas 3D Graphics Engine
cmake_minimum_required(VERSION 3.24)

project(CanvasGfx12 VERSION 1.0.0)

# Add subdirectories
add_subdirectory(D3D12ResourceUtils)
add_subdirectory(Lib)

# Installation of d3dx12 headers (Microsoft helper headers)
install(DIRECTORY d3dx12/
    DESTINATION include/d3dx12
    FILES_MATCHING PATTERN "*.h" PATTERN "*.hpp"
)