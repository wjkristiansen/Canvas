# src/Common/CMakeLists.txt
# Common libraries used across Canvas components

# Merges Utf8.manifest into the manifest of an executable that links this target, so it runs
# with the UTF-8 active code page.
add_library(CanvasUtf8Manifest INTERFACE)
target_sources(CanvasUtf8Manifest INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/Utf8.manifest)

# JSON reading and writing helpers built on nlohmann::ordered_json (see JsonText.h).
add_library(CanvasJsonText STATIC
    JsonText.cpp
    JsonText.h
)

set_target_properties(CanvasJsonText PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
)

target_include_directories(CanvasJsonText PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
)

target_compile_definitions(CanvasJsonText PRIVATE
    $<$<CONFIG:Debug>:_DEBUG>
    $<$<CONFIG:Release>:NDEBUG>
    NOMINMAX
)

target_link_libraries(CanvasJsonText PUBLIC NlohmannJson)
