# CanvasLightGarden - procedural deterministic many-light sample for Canvas
cmake_minimum_required(VERSION 3.24)

include(${CMAKE_SOURCE_DIR}/cmake/FetchFonts.cmake)

add_executable(CanvasLightGarden WIN32
    CanvasLightGarden.cpp
    pch.h
    targetver.h
    QLogAdapter.h
)

set_target_properties(CanvasLightGarden PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    OUTPUT_NAME "CanvasLightGarden"
)

target_include_directories(CanvasLightGarden PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../Inc
    ${CMAKE_CURRENT_SOURCE_DIR}/../Common
    ${CMAKE_CURRENT_SOURCE_DIR}/../../deps/InCommand/inc
)

target_compile_definitions(CanvasLightGarden PRIVATE
    $<$<CONFIG:Debug>:_DEBUG>
    $<$<CONFIG:Release>:NDEBUG>
    NOMINMAX
)

if(WIN32)
    target_compile_definitions(CanvasLightGarden PRIVATE WIN32 _WINDOWS)

    target_link_libraries(CanvasLightGarden PRIVATE
        kernel32 user32 gdi32 winspool comdlg32 advapi32
        shell32 ole32 oleaut32 uuid odbc32 odbccp32 comctl32
    )

    set_target_properties(CanvasLightGarden PROPERTIES
        LINK_FLAGS "/SUBSYSTEM:WINDOWS"
    )
endif()

target_link_libraries(CanvasLightGarden PRIVATE
    CanvasCore
    CanvasPlatformWin32
    InCommandLib
    InCommandHeaders
)

target_precompile_headers(CanvasLightGarden PRIVATE pch.h)

install(TARGETS CanvasLightGarden
    RUNTIME DESTINATION ${CANVAS_INSTALL_DEST}/bin
)
install(FILES $<TARGET_PDB_FILE:CanvasLightGarden> DESTINATION ${CANVAS_INSTALL_DEST}/bin OPTIONAL)

install(FILES
    "${CANVAS_FONT_INTER}"
    "${CANVAS_FONT_JETBRAINSMONO}"
    DESTINATION ${CANVAS_INSTALL_DEST}/bin/fonts
)

add_custom_command(TARGET CanvasLightGarden POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CANVAS_RUNTIME_DIR}/fonts"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CANVAS_FONT_INTER}"         "${CANVAS_RUNTIME_DIR}/fonts/"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CANVAS_FONT_JETBRAINSMONO}" "${CANVAS_RUNTIME_DIR}/fonts/"
    COMMENT "Staging fonts to runtime directory (CanvasLightGarden)"
    VERBATIM
)
