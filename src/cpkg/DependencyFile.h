//================================================================================================
// DependencyFile - the Makefile-format rule naming a build's output and the files it read
//
// See "Dependency Files" in src/cpkg/README.md.
//================================================================================================

#pragma once

#include "Diagnostics.h"

#include <string>
#include <vector>

namespace CpkgTool
{

// The paths made absolute, as AbsolutePath makes them, in their order, each once.
std::vector<std::string> UniqueAbsolutePaths(const std::vector<std::string> &paths);

// Reports the target and each input, absolute paths, that a dependency file cannot hold. Returns
// true when it can hold all of them.
bool CheckDependencyPaths(const std::string &target, const std::vector<std::string> &inputs, CDiagnostics &diagnostics);

// The text of the rule whose target is target and whose inputs are inputs, absolute paths
// CheckDependencyPaths accepted.
std::string DependencyRule(const std::string &target, const std::vector<std::string> &inputs);

} // namespace CpkgTool
