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

# Per-config runtime image directory
set(CANVAS_RUNTIME_DIR "${CMAKE_BINARY_DIR}/runtime/$<CONFIG>")

# Route all EXEs and DLLs into the runtime directory
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/runtime/$<CONFIG>")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/runtime/$<CONFIG>")

# Static libs stay in their default locations — no need to move them
# set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ...) intentionally omitted

message(STATUS "[RuntimeStaging] Runtime image: ${CMAKE_BINARY_DIR}/runtime/<Config>/")
