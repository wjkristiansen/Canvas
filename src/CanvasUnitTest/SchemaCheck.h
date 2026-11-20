//================================================================================================
// SchemaCheck - Checks JSON files against the Canvas JSON schemas by running jsonschema-cli
//
// The build provides jsonschema-cli (see cmake/FetchJsonSchemaCli.cmake). Without it, every
// check passes, and the first check prints a note that the schema checks are skipped.
//================================================================================================

#pragma once

#include <gtest/gtest.h>

#include <filesystem>
#include <string_view>

enum class CanvasSchema
{
    PackageSource,  // src/cpkg/package-source.schema.json: package files and bundle files
    Project,        // src/CanvasProject/project.schema.json: project manifests and built projects
};

// True when the build provides jsonschema-cli, so the checks below run.
bool SchemaChecksEnabled();

// Succeeds when the JSON file at path matches schema. A failure lists each place the file
// breaks the schema, as its JSON Pointer and message.
::testing::AssertionResult MatchesSchema(CanvasSchema schema, const std::filesystem::path &path);

// As MatchesSchema, for JSON text.
::testing::AssertionResult TextMatchesSchema(CanvasSchema schema, std::string_view text);

// Succeeds when the JSON text breaks schema.
::testing::AssertionResult TextBreaksSchema(CanvasSchema schema, std::string_view text);
