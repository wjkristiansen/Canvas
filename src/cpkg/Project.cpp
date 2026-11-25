//================================================================================================
// Project - project context, and the build and validate commands for a project
//================================================================================================

#include "CpkgTool.h"
#include "DependencyFile.h"
#include "Project.h"
#include "Stages.h"

#ifndef CPKG_VERSION
#error CPKG_VERSION must name the cpkg version
#endif

namespace CpkgTool
{

namespace fs = std::filesystem;
using Canvas::AssetType;

namespace
{
    // A stream buffer that discards what is written to it.
    class CNullBuffer : public std::streambuf
    {
    protected:
        int overflow(int c) override { return c; }
    };

    // Writes bytes over the file at path, a UTF-8 path, unless it already holds them. Returns
    // false when the file could not be written.
    bool SaveUtf8(const std::string &path, std::string_view bytes)
    {
        return SaveFile(PathOf(path), bytes) == FileResult::Success;
    }

    // Copies the file at from over the file at to, UTF-8 paths, unless to already holds the same
    // bytes. Returns false when to could not be written.
    bool CopyFileTo(const std::string &from, const std::string &to)
    {
        CStagedFile file(PathOf(to));
        return file.CopyFrom(PathOf(from)) == FileResult::Success && file.Commit() == FileResult::Success;
    }

    // The file name of a UTF-8 path.
    std::string FileName(const std::string &path)
    {
        return Utf8Of(PathOf(path).filename());
    }

    //--------------------------------------------------------------------------------------------
    // Input records

    // The size and modification time of one input of a package.
    struct InputStamp
    {
        uint64_t Size = 0;
        int64_t Time = 0;       // nanoseconds since the file clock's epoch
        std::string Path;       // absolute, UTF-8, with '/' separators
    };

    bool Stamp(const std::string &path, InputStamp *pStamp)
    {
        pStamp->Path = path;
        return FileStamp(PathOf(path), &pStamp->Size, &pStamp->Time) == FileResult::Success;
    }

    constexpr std::string_view kRecordVersion = "cpkg " CPKG_VERSION;
    constexpr std::string_view kRecordWarning = "warning ";

    // Reads the record at path. Returns false when there is none, or it was written by another
    // version of cpkg or is not a record.
    bool ReadRecord(const std::string &path, std::vector<InputStamp> *pInputs, std::vector<std::string> *pWarnings)
    {
        std::string bytes;
        if (ReadWholeFile(PathOf(path), &bytes) != FileResult::Success)
            return false;
        std::istringstream stream(bytes);
        std::string line;
        if (!std::getline(stream, line) || line != kRecordVersion)
            return false;
        while (std::getline(stream, line))
        {
            if (line.starts_with(kRecordWarning))
            {
                pWarnings->push_back(line.substr(kRecordWarning.size()));
                continue;
            }
            if (!pWarnings->empty())
                return false;
            InputStamp input;
            const char *pEnd = line.data() + line.size();
            auto [pTime, sizeError] = std::from_chars(line.data(), pEnd, input.Size);
            if (sizeError != std::errc() || pTime == pEnd || *pTime != ' ')
                return false;
            auto [pPath, timeError] = std::from_chars(pTime + 1, pEnd, input.Time);
            if (timeError != std::errc() || pPath == pEnd || *pPath != ' ')
                return false;
            input.Path.assign(pPath + 1, pEnd);
            pInputs->push_back(std::move(input));
        }
        return !pInputs->empty();
    }

    // Whether every input has the size and modification time the record gives it.
    bool UpToDate(const std::vector<InputStamp> &inputs)
    {
        for (const InputStamp &input : inputs)
        {
            InputStamp now;
            if (!Stamp(input.Path, &now) || now.Size != input.Size || now.Time != input.Time)
                return false;
        }
        return true;
    }

