//================================================================================================
// ProjectManifest - CProjectManifest
//================================================================================================

#include "CanvasProject.h"
#include "JsonText.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <new>
#include <unordered_set>

namespace Canvas
{

namespace
{

using Json = nlohmann::ordered_json;

bool IsName(std::string_view name)
{
    return !name.empty() && CpkgIsValidName(name);
}

bool EndsWith(std::string_view text, std::string_view suffix)
{
    return text.size() >= suffix.size() && text.substr(text.size() - suffix.size()) == suffix;
}

// The part of a '/'-separated path after its last '/'.
std::string_view FileName(std::string_view path)
{
    const size_t slash = path.rfind('/');
    return slash == std::string_view::npos ? path : path.substr(slash + 1);
}

std::filesystem::path PathFromUtf8(std::string_view utf8)
{
    return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t *>(utf8.data()), utf8.size()));
}

std::string Utf8FromPath(const std::filesystem::path &path)
{
    const std::u8string text = path.generic_u8string();
    return std::string(reinterpret_cast<const char *>(text.data()), text.size());
}

// Resolves a manifest file path against the directory of the manifest's own path.
std::string ResolvePath(const std::filesystem::path &manifestDirectory, std::string_view path)
{
    const std::filesystem::path filePath = PathFromUtf8(path);
    if (filePath.is_absolute())
        return Utf8FromPath(filePath.lexically_normal());
    return Utf8FromPath((manifestDirectory / filePath).lexically_normal());
}

std::string Quoted(std::string_view text)
{
    return Json(std::string(text)).dump();
}

// Checks one manifest document, collecting errors and the values it reads.
class CManifestReader
{
public:
    CManifestReader(std::string_view text, std::vector<ProjectError> *pErrors) :
        m_Text(text),
        m_Lines(text),
        m_pErrors(pErrors)
    {
    }

    struct Result
    {
        std::string Name;
        std::string Output = "build";
        std::vector<ProjectPackage> Packages;
        std::vector<std::string> FilePointers;
        std::vector<size_t> FileOffsets;
        std::vector<ProjectWorld> Worlds;
        size_t DefaultWorld = kNoWorld;
    };

    void Read(const Json &document, Result *pResult)
    {
        if (!document.is_object())
        {
            ErrorAt(document, "", "expected an object");
            return;
        }

        JsonForEachUnknownKey(document,
            { "$schema", "formatVersion", "name", "output", "packages", "worlds", "defaultWorld" },
            [&](const std::string &key, const Json &value) { UnknownKey(key, value, ""); });

        RequireKeys(document, "", { "formatVersion", "name", "packages" });

        if (const Json *pValue = Member(document, "$schema"))
            ExpectString(*pValue, "/$schema");

        if (const Json *pValue = Member(document, "formatVersion"))
        {
            if (!pValue->is_number() || pValue->get<double>() != 1.0)
                ErrorAt(*pValue, "/formatVersion", "expected format version 1");
        }

        if (const Json *pValue = Member(document, "name"))
        {
            if (ExpectString(*pValue, "/name"))
            {
                pResult->Name = pValue->get<std::string>();
                if (pResult->Name.empty())
                    ErrorAt(*pValue, "/name", "expected a non-empty name");
            }
        }

        if (const Json *pValue = Member(document, "output"))
        {
            if (ExpectFile(*pValue, "/output", ""))
                pResult->Output = pValue->get<std::string>();
        }

        if (const Json *pValue = Member(document, "packages"))
            ReadPackages(*pValue, pResult);

        if (const Json *pValue = Member(document, "worlds"))
            ReadWorlds(*pValue, pResult);

        if (const Json *pValue = Member(document, "defaultWorld"))
        {
            if (ExpectWorldPath(*pValue, "/defaultWorld"))
            {
                const std::string &world = pValue->get_ref<const std::string &>();
                const Json *pWorlds = Member(document, "worlds");
                bool listed = false;
                if (pWorlds && pWorlds->is_array())
                {
                    for (const Json &entry : *pWorlds)
                        listed = listed || (entry.is_string() && entry.get_ref<const std::string &>() == world);
                }
                if (!listed)
                    ErrorAt(*pValue, "/defaultWorld", "default world " + Quoted(world) + " is not one of the project's worlds");
            }
        }
    }

