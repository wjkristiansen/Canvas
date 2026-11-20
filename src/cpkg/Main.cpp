//================================================================================================
// cpkg - builds Canvas package sources and projects; see src/cpkg/README.md
//================================================================================================

#include "CpkgTool.h"

#include <iostream>

#ifdef _WIN32
#include <windows.h>
#endif

int main(int argc, char *argv[])
{
#ifdef _WIN32
    // The process runs with the UTF-8 code page, so argv is UTF-8. A console shows output in its
    // own code page, so it is set to UTF-8 while cpkg writes, and restored for the next program.
    const UINT consoleCodePage = GetConsoleOutputCP();
    if (consoleCodePage != 0)
        SetConsoleOutputCP(CP_UTF8);
#endif

    const int status = CpkgTool::Run(argc, argv, std::cout, std::cerr);
    std::cout.flush();
    std::cerr.flush();

#ifdef _WIN32
    if (consoleCodePage != 0)
        SetConsoleOutputCP(consoleCodePage);
#endif
    return status;
}
