//================================================================================================
// DependencyFile - the Makefile-format rule naming a build's output and the files it read
//================================================================================================

#include "DependencyFile.h"

namespace CpkgTool
{

namespace
{
    // What in path a dependency file cannot hold, or null.
    const char *Unwritable(std::string_view path)
    {
        for (size_t i = 0; i < path.size(); ++i)
        {
            switch (path[i])
            {
            case '#':
                return "'#'";
            case '\\':
                return "'\\'";
            case '\n':
            case '\r':
                return "a line break";
            case ':':
                // A drive letter's colon, as in "D:/".
                if (i != 1 || !((path[0] >= 'A' && path[0] <= 'Z') || (path[0] >= 'a' && path[0] <= 'z')))
                    return "':'";
                break;
            }
        }
        return nullptr;
    }

    // path as a dependency file writes it.
    std::string Escaped(std::string_view path)
    {
        std::string text;
        for (const char c : path)
        {
            if (c == ' ')
                text += "\\ ";
            else if (c == '$')
                text += "$$";
            else
                text += c;
        }
        return text;
    }
}

//------------------------------------------------------------------------------------------------
std::vector<std::string> UniqueAbsolutePaths(const std::vector<std::string> &paths)
{
    std::vector<std::string> unique;
    for (const std::string &path : paths)
    {
        std::string absolute = AbsolutePath(path);
        if (std::find(unique.begin(), unique.end(), absolute) == unique.end())
            unique.push_back(std::move(absolute));
    }
    return unique;
}

bool CheckDependencyPaths(const std::string &target, const std::vector<std::string> &inputs, CDiagnostics &diagnostics)
{
    bool writable = true;
    auto check = [&](const std::string &path)
    {
        if (const char *pWhat = Unwritable(path))
        {
            diagnostics.Report(Severity::Error, path, std::string("a dependency file cannot name a path that holds ") + pWhat);
            writable = false;
        }
    };
    check(target);
    for (const std::string &input : inputs)
        check(input);
    return writable;
}

std::string DependencyRule(const std::string &target, const std::vector<std::string> &inputs)
{
    std::string text = Escaped(target) + ":";
    for (const std::string &input : inputs)
        text += " \\\n  " + Escaped(input);
    text += '\n';
    return text;
}

} // namespace CpkgTool
