//================================================================================================
// Build - the build pipeline's stages, run by the build and validate commands
//================================================================================================

#include "CpkgTool.h"
#include "DependencyFile.h"
#include "FileSystem.h"
#include "Project.h"
#include "Stages.h"

namespace CpkgTool
{

namespace
{
    bool IsProject(const std::string &path)
    {
        return path.ends_with(".cproj.json");
    }
}

//------------------------------------------------------------------------------------------------
void Build(const BuildOptions &options, CDiagnostics &diagnostics)
{
    if (IsProject(options.Package))
        return BuildProject(options, diagnostics);

    SourcePackage package;
    if (!LoadAndValidate(options.Package, options.Project, &package, diagnostics))
        return;

    // The default output is the package file without its ".json" ending.
    const std::string output = options.Output ? *options.Output : options.Package.substr(0, options.Package.size() - 5);
    const std::string target = AbsolutePath(output);
    const std::vector<std::string> inputs = UniqueAbsolutePaths(package.Inputs);
    std::vector<std::string> written{ output };
    if (options.Depfile)
        written.push_back(*options.Depfile);
    if (!CheckWrittenPaths(written, inputs, diagnostics))
        return;

    // The dependency file is staged first, so that a file it cannot be written to leaves the
    // output as it was.
    std::optional<CStagedFile> depfile;
    if (options.Depfile)
    {
        if (!CheckDependencyPaths(target, inputs, diagnostics))
            return;
        depfile.emplace(PathOf(*options.Depfile));
        if (depfile->Write(DependencyRule(target, inputs)) != FileResult::Success)
        {
            diagnostics.Report(Severity::Error, *options.Depfile, "cannot write the file");
            return;
        }
    }
    if (!WritePackage(package, output, diagnostics))
        return;
    if (depfile && depfile->Commit() != FileResult::Success)
        diagnostics.Report(Severity::Error, *options.Depfile, "cannot write the file");
}

void Validate(const ValidateOptions &options, CDiagnostics &diagnostics)
{
    if (IsProject(options.Package))
        return ValidateProject(options, diagnostics);

    SourcePackage package;
    LoadAndValidate(options.Package, options.Project, &package, diagnostics);
}

} // namespace CpkgTool