    bool HasErrors() const { return m_ErrorCount > 0; }

    void ErrorAtOffset(size_t offset, std::string pointer, std::string message)
    {
        ++m_ErrorCount;
        if (!m_pErrors)
            return;
        const JsonPosition position = m_Lines.Locate(offset);
        ProjectError &error = m_pErrors->emplace_back();
        error.Message = std::move(message);
        error.Pointer = std::move(pointer);
        error.Line = position.Line;
        error.Column = position.Column;
    }

private:
    static const Json *Member(const Json &object, const char *key)
    {
        const auto it = object.find(key);
        return it == object.end() ? nullptr : &*it;
    }

    void ErrorAt(const Json &value, std::string pointer, std::string message)
    {
        ErrorAtOffset(value.start_pos(), std::move(pointer), std::move(message));
    }

    void UnknownKey(const std::string &key, const Json &value, const std::string &objectPointer)
    {
        std::string pointer = objectPointer;
        JsonPointerAppend(&pointer, key);
        ErrorAtOffset(JsonKeyOffset(m_Text, value.start_pos()), std::move(pointer), "unknown key " + Quoted(key));
    }

    void RequireKeys(const Json &object, const std::string &pointer, std::initializer_list<const char *> keys)
    {
        for (const char *key : keys)
        {
            if (!object.contains(key))
                ErrorAt(object, pointer, "missing required key " + Quoted(key));
        }
    }

    bool ExpectString(const Json &value, const std::string &pointer)
    {
        if (value.is_string())
            return true;
        ErrorAt(value, pointer, "expected a string");
        return false;
    }

    bool ExpectName(const Json &value, const std::string &pointer)
    {
        if (!ExpectString(value, pointer))
            return false;
        if (IsName(value.get_ref<const std::string &>()))
            return true;
        ErrorAt(value, pointer, "expected a package name: non-empty, with no '/' or NUL");
        return false;
    }

    // A file path: non-empty, '/' separators, and ending in suffix.
    bool ExpectFile(const Json &value, const std::string &pointer, std::string_view suffix)
    {
        if (!ExpectString(value, pointer))
            return false;
        const std::string &path = value.get_ref<const std::string &>();
        if (path.empty())
        {
            ErrorAt(value, pointer, "expected a non-empty file path");
            return false;
        }
        if (path.find('\\') != std::string::npos)
        {
            ErrorAt(value, pointer, "expected '/' as the path separator");
            return false;
        }
        if (!EndsWith(path, suffix))
        {
            ErrorAt(value, pointer, "expected a file name ending in " + Quoted(suffix));
            return false;
        }
        return true;
    }

    // A world path: "package/bundle", both parts names.
    bool ExpectWorldPath(const Json &value, const std::string &pointer)
    {
        if (!ExpectString(value, pointer))
            return false;
        const std::string_view path = value.get_ref<const std::string &>();
        const size_t slash = path.find('/');
        if (slash != std::string_view::npos && IsName(path.substr(0, slash)) && IsName(path.substr(slash + 1)))
            return true;
        ErrorAt(value, pointer, "expected a world written package/bundle");
        return false;
    }

