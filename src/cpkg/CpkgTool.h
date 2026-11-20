//================================================================================================
// CpkgTool - the cpkg command-line tool's commands
//
// See src/cpkg/README.md. Main.cpp runs Run with the process's arguments and standard streams.
//================================================================================================

#pragma once

#include "Diagnostics.h"

#include <optional>
#include <ostream>
#include <string>

namespace CpkgTool
{

// Exit statuses; see "Results" in src/cpkg/README.md.
constexpr int kExitSuccess = 0;
constexpr int kExitError = 1;
constexpr int kExitUsage = 2;

// Runs the command line argv[0..argc), whose strings are UTF-8, writing command output to out and
// diagnostics to err. Returns the exit status.
int Run(int argc, const char *const argv[], std::ostream &out, std::ostream &err);

struct DumpOptions
{
    std::string Package;                // UTF-8 path of the .cpkg file
    std::optional<std::string> Bundle;  // the one bundle to list
    bool Payloads = false;
};

// Prints the package's contents to out, and reports what it cannot read to diagnostics.
void Dump(const DumpOptions &options, std::ostream &out, CDiagnostics &diagnostics);

struct BuildOptions
{
    std::string Package;                // UTF-8 path of the .cpkg.json file
    std::optional<std::string> Output;  // the .cpkg file to write; by default beside the package file
};

// Builds the package source into a .cpkg file, reporting what the build finds.
void Build(const BuildOptions &options, CDiagnostics &diagnostics);

struct ValidateOptions
{
    std::string Package;                // UTF-8 path of the .cpkg.json file
};

// Runs the build's stages short of Write on the package source, reporting what they find.
void Validate(const ValidateOptions &options, CDiagnostics &diagnostics);

} // namespace CpkgTool
