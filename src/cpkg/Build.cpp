//================================================================================================
// Build - the build pipeline's stages, run by the build and validate commands
//================================================================================================

#include "CpkgTool.h"
#include "Source.h"

namespace CpkgTool
{

//------------------------------------------------------------------------------------------------
void Validate(const ValidateOptions &options, CDiagnostics &diagnostics)
{
    SourcePackage package;
    LoadPackageSource(options.Package, &package, diagnostics);
}

} // namespace CpkgTool
