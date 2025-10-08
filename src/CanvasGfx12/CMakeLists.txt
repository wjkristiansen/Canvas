# CanvasGfx12 - DirectX 12 Graphics implementation for Canvas 3D Graphics Engine
cmake_minimum_required(VERSION 3.24)

project(CanvasGfx12 VERSION 1.0.0)

option(USE_D3D12_AGILITY "Download + use Direct3D 12 Agility SDK instead of Windows SDK d3d12" ON)

# ---------------------------------------------------------------------------
# Agility SDK (Microsoft.Direct3D.D3D12 NuGet package)
# ---------------------------------------------------------------------------
if(WIN32 AND USE_D3D12_AGILITY)
    message(STATUS "[AgilitySDK] Configuring Agility SDK 1.616.1...")
    set(AGILITY_NUGET_PACKAGE "Microsoft.Direct3D.D3D12")
    set(AGILITY_SDK_VERSION "1.616.1")
    set(AGILITY_BASE_DIR "${CMAKE_BINARY_DIR}/_deps/AgilityD3D12")
    set(AGILITY_NUPKG "${AGILITY_BASE_DIR}/${AGILITY_NUGET_PACKAGE}.${AGILITY_SDK_VERSION}.nupkg")
    set(AGILITY_EXTRACT_DIR "${AGILITY_BASE_DIR}/extracted")
    set(AGILITY_INCLUDE_DIR "${AGILITY_EXTRACT_DIR}/build/native/include")
    set(AGILITY_BIN_DIR     "${AGILITY_EXTRACT_DIR}/build/native/bin/x64")

    file(MAKE_DIRECTORY "${AGILITY_BASE_DIR}")

    if(NOT EXISTS "${AGILITY_NUPKG}")
        message(STATUS "[AgilitySDK] Downloading ${AGILITY_NUGET_PACKAGE} ${AGILITY_SDK_VERSION} ...")
        file(DOWNLOAD "https://www.nuget.org/api/v2/package/${AGILITY_NUGET_PACKAGE}/${AGILITY_SDK_VERSION}" "${AGILITY_NUPKG}" SHOW_PROGRESS STATUS dl_status)
        list(GET dl_status 0 dl_status_code)
        if(NOT dl_status_code EQUAL 0)
            message(WARNING "[AgilitySDK] Download failed (status=${dl_status}); falling back to Windows SDK d3d12.")
            set(USE_D3D12_AGILITY OFF CACHE BOOL "" FORCE)
        endif()
    endif()

    if(USE_D3D12_AGILITY)
        if(NOT EXISTS "${AGILITY_EXTRACT_DIR}/build/native/include/d3d12.h")
            message(STATUS "[AgilitySDK] Extracting package ...")
            file(MAKE_DIRECTORY "${AGILITY_EXTRACT_DIR}")
            file(ARCHIVE_EXTRACT INPUT "${AGILITY_NUPKG}" DESTINATION "${AGILITY_EXTRACT_DIR}")
        endif()

        if(EXISTS "${AGILITY_INCLUDE_DIR}/d3d12.h")
            message(STATUS "[AgilitySDK] Creating D3D12Agility interface library")
            add_library(D3D12Agility INTERFACE)
            target_include_directories(D3D12Agility INTERFACE "${AGILITY_INCLUDE_DIR}" "${AGILITY_INCLUDE_DIR}/d3dx12")
            # Link against Windows SDK d3d12.lib (Agility SDK is runtime-only, no import libs)
            target_link_libraries(D3D12Agility INTERFACE d3d12 dxgi d3dcompiler)
            message(STATUS "[AgilitySDK] D3D12Agility target created - newer headers with Windows SDK import libraries")
        else()
            message(WARNING "[AgilitySDK] Header not found after extraction; disabling Agility SDK usage.")
            set(USE_D3D12_AGILITY OFF CACHE BOOL "" FORCE)
        endif()
    endif()
endif()

# Add subdirectories (after defining interface library so it can be consumed)
add_subdirectory(D3D12ResourceUtils)
add_subdirectory(Lib)