    void ReadPackages(const Json &packages, Result *pResult)
    {
        if (!packages.is_array())
        {
            ErrorAt(packages, "/packages", "expected an array");
            return;
        }

        std::unordered_set<std::string_view> names;
        for (size_t i = 0; i < packages.size(); ++i)
        {
            const Json &entry = packages[i];
            std::string pointer = "/packages";
            JsonPointerAppend(&pointer, i);
            if (!entry.is_object())
            {
                ErrorAt(entry, pointer, "expected an object");
                continue;
            }

            JsonForEachUnknownKey(entry, { "name", "source", "package", "references" },
                [&](const std::string &key, const Json &value) { UnknownKey(key, value, pointer); });
            RequireKeys(entry, pointer, { "name" });

            ProjectPackage package;
            if (const Json *pName = Member(entry, "name"))
            {
                if (ExpectName(*pName, pointer + "/name"))
                {
                    package.Name = pName->get<std::string>();
                    if (!names.insert(pName->get_ref<const std::string &>()).second)
                        ErrorAt(*pName, pointer + "/name", "project name " + Quoted(package.Name) + " is already used");
                }
            }

            const Json *pSource = Member(entry, "source");
            const Json *pPackage = Member(entry, "package");
            if (pSource && pPackage)
            {
                ErrorAt(*pPackage, pointer + "/package", "a package entry holds one of \"source\" and \"package\"");
            }
            else if (!pSource && !pPackage)
            {
                ErrorAt(entry, pointer, "missing required key \"source\" or \"package\"");
            }
            else if (pSource)
            {
                if (ExpectFile(*pSource, pointer + "/source", ".cpkg.json"))
                    package.Source = pSource->get<std::string>();
            }
            else if (ExpectFile(*pPackage, pointer + "/package", ".cpkg"))
            {
                package.Package = pPackage->get<std::string>();
            }

            if (const Json *pReferences = Member(entry, "references"))
                ReadReferences(*pReferences, pointer + "/references", &package);

            const Json *pFile = pSource ? pSource : pPackage;
            pResult->FilePointers.push_back(pointer + (pSource ? "/source" : "/package"));
            pResult->FileOffsets.push_back(pFile ? pFile->start_pos() : entry.start_pos());
            pResult->Packages.push_back(std::move(package));
        }

        // Each references value names a package of the project.
        for (size_t i = 0; i < packages.size(); ++i)
        {
            const Json &entry = packages[i];
            const Json *pReferences = entry.is_object() ? Member(entry, "references") : nullptr;
            if (!pReferences || !pReferences->is_object())
                continue;
            for (const auto &[key, value] : pReferences->items())
            {
                if (!value.is_string() || !IsName(value.get_ref<const std::string &>()))
                    continue;
                if (!names.contains(value.get_ref<const std::string &>()))
                {
                    std::string pointer = "/packages";
                    JsonPointerAppend(&pointer, i);
                    pointer += "/references";
                    JsonPointerAppend(&pointer, key);
                    ErrorAt(value, pointer, "no package of the project has the project name " + Quoted(value.get_ref<const std::string &>()));
                }
            }
        }
    }

    void ReadReferences(const Json &references, const std::string &pointer, ProjectPackage *pPackage)
    {
        if (!references.is_object())
        {
            ErrorAt(references, pointer, "expected an object");
            return;
        }
        for (const auto &[key, value] : references.items())
        {
            std::string memberPointer = pointer;
            JsonPointerAppend(&memberPointer, key);
            bool valid = true;
            if (!IsName(key))
            {
                ErrorAtOffset(JsonKeyOffset(m_Text, value.start_pos()), memberPointer, "expected a package name: non-empty, with no '/' or NUL");
                valid = false;
            }
            valid = ExpectName(value, memberPointer) && valid;
            if (valid)
                pPackage->References.push_back({ key, value.get<std::string>() });
        }
    }

    void ReadWorlds(const Json &worlds, Result *pResult)
    {
        if (!worlds.is_array())
        {
            ErrorAt(worlds, "/worlds", "expected an array");
            return;
        }

        std::unordered_set<std::string_view> seen;
        for (size_t i = 0; i < worlds.size(); ++i)
        {
            const Json &entry = worlds[i];
            std::string pointer = "/worlds";
            JsonPointerAppend(&pointer, i);
            if (!ExpectWorldPath(entry, pointer))
                continue;

            const std::string_view path = entry.get_ref<const std::string &>();
            if (!seen.insert(path).second)
            {
                ErrorAt(entry, pointer, "world " + Quoted(path) + " is already listed");
                continue;
            }

            const size_t slash = path.find('/');
            const std::string_view packageName = path.substr(0, slash);
            size_t package = SIZE_MAX;
            for (size_t p = 0; p < pResult->Packages.size(); ++p)
            {
                if (pResult->Packages[p].Name == packageName)
                {
                    package = p;
                    break;
                }
            }
            if (package == SIZE_MAX)
            {
                ErrorAt(entry, pointer, "no package of the project has the project name " + Quoted(packageName));
                continue;
            }
            pResult->Worlds.push_back({ package, std::string(path.substr(slash + 1)) });
        }
    }

    std::string_view m_Text;
    CJsonLineIndex m_Lines;
    std::vector<ProjectError> *m_pErrors;
    size_t m_ErrorCount = 0;
};

} // namespace

