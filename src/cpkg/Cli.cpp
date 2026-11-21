//================================================================================================
// Cli - parses cpkg's command line and runs its command
//================================================================================================

#include "CpkgTool.h"

#include "InCommand.h"

#include <new>
#include <string>
#include <string_view>
#include <vector>

#ifndef CPKG_VERSION
#error CPKG_VERSION must name the cpkg version
#endif

namespace CpkgTool
{

namespace
{
    int UsageError(std::ostream &err, std::string_view message)
    {
        err << "cpkg: error: " << message << "\nRun 'cpkg --help' for usage.\n";
        return kExitUsage;
    }

    // The message for a command line InCommand rejects, in cpkg's form: lowercase, after the
    // command it concerns, such as "build: missing argument <package.cpkg.json>".
    std::string SyntaxMessage(const InCommand::SyntaxException &e)
    {
        std::string message = e.GetMessage();
        if (!message.empty() && message[0] >= 'A' && message[0] <= 'Z')
            message[0] = char(message[0] - 'A' + 'a');
        const std::vector<std::string> &path = e.GetCommandPath();
        return path.size() > 1 ? path.back() + ": " + message : message;
    }

    bool HasExtension(std::string_view path, std::string_view extension)
    {
        return path.size() > extension.size() && path.ends_with(extension);
    }
}

//------------------------------------------------------------------------------------------------
int Run(int argc, const char *const argv[], std::ostream &out, std::ostream &err)
{
    try
    {
        InCommand::CommandParser parser("cpkg");
        InCommand::CommandDecl &root = parser.GetAppCommandDecl();
        root.SetDescription("Builds Canvas package sources and projects, and inspects packages.");

        parser.EnableAutoHelp("help", 'h', out);
        parser.SetAutoHelpDescription("Prints help for the command and exits");
        parser.AddGlobalOption(InCommand::OptionType::Switch, "werror")
            .SetDescription("Treats warnings as errors");
        parser.AddGlobalOption(InCommand::OptionType::Switch, "quiet", 'q')
            .SetDescription("Reports errors only");
        parser.AddGlobalOption(InCommand::OptionType::Switch, "version")
            .SetDescription("Prints the version of cpkg");

        InCommand::CommandDecl &build = root.AddSubCommand("build");
        build.SetDescription("Builds a package source into a .cpkg file");
        build.AddOption(InCommand::OptionType::Parameter, "package.cpkg.json")
            .SetRequired()
            .SetDescription("The package file");
        build.AddOption(InCommand::OptionType::Variable, "output", 'o')
            .SetDescription("The .cpkg file to write; by default the package file's name without .json, beside it");

        InCommand::CommandDecl &validate = root.AddSubCommand("validate");
        validate.SetDescription("Reports the diagnostics a build would report, without writing anything");
        validate.AddOption(InCommand::OptionType::Parameter, "package.cpkg.json")
            .SetRequired()
            .SetDescription("The package file");

        InCommand::CommandDecl &dump = root.AddSubCommand("dump");
        dump.SetDescription("Prints the contents of a .cpkg file");
        dump.AddOption(InCommand::OptionType::Parameter, "package.cpkg")
            .SetRequired()
            .SetDescription("The package file");
        dump.AddOption(InCommand::OptionType::Variable, "bundle")
            .SetDescription("Lists only the bundle of this name");
        dump.AddOption(InCommand::OptionType::Switch, "payloads")
            .SetDescription("Adds each payload's fields");

        InCommand::CommandDecl &unpack = root.AddSubCommand("unpack");
        unpack.SetDescription("Writes a package source that builds a .cpkg file");
        unpack.AddOption(InCommand::OptionType::Parameter, "package.cpkg")
            .SetRequired()
            .SetDescription("The package file");
        unpack.AddOption(InCommand::OptionType::Variable, "output", 'o')
            .SetDescription("The package file to write; by default the .cpkg file's name with .json added, beside it");
        unpack.AddOption(InCommand::OptionType::Switch, "force")
            .SetDescription("Replaces files that exist");

        InCommand::CommandDecl &format = root.AddSubCommand("format");
        format.SetDescription("Rewrites a package source in the standard form");
        format.AddOption(InCommand::OptionType::Parameter, "package.cpkg.json")
            .SetRequired()
            .SetDescription("The package file");
        format.AddOption(InCommand::OptionType::Switch, "check")
            .SetDescription("Saves nothing, and reports each file that is not in the standard form as an error");

        size_t blockCount = 0;
        try
        {
            blockCount = parser.ParseArgs(argc, argv);
        }
        catch (const InCommand::SyntaxException &e)
        {
            return UsageError(err, SyntaxMessage(e));
        }

        if (parser.WasAutoHelpRequested())
            return kExitSuccess;
        if (parser.IsGlobalOptionSet("version"))
        {
            out << "cpkg " CPKG_VERSION "\n";
            return kExitSuccess;
        }
        // Not InCommand's SetSubCommandRequired: --version alone is a complete command line.
        if (blockCount < 2)
            return UsageError(err, "missing command");

        CDiagnostics diagnostics(err, parser.IsGlobalOptionSet("werror"), parser.IsGlobalOptionSet("quiet"));
        const InCommand::CommandBlock &block = parser.GetCommandBlock(blockCount - 1);
        const std::string &command = block.GetDecl().GetName();

        if (command == "build")
        {
            BuildOptions options;
            options.Package = block.GetOptionValue("package.cpkg.json");
            if (!HasExtension(options.Package, ".cpkg.json"))
                return UsageError(err, "build: expected a .cpkg.json file: " + options.Package);
            if (block.IsOptionSet("output"))
                options.Output = block.GetOptionValue("output");
            Build(options, diagnostics);
        }
        else if (command == "validate")
        {
            ValidateOptions options;
            options.Package = block.GetOptionValue("package.cpkg.json");
            if (!HasExtension(options.Package, ".cpkg.json"))
                return UsageError(err, "validate: expected a .cpkg.json file: " + options.Package);
            Validate(options, diagnostics);
        }
        else if (command == "dump")
        {
            DumpOptions options;
            options.Package = block.GetOptionValue("package.cpkg");
            if (!HasExtension(options.Package, ".cpkg"))
                return UsageError(err, "dump: expected a .cpkg file: " + options.Package);
            if (block.IsOptionSet("bundle"))
                options.Bundle = block.GetOptionValue("bundle");
            options.Payloads = block.IsOptionSet("payloads");
            Dump(options, out, diagnostics);
        }
        else if (command == "unpack")
        {
            UnpackOptions options;
            options.Package = block.GetOptionValue("package.cpkg");
            if (!HasExtension(options.Package, ".cpkg"))
                return UsageError(err, "unpack: expected a .cpkg file: " + options.Package);
            if (block.IsOptionSet("output"))
            {
                options.Output = block.GetOptionValue("output");
                if (!HasExtension(*options.Output, ".cpkg.json"))
                    return UsageError(err, "unpack: expected a .cpkg.json output file: " + *options.Output);
            }
            options.Force = block.IsOptionSet("force");
            Unpack(options, diagnostics);
        }
        else if (command == "format")
        {
            FormatOptions options;
            options.Package = block.GetOptionValue("package.cpkg.json");
            if (!HasExtension(options.Package, ".cpkg.json"))
                return UsageError(err, "format: expected a .cpkg.json file: " + options.Package);
            options.Check = block.IsOptionSet("check");
            Format(options, diagnostics);
        }

        return diagnostics.GetErrorCount() == 0 ? kExitSuccess : kExitError;
    }
    catch (const std::bad_alloc &)
    {
        err << "cpkg: error: out of memory\n";
        return kExitError;
    }
    catch (const InCommand::ApiException &e)
    {
        err << "cpkg: error: internal error: " << e.GetMessage() << '\n';
        return kExitError;
    }
}

} // namespace CpkgTool
