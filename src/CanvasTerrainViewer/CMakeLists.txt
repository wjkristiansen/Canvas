# CanvasTerrainViewer - heightfield-driven terrain renderer
cmake_minimum_required(VERSION 3.24)

include(${CMAKE_SOURCE_DIR}/cmake/FetchFonts.cmake)

add_executable(CanvasTerrainViewer WIN32
    CanvasTerrainViewer.cpp
    HeightField.cpp
    HeightField.h
    pch.h
    QLogAdapter.h
    CanvasTerrainViewer.rc
)

set_target_properties(CanvasTerrainViewer PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    OUTPUT_NAME "CanvasTerrainViewer"
)

target_include_directories(CanvasTerrainViewer PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../Inc
    ${CMAKE_CURRENT_SOURCE_DIR}/../ThinWin/Inc
    ${CMAKE_CURRENT_SOURCE_DIR}/../Common
    ${CMAKE_CURRENT_SOURCE_DIR}/../../deps/InCommand/inc
)

target_compile_definitions(CanvasTerrainViewer PRIVATE
    $<$<CONFIG:Debug>:_DEBUG>
    $<$<CONFIG:Release>:NDEBUG>
    NOMINMAX
)

if(WIN32)
    target_compile_definitions(CanvasTerrainViewer PRIVATE WIN32 _WINDOWS)

    target_link_libraries(CanvasTerrainViewer PRIVATE
        kernel32 user32 gdi32 winspool comdlg32 advapi32
        shell32 ole32 oleaut32 uuid odbc32 odbccp32 comctl32
        windowscodecs
    )

    set_target_properties(CanvasTerrainViewer PROPERTIES
        LINK_FLAGS "/SUBSYSTEM:WINDOWS"
    )
endif()

target_link_libraries(CanvasTerrainViewer PRIVATE
    CanvasCore
    ThinWin
    InCommandLib
    InCommandHeaders
)

target_precompile_headers(CanvasTerrainViewer PRIVATE pch.h)

install(TARGETS CanvasTerrainViewer
    RUNTIME DESTINATION ${CANVAS_INSTALL_DEST}/bin
)
install(FILES $<TARGET_PDB_FILE:CanvasTerrainViewer> DESTINATION ${CANVAS_INSTALL_DEST}/bin OPTIONAL)

install(FILES
    "${CANVAS_FONT_INTER}"
    "${CANVAS_FONT_JETBRAINSMONO}"
    DESTINATION ${CANVAS_INSTALL_DEST}/bin/fonts
)

# Sample assets ship with the viewer. Anything checked into
# src/CanvasTerrainViewer/assets/ gets installed alongside the executable
# (under bin/assets/) and staged into the runtime image dir at build time
# so the viewer can find a default heightmap without command-line args.
install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/assets/"
    DESTINATION "${CANVAS_INSTALL_DEST}/bin/assets"
)

# Stage fonts + sample assets into the shared runtime image directory
add_custom_command(TARGET CanvasTerrainViewer POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CANVAS_RUNTIME_DIR}/fonts"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CANVAS_FONT_INTER}"        "${CANVAS_RUNTIME_DIR}/fonts/"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CANVAS_FONT_JETBRAINSMONO}" "${CANVAS_RUNTIME_DIR}/fonts/"
    COMMAND ${CMAKE_COMMAND} -E copy_directory    "${CMAKE_CURRENT_SOURCE_DIR}/assets" "${CANVAS_RUNTIME_DIR}/assets"
    COMMENT "Staging fonts + sample assets to runtime directory (CanvasTerrainViewer)"
    VERBATIM
)