void CProjectManifest::Clear()
{
    m_Loaded = false;
    m_Path.clear();
    m_Name.clear();
    m_Output.clear();
    m_Packages.clear();
    m_Worlds.clear();
    m_DefaultWorld = kNoWorld;
    m_PackageIndex.clear();
    m_Text.clear();
    m_FileLocations.clear();
}

Gem::Result CProjectManifest::Load(const char *path, std::vector<ProjectError> *pErrors)
{
    Clear();
    if (!path)
        return Gem::Result::InvalidArg;

    try
    {
        const std::filesystem::path filePath = PathFromUtf8(path);
        std::error_code error;
        const std::filesystem::file_status status = std::filesystem::status(filePath, error);
        if (status.type() == std::filesystem::file_type::not_found)
            return Gem::Result::NotFound;
        if (error)
            return Gem::Result::IoError;
        if (status.type() != std::filesystem::file_type::regular)
            return Gem::Result::NotFound;

        std::ifstream file(filePath, std::ios::binary);
        if (!file)
            return Gem::Result::IoError;
        std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        if (file.bad())
            return Gem::Result::IoError;

        return Parse(text, path, pErrors);
    }
    catch (const std::bad_alloc &)
    {
        return Gem::Result::OutOfMemory;
    }
    catch (const std::exception &)
    {
        return Gem::Result::IoError;
    }
}

Gem::Result CProjectManifest::Parse(std::string_view text, const char *path, std::vector<ProjectError> *pErrors)
{
    Clear();
    if (!path)
        return Gem::Result::InvalidArg;

    try
    {
        std::vector<ProjectError> errors;
        std::vector<ProjectError> *pCollect = pErrors ? &errors : nullptr;

        Json document;
        JsonParseError parseError;
        if (!JsonParse(text, &document, &parseError))
        {
            if (pErrors)
            {
                const JsonPosition position = CJsonLineIndex(text).Locate(parseError.Offset);
                ProjectError &error = pErrors->emplace_back();
                error.Message = std::move(parseError.Message);
                error.Line = position.Line;
                error.Column = position.Column;
            }
            return Gem::Result::CorruptedData;
        }

        CManifestReader reader(text, pCollect);
        CManifestReader::Result result;
        reader.Read(document, &result);
        if (reader.HasErrors())
        {
            // The reader checks members in a fixed order; report errors in document order.
            std::stable_sort(errors.begin(), errors.end(), [](const ProjectError &a, const ProjectError &b)
            {
                return a.Line != b.Line ? a.Line < b.Line : a.Column < b.Column;
            });
            if (pErrors)
                pErrors->insert(pErrors->end(), std::make_move_iterator(errors.begin()), std::make_move_iterator(errors.end()));
            return Gem::Result::CorruptedData;
        }

        // Resolve file paths against the manifest's directory.
        std::string manifestPath(path);
        std::replace(manifestPath.begin(), manifestPath.end(), '\\', '/');
        const std::filesystem::path manifestDirectory = PathFromUtf8(manifestPath).parent_path();
        manifestPath = path;
        for (ProjectPackage &package : result.Packages)
            package.FilePath = ResolvePath(manifestDirectory, package.Source.empty() ? package.Package : package.Source);

        CJsonLineIndex lines(text);
        std::vector<Location> fileLocations;
        fileLocations.reserve(result.Packages.size());
        for (size_t i = 0; i < result.Packages.size(); ++i)
        {
            const JsonPosition position = lines.Locate(result.FileOffsets[i]);
            fileLocations.push_back({ std::move(result.FilePointers[i]), position.Line, position.Column });
        }

        std::unordered_map<std::string, size_t, CpkgDetail::NameHash, std::equal_to<>> packageIndex;
        for (size_t i = 0; i < result.Packages.size(); ++i)
            packageIndex.emplace(result.Packages[i].Name, i);

        // Every allocation has succeeded; commit.
        m_Path = std::move(manifestPath);
        m_Name = std::move(result.Name);
        m_Output = ResolvePath(manifestDirectory, result.Output);
        m_Packages = std::move(result.Packages);
        m_Worlds = std::move(result.Worlds);
        m_PackageIndex = std::move(packageIndex);
        m_Text = std::string(text);
        m_FileLocations = std::move(fileLocations);
        if (const auto it = document.find("defaultWorld"); it != document.end())
        {
            const std::string &world = it->get_ref<const std::string &>();
            for (size_t i = 0; i < m_Worlds.size(); ++i)
            {
                const ProjectWorld &entry = m_Worlds[i];
                if (m_Packages[entry.Package].Name + "/" + entry.Bundle == world)
                {
                    m_DefaultWorld = i;
                    break;
                }
            }
        }
        m_Loaded = true;
        return Gem::Result::Success;
    }
    catch (const std::bad_alloc &)
    {
        Clear();
        return Gem::Result::OutOfMemory;
    }
    catch (const std::exception &)
    {
        Clear();
        return Gem::Result::Fail;
    }
}