    // Writes the record of a package built from inputs, absolute UTF-8 paths, to the .cpkg file
    // at package. Returns false when an input or the package cannot be stamped, or the record
    // cannot be written.
    bool WriteRecord(const std::string &path, const std::vector<std::string> &inputs, const std::string &package,
        const std::vector<std::string> &warnings)
    {
        std::string text(kRecordVersion);
        text += '\n';
        auto add = [&](const std::string &input)
        {
            InputStamp stamp;
            if (!Stamp(input, &stamp))
                return false;
            text += std::to_string(stamp.Size) + ' ' + std::to_string(stamp.Time) + ' ' + stamp.Path + '\n';
            return true;
        };
        for (const std::string &input : inputs)
        {
            if (!add(input))
                return false;
        }
        if (!add(package))
            return false;
        for (const std::string &warning : warnings)
            text += std::string(kRecordWarning) + warning + '\n';
        return SaveUtf8(path, text);
    }

    // Reports again the warnings of a package's record, each concerning one of its inputs.
    void ReportRecordedWarnings(const std::vector<InputStamp> &inputs, const std::vector<std::string> &warnings, CDiagnostics &diagnostics)
    {
        for (const std::string &warning : warnings)
        {
            // The longest input path the warning begins with is its file.
            const InputStamp *pFile = nullptr;
            for (const InputStamp &input : inputs)
            {
                if (warning.starts_with(input.Path) && warning.size() > input.Path.size() && warning[input.Path.size()] == ':'
                    && (!pFile || input.Path.size() > pFile->Path.size()))
                {
                    pFile = &input;
                }
            }
            if (pFile)
                diagnostics.ReportLogged(pFile->Path, warning);
        }
    }

    //--------------------------------------------------------------------------------------------
    // The packages of a project

    // Runs Validate on the package, against the project's package at index, and adds the files
    // the project lookup read to the package's inputs. Returns true when it reported no error.
    bool ValidateInProject(const CProject &project, size_t index, SourcePackage &package, CDiagnostics &diagnostics)
    {
        CProjectLookup lookup(project, index, package.PackageName.Text);
        const bool valid = ValidatePackageSource(package, diagnostics, &lookup);
        for (const std::string &input : lookup.GetInputs())
            RecordInput(package, input);
        return valid && diagnostics.GetErrorCount() == 0;
    }

    // Runs the stages before Write on the package at index, validating it against the project.
    // Returns true when they reported no error.
    bool CheckPackage(const CProject &project, size_t index, SourcePackage *pPackage, CDiagnostics &diagnostics)
    {
        const Canvas::ProjectPackage &entry = project.GetManifest().GetPackages()[index];
        if (!LoadPackageSource(entry.FilePath, pPackage, diagnostics) || !ExpandPackageSource(*pPackage, diagnostics))
            return false;
        return ValidateInProject(project, index, *pPackage, diagnostics);
    }

    // What building or validating one package reported, and for a package built or up to date,
    // its inputs, absolute paths.
    struct PackageRun
    {
        std::string Text;
        size_t Errors = 0;
        std::vector<std::string> Inputs;
    };

    // Builds the package at index into the directory output, unless its record says it is up to
    // date.
    PackageRun BuildPackage(const CProject &project, size_t index, const std::string &output, bool werror, bool quiet)
    {
        std::ostringstream text;
        CDiagnostics diagnostics(text, werror, quiet);
        const std::string package = output + "/" + std::string(project.GetManifest().GetBuiltFileName(index));
        const std::string record = package + ".inputs";
        std::error_code error;

        std::vector<InputStamp> recorded;
        std::vector<std::string> warnings;
        if (ReadRecord(record, &recorded, &warnings) && UpToDate(recorded))
        {
            ReportRecordedWarnings(recorded, warnings, diagnostics);
            if (diagnostics.GetErrorCount() != 0)
            {
                Remove(PathOf(record));
                return { text.str(), diagnostics.GetErrorCount() };
            }
            // The record's last entry is the .cpkg file itself.
            std::vector<std::string> inputs;
            for (size_t i = 0; i + 1 < recorded.size(); ++i)
                inputs.push_back(recorded[i].Path);
            return { text.str(), 0, std::move(inputs) };
        }
        warnings.clear();

        diagnostics.LogWarnings(&warnings);
        SourcePackage source;
        const bool built = CheckPackage(project, index, &source, diagnostics) && WritePackage(source, package, diagnostics);
        diagnostics.LogWarnings(nullptr);

        std::vector<std::string> inputs;
        if (built)
            inputs = UniqueAbsolutePaths(source.Inputs);
        if (!built || !WriteRecord(record, inputs, AbsolutePath(package), warnings))
            Remove(PathOf(record));
        return { text.str(), diagnostics.GetErrorCount(), std::move(inputs) };
    }

