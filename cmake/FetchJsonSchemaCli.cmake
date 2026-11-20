# FetchJsonSchemaCli.cmake
#
# Provides jsonschema-cli (https://github.com/Stranger6667/jsonschema), a JSON Schema validator
# that the unit tests run to check JSON files against the Canvas schemas. After inclusion:
#
#   CANVAS_JSONSCHEMA_CLI_EXE - Absolute path to the jsonschema-cli executable, or empty when
#                               none is available, in which case the schema checks are skipped
#
# Set the cache variable CANVAS_JSONSCHEMA_CLI to an existing executable to use it in place of
# the download. On Windows x64 and ARM64 hosts, the pinned release is downloaded at configure
# time into ${CMAKE_BINARY_DIR}/tools/; on other hosts, jsonschema-cli is searched for on PATH.

include(FetchContent)

set(CANVAS_JSONSCHEMA_CLI "" CACHE FILEPATH
    "jsonschema-cli executable for the unit tests' schema checks; empty to use the pinned download")

set(_jsonschema_cli_version "0.58.5")

if(CANVAS_JSONSCHEMA_CLI)
    if(NOT EXISTS "${CANVAS_JSONSCHEMA_CLI}")
        message(FATAL_ERROR "Canvas: CANVAS_JSONSCHEMA_CLI names no file: ${CANVAS_JSONSCHEMA_CLI}")
    endif()
    set(CANVAS_JSONSCHEMA_CLI_EXE "${CANVAS_JSONSCHEMA_CLI}")
elseif(CMAKE_HOST_WIN32)
    if(CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "ARM64")
        set(_jsonschema_cli_target "aarch64-pc-windows-msvc")
        set(_jsonschema_cli_sha256 "cd8d72d517a4f3a58569b0819db6c6fbc3747db26818af704248288b744a78bf")
    else()
        set(_jsonschema_cli_target "x86_64-pc-windows-msvc")
        set(_jsonschema_cli_sha256 "4003ed352f9f510e763e27616c6df9762988935c9ade043f07592c409d2290fd")
    endif()

    # The release zip holds jsonschema-cli.exe at its root.
    FetchContent_Declare(
        CanvasJsonSchemaCli
        URL "https://github.com/Stranger6667/jsonschema/releases/download/cli-v${_jsonschema_cli_version}/jsonschema-cli-${_jsonschema_cli_target}.zip"
        URL_HASH SHA256=${_jsonschema_cli_sha256}
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
        DOWNLOAD_DIR "${CMAKE_BINARY_DIR}/tools-downloads"
        SOURCE_DIR   "${CMAKE_BINARY_DIR}/tools/jsonschema-cli"
    )
    message(STATUS "Canvas: Fetching jsonschema-cli ${_jsonschema_cli_version}...")
    FetchContent_MakeAvailable(CanvasJsonSchemaCli)
    set(CANVAS_JSONSCHEMA_CLI_EXE "${canvasjsonschemacli_SOURCE_DIR}/jsonschema-cli.exe")
    if(NOT EXISTS "${CANVAS_JSONSCHEMA_CLI_EXE}")
        message(FATAL_ERROR "Canvas: Could not locate jsonschema-cli.exe in fetched archive at ${canvasjsonschemacli_SOURCE_DIR}")
    endif()
else()
    find_program(_jsonschema_cli_found NAMES jsonschema-cli)
    if(_jsonschema_cli_found)
        set(CANVAS_JSONSCHEMA_CLI_EXE "${_jsonschema_cli_found}")
    else()
        set(CANVAS_JSONSCHEMA_CLI_EXE "")
    endif()
endif()

if(CANVAS_JSONSCHEMA_CLI_EXE)
    message(STATUS "Canvas: jsonschema-cli -> ${CANVAS_JSONSCHEMA_CLI_EXE}")
else()
    message(STATUS "Canvas: jsonschema-cli not found; the unit tests skip their schema checks")
endif()
