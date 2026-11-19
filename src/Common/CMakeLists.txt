# src/Common/CMakeLists.txt
# Common libraries used across Canvas components

# Merges Utf8.manifest into the manifest of an executable that links this target, so it runs
# with the UTF-8 active code page.
add_library(CanvasUtf8Manifest INTERFACE)
target_sources(CanvasUtf8Manifest INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/Utf8.manifest)