    // Runs work(i) for each package listed by source, as many at once as jobs allows, and reports
    // each package's diagnostics in manifest order. Returns the runs, in manifest order.
    template <class TWork>
    std::vector<PackageRun> ForEachSource(const CProject &project, unsigned jobs, TWork &&work, CDiagnostics &diagnostics)
    {
        const std::span<const Canvas::ProjectPackage> packages = project.GetManifest().GetPackages();
        std::vector<size_t> sources;
        for (size_t i = 0; i < packages.size(); ++i)
        {
            if (!packages[i].Source.empty())
                sources.push_back(i);
        }

        std::vector<PackageRun> runs(sources.size());
        std::atomic<size_t> next = 0;
        std::atomic<bool> outOfMemory = false;
        auto worker = [&]()
        {
            for (size_t at = next++; at < sources.size(); at = next++)
            {
                try
                {
                    runs[at] = work(sources[at]);
                }
                catch (const std::bad_alloc &)
                {
                    outOfMemory = true;
                }
            }
        };

        const size_t threadCount = std::min<size_t>(std::max(jobs, 1u), sources.size());
        std::vector<std::thread> threads;
        for (size_t i = 1; i < threadCount; ++i)
            threads.emplace_back(worker);
        worker();
        for (std::thread &thread : threads)
            thread.join();
        if (outOfMemory)
            throw std::bad_alloc();

        for (const PackageRun &run : runs)
            diagnostics.Append(run.Text, run.Errors);
        return runs;
    }

    // Reports a prebuilt package that does not open, and a worlds entry that names no world
    // bundle.
    void CheckProjectContent(const CProject &project, CDiagnostics &diagnostics)
    {
        const Canvas::CProjectManifest &manifest = project.GetManifest();
        const std::span<const Canvas::ProjectPackage> packages = manifest.GetPackages();
        for (size_t i = 0; i < packages.size(); ++i)
        {
            if (packages[i].Package.empty())
                continue;
            const ProjectPackageNames &names = project.GetNames(i);
            if (!names.Readable)
                project.Report(diagnostics, Severity::Error, CProject::FilePointer(packages[i], i), OpenFailure(names.OpenResult, packages[i].FilePath));
        }

        const std::span<const Canvas::ProjectWorld> worlds = manifest.GetWorlds();
        for (size_t i = 0; i < worlds.size(); ++i)
        {
            const ProjectPackageNames &names = project.GetNames(worlds[i].Package);
            if (!names.Readable)
                continue;
            const std::string pointer = "/worlds/" + std::to_string(i);
            const auto found = names.Bundles.find(worlds[i].Bundle);
            if (found == names.Bundles.end())
            {
                project.Report(diagnostics, Severity::Error, pointer,
                    "the package " + Quoted(packages[worlds[i].Package].Name) + " has no bundle " + Quoted(worlds[i].Bundle));
            }
            else if (!found->second.World)
            {
                project.Report(diagnostics, Severity::Error, pointer,
                    Quoted(packages[worlds[i].Package].Name + "/" + worlds[i].Bundle) + " is not a world bundle");
            }
        }
    }

    // Reports built file names that are not unique. Sets *pBuiltProject to the built project's text.
    bool CheckBuiltNames(const CProject &project, std::string *pBuiltProject, CDiagnostics &diagnostics)
    {
        std::vector<Canvas::ProjectError> errors;
        const Gem::Result result = project.GetManifest().FormatBuiltProject(pBuiltProject, &errors);
        if (result == Gem::Result::OutOfMemory)
            throw std::bad_alloc();
        for (const Canvas::ProjectError &error : errors)
            diagnostics.Report(Severity::Error, project.GetManifest().GetPath(), error.Line, error.Column, error.Message, error.Pointer);
        return Gem::Succeeded(result);
    }

