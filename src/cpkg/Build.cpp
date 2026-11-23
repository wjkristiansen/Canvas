//================================================================================================
// Build - the build pipeline's stages, run by the build and validate commands
//================================================================================================

#include "CpkgTool.h"
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
    WritePackage(package, output, diagnostics);
}

void Validate(const ValidateOptions &options, CDiagnostics &diagnostics)
{
    if (IsProject(options.Package))
        return ValidateProject(options, diagnostics);

    SourcePackage package;
    LoadAndValidate(options.Package, options.Project, &package, diagnostics);
}

} // namespace CpkgTool
