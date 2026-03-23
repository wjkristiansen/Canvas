# FetchFonts.cmake
#
# Downloads open source fonts at CMake configure time using FetchContent.
# After inclusion, the following variables are set:
#
#   CANVAS_FONT_INTER       - Absolute path to Inter-Regular.ttf
#   CANVAS_FONT_JETBRAINSMONO - Absolute path to JetBrainsMono-Regular.ttf
#
# Fonts are extracted into ${CMAKE_BINARY_DIR}/fonts-src/ and are not modified.
# Call sites are responsible for installing them to the desired destination.

include(FetchContent)

# ---------------------------------------------------------------------------
# Inter
# Source: https://github.com/rsms/inter
# The release zip contains a flat layout:  Inter Desktop/Inter-Regular.ttf
# ---------------------------------------------------------------------------
FetchContent_Declare(
    CanvasFonts_Inter
    URL "https://github.com/rsms/inter/releases/download/v4.1/Inter-4.1.zip"
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    DOWNLOAD_DIR "${CMAKE_BINARY_DIR}/fonts-downloads"
    SOURCE_DIR   "${CMAKE_BINARY_DIR}/fonts-src/inter"
)
message(STATUS "Canvas: Fetching Inter font...")
FetchContent_MakeAvailable(CanvasFonts_Inter)

# Locate Inter-Regular.ttf (known path, with glob fallback)
set(CANVAS_FONT_INTER "${canvasfonts_inter_SOURCE_DIR}/Inter Desktop/Inter-Regular.ttf")
if(NOT EXISTS "${CANVAS_FONT_INTER}")
    file(GLOB_RECURSE _inter_candidates
        "${canvasfonts_inter_SOURCE_DIR}/*Inter-Regular.ttf"
        "${canvasfonts_inter_SOURCE_DIR}/*Inter Regular.ttf")
    if(_inter_candidates)
        list(GET _inter_candidates 0 CANVAS_FONT_INTER)
    else()
        message(FATAL_ERROR "Canvas: Could not locate Inter-Regular.ttf in fetched archive at ${canvasfonts_inter_SOURCE_DIR}")
    endif()
endif()
message(STATUS "Canvas: Inter font -> ${CANVAS_FONT_INTER}")

# ---------------------------------------------------------------------------
# JetBrains Mono
# Source: https://github.com/JetBrains/JetBrainsMono
# The release zip structure:  fonts/ttf/JetBrainsMono-Regular.ttf
# ---------------------------------------------------------------------------
FetchContent_Declare(
    CanvasFonts_JetBrainsMono
    URL "https://github.com/JetBrains/JetBrainsMono/releases/download/v2.304/JetBrainsMono-2.304.zip"
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    DOWNLOAD_DIR "${CMAKE_BINARY_DIR}/fonts-downloads"
    SOURCE_DIR   "${CMAKE_BINARY_DIR}/fonts-src/jetbrainsmono"
)
message(STATUS "Canvas: Fetching JetBrains Mono font...")
FetchContent_MakeAvailable(CanvasFonts_JetBrainsMono)

set(CANVAS_FONT_JETBRAINSMONO "${canvasfonts_jetbrainsmono_SOURCE_DIR}/fonts/ttf/JetBrainsMono-Regular.ttf")
if(NOT EXISTS "${CANVAS_FONT_JETBRAINSMONO}")
    file(GLOB_RECURSE _jbmono_candidates
        "${canvasfonts_jetbrainsmono_SOURCE_DIR}/*JetBrainsMono-Regular.ttf")
    if(_jbmono_candidates)
        list(GET _jbmono_candidates 0 CANVAS_FONT_JETBRAINSMONO)
    else()
        message(FATAL_ERROR "Canvas: Could not locate JetBrainsMono-Regular.ttf in fetched archive at ${canvasfonts_jetbrainsmono_SOURCE_DIR}")
    endif()
endif()
message(STATUS "Canvas: JetBrains Mono font -> ${CANVAS_FONT_JETBRAINSMONO}")