    // Claims each built file name in a new temporary directory in output, so the file system
    // holding output decides which names are one. Reports each package whose name an earlier one
    // took.
    bool ClaimBuiltNames(const CProject &project, const std::string &output, CDiagnostics &diagnostics)
    {
        const Canvas::CProjectManifest &manifest = project.GetManifest();
        const CScopedDirectory directory(PathOf(output + "/" + FileName(manifest.GetPath())));
        if (!directory.IsCreated())
        {
            diagnostics.Report(Severity::Error, output, "cannot create a file in the directory");
            return false;
        }

        bool unique = true;
        const std::span<const Canvas::ProjectPackage> packages = manifest.GetPackages();
        for (size_t i = 0; i < packages.size(); ++i)
        {
            const std::string_view name = manifest.GetBuiltFileName(i);
            const FileResult made = MakeFile(directory.GetPath() / PathOf(name));
            if (made == FileResult::Success)
                continue;
            unique = false;
            if (made == FileResult::Exists)
            {
                project.Report(diagnostics, Severity::Error, CProject::FilePointer(packages[i], i),
                    "the output directory's file system holds the built file name " + Quoted(name) + " of an earlier package");
            }
            else
                diagnostics.Report(Severity::Error, output, "cannot create a file in the directory");
        }
        return unique;
    }
}

//------------------------------------------------------------------------------------------------
// CProject

bool CProject::Load(const std::string &path, CDiagnostics &diagnostics)
{
    std::vector<Canvas::ProjectError> errors;
    const Gem::Result result = m_Manifest.Load(path.c_str(), &errors);
    switch (result)
    {
    case Gem::Result::Success:
        break;
    case Gem::Result::NotFound:
        diagnostics.Report(Severity::Error, path, "file not found");
        return false;
    case Gem::Result::OutOfMemory:
        throw std::bad_alloc();
    case Gem::Result::CorruptedData:
        for (const Canvas::ProjectError &error : errors)
        {
            if (error.Pointer.empty())
                diagnostics.Report(Severity::Error, path, error.Line, error.Column, error.Message);
            else
                diagnostics.Report(Severity::Error, path, error.Line, error.Column, error.Message, error.Pointer);
        }
        return false;
    default:
        diagnostics.Report(Severity::Error, path, "cannot read the file");
        return false;
    }

    // The manifest's text again, to locate the values the build reports at.
    Canvas::JsonParseError parseError;
    if (ReadWholeFile(PathOf(path), &m_Text) != FileResult::Success || !Canvas::JsonParse(m_Text, &m_Document, &parseError))
    {
        diagnostics.Report(Severity::Error, path, "cannot read the file");
        return false;
    }
    m_Lines = std::make_unique<Canvas::CJsonLineIndex>(m_Text);

    m_Slots.clear();
    for (size_t i = 0; i < m_Manifest.GetPackages().size(); ++i)
        m_Slots.push_back(std::make_unique<Slot>());
    return true;
}

size_t CProject::FindSource(const std::string &path) const
{
    const std::string absolute = AbsolutePath(path);
    const std::span<const Canvas::ProjectPackage> packages = m_Manifest.GetPackages();
    for (size_t i = 0; i < packages.size(); ++i)
    {
        if (packages[i].Source.empty())
            continue;
        if (AbsolutePath(packages[i].FilePath) == absolute || SameFile(PathOf(packages[i].FilePath), PathOf(path)))
            return i;
    }
    return kNoPackage;
}

const ProjectPackageNames &CProject::GetNames(size_t index) const
{
    Slot &slot = *m_Slots[index];
    std::call_once(slot.Once, [&]() { Read(index, &slot.Names); });
    return slot.Names;
}

void CProject::Read(size_t index, ProjectPackageNames *pNames) const
{
    const Canvas::ProjectPackage &entry = m_Manifest.GetPackages()[index];
    if (!entry.Source.empty())
    {
        // Load alone; the package's own build reports what it finds.
        CNullBuffer buffer;
        std::ostream discard(&buffer);
        CDiagnostics diagnostics(discard, false, true);
        SourcePackage source;
        pNames->Readable = LoadPackageSource(entry.FilePath, &source, diagnostics);
        pNames->Inputs = source.Inputs;
        if (pNames->Inputs.empty())
            pNames->Inputs.push_back(entry.FilePath);
        if (!pNames->Readable)
            return;

        for (const SourceBundle &bundle : source.Bundles)
        {
            ProjectPackageNames::Bundle &names = pNames->Bundles[bundle.Name.Text];
            names.World = bundle.World.has_value();
            names.Imports = !bundle.Imports.empty();
            auto add = [&](const auto &assets, AssetType type)
            {
                for (const auto &asset : assets)
                    names.Assets.emplace_back(asset.Name.Text, type);
            };
            add(bundle.Images, AssetType::Image);
            add(bundle.Skyboxes, AssetType::Skybox);
            add(bundle.Materials, AssetType::Material);
            add(bundle.Meshes, AssetType::MeshData);
            add(bundle.Skeletons, AssetType::Skeleton);
            add(bundle.Skins, AssetType::MeshSkin);
            add(bundle.SkeletalAnimations, AssetType::SkeletalAnimation);
        }
        return;
    }

    pNames->Inputs.push_back(entry.FilePath);
    Canvas::CCpkgReader reader;
    pNames->OpenResult = reader.Open(entry.FilePath.c_str());
    if (pNames->OpenResult == Gem::Result::OutOfMemory)
        throw std::bad_alloc();
    pNames->Readable = Gem::Succeeded(pNames->OpenResult);
    if (!pNames->Readable)
        return;
    for (uint32_t bundle = 0; bundle < reader.GetBundleCount(); ++bundle)
    {
        ProjectPackageNames::Bundle &names = pNames->Bundles[std::string(reader.GetBundleName(bundle))];
        for (uint32_t asset = 0; asset < reader.GetAssetCount(bundle); ++asset)
        {
            const AssetType type = reader.GetAssetType(bundle, asset);
            if (type == AssetType::WorldSettings)
                names.World = true;
            const std::string_view name = reader.GetAssetName(bundle, asset);
            if (!name.empty())
                names.Assets.emplace_back(std::string(name), type);
        }
    }
}

void CProject::Report(CDiagnostics &diagnostics, Severity severity, const std::string &pointer, std::string_view message) const
{
    size_t offset = 0;
    try
    {
        offset = m_Document.at(nlohmann::ordered_json::json_pointer(pointer)).start_pos();
    }
    catch (const nlohmann::ordered_json::exception &)
    {
    }
    const Canvas::JsonPosition position = m_Lines->Locate(offset);
    diagnostics.Report(severity, m_Manifest.GetPath(), position.Line, position.Column, message, pointer);
}

std::string CProject::FilePointer(const Canvas::ProjectPackage &package, size_t index)
{
    return "/packages/" + std::to_string(index) + (package.Source.empty() ? "/package" : "/source");
}

//------------------------------------------------------------------------------------------------
// CProjectLookup

CProjectLookup::CProjectLookup(const CProject &project, size_t package, std::string declaredName) :
    m_Project(project),
    m_Package(package),
    m_DeclaredName(std::move(declaredName)),
    m_Inputs{ project.GetManifest().GetPath() },
    m_Read(project.GetManifest().GetPackages().size(), false)
{
}

const ProjectPackageNames *CProjectLookup::Resolve(std::string_view packageName, ProjectTarget *pTarget)
{
    size_t index = 0;
    if (Gem::Failed(m_Project.GetManifest().ResolvePackageName(m_Package, m_DeclaredName, packageName, &index)))
    {
        pTarget->State = ProjectTarget::Kind::NoPackage;
        return nullptr;
    }
    pTarget->Package = m_Project.GetManifest().GetPackages()[index].Name;

    const ProjectPackageNames &names = m_Project.GetNames(index);
    if (!m_Read[index])
    {
        m_Read[index] = true;
        for (const std::string &input : names.Inputs)
        {
            if (std::find(m_Inputs.begin(), m_Inputs.end(), input) == m_Inputs.end())
                m_Inputs.push_back(input);
        }
    }
    if (!names.Readable)
    {
        pTarget->State = ProjectTarget::Kind::Unreadable;
        return nullptr;
    }
    return &names;
}

ProjectTarget CProjectLookup::FindBundle(std::string_view packageName, std::string_view bundle)
{
    ProjectTarget target;
    const ProjectPackageNames *pNames = Resolve(packageName, &target);
    if (!pNames)
        return target;
    const auto found = pNames->Bundles.find(bundle);
    if (found == pNames->Bundles.end())
        target.State = ProjectTarget::Kind::NoBundle;
    else if (found->second.World)
        target.State = ProjectTarget::Kind::WorldBundle;
    return target;
}

ProjectTarget CProjectLookup::FindAsset(std::string_view packageName, std::string_view bundle, std::string_view asset, AssetType type)
{
    ProjectTarget target;
    const ProjectPackageNames *pNames = Resolve(packageName, &target);
    if (!pNames)
        return target;
    const auto found = pNames->Bundles.find(bundle);
    if (found == pNames->Bundles.end())
    {
        target.State = ProjectTarget::Kind::NoBundle;
        return target;
    }

    const auto &assets = found->second.Assets;
    const auto named = [&](const auto &entry) { return entry.first == asset; };
    if (std::any_of(assets.begin(), assets.end(), [&](const auto &entry) { return named(entry) && entry.second == type; }))
        return target;
    const auto other = std::find_if(assets.begin(), assets.end(), named);
    if (other != assets.end())
    {
        target.State = ProjectTarget::Kind::OtherType;
        target.Type = other->second;
    }
    else
        target.State = found->second.Imports ? ProjectTarget::Kind::ByImport : ProjectTarget::Kind::NoAsset;
    return target;
}

//------------------------------------------------------------------------------------------------
// Commands

bool CheckWrittenPaths(const std::vector<std::string> &written, const std::vector<std::string> &inputs, CDiagnostics &diagnostics)
{
    auto same = [](const std::string &a, const std::string &b) { return a == b || SameFile(PathOf(a), PathOf(b)); };
    bool separate = true;
    for (size_t i = 0; i < written.size(); ++i)
    {
        const bool read = std::any_of(inputs.begin(), inputs.end(), [&](const std::string &input) { return same(written[i], input); });
        if (read)
            diagnostics.Report(Severity::Error, written[i], "the build reads this file, and cannot also write it");
        else if (std::any_of(written.begin(), written.begin() + i, [&](const std::string &other) { return same(written[i], other); }))
            diagnostics.Report(Severity::Error, written[i], "the build writes two files to this path");
        else
            continue;
        separate = false;
    }
    return separate;
}

bool LoadAndValidate(const std::string &packageFile, const std::optional<std::string> &projectFile, SourcePackage *pPackage,
    CDiagnostics &diagnostics)
{
    CProject project;
    size_t index = CProject::kNoPackage;
    if (projectFile)
    {
        if (!project.Load(*projectFile, diagnostics))
            return false;
        index = project.FindSource(packageFile);
        if (index == CProject::kNoPackage)
            diagnostics.Report(Severity::Error, packageFile, "the project " + Quoted(DisplayPath(*projectFile)) + " lists no package with this source");
    }

    if (!LoadPackageSource(packageFile, pPackage, diagnostics) || !ExpandPackageSource(*pPackage, diagnostics))
        return false;
    if (index == CProject::kNoPackage)
        return ValidatePackageSource(*pPackage, diagnostics) && diagnostics.GetErrorCount() == 0;
    return ValidateInProject(project, index, *pPackage, diagnostics);
}

void BuildProject(const BuildOptions &options, CDiagnostics &diagnostics)
{
    CProject project;
    if (!project.Load(options.Package, diagnostics))
        return;
    const Canvas::CProjectManifest &manifest = project.GetManifest();
    const std::string output = options.Output ? *options.Output : manifest.GetOutput();
    const std::string builtProject = output + "/" + FileName(manifest.GetPath());
    auto fail = [&]() { Remove(PathOf(builtProject)); };

    std::string builtText;
    if (!CheckBuiltNames(project, &builtText, diagnostics))
        return fail();
    if (MakeDirectories(PathOf(output)) != FileResult::Success)
    {
        diagnostics.Report(Severity::Error, output, "cannot create the directory");
        return fail();
    }
    if (!ClaimBuiltNames(project, output, diagnostics))
        return fail();

    // Packages
    const unsigned jobs = options.Jobs ? *options.Jobs : std::max(std::thread::hardware_concurrency(), 1u);
    const bool werror = diagnostics.IsWerror();
    const bool quiet = diagnostics.IsQuiet();
    const std::vector<PackageRun> runs =
        ForEachSource(project, jobs, [&](size_t index) { return BuildPackage(project, index, output, werror, quiet); }, diagnostics);
    CheckProjectContent(project, diagnostics);
    if (diagnostics.GetErrorCount() != 0)
        return fail();

    // The dependency file names the manifest, each package's inputs, and each prebuilt package.
    const std::span<const Canvas::ProjectPackage> packages = manifest.GetPackages();
    const std::string target = AbsolutePath(builtProject);
    std::vector<std::string> inputs;
    if (options.Depfile)
    {
        std::vector<std::string> read{ manifest.GetPath() };
        for (const PackageRun &run : runs)
            read.insert(read.end(), run.Inputs.begin(), run.Inputs.end());
        for (const Canvas::ProjectPackage &package : packages)
        {
            if (!package.Package.empty())
                read.push_back(package.FilePath);
        }
        inputs = UniqueAbsolutePaths(read);

        // The dependency file is a file other than those the build reads and those it writes in output.
        std::vector<std::string> notWritten = inputs;
        notWritten.push_back(target);
        for (size_t i = 0; i < packages.size(); ++i)
        {
            const std::string built = AbsolutePath(output + "/" + std::string(manifest.GetBuiltFileName(i)));
            notWritten.push_back(built + ".inputs");
            notWritten.push_back(built);
        }
        if (!CheckWrittenPaths({ *options.Depfile }, notWritten, diagnostics) || !CheckDependencyPaths(target, inputs, diagnostics))
            return fail();
    }

    // Built project
    for (size_t i = 0; i < packages.size(); ++i)
    {
        if (packages[i].Package.empty())
            continue;
        const std::string copy = output + "/" + std::string(manifest.GetBuiltFileName(i));
        if (!CopyFileTo(packages[i].FilePath, copy))
            diagnostics.Report(Severity::Error, copy, "cannot write the file");
    }
    if (diagnostics.GetErrorCount() != 0)
        return fail();
    if (!SaveUtf8(builtProject, builtText))
    {
        diagnostics.Report(Severity::Error, builtProject, "cannot write the file");
        return fail();
    }
    if (options.Depfile && SaveFile(PathOf(*options.Depfile), DependencyRule(target, inputs)) != FileResult::Success)
    {
        diagnostics.Report(Severity::Error, *options.Depfile, "cannot write the file");
        fail();
    }
}

void ValidateProject(const ValidateOptions &options, CDiagnostics &diagnostics)
{
    CProject project;
    if (!project.Load(options.Package, diagnostics))
        return;
    std::string builtText;
    CheckBuiltNames(project, &builtText, diagnostics);

    const bool werror = diagnostics.IsWerror();
    const bool quiet = diagnostics.IsQuiet();
    auto validate = [&](size_t index)
    {
        std::ostringstream text;
        CDiagnostics packageDiagnostics(text, werror, quiet);
        SourcePackage source;
        CheckPackage(project, index, &source, packageDiagnostics);
        return PackageRun{ text.str(), packageDiagnostics.GetErrorCount() };
    };
    ForEachSource(project, std::max(std::thread::hardware_concurrency(), 1u), validate, diagnostics);
    CheckProjectContent(project, diagnostics);
}

} // namespace CpkgTool
