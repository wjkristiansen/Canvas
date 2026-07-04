# CanvasBake - command-line .cpkg bake tool
cmake_minimum_required(VERSION 3.24)

add_executable(CanvasBake
    CanvasBake.cpp
    pch.h
    targetver.h
)

set_target_properties(CanvasBake PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    OUTPUT_NAME "CanvasBake"
)

target_include_directories(CanvasBake PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../Inc                  # CanvasTypes.h, CanvasMath.hpp, CanvasPlatformWin32.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../CanvasPackage/Inc    # CanvasPackageData.h (public)
    ${CMAKE_CURRENT_SOURCE_DIR}/../CanvasPackage        # BakeManifest.h, BakePaths.h, TextureTable.h, SerializeScene.h (internal)
    ${CMAKE_CURRENT_SOURCE_DIR}/../CanvasFbx/Inc        # CanvasFbx.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../../deps/InCommand/inc
)

target_compile_definitions(CanvasBake PRIVATE
    $<$<CONFIG:Debug>:_DEBUG>
    $<$<CONFIG:Release>:NDEBUG>
    _CONSOLE
    NOMINMAX
)

if(WIN32)
    target_compile_definitions(CanvasBake PRIVATE
        WIN32
        _WINDOWS
    )

    target_link_libraries(CanvasBake PRIVATE
        kernel32
        user32
        ole32
        oleaut32
        uuid
    )

    set_target_properties(CanvasBake PROPERTIES
        LINK_FLAGS "/SUBSYSTEM:CONSOLE"
    )
endif()

target_link_libraries(CanvasBake PRIVATE
    CanvasPackage         # bake helpers + PackageData::WritePackage
    CanvasFbx             # Fbx::ImportScene
    CanvasPlatformWin32   # WIC texture decode (LoadImageData)
    CanvasCore            # XLogger / imported-scene support types
    InCommandLib
    InCommandHeaders
)

target_precompile_headers(CanvasBake PRIVATE pch.h)

install(TARGETS CanvasBake
    RUNTIME DESTINATION ${CANVAS_INSTALL_DEST}/bin
)

install(FILES $<TARGET_PDB_FILE:CanvasBake> DESTINATION ${CANVAS_INSTALL_DEST}/bin OPTIONAL)
