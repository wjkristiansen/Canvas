//================================================================================================
// Cli - parses cpkg's command line and runs its command
//================================================================================================

#include "CpkgTool.h"

#include "InCommand.h"

#include <new>
#include <string_view>

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

        InCommand::CommandDecl &validate = root.AddSubCommand("validate");
        validate.SetDescription("Reports the diagnostics a build would report, without writing anything");
        validate.AddOption(InCommand::OptionType::Parameter, "package.cpkg.json")
            .SetDescription("The package file");

        InCommand::CommandDecl &dump = root.AddSubCommand("dump");
        dump.SetDescription("Prints the contents of a .cpkg file");
        dump.AddOption(InCommand::OptionType::Parameter, "package.cpkg")
            .SetDescription("The package file");
        dump.AddOption(InCommand::OptionType::Variable, "bundle")
            .SetDescription("Lists only the bundle of this name");
        dump.AddOption(InCommand::OptionType::Switch, "payloads")
            .SetDescription("Adds each payload's fields");

        size_t blockCount = 0;
        try
        {
            blockCount = parser.ParseArgs(argc, const_cast<const char **>(argv));
        }
        catch (const InCommand::SyntaxException &e)
        {
            return UsageError(err, e.GetMessage());
        }

        if (parser.WasAutoHelpRequested())
            return kExitSuccess;
        if (parser.IsGlobalOptionSet("version"))
        {
            out << "cpkg " CPKG_VERSION "\n";
            return kExitSuccess;
        }
        if (blockCount < 2)
            return UsageError(err, "missing command");

        CDiagnostics diagnostics(err, parser.IsGlobalOptionSet("werror"), parser.IsGlobalOptionSet("quiet"));
        const InCommand::CommandBlock &block = parser.GetCommandBlock(blockCount - 1);
        const std::string &command = block.GetDecl().GetName();

        if (command == "validate")
        {
            if (!block.IsOptionSet("package.cpkg.json"))
                return UsageError(err, "validate: missing argument <package.cpkg.json>");

            ValidateOptions options;
            options.Package = block.GetOptionValue("package.cpkg.json");
            if (!HasExtension(options.Package, ".cpkg.json"))
                return UsageError(err, "validate: expected a .cpkg.json file: " + options.Package);
            Validate(options, diagnostics);
        }
        else if (command == "dump")
        {
            if (!block.IsOptionSet("package.cpkg"))
                return UsageError(err, "dump: missing argument <package.cpkg>");

            DumpOptions options;
            options.Package = block.GetOptionValue("package.cpkg");
            if (!HasExtension(options.Package, ".cpkg"))
                return UsageError(err, "dump: expected a .cpkg file: " + options.Package);
            if (block.IsOptionSet("bundle"))
                options.Bundle = block.GetOptionValue("bundle");
            options.Payloads = block.IsOptionSet("payloads");
            Dump(options, out, diagnostics);
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
