# RuntimeStaging.cmake
#
# Creates a single shared runtime image directory in the build tree so that
# all executables, DLLs, and data files (shaders, fonts, D3D12 Agility SDK)
# coexist side-by-side. This lets tests and applications run directly from
# the build tree — no install step required.
#
# Layout:
#   ${CMAKE_BINARY_DIR}/runtime/<Config>/
#       CanvasCore.dll
#       CanvasGfx12.dll
#       CanvasUnitTest.dll
#       CanvasModelViewer.exe
#       ...
#       shaders/
#           VSPrimary.cso
#           PSPrimary.cso
#           ...
#       fonts/
#           Inter-Regular.ttf
#           JetBrainsMono-Regular.ttf
#       D3D12/
#           1.616.1/
#               D3D12Core.dll
#               d3d12SDKLayers.dll
#
# Usage: include(cmake/RuntimeStaging.cmake) from the top-level CMakeLists.txt
#        AFTER project() and BEFORE any add_subdirectory() calls.
#
# After including, CANVAS_RUNTIME_DIR is available as a generator expression
# for use in custom commands that need the per-config runtime path.
#
# A client project that includes Canvas with add_subdirectory() does not see
# CANVAS_RUNTIME_DIR (it is scoped to Canvas). It calls
# canvas_target_runtime(<target>) for each of its executables instead, or
# reads the CANVAS_RUNTIME_DIR global property.

# Per-config runtime image directory
set(CANVAS_RUNTIME_DIR "${CMAKE_BINARY_DIR}/runtime/$<CONFIG>")
set_property(GLOBAL PROPERTY CANVAS_RUNTIME_DIR "${CANVAS_RUNTIME_DIR}")

# canvas_target_runtime(<target>)
#
# Builds <target> into the runtime image and makes it depend on the parts of
# Canvas that are loaded at runtime rather than linked: the graphics plugin
# and the compiled shaders.
function(canvas_target_runtime target)
    get_property(runtime_dir GLOBAL PROPERTY CANVAS_RUNTIME_DIR)
    set_target_properties(${target} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${runtime_dir}"
        LIBRARY_OUTPUT_DIRECTORY "${runtime_dir}"
    )
    add_dependencies(${target} CanvasGfx12 HLSL)
endfunction()

# Route all EXEs and DLLs into the runtime directory
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/runtime/$<CONFIG>")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/runtime/$<CONFIG>")

# Static libs stay in their default locations — no need to move them
# set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ...) intentionally omitted

message(STATUS "[RuntimeStaging] Runtime image: ${CMAKE_BINARY_DIR}/runtime/<Config>/")
