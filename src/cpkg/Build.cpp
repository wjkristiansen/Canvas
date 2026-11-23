//================================================================================================
// Build - the build pipeline's stages, run by the build and validate commands
//================================================================================================

#include "CpkgTool.h"
#include "Stages.h"

namespace CpkgTool
{

namespace
{
    // Runs the stages before Write. Returns true when they reported no error.
    bool LoadAndValidate(const std::string &packageFile, SourcePackage *pPackage, CDiagnostics &diagnostics)
    {
        if (!LoadPackageSource(packageFile, pPackage, diagnostics) || !TranslatePackageSource(*pPackage, diagnostics))
            return false;
        return ValidatePackageSource(*pPackage, diagnostics) && diagnostics.GetErrorCount() == 0;
    }
}

//------------------------------------------------------------------------------------------------
void Build(const BuildOptions &options, CDiagnostics &diagnostics)
{
    SourcePackage package;
    if (!LoadAndValidate(options.Package, &package, diagnostics))
        return;

    // The default output is the package file without its ".json" ending.
    const std::string output = options.Output ? *options.Output : options.Package.substr(0, options.Package.size() - 5);
    WritePackage(package, output, diagnostics);
}

void Validate(const ValidateOptions &options, CDiagnostics &diagnostics)
{
    SourcePackage package;
    LoadAndValidate(options.Package, &package, diagnostics);
}

} // namespace CpkgTool