Gem::Result CProjectManifest::FindPackage(std::string_view name, size_t *pIndex) const
{
    const auto it = m_PackageIndex.find(name);
    if (it == m_PackageIndex.end())
        return Gem::Result::NotFound;
    if (pIndex)
        *pIndex = it->second;
    return Gem::Result::Success;
}

Gem::Result CProjectManifest::ResolvePackageName(size_t package, std::string_view declaredName, std::string_view name, size_t *pIndex) const
{
    if (package >= m_Packages.size())
        return Gem::Result::InvalidArg;

    // 1. The package's own declared name.
    if (name == declaredName)
    {
        if (pIndex)
            *pIndex = package;
        return Gem::Result::Success;
    }

    // 2. A key of the package's references.
    for (const ProjectReference &reference : m_Packages[package].References)
    {
        if (reference.Name == name)
            return FindPackage(reference.ProjectName, pIndex);
    }

    // 3. Any other name is a project name.
    return FindPackage(name, pIndex);
}

std::string_view CProjectManifest::GetBuiltFileName(size_t package) const
{
    if (package >= m_Packages.size())
        return {};
    const ProjectPackage &entry = m_Packages[package];
    if (entry.Source.empty())
        return FileName(entry.Package);
    const std::string_view name = FileName(entry.Source);
    return name.substr(0, name.size() - std::string_view(".json").size());
}

Gem::Result CProjectManifest::FormatBuiltProject(std::string *pText, std::vector<ProjectError> *pErrors) const
{
    if (!m_Loaded)
        return Gem::Result::NotOpen;
    if (!pText)
        return Gem::Result::InvalidArg;

    try
    {
        // Built file names are compared without regard to ASCII case.
        auto lower = [](std::string_view name)
        {
            std::string result(name);
            for (char &c : result)
            {
                if (c >= 'A' && c <= 'Z')
                    c = char(c - 'A' + 'a');
            }
            return result;
        };

        std::vector<ProjectError> errors;
        std::unordered_map<std::string, size_t> builtNames;
        for (size_t i = 0; i < m_Packages.size(); ++i)
        {
            const auto [it, inserted] = builtNames.emplace(lower(GetBuiltFileName(i)), i);
            if (!inserted)
            {
                const Location &location = m_FileLocations[i];
                ProjectError &error = errors.emplace_back();
                error.Message = "built file name " + Quoted(GetBuiltFileName(i)) + " is already the built file name of package "
                    + Quoted(m_Packages[it->second].Name);
                error.Pointer = location.Pointer;
                error.Line = location.Line;
                error.Column = location.Column;
            }
        }
        if (!errors.empty())
        {
            if (pErrors)
                pErrors->insert(pErrors->end(), std::make_move_iterator(errors.begin()), std::make_move_iterator(errors.end()));
            return Gem::Result::CorruptedData;
        }

        // The authored content without output, each entry listed by its built file name.
        Json document = Json::parse(m_Text);
        document.erase("output");
        Json &packages = document["packages"];
        for (size_t i = 0; i < packages.size(); ++i)
        {
            Json entry = Json::object();
            for (auto &[key, value] : packages[i].items())
            {
                if (key == "source" || key == "package")
                    entry["package"] = std::string(GetBuiltFileName(i));
                else
                    entry[key] = std::move(value);
            }
            packages[i] = std::move(entry);
        }

        *pText = JsonFormat(document);
        return Gem::Result::Success;
    }
    catch (const std::bad_alloc &)
    {
        return Gem::Result::OutOfMemory;
    }
    catch (const std::exception &)
    {
        return Gem::Result::Fail;
    }
}

} // namespace Canvas
