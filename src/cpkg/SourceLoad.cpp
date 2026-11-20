//================================================================================================
// SourceLoad - the build's Load stage: reads a package source into a SourcePackage
//================================================================================================

#include "Source.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <iterator>
#include <numbers>
#include <system_error>

namespace CpkgTool
{

namespace
{

namespace fs = std::filesystem;

using Canvas::AnimationInterpolation;
using Canvas::CpkgAnimationProperty;
using Canvas::CpkgImageDimension;
using Canvas::CpkgMobility;
using Canvas::GfxPrimitiveTopology;
using Canvas::LightType;

// The least magnitude a double rounds up from to infinity as a float: FLT_MAX plus half its ULP.
constexpr double kFloatLimit = 0x1.ffffffp+127;

fs::path PathFromUtf8(std::string_view utf8)
{
    return fs::path(std::u8string(reinterpret_cast<const char8_t *>(utf8.data()), utf8.size()));
}

std::string Utf8FromPath(const fs::path &path)
{
    const std::u8string text = path.generic_u8string();
    return std::string(reinterpret_cast<const char *>(text.data()), text.size());
}

bool IsFormatKey(std::string_view key)
{
    return key.starts_with('$');
}

bool IsName(std::string_view name)
{
    return !name.empty() && !IsFormatKey(name) && Canvas::CpkgIsValidName(name);
}

constexpr const char *kNameExpected = "expected a name: non-empty, not beginning with '$', with no '/' or NUL";

// The number of names in a '/'-separated path, or 0 when any of them is not a name.
size_t PathNameCount(std::string_view path)
{
    size_t count = 0;
    for (;;)
    {
        const size_t slash = path.find('/');
        if (!IsName(path.substr(0, slash)))
            return 0;
        ++count;
        if (slash == std::string_view::npos)
            return count;
        path.remove_prefix(slash + 1);
    }
}

std::string Child(const std::string &pointer, std::string_view key)
{
    std::string child = pointer;
    Canvas::JsonPointerAppend(&child, key);
    return child;
}

std::string Child(const std::string &pointer, size_t index)
{
    std::string child = pointer;
    Canvas::JsonPointerAppend(&child, index);
    return child;
}

const Json *Member(const Json &object, const char *key)
{
    const auto it = object.find(key);
    return it == object.end() ? nullptr : &*it;
}

template <class T>
struct EnumName
{
    std::string_view Name;
    T Value;
};

constexpr EnumName<CpkgMobility> kMobilities[] = {
    { "static", CpkgMobility::Static },
    { "stationary", CpkgMobility::Stationary },
    { "movable", CpkgMobility::Movable },
};

constexpr EnumName<GfxPrimitiveTopology> kTopologies[] = {
    { "triangleList", GfxPrimitiveTopology::TriangleList },
    { "patchList4CP", GfxPrimitiveTopology::PatchList4CP },
};

constexpr EnumName<CpkgImageDimension> kDimensions[] = {
    { "texture1D", CpkgImageDimension::Texture1D },
    { "texture2D", CpkgImageDimension::Texture2D },
    { "texture3D", CpkgImageDimension::Texture3D },
    { "cube", CpkgImageDimension::Cube },
};

constexpr EnumName<CpkgAnimationProperty> kProperties[] = {
    { "translation", CpkgAnimationProperty::Translation },
    { "rotation", CpkgAnimationProperty::Rotation },
    { "scale", CpkgAnimationProperty::Scale },
};

constexpr EnumName<AnimationInterpolation> kInterpolations[] = {
    { "linear", AnimationInterpolation::Linear },
    { "step", AnimationInterpolation::Step },
    { "cubic", AnimationInterpolation::Cubic },
};

constexpr EnumName<LightType> kLightTypes[] = {
    { "ambient", LightType::Ambient },
    { "point", LightType::Point },
    { "directional", LightType::Directional },
    { "spot", LightType::Spot },
    { "area", LightType::Area },
};

// Keys of a material's "textures", in SourceTextureRole order.
constexpr const char *kTextureRoles[] = { "albedo", "normal", "roughness", "metallic", "ambientOcclusion", "emissive" };
static_assert(std::size(kTextureRoles) == size_t(SourceTextureRole::Count));

// Keys of an import's "names" and "rename", in SourceImportKind order.
constexpr const char *kImportKinds[] = { "images", "materials", "meshes", "skeletons", "skins", "skeletalAnimations", "clips", "nodes" };
static_assert(std::size(kImportKinds) == size_t(SourceImportKind::Count));

// Keys of a cubemap, in SourceImage::Files order.
constexpr const char *kCubeFaces[] = { "+X", "-X", "+Y", "-Y", "+Z", "-Z" };

// The rotation { "degrees": [x, y, z] } describes: about X by x, then Y by y, then Z by z, each
// axis fixed in the parent space, so the quaternion is qz * qy * qx.
Vec4 QuaternionFromDegrees(const Vec3 &degrees)
{
    double half[3];
    for (int i = 0; i < 3; ++i)
        half[i] = double(degrees[i]) * (std::numbers::pi / 360.0);

    const double sx = std::sin(half[0]), cx = std::cos(half[0]);
    const double sy = std::sin(half[1]), cy = std::cos(half[1]);
    const double sz = std::sin(half[2]), cz = std::cos(half[2]);

    // qy * qx
    const double yxX = cy * sx;
    const double yxY = sy * cx;
    const double yxZ = -sy * sx;
    const double yxW = cy * cx;

    // qz * (qy * qx)
    const double x = cz * yxX - sz * yxY;
    const double y = cz * yxY + sz * yxX;
    const double z = cz * yxZ + sz * yxW;
    const double w = cz * yxW - sz * yxZ;
    return { float(x), float(y), float(z), float(w) };
}

struct Finding
{
    size_t Offset = 0;
    std::string Pointer;
    std::string Message;
};

//------------------------------------------------------------------------------------------------
// Reads the files of one package source. Findings in the file being read are collected and
// reported in document order when the file is done.
class CSourceLoader
{
public:
    CSourceLoader(SourcePackage &package, CDiagnostics &diagnostics) :
        m_Package(package),
        m_Diagnostics(diagnostics)
    {
    }

    void Load(std::string_view packageFile)
    {
        const std::string path = Utf8FromPath(PathFromUtf8(packageFile).lexically_normal());
        std::string text;
        if (!ReadWholeFile(path, &text, nullptr))
            return;

        const uint32_t packageIndex = AddFile(path, std::move(text));
        std::vector<Include> includes;
        if (BeginFile(packageIndex))
            ReadPackageFile(&includes);
        EndFile();

        for (Include &include : includes)
        {
            if (BeginFile(include.File))
                ReadBundleFile(&m_Package.Bundles[include.Bundle]);
            EndFile();
        }
    }

private:
    struct Include
    {
        size_t Bundle = 0;
        uint32_t File = 0;
    };

    //--------------------------------------------------------------------------------------------
    // Files

    // Reads the file at path. A failure is reported at pRef, or at the file itself without one.
    bool ReadWholeFile(const std::string &path, std::string *pBytes, const SourceFileRef *pRef)
    {
        auto fail = [&](const std::string &message)
        {
            if (pRef)
                Error(pRef->Where, message + ": " + Quoted(pRef->Written));
            else
                m_Diagnostics.Report(Severity::Error, path, message);
            return false;
        };

        const fs::path filePath = PathFromUtf8(path);
        std::error_code error;
        if (!fs::is_regular_file(filePath, error))
            return fail("file not found");

        std::ifstream file(filePath, std::ios::binary);
        if (!file)
            return fail("cannot read the file");
        pBytes->assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
        if (file.bad())
            return fail("cannot read the file");
        return true;
    }

    bool FileError(const SourceFileRef &ref, const char *message)
    {
        Error(ref.Where, message + (": " + Quoted(ref.Written)));
        return false;
    }

    // Finds the size of the file ref names. A failure is reported at ref.
    bool FileSize(const SourceFileRef &ref, uint64_t *pSize)
    {
        const fs::path filePath = PathFromUtf8(ref.Path);
        std::error_code error;
        if (!fs::is_regular_file(filePath, error))
            return FileError(ref, "file not found");
        *pSize = fs::file_size(filePath, error);
        if (error)
            return FileError(ref, "cannot read the file");
        return true;
    }

    // Reads size bytes at offset of the file ref names. A failure is reported at ref.
    bool ReadFileRange(const SourceFileRef &ref, uint64_t offset, uint64_t size, std::string *pBytes)
    {
        std::ifstream file(PathFromUtf8(ref.Path), std::ios::binary);
        if (!file || !file.seekg(std::streamoff(offset)))
            return FileError(ref, "cannot read the file");
        pBytes->resize(size_t(size));
        if (!file.read(pBytes->data(), std::streamsize(size)))
            return FileError(ref, "cannot read the file");
        return true;
    }

    uint32_t AddFile(std::string path, std::string text)
    {
        auto pFile = std::make_unique<SourceFile>();
        pFile->Path = std::move(path);
        pFile->Text = std::move(text);
        pFile->Lines = std::make_unique<Canvas::CJsonLineIndex>(pFile->Text);
        m_Package.Files.push_back(std::move(pFile));
        return uint32_t(m_Package.Files.size() - 1);
    }

    // Parses the file and makes it the one being read. Returns false on a syntax error.
    bool BeginFile(uint32_t fileIndex)
    {
        m_File = fileIndex;
        SourceFile &file = *m_Package.Files[fileIndex];
        m_Directory = PathFromUtf8(file.Path).parent_path();

        Canvas::JsonParseError parseError;
        if (Canvas::JsonParse(file.Text, &file.Document, &parseError))
            return true;

        const Canvas::JsonPosition position = file.Lines->Locate(parseError.Offset);
        m_Diagnostics.Report(Severity::Error, file.Path, position.Line, position.Column, parseError.Message);
        return false;
    }

    void EndFile()
    {
        std::stable_sort(m_Findings.begin(), m_Findings.end(),
            [](const Finding &a, const Finding &b) { return a.Offset < b.Offset; });

        const SourceFile &file = *m_Package.Files[m_File];
        for (const Finding &finding : m_Findings)
        {
            const Canvas::JsonPosition position = file.Lines->Locate(finding.Offset);
            m_Diagnostics.Report(Severity::Error, file.Path, position.Line, position.Column, finding.Message, finding.Pointer);
        }
        m_Findings.clear();
    }

    //--------------------------------------------------------------------------------------------
    // Locations and findings

    SourceLocation At(const Json &value, const std::string &pointer) const
    {
        return { m_File, value.start_pos(), pointer };
    }

    // The location of the key of the member whose value is value.
    SourceLocation AtKey(const Json &value, const std::string &pointer) const
    {
        return { m_File, Canvas::JsonKeyOffset(m_Package.Files[m_File]->Text, value.start_pos()), pointer };
    }

    void Error(const SourceLocation &where, std::string message)
    {
        m_Findings.push_back({ where.Offset, where.Pointer, std::move(message) });
    }

    void Error(const Json &value, const std::string &pointer, std::string message)
    {
        Error(At(value, pointer), std::move(message));
    }

    //--------------------------------------------------------------------------------------------
    // Values

    // Checks a member whose key begins with '$': "$comment" holds a string, "$extras" holds any
    // value, and any other such key is unknown.
    void ReadFormatKey(const std::string &key, const Json &member, const std::string &pointer)
    {
        const std::string memberPointer = Child(pointer, key);
        if (key == "$comment")
        {
            if (!member.is_string())
                Error(member, memberPointer, "expected a string");
        }
        else if (key != "$extras")
            Error(AtKey(member, memberPointer), "unknown key " + Quoted(key));
    }

    // Checks that value is an object, that each of its keys is one of known or a format key, and
    // that it holds each of required.
    bool ExpectObject(const Json &value, const std::string &pointer, std::initializer_list<std::string_view> known,
        std::initializer_list<const char *> required = {})
    {
        if (!value.is_object())
        {
            Error(value, pointer, "expected an object");
            return false;
        }

        for (const auto &[key, member] : value.items())
        {
            if (std::find(known.begin(), known.end(), key) != known.end())
                continue;
            if (IsFormatKey(key))
                ReadFormatKey(key, member, pointer);
            else
                Error(AtKey(member, Child(pointer, key)), "unknown key " + Quoted(key));
        }
        for (const char *key : required)
        {
            if (!value.contains(key))
                Error(value, pointer, "missing required key " + Quoted(key));
        }
        return true;
    }

    bool ReadNumber(const Json &value, const std::string &pointer, float *pNumber)
    {
        if (!value.is_number())
        {
            Error(value, pointer, "expected a number");
            return false;
        }
        const double number = value.get<double>();
        if (!(std::fabs(number) < kFloatLimit))
        {
            Error(value, pointer, "expected a number within the range of a 32-bit float");
            return false;
        }
        *pNumber = float(number);
        return true;
    }

    bool ReadNumberAtLeast(const Json &value, const std::string &pointer, float minimum, bool inclusive, float *pNumber)
    {
        float number = 0.0f;
        if (!ReadNumber(value, pointer, &number))
            return false;
        if (inclusive ? number < minimum : number <= minimum)
        {
            char bound[32];
            std::snprintf(bound, sizeof(bound), "%g", double(minimum));
            Error(value, pointer, std::string("expected a number ") + (inclusive ? "of at least " : "greater than ") + bound);
            return false;
        }
        *pNumber = number;
        return true;
    }

    // A byte offset into a binary file: a multiple of 4, no larger than a double holds exactly.
    bool ReadByteOffset(const Json &value, const std::string &pointer, uint64_t *pOffset)
    {
        constexpr uint64_t kLimit = uint64_t(1) << 53;
        std::optional<uint64_t> offset;
        if (value.is_number_unsigned())
            offset = value.get<uint64_t>();
        else if (value.is_number_float())
        {
            const double number = value.get<double>();
            if (number >= 0.0 && number <= double(kLimit) && number == std::floor(number))
                offset = uint64_t(number);
        }
        if (!offset || *offset > kLimit || *offset % 4 != 0)
        {
            Error(value, pointer, "expected a byte offset: a non-negative integer that is a multiple of 4");
            return false;
        }
        *pOffset = *offset;
        return true;
    }

    bool ReadInteger(const Json &value, const std::string &pointer, uint32_t minimum, uint32_t *pInteger)
    {
        bool integral = false;
        double number = 0.0;
        if (value.is_number_unsigned())
        {
            integral = true;
            number = double(std::min<uint64_t>(value.get<uint64_t>(), uint64_t(UINT32_MAX) + 1));
        }
        else if (value.is_number_integer())
        {
            integral = true;
            number = double(std::max<int64_t>(value.get<int64_t>(), -1));
        }
        else if (value.is_number_float())
        {
            number = value.get<double>();
            integral = std::isfinite(number) && number == std::floor(number);
        }

        if (!integral)
        {
            Error(value, pointer, "expected an integer");
            return false;
        }
        if (number < double(minimum) || number > double(UINT32_MAX))
        {
            Error(value, pointer, "expected an integer from " + std::to_string(minimum) + " to " + std::to_string(UINT32_MAX));
            return false;
        }
        *pInteger = uint32_t(number);
        return true;
    }

    bool ReadBool(const Json &value, const std::string &pointer, bool *pValue)
    {
        if (!value.is_boolean())
        {
            Error(value, pointer, "expected true or false");
            return false;
        }
        *pValue = value.get<bool>();
        return true;
    }

    template <size_t N>
    bool ReadVector(const Json &value, const std::string &pointer, std::array<float, N> *pVector)
    {
        if (!value.is_array() || value.size() != N)
        {
            Error(value, pointer, "expected an array of " + std::to_string(N) + " numbers");
            return false;
        }
        std::array<float, N> vector = {};
        bool read = true;
        for (size_t i = 0; i < N; ++i)
            read = ReadNumber(value[i], Child(pointer, i), &vector[i]) && read;
        if (read)
            *pVector = vector;
        return read;
    }

    template <size_t N>
    void ReadOptionalVector(const Json &object, const char *key, const std::string &pointer, std::optional<std::array<float, N>> *pVector)
    {
        if (const Json *pValue = Member(object, key))
        {
            std::array<float, N> vector = {};
            if (ReadVector(*pValue, Child(pointer, key), &vector))
                *pVector = vector;
        }
    }

    template <size_t N>
    void ReadDefaultedVector(const Json &object, const char *key, const std::string &pointer, std::array<float, N> *pVector)
    {
        if (const Json *pValue = Member(object, key))
            ReadVector(*pValue, Child(pointer, key), pVector);
    }

    void ReadOptionalNumber(const Json &object, const char *key, const std::string &pointer, std::optional<float> *pNumber)
    {
        if (const Json *pValue = Member(object, key))
        {
            float number = 0.0f;
            if (ReadNumber(*pValue, Child(pointer, key), &number))
                *pNumber = number;
        }
    }

    void ReadDefaultedNumber(const Json &object, const char *key, const std::string &pointer, float *pNumber)
    {
        if (const Json *pValue = Member(object, key))
            ReadNumber(*pValue, Child(pointer, key), pNumber);
    }

    void ReadOptionalBool(const Json &object, const char *key, const std::string &pointer, std::optional<bool> *pBool)
    {
        if (const Json *pValue = Member(object, key))
        {
            bool flag = false;
            if (ReadBool(*pValue, Child(pointer, key), &flag))
                *pBool = flag;
        }
    }

    template <class T, size_t N>
    bool ReadEnum(const Json &value, const std::string &pointer, const EnumName<T> (&names)[N], T *pValue)
    {
        if (value.is_string())
        {
            const std::string &text = value.get_ref<const std::string &>();
            for (const EnumName<T> &name : names)
            {
                if (text == name.Name)
                {
                    *pValue = name.Value;
                    return true;
                }
            }
        }

        std::string message = "expected ";
        for (size_t i = 0; i < N; ++i)
        {
            message += i == 0 ? "" : i + 1 < N ? ", " : N == 2 ? " or " : ", or ";
            message += Quoted(names[i].Name);
        }
        Error(value, pointer, std::move(message));
        return false;
    }

    template <class T, size_t N>
    void ReadOptionalEnum(const Json &object, const char *key, const std::string &pointer, const EnumName<T> (&names)[N], std::optional<T> *pValue)
    {
        if (const Json *pMember = Member(object, key))
        {
            T value = {};
            if (ReadEnum(*pMember, Child(pointer, key), names, &value))
                *pValue = value;
        }
    }

    bool ReadString(const Json &value, const std::string &pointer, SourceString *pString)
    {
        if (!value.is_string())
        {
            Error(value, pointer, "expected a string");
            return false;
        }
        pString->Text = value.get<std::string>();
        pString->Where = At(value, pointer);
        return true;
    }

    bool ReadName(const Json &value, const std::string &pointer, SourceString *pName)
    {
        if (!ReadString(value, pointer, pName))
            return false;
        if (IsName(pName->Text))
            return true;
        Error(value, pointer, kNameExpected);
        return false;
    }

    // A path of 1 to maxNames names.
    bool ReadPath(const Json &value, const std::string &pointer, size_t maxNames, SourceString *pPath)
    {
        if (!ReadString(value, pointer, pPath))
            return false;
        const size_t count = PathNameCount(pPath->Text);
        if (count >= 1 && count <= maxNames)
            return true;
        Error(value, pointer, maxNames == 3
            ? "expected an asset path: 1 to 3 names separated by '/'"
            : "expected a bundle path: 1 or 2 names separated by '/'");
        return false;
    }

    bool ReadAssetPath(const Json &value, const std::string &pointer, SourceString *pPath)
    {
        return ReadPath(value, pointer, 3, pPath);
    }

    bool ReadBundlePath(const Json &value, const std::string &pointer, SourceString *pPath)
    {
        return ReadPath(value, pointer, 2, pPath);
    }

    using StringReader = bool (CSourceLoader::*)(const Json &, const std::string &, SourceString *);

    void ReadOptionalString(const Json &object, const char *key, const std::string &pointer, StringReader read, std::optional<SourceString> *pString)
    {
        if (const Json *pValue = Member(object, key))
        {
            SourceString text;
            if ((this->*read)(*pValue, Child(pointer, key), &text))
                *pString = std::move(text);
        }
    }

    void ReadRequiredString(const Json &object, const char *key, const std::string &pointer, StringReader read, SourceString *pString)
    {
        if (const Json *pValue = Member(object, key))
            (this->*read)(*pValue, Child(pointer, key), pString);
    }

    bool ReadFile(const Json &value, const std::string &pointer, SourceFileRef *pFile)
    {
        if (!value.is_string())
        {
            Error(value, pointer, "expected a file path");
            return false;
        }
        const std::string &written = value.get_ref<const std::string &>();
        if (written.empty())
        {
            Error(value, pointer, "expected a non-empty file path");
            return false;
        }
        if (written.find('\\') != std::string::npos)
        {
            Error(value, pointer, "expected '/' as the path separator");
            return false;
        }

        const fs::path filePath = PathFromUtf8(written);
        pFile->Written = written;
        pFile->Path = Utf8FromPath((filePath.is_absolute() ? filePath : m_Directory / filePath).lexically_normal());
        pFile->Where = At(value, pointer);
        return true;
    }

    bool ReadRotation(const Json &value, const std::string &pointer, SourceRotation *pRotation)
    {
        pRotation->Where = At(value, pointer);
        if (value.is_array())
            return ReadVector(value, pointer, &pRotation->Quaternion);

        if (value.is_object() && value.contains("degrees"))
        {
            ExpectObject(value, pointer, { "degrees" });
            Vec3 degrees = {};
            if (!ReadVector(value["degrees"], Child(pointer, "degrees"), &degrees))
                return false;
            pRotation->Quaternion = QuaternionFromDegrees(degrees);
            return true;
        }

        Error(value, pointer, "expected a quaternion [x, y, z, w] or { \"degrees\": [x, y, z] }");
        return false;
    }

    void ReadOptionalRotation(const Json &object, const char *key, const std::string &pointer, std::optional<SourceRotation> *pRotation)
    {
        if (const Json *pValue = Member(object, key))
        {
            SourceRotation rotation;
            if (ReadRotation(*pValue, Child(pointer, key), &rotation))
                *pRotation = std::move(rotation);
        }
    }

    // Reads the translation, rotation, and scale members of object.
    void ReadTransformMembers(const Json &object, const std::string &pointer, SourceTransform *pTransform)
    {
        ReadOptionalVector(object, "translation", pointer, &pTransform->Translation);
        ReadOptionalRotation(object, "rotation", pointer, &pTransform->Rotation);
        ReadOptionalVector(object, "scale", pointer, &pTransform->Scale);
    }

    void ReadTransform(const Json &value, const std::string &pointer, SourceTransform *pTransform)
    {
        if (ExpectObject(value, pointer, { "translation", "rotation", "scale" }))
            ReadTransformMembers(value, pointer, pTransform);
    }

    void ReadBounds(const Json &value, const std::string &pointer, SourceBounds *pBounds)
    {
        pBounds->Where = At(value, pointer);
        if (!ExpectObject(value, pointer, { "min", "max" }, { "min", "max" }))
            return;
        ReadDefaultedVector(value, "min", pointer, &pBounds->Min);
        ReadDefaultedVector(value, "max", pointer, &pBounds->Max);
    }

    // Calls read(element, pointer) for each element of an array.
    template <class TRead>
    bool ReadList(const Json &value, const std::string &pointer, TRead &&read)
    {
        if (!value.is_array())
        {
            Error(value, pointer, "expected an array");
            return false;
        }
        for (size_t i = 0; i < value.size(); ++i)
            read(value[i], Child(pointer, i));
        return true;
    }

    template <class TRead>
    void ReadOptionalList(const Json &object, const char *key, const std::string &pointer, TRead &&read)
    {
        if (const Json *pValue = Member(object, key))
            ReadList(*pValue, Child(pointer, key), read);
    }

    // A list of at least one element.
    template <class TRead>
    void ReadRequiredList(const Json &object, const char *key, const std::string &pointer, const char *element, TRead &&read)
    {
        const Json *pValue = Member(object, key);
        if (!pValue)
            return;
        const std::string listPointer = Child(pointer, key);
        if (ReadList(*pValue, listPointer, read) && pValue->empty())
            Error(*pValue, listPointer, std::string("expected at least one ") + element);
    }

    // Calls read(name, value, pointer) for each member of a map from names, and checks each format
    // key the map holds.
    template <class TRead>
    void ReadNameMap(const Json &object, const char *key, const std::string &pointer, TRead &&read)
    {
        const Json *pMap = Member(object, key);
        if (!pMap)
            return;
        const std::string mapPointer = Child(pointer, key);
        if (!pMap->is_object())
        {
            Error(*pMap, mapPointer, "expected an object");
            return;
        }
        for (const auto &[name, value] : pMap->items())
        {
            if (IsFormatKey(name))
            {
                ReadFormatKey(name, value, mapPointer);
                continue;
            }
            const std::string memberPointer = Child(mapPointer, name);
            if (!IsName(name))
            {
                Error(AtKey(value, memberPointer), kNameExpected);
                continue;
            }
            read(SourceString{ name, AtKey(value, memberPointer) }, value, memberPointer);
        }
    }

    template <class T>
    void ReadAssetMap(const Json &bundle, const char *key, const std::string &pointer, std::vector<SourceNamed<T>> *pAssets,
        void (CSourceLoader::*read)(const Json &, const std::string &, T *))
    {
        ReadNameMap(bundle, key, pointer, [&](SourceString name, const Json &value, const std::string &memberPointer)
        {
            SourceNamed<T> &asset = pAssets->emplace_back();
            asset.Name = std::move(name);
            (this->*read)(value, memberPointer, &asset.Value);
        });
    }

    template <class T>
    bool ReadArrayElement(const Json &value, const std::string &pointer, T *pElement)
    {
        if constexpr (std::is_same_v<T, float>)
            return ReadNumber(value, pointer, pElement);
        else
            return ReadInteger(value, pointer, 0, pElement);
    }

    // A large numeric array whose elements each hold components values of T.
    template <class T>
    bool ReadArray(const Json &value, const std::string &pointer, size_t components, SourceArray<T> *pArray)
    {
        pArray->Where = At(value, pointer);
        if (value.is_array())
        {
            pArray->Values.resize(value.size());
            for (size_t i = 0; i < value.size(); ++i)
            {
                if (!ReadArrayElement(value[i], Child(pointer, i), &pArray->Values[i]))
                    return false;
            }
            if (value.size() % components != 0)
            {
                Error(value, pointer, "expected a multiple of " + std::to_string(components) + " numbers; found " + std::to_string(value.size()));
                return false;
            }
            return true;
        }

        if (value.is_object() && value.contains("file"))
        {
            ExpectObject(value, pointer, { "file", "offset", "count" });
            SourceArrayFile arrayFile;
            bool valid = ReadFile(value["file"], Child(pointer, "file"), &arrayFile.Ref);
            const Json *pOffset = Member(value, "offset");
            if (pOffset)
                valid = ReadByteOffset(*pOffset, Child(pointer, "offset"), &arrayFile.Offset.emplace()) && valid;
            const Json *pCount = Member(value, "count");
            if (pCount)
                valid = ReadInteger(*pCount, Child(pointer, "count"), 0, &arrayFile.Count.emplace()) && valid;
            uint64_t fileSize = 0;
            if (!valid || !FileSize(arrayFile.Ref, &fileSize))
                return false;

            const SourceFileRef &file = arrayFile.Ref;
            const uint64_t offset = arrayFile.Offset.value_or(0);
            const uint64_t elementSize = sizeof(T) * components;
            const std::string fileText = Quoted(file.Written) + ", " + std::to_string(fileSize) + " bytes";
            if (offset > fileSize)
            {
                Error(*pOffset, Child(pointer, "offset"), "offset " + std::to_string(offset) + " is past the end of " + fileText);
                return false;
            }
            uint64_t byteCount = fileSize - offset;
            if (arrayFile.Count)
            {
                if (uint64_t(*arrayFile.Count) * elementSize > byteCount)
                {
                    Error(*pCount, Child(pointer, "count"), std::to_string(*arrayFile.Count) + " elements of " + std::to_string(elementSize)
                        + " bytes from offset " + std::to_string(offset) + " extend past the end of " + fileText);
                    return false;
                }
                byteCount = *arrayFile.Count * elementSize;
            }
            else if (byteCount % elementSize != 0)
            {
                const std::string elementText = "its element size, " + std::to_string(elementSize) + " bytes";
                if (offset == 0)
                    Error(file.Where, "the size of " + fileText + ", is not a multiple of " + elementText);
                else
                    Error(file.Where, "the " + std::to_string(byteCount) + " bytes of " + Quoted(file.Written) + " from offset "
                        + std::to_string(offset) + " are not a multiple of " + elementText);
                return false;
            }

            std::string bytes;
            if (!ReadFileRange(file, offset, byteCount, &bytes))
                return false;
            pArray->Values.resize(bytes.size() / sizeof(T));
            std::memcpy(pArray->Values.data(), bytes.data(), bytes.size());
            pArray->File = std::move(arrayFile);
            return true;
        }

        Error(value, pointer, "expected an array of numbers or { \"file\": file }");
        return false;
    }

    template <class T>
    void ReadOptionalArray(const Json &object, const char *key, const std::string &pointer, size_t components, std::optional<SourceArray<T>> *pArray)
    {
        if (const Json *pValue = Member(object, key))
        {
            SourceArray<T> array;
            if (ReadArray(*pValue, Child(pointer, key), components, &array))
                *pArray = std::move(array);
        }
    }

    template <class T>
    void ReadRequiredArray(const Json &object, const char *key, const std::string &pointer, size_t components, SourceArray<T> *pArray)
    {
        if (const Json *pValue = Member(object, key))
            ReadArray(*pValue, Child(pointer, key), components, pArray);
    }

    //--------------------------------------------------------------------------------------------
    // Package and bundle files

    // Reads the members a package file and a bundle file share.
    void ReadFileHeader(const Json &document)
    {
        if (const Json *pValue = Member(document, "$schema"))
        {
            if (!pValue->is_string())
                Error(*pValue, "/$schema", "expected a string");
        }
        if (const Json *pValue = Member(document, "formatVersion"))
        {
            if (!pValue->is_number() || pValue->get<double>() != 1.0)
                Error(*pValue, "/formatVersion", "expected format version 1");
        }
    }

    void ReadPackageFile(std::vector<Include> *pIncludes)
    {
        const Json &document = m_Package.Files[m_File]->Document;
        if (!ExpectObject(document, "", { "$schema", "formatVersion", "packageName", "bundles" }, { "formatVersion", "packageName", "bundles" }))
            return;

        ReadFileHeader(document);
        ReadRequiredString(document, "packageName", "", &CSourceLoader::ReadName, &m_Package.PackageName);

        std::vector<SourceFileRef> included;
        ReadOptionalList(document, "bundles", "", [&](const Json &entry, const std::string &pointer)
        {
            SourceBundle &bundle = m_Package.Bundles.emplace_back();
            if (!entry.is_object() || !entry.contains("include"))
            {
                ReadBundle(entry, pointer, &bundle);
                return;
            }

            ExpectObject(entry, pointer, { "include" });
            SourceFileRef file;
            if (!ReadFile(entry["include"], Child(pointer, "include"), &file))
                return;
            if (!file.Written.ends_with(".cbundle.json"))
            {
                Error(file.Where, "expected a bundle file name ending in \".cbundle.json\"");
                return;
            }
            for (const SourceFileRef &earlier : included)
            {
                std::error_code error;
                if (earlier.Path == file.Path || fs::equivalent(PathFromUtf8(earlier.Path), PathFromUtf8(file.Path), error))
                {
                    Error(file.Where, "the bundle file " + Quoted(file.Written) + " is already included");
                    return;
                }
            }

            std::string text;
            if (!ReadWholeFile(file.Path, &text, &file))
                return;
            included.push_back(file);
            pIncludes->push_back({ m_Package.Bundles.size() - 1, AddFile(file.Path, std::move(text)) });
        });
    }

    void ReadBundleFile(SourceBundle *pBundle)
    {
        const Json &document = m_Package.Files[m_File]->Document;
        if (!ExpectObject(document, "", { "$schema", "formatVersion", "bundle" }, { "formatVersion", "bundle" }))
            return;

        ReadFileHeader(document);
        if (const Json *pBundleValue = Member(document, "bundle"))
            ReadBundle(*pBundleValue, "/bundle", pBundle);
    }

    void ReadBundle(const Json &value, const std::string &pointer, SourceBundle *pBundle)
    {
        pBundle->Where = At(value, pointer);
        if (!ExpectObject(value, pointer,
            { "name", "images", "materials", "meshes", "skeletons", "skins", "skeletalAnimations", "skyboxes", "nodes",
              "placements", "meshFields", "importNodes", "clips", "exports", "world", "imports" },
            { "name" }))
        {
            return;
        }

        ReadRequiredString(value, "name", pointer, &CSourceLoader::ReadName, &pBundle->Name);
        ReadAssetMap(value, "images", pointer, &pBundle->Images, &CSourceLoader::ReadImage);
        ReadAssetMap(value, "materials", pointer, &pBundle->Materials, &CSourceLoader::ReadMaterial);
        ReadAssetMap(value, "meshes", pointer, &pBundle->Meshes, &CSourceLoader::ReadMesh);
        ReadAssetMap(value, "skeletons", pointer, &pBundle->Skeletons, &CSourceLoader::ReadSkeleton);
        ReadAssetMap(value, "skins", pointer, &pBundle->Skins, &CSourceLoader::ReadSkin);
        ReadAssetMap(value, "skeletalAnimations", pointer, &pBundle->SkeletalAnimations, &CSourceLoader::ReadSkeletalAnimation);
        ReadAssetMap(value, "skyboxes", pointer, &pBundle->Skyboxes, &CSourceLoader::ReadSkybox);
        ReadOptionalList(value, "nodes", pointer, [&](const Json &entry, const std::string &entryPointer)
        {
            ReadNode(entry, entryPointer, &pBundle->Nodes.emplace_back());
        });
        ReadOptionalList(value, "placements", pointer, [&](const Json &entry, const std::string &entryPointer)
        {
            ReadPlacement(entry, entryPointer, &pBundle->Placements.emplace_back());
        });
        ReadOptionalList(value, "meshFields", pointer, [&](const Json &entry, const std::string &entryPointer)
        {
            ReadMeshField(entry, entryPointer, &pBundle->MeshFields.emplace_back());
        });
        ReadOptionalList(value, "importNodes", pointer, [&](const Json &entry, const std::string &entryPointer)
        {
            ReadImportNode(entry, entryPointer, &pBundle->ImportNodes.emplace_back());
        });
        ReadAssetMap(value, "clips", pointer, &pBundle->Clips, &CSourceLoader::ReadClip);
        if (Member(value, "exports"))
        {
            pBundle->Exports.emplace();
            ReadUniqueNames(value, "exports", pointer, "is already exported", &CSourceLoader::ReadName, &*pBundle->Exports);
        }
        if (const Json *pWorld = Member(value, "world"))
            ReadWorld(*pWorld, Child(pointer, "world"), &pBundle->World.emplace());
        ReadOptionalList(value, "imports", pointer, [&](const Json &entry, const std::string &entryPointer)
        {
            ReadImport(entry, entryPointer, &pBundle->Imports.emplace_back());
        });
    }

    // A list of strings, each listed once.
    void ReadUniqueNames(const Json &object, const char *key, const std::string &pointer, const char *duplicate, StringReader read, std::vector<SourceString> *pStrings)
    {
        ReadOptionalList(object, key, pointer, [&](const Json &entry, const std::string &entryPointer)
        {
            SourceString text;
            if (!(this->*read)(entry, entryPointer, &text))
                return;
            const bool listed = std::any_of(pStrings->begin(), pStrings->end(), [&](const SourceString &earlier) { return earlier.Text == text.Text; });
            if (listed)
                Error(text.Where, Quoted(text.Text) + " " + duplicate);
            else
                pStrings->push_back(std::move(text));
        });
    }

    //--------------------------------------------------------------------------------------------
    // Shared assets

    void ReadImage(const Json &value, const std::string &pointer, SourceImage *pImage)
    {
        pImage->Where = At(value, pointer);
        if (value.is_string())
        {
            SourceFileRef file;
            if (ReadFile(value, pointer, &file))
                pImage->Files.push_back(std::move(file));
            return;
        }

        if (value.is_object() && value.contains("cubemap"))
        {
            pImage->Form = SourceImageForm::Cubemap;
            pImage->Dimension = CpkgImageDimension::Cube;
            pImage->ArraySize = 6;
            ExpectObject(value, pointer, { "cubemap" });

            const Json &faces = value["cubemap"];
            const std::string facesPointer = Child(pointer, "cubemap");
            if (!ExpectObject(faces, facesPointer, { "+X", "-X", "+Y", "-Y", "+Z", "-Z" }, { "+X", "-X", "+Y", "-Y", "+Z", "-Z" }))
                return;
            for (const char *face : kCubeFaces)
            {
                if (const Json *pFace = Member(faces, face))
                {
                    SourceFileRef file;
                    if (ReadFile(*pFace, Child(facesPointer, face), &file))
                        pImage->Files.push_back(std::move(file));
                }
            }
            return;
        }

        if (value.is_object() && (value.contains("dimension") || value.contains("files")))
        {
            pImage->Form = SourceImageForm::General;
            if (!ExpectObject(value, pointer, { "dimension", "arraySize", "mipLevels", "files" }, { "dimension", "files" }))
                return;
            if (const Json *pDimension = Member(value, "dimension"))
                ReadEnum(*pDimension, Child(pointer, "dimension"), kDimensions, &pImage->Dimension);
            if (const Json *pArraySize = Member(value, "arraySize"))
                ReadInteger(*pArraySize, Child(pointer, "arraySize"), 1, &pImage->ArraySize);
            if (const Json *pMipLevels = Member(value, "mipLevels"))
                ReadInteger(*pMipLevels, Child(pointer, "mipLevels"), 1, &pImage->MipLevels);
            ReadRequiredList(value, "files", pointer, "file", [&](const Json &entry, const std::string &entryPointer)
            {
                SourceFileRef file;
                if (ReadFile(entry, entryPointer, &file))
                    pImage->Files.push_back(std::move(file));
            });
            return;
        }

        Error(value, pointer, "expected an image: a file, { \"cubemap\" }, or { \"dimension\", \"files\" }");
    }

    void ReadMaterial(const Json &value, const std::string &pointer, SourceMaterial *pMaterial)
    {
        pMaterial->Where = At(value, pointer);
        if (!ExpectObject(value, pointer, { "baseColor", "emissive", "roughness", "metallic", "ambientOcclusion", "textures", "displacement" }))
            return;

        ReadDefaultedVector(value, "baseColor", pointer, &pMaterial->BaseColor);
        ReadDefaultedVector(value, "emissive", pointer, &pMaterial->Emissive);
        ReadDefaultedNumber(value, "roughness", pointer, &pMaterial->Roughness);
        ReadDefaultedNumber(value, "metallic", pointer, &pMaterial->Metallic);
        ReadDefaultedNumber(value, "ambientOcclusion", pointer, &pMaterial->AmbientOcclusion);

        if (const Json *pTextures = Member(value, "textures"))
        {
            const std::string texturesPointer = Child(pointer, "textures");
            if (ExpectObject(*pTextures, texturesPointer, { "albedo", "normal", "roughness", "metallic", "ambientOcclusion", "emissive" }))
            {
                for (size_t role = 0; role < std::size(kTextureRoles); ++role)
                    ReadOptionalString(*pTextures, kTextureRoles[role], texturesPointer, &CSourceLoader::ReadAssetPath, &pMaterial->Textures[role]);
            }
        }

        if (const Json *pDisplacement = Member(value, "displacement"))
        {
            const std::string displacementPointer = Child(pointer, "displacement");
            SourceDisplacement &displacement = pMaterial->Displacement.emplace();
            displacement.Where = At(*pDisplacement, displacementPointer);
            if (ExpectObject(*pDisplacement, displacementPointer,
                { "map", "scale", "bias", "minTessFactor", "maxTessFactor", "distanceLodScale", "curvatureLodScale" }, { "map" }))
            {
                ReadRequiredString(*pDisplacement, "map", displacementPointer, &CSourceLoader::ReadAssetPath, &displacement.Map);
                ReadDefaultedNumber(*pDisplacement, "scale", displacementPointer, &displacement.Scale);
                ReadDefaultedNumber(*pDisplacement, "bias", displacementPointer, &displacement.Bias);
                ReadDefaultedNumber(*pDisplacement, "minTessFactor", displacementPointer, &displacement.MinTessFactor);
                ReadDefaultedNumber(*pDisplacement, "maxTessFactor", displacementPointer, &displacement.MaxTessFactor);
                ReadDefaultedNumber(*pDisplacement, "distanceLodScale", displacementPointer, &displacement.DistanceLodScale);
                ReadDefaultedNumber(*pDisplacement, "curvatureLodScale", displacementPointer, &displacement.CurvatureLodScale);
            }
        }
    }

    void ReadMesh(const Json &value, const std::string &pointer, SourceMesh *pMesh)
    {
        pMesh->Where = At(value, pointer);
        if (!ExpectObject(value, pointer, { "topology", "bounds", "groups" }, { "groups" }))
            return;

        if (const Json *pTopology = Member(value, "topology"))
            ReadEnum(*pTopology, Child(pointer, "topology"), kTopologies, &pMesh->Topology);

        if (const Json *pBounds = Member(value, "bounds"))
        {
            const std::string boundsPointer = Child(pointer, "bounds");
            if (pBounds->is_string() && pBounds->get_ref<const std::string &>() == "none")
            {
                pMesh->BoundsForm = SourceBoundsForm::None;
            }
            else if (pBounds->is_object())
            {
                pMesh->BoundsForm = SourceBoundsForm::Declared;
                ReadBounds(*pBounds, boundsPointer, &pMesh->Bounds);
            }
            else
            {
                Error(*pBounds, boundsPointer, "expected { \"min\", \"max\" } or \"none\"");
            }
        }

        ReadRequiredList(value, "groups", pointer, "group", [&](const Json &entry, const std::string &entryPointer)
        {
            SourceMeshGroup &group = pMesh->Groups.emplace_back();
            group.Where = At(entry, entryPointer);
            if (!ExpectObject(entry, entryPointer, { "material", "positions", "normals", "uv0", "tangents", "boneIndices", "boneWeights" }, { "positions" }))
                return;
            ReadOptionalString(entry, "material", entryPointer, &CSourceLoader::ReadAssetPath, &group.Material);
            ReadRequiredArray(entry, "positions", entryPointer, 3, &group.Positions);
            ReadOptionalArray(entry, "normals", entryPointer, 3, &group.Normals);
            ReadOptionalArray(entry, "uv0", entryPointer, 2, &group.UV0);
            ReadOptionalArray(entry, "tangents", entryPointer, 4, &group.Tangents);
            ReadOptionalArray(entry, "boneIndices", entryPointer, 4, &group.BoneIndices);
            ReadOptionalArray(entry, "boneWeights", entryPointer, 4, &group.BoneWeights);
        });
    }

    void ReadSkeleton(const Json &value, const std::string &pointer, SourceSkeleton *pSkeleton)
    {
        pSkeleton->Where = At(value, pointer);
        if (!ExpectObject(value, pointer, { "bones" }, { "bones" }))
            return;

        ReadRequiredList(value, "bones", pointer, "bone", [&](const Json &entry, const std::string &entryPointer)
        {
            SourceBone &bone = pSkeleton->Bones.emplace_back();
            bone.Where = At(entry, entryPointer);
            if (!ExpectObject(entry, entryPointer, { "name", "parent", "rest" }, { "name" }))
                return;
            ReadRequiredString(entry, "name", entryPointer, &CSourceLoader::ReadName, &bone.Name);
            ReadOptionalString(entry, "parent", entryPointer, &CSourceLoader::ReadName, &bone.Parent);
            if (const Json *pRest = Member(entry, "rest"))
                ReadTransform(*pRest, Child(entryPointer, "rest"), &bone.Rest);
        });
    }

    void ReadSkin(const Json &value, const std::string &pointer, SourceSkin *pSkin)
    {
        pSkin->Where = At(value, pointer);
        if (!ExpectObject(value, pointer, { "skeleton", "bones", "bindPoses" }, { "skeleton", "bones", "bindPoses" }))
            return;

        ReadRequiredString(value, "skeleton", pointer, &CSourceLoader::ReadAssetPath, &pSkin->Skeleton);
        ReadRequiredList(value, "bones", pointer, "bone", [&](const Json &entry, const std::string &entryPointer)
        {
            SourceString bone;
            if (ReadName(entry, entryPointer, &bone))
                pSkin->Bones.push_back(std::move(bone));
        });
        if (const Json *pPoses = Member(value, "bindPoses"))
            pSkin->BindPosesWhere = At(*pPoses, Child(pointer, "bindPoses"));
        ReadRequiredList(value, "bindPoses", pointer, "bind pose", [&](const Json &entry, const std::string &entryPointer)
        {
            ReadTransform(entry, entryPointer, &pSkin->BindPoses.emplace_back());
        });
    }

    // A track of a skeletal animation, whose target key is "bone", or of a clip, "node".
    void ReadTrack(const Json &value, const std::string &pointer, const char *targetKey, SourceTrack *pTrack)
    {
        pTrack->Where = At(value, pointer);
        if (!ExpectObject(value, pointer, { targetKey, "property", "times", "values", "interpolation", "tangents" },
            { targetKey, "property", "times", "values" }))
        {
            return;
        }

        ReadRequiredString(value, targetKey, pointer, &CSourceLoader::ReadName, &pTrack->Target);
        if (const Json *pProperty = Member(value, "property"))
            ReadEnum(*pProperty, Child(pointer, "property"), kProperties, &pTrack->Property);
        ReadRequiredArray(value, "times", pointer, 1, &pTrack->Times);
        ReadRequiredArray(value, "values", pointer, 1, &pTrack->Values);

        pTrack->Interpolations = { AnimationInterpolation::Linear };
        if (const Json *pInterpolation = Member(value, "interpolation"))
        {
            const std::string interpolationPointer = Child(pointer, "interpolation");
            pTrack->InterpolationWhere = At(*pInterpolation, interpolationPointer);
            if (pInterpolation->is_array())
            {
                pTrack->InterpolationPerKey = true;
                pTrack->Interpolations.clear();
                if (pInterpolation->empty())
                    Error(*pInterpolation, interpolationPointer, "expected at least one interpolation");
                for (size_t i = 0; i < pInterpolation->size(); ++i)
                {
                    AnimationInterpolation interpolation = AnimationInterpolation::Linear;
                    ReadEnum((*pInterpolation)[i], Child(interpolationPointer, i), kInterpolations, &interpolation);
                    pTrack->Interpolations.push_back(interpolation);
                }
            }
            else
            {
                ReadEnum(*pInterpolation, interpolationPointer, kInterpolations, &pTrack->Interpolations[0]);
            }
        }

        ReadOptionalArray(value, "tangents", pointer, 1, &pTrack->Tangents);
    }

    void ReadSkeletalAnimation(const Json &value, const std::string &pointer, SourceSkeletalAnimation *pAnimation)
    {
        pAnimation->Where = At(value, pointer);
        if (!ExpectObject(value, pointer, { "skeleton", "tracks" }, { "skeleton", "tracks" }))
            return;

        ReadRequiredString(value, "skeleton", pointer, &CSourceLoader::ReadAssetPath, &pAnimation->Skeleton);
        ReadOptionalList(value, "tracks", pointer, [&](const Json &entry, const std::string &entryPointer)
        {
            ReadTrack(entry, entryPointer, "bone", &pAnimation->Tracks.emplace_back());
        });
    }

    void ReadSkybox(const Json &value, const std::string &pointer, SourceSkybox *pSkybox)
    {
        pSkybox->Where = At(value, pointer);
        if (!ExpectObject(value, pointer, { "image", "orientation", "intensity" }, { "image" }))
            return;

        ReadRequiredString(value, "image", pointer, &CSourceLoader::ReadAssetPath, &pSkybox->Image);
        ReadOptionalRotation(value, "orientation", pointer, &pSkybox->Orientation);
        ReadDefaultedNumber(value, "intensity", pointer, &pSkybox->Intensity);
    }

    //--------------------------------------------------------------------------------------------
    // Bundle structure

    // Reads the attachment members of a node, an import node, or an edit, which holds no
    // skeleton instance.
    void ReadAttachments(const Json &object, const std::string &pointer, bool edit, SourceAttachments *pAttachments)
    {
        if (const Json *pInstance = Member(object, "skeletonInstance"); pInstance && !edit)
        {
            const std::string instancePointer = Child(pointer, "skeletonInstance");
            SourceSkeletonInstance &instance = pAttachments->SkeletonInstance.emplace();
            instance.Where = At(*pInstance, instancePointer);
            if (ExpectObject(*pInstance, instancePointer, { "skeleton" }, { "skeleton" }))
                ReadRequiredString(*pInstance, "skeleton", instancePointer, &CSourceLoader::ReadAssetPath, &instance.Skeleton);
        }

        ReadOptionalList(object, "meshInstances", pointer, [&](const Json &entry, const std::string &entryPointer)
        {
            SourceMeshInstance &instance = pAttachments->MeshInstances.emplace_back();
            instance.Where = At(entry, entryPointer);
            if (!ExpectObject(entry, entryPointer, { "mesh", "skin", "skeletonInstance" }, { "mesh" }))
                return;
            ReadRequiredString(entry, "mesh", entryPointer, &CSourceLoader::ReadAssetPath, &instance.Mesh);
            ReadOptionalString(entry, "skin", entryPointer, &CSourceLoader::ReadAssetPath, &instance.Skin);
            ReadOptionalString(entry, "skeletonInstance", entryPointer, &CSourceLoader::ReadName, &instance.SkeletonInstance);
        });

        ReadOptionalList(object, "meshFields", pointer, [&](const Json &entry, const std::string &entryPointer)
        {
            ReadMeshField(entry, entryPointer, &pAttachments->MeshFields.emplace_back());
        });

        if (const Json *pCamera = Member(object, "camera"))
            ReadCamera(*pCamera, Child(pointer, "camera"), &pAttachments->Camera.emplace());

        if (const Json *pLight = Member(object, "light"))
            ReadLight(*pLight, Child(pointer, "light"), !edit, &pAttachments->Light.emplace());

        ReadOptionalList(object, "placements", pointer, [&](const Json &entry, const std::string &entryPointer)
        {
            ReadPlacement(entry, entryPointer, &pAttachments->Placements.emplace_back());
        });

        ReadOptionalList(object, "children", pointer, [&](const Json &entry, const std::string &entryPointer)
        {
            ReadNode(entry, entryPointer, &pAttachments->Children.emplace_back());
        });
    }

    void ReadNode(const Json &value, const std::string &pointer, SourceNode *pNode)
    {
        pNode->Where = At(value, pointer);
        if (!ExpectObject(value, pointer,
            { "name", "translation", "rotation", "scale", "mobility", "bone", "skeletonInstance", "meshInstances", "meshFields",
              "camera", "light", "placements", "children" }))
        {
            return;
        }

        ReadOptionalString(value, "name", pointer, &CSourceLoader::ReadName, &pNode->Name);
        ReadTransformMembers(value, pointer, &pNode->Transform);
        ReadOptionalEnum(value, "mobility", pointer, kMobilities, &pNode->Mobility);
        ReadOptionalString(value, "bone", pointer, &CSourceLoader::ReadName, &pNode->Bone);
        ReadAttachments(value, pointer, false, &pNode->Attachments);
    }

    void ReadImportNode(const Json &value, const std::string &pointer, SourceImportNode *pNode)
    {
        pNode->Where = At(value, pointer);
        if (!ExpectObject(value, pointer,
            { "name", "placement", "export", "translation", "rotation", "scale", "skeletonInstance", "meshInstances", "meshFields",
              "camera", "light", "placements", "children" },
            { "placement", "export" }))
        {
            return;
        }

        ReadOptionalString(value, "name", pointer, &CSourceLoader::ReadName, &pNode->Name);
        ReadRequiredString(value, "placement", pointer, &CSourceLoader::ReadName, &pNode->Placement);
        ReadRequiredString(value, "export", pointer, &CSourceLoader::ReadName, &pNode->Export);
        ReadTransformMembers(value, pointer, &pNode->Transform);
        ReadAttachments(value, pointer, false, &pNode->Attachments);
    }

    void ReadPlacement(const Json &value, const std::string &pointer, SourcePlacement *pPlacement)
    {
        pPlacement->Where = At(value, pointer);
        if (!ExpectObject(value, pointer, { "name", "bundle", "mobility", "translation", "rotation", "scale" }, { "bundle" }))
            return;

        ReadOptionalString(value, "name", pointer, &CSourceLoader::ReadName, &pPlacement->Name);
        ReadRequiredString(value, "bundle", pointer, &CSourceLoader::ReadBundlePath, &pPlacement->Bundle);
        ReadOptionalEnum(value, "mobility", pointer, kMobilities, &pPlacement->Mobility);
        ReadTransformMembers(value, pointer, &pPlacement->Transform);
    }

    void ReadMeshField(const Json &value, const std::string &pointer, SourceMeshField *pField)
    {
        pField->Where = At(value, pointer);
        if (!ExpectObject(value, pointer, { "name", "mesh", "cullDistance", "transforms" }, { "mesh", "transforms" }))
            return;

        ReadOptionalString(value, "name", pointer, &CSourceLoader::ReadName, &pField->Name);
        ReadRequiredString(value, "mesh", pointer, &CSourceLoader::ReadAssetPath, &pField->Mesh);
        ReadDefaultedNumber(value, "cullDistance", pointer, &pField->CullDistance);
        ReadRequiredList(value, "transforms", pointer, "transform", [&](const Json &entry, const std::string &entryPointer)
        {
            ReadTransform(entry, entryPointer, &pField->Transforms.emplace_back());
        });
    }

    void ReadCamera(const Json &value, const std::string &pointer, SourceCamera *pCamera)
    {
        pCamera->Where = At(value, pointer);
        if (!ExpectObject(value, pointer, { "nearClip", "farClip", "fovAngle", "exposureStops" }))
            return;

        ReadOptionalNumber(value, "nearClip", pointer, &pCamera->NearClip);
        ReadOptionalNumber(value, "farClip", pointer, &pCamera->FarClip);
        ReadOptionalNumber(value, "fovAngle", pointer, &pCamera->FovAngle);
        ReadOptionalNumber(value, "exposureStops", pointer, &pCamera->ExposureStops);
    }

    void ReadLight(const Json &value, const std::string &pointer, bool typeRequired, SourceLight *pLight)
    {
        pLight->Where = At(value, pointer);
        std::initializer_list<const char *> required = { "type" };
        if (!ExpectObject(value, pointer,
            { "type", "color", "intensity", "enabled", "castsShadows", "range", "attenuationConstant", "attenuationLinear",
              "attenuationQuadratic", "spotInnerAngle", "spotOuterAngle", "shadowResolution", "shadowConstantBias",
              "shadowSlopeScaleBias", "shadowNormalOffset" },
            typeRequired ? required : std::initializer_list<const char *>{}))
        {
            return;
        }

        ReadOptionalEnum(value, "type", pointer, kLightTypes, &pLight->Type);
        ReadOptionalVector(value, "color", pointer, &pLight->Color);
        ReadOptionalNumber(value, "intensity", pointer, &pLight->Intensity);
        ReadOptionalBool(value, "enabled", pointer, &pLight->Enabled);
        ReadOptionalBool(value, "castsShadows", pointer, &pLight->CastsShadows);
        ReadOptionalNumber(value, "range", pointer, &pLight->Range);
        ReadOptionalNumber(value, "attenuationConstant", pointer, &pLight->AttenuationConstant);
        ReadOptionalNumber(value, "attenuationLinear", pointer, &pLight->AttenuationLinear);
        ReadOptionalNumber(value, "attenuationQuadratic", pointer, &pLight->AttenuationQuadratic);
        ReadOptionalNumber(value, "spotInnerAngle", pointer, &pLight->SpotInnerAngle);
        ReadOptionalNumber(value, "spotOuterAngle", pointer, &pLight->SpotOuterAngle);
        if (const Json *pResolution = Member(value, "shadowResolution"))
        {
            uint32_t resolution = 0;
            if (ReadInteger(*pResolution, Child(pointer, "shadowResolution"), 0, &resolution))
                pLight->ShadowResolution = resolution;
        }
        ReadOptionalNumber(value, "shadowConstantBias", pointer, &pLight->ShadowConstantBias);
        ReadOptionalNumber(value, "shadowSlopeScaleBias", pointer, &pLight->ShadowSlopeScaleBias);
        ReadOptionalNumber(value, "shadowNormalOffset", pointer, &pLight->ShadowNormalOffset);
    }

    void ReadClip(const Json &value, const std::string &pointer, SourceClip *pClip)
    {
        pClip->Where = At(value, pointer);
        if (!ExpectObject(value, pointer, { "duration", "tracks", "skeletalAnimations" }))
            return;

        if (const Json *pDuration = Member(value, "duration"))
        {
            float duration = 0.0f;
            if (ReadNumberAtLeast(*pDuration, Child(pointer, "duration"), 0.0f, true, &duration))
                pClip->Duration = duration;
        }
        ReadOptionalList(value, "tracks", pointer, [&](const Json &entry, const std::string &entryPointer)
        {
            ReadTrack(entry, entryPointer, "node", &pClip->Tracks.emplace_back());
        });
        ReadOptionalList(value, "skeletalAnimations", pointer, [&](const Json &entry, const std::string &entryPointer)
        {
            SourceClipBinding &binding = pClip->SkeletalAnimations.emplace_back();
            binding.Where = At(entry, entryPointer);
            if (!ExpectObject(entry, entryPointer, { "skeletonInstance", "animation" }, { "skeletonInstance", "animation" }))
                return;
            ReadRequiredString(entry, "skeletonInstance", entryPointer, &CSourceLoader::ReadName, &binding.SkeletonInstance);
            ReadRequiredString(entry, "animation", entryPointer, &CSourceLoader::ReadAssetPath, &binding.Animation);
        });
    }

    void ReadWorld(const Json &value, const std::string &pointer, SourceWorld *pWorld)
    {
        pWorld->Where = At(value, pointer);
        if (!ExpectObject(value, pointer, { "regions", "skyboxes", "defaultSkybox", "activeCamera", "backgroundColor" }))
            return;

        ReadOptionalList(value, "regions", pointer, [&](const Json &entry, const std::string &entryPointer)
        {
            SourceRegion &region = pWorld->Regions.emplace_back();
            region.Where = At(entry, entryPointer);
            if (!ExpectObject(entry, entryPointer, { "region", "bounds" }, { "region", "bounds" }))
                return;
            ReadRequiredString(entry, "region", entryPointer, &CSourceLoader::ReadBundlePath, &region.Region);
            if (const Json *pBounds = Member(entry, "bounds"))
                ReadBounds(*pBounds, Child(entryPointer, "bounds"), &region.Bounds);
        });
        ReadUniqueNames(value, "skyboxes", pointer, "is already listed", &CSourceLoader::ReadAssetPath, &pWorld->Skyboxes);
        ReadOptionalString(value, "defaultSkybox", pointer, &CSourceLoader::ReadAssetPath, &pWorld->DefaultSkybox);
        ReadOptionalString(value, "activeCamera", pointer, &CSourceLoader::ReadName, &pWorld->ActiveCamera);
        ReadDefaultedVector(value, "backgroundColor", pointer, &pWorld->BackgroundColor);
    }

    //--------------------------------------------------------------------------------------------
    // Imports

    bool ReadNameTemplate(const Json &value, const std::string &pointer, SourceString *pTemplate)
    {
        if (!ReadString(value, pointer, pTemplate))
            return false;
        if (!pTemplate->Text.empty() && pTemplate->Text.find('/') == std::string::npos && pTemplate->Text.find('\0') == std::string::npos)
            return true;
        Error(value, pointer, "expected a name template: non-empty, with no '/' or NUL");
        return false;
    }

    // A map from a name in an FBX file, any string not beginning with '$', to a value that read
    // checks. The map may also hold format keys.
    void ReadStringMap(const Json &value, const std::string &pointer, StringReader read, SourceStringMap *pMap)
    {
        if (!value.is_object())
        {
            Error(value, pointer, "expected an object");
            return;
        }
        for (const auto &[key, member] : value.items())
        {
            if (IsFormatKey(key))
            {
                ReadFormatKey(key, member, pointer);
                continue;
            }
            const std::string memberPointer = Child(pointer, key);
            SourceString target;
            if ((this->*read)(member, memberPointer, &target))
                pMap->emplace_back(SourceString{ key, AtKey(member, memberPointer) }, std::move(target));
        }
    }

    void ReadImport(const Json &value, const std::string &pointer, SourceImport *pImport)
    {
        pImport->Where = At(value, pointer);
        if (!ExpectObject(value, pointer,
            { "file", "scale", "rotation", "nodes", "parent", "skeleton", "names", "rename", "replace", "clips", "edits" },
            { "file" }))
        {
            return;
        }

        if (const Json *pFile = Member(value, "file"))
            ReadFile(*pFile, Child(pointer, "file"), &pImport->File);
        if (const Json *pScale = Member(value, "scale"))
            ReadNumberAtLeast(*pScale, Child(pointer, "scale"), 0.0f, false, &pImport->Scale);
        ReadOptionalRotation(value, "rotation", pointer, &pImport->Rotation);
        if (const Json *pNodes = Member(value, "nodes"))
            ReadBool(*pNodes, Child(pointer, "nodes"), &pImport->Nodes);
        ReadOptionalString(value, "parent", pointer, &CSourceLoader::ReadName, &pImport->Parent);
        ReadOptionalString(value, "skeleton", pointer, &CSourceLoader::ReadAssetPath, &pImport->Skeleton);

        if (const Json *pNames = Member(value, "names"))
        {
            const std::string namesPointer = Child(pointer, "names");
            if (ExpectObject(*pNames, namesPointer, { "images", "materials", "meshes", "skeletons", "skins", "skeletalAnimations", "clips", "nodes" }))
            {
                for (size_t kind = 0; kind < std::size(kImportKinds); ++kind)
                    ReadOptionalString(*pNames, kImportKinds[kind], namesPointer, &CSourceLoader::ReadNameTemplate, &pImport->Names[kind]);
            }
        }

        if (const Json *pRename = Member(value, "rename"))
        {
            const std::string renamePointer = Child(pointer, "rename");
            if (ExpectObject(*pRename, renamePointer, { "images", "materials", "meshes", "skeletons", "skins", "skeletalAnimations", "clips", "nodes" }))
            {
                for (size_t kind = 0; kind < std::size(kImportKinds); ++kind)
                {
                    if (const Json *pMap = Member(*pRename, kImportKinds[kind]))
                        ReadStringMap(*pMap, Child(renamePointer, kImportKinds[kind]), &CSourceLoader::ReadName, &pImport->Rename[kind]);
                }
            }
        }

        if (const Json *pReplace = Member(value, "replace"))
        {
            const std::string replacePointer = Child(pointer, "replace");
            if (ExpectObject(*pReplace, replacePointer, { "images", "materials" }))
            {
                if (const Json *pMap = Member(*pReplace, "images"))
                    ReadStringMap(*pMap, Child(replacePointer, "images"), &CSourceLoader::ReadAssetPath, &pImport->ReplaceImages);
                if (const Json *pMap = Member(*pReplace, "materials"))
                    ReadStringMap(*pMap, Child(replacePointer, "materials"), &CSourceLoader::ReadAssetPath, &pImport->ReplaceMaterials);
            }
        }

        if (Member(value, "clips"))
        {
            pImport->Clips.emplace();
            ReadOptionalList(value, "clips", pointer, [&](const Json &entry, const std::string &entryPointer)
            {
                ReadImportClip(entry, entryPointer, &pImport->Clips->emplace_back());
            });
        }

        ReadNameMap(value, "edits", pointer, [&](SourceString name, const Json &editValue, const std::string &editPointer)
        {
            SourceNamed<SourceNodeEdit> &edit = pImport->Edits.emplace_back();
            edit.Name = std::move(name);
            ReadNodeEdit(editValue, editPointer, &edit.Value);
        });
    }

    void ReadImportClip(const Json &value, const std::string &pointer, SourceImportClip *pClip)
    {
        pClip->Where = At(value, pointer);
        if (!ExpectObject(value, pointer, { "name", "stack", "start", "end", "frames" }, { "name" }))
            return;

        ReadRequiredString(value, "name", pointer, &CSourceLoader::ReadName, &pClip->Name);
        ReadOptionalString(value, "stack", pointer, &CSourceLoader::ReadString, &pClip->Stack);
        for (const char *key : { "start", "end" })
        {
            if (const Json *pTime = Member(value, key))
            {
                float time = 0.0f;
                if (ReadNumberAtLeast(*pTime, Child(pointer, key), 0.0f, true, &time))
                    (key[0] == 's' ? pClip->Start : pClip->End) = time;
                if (value.contains("frames"))
                    Error(AtKey(*pTime, Child(pointer, key)), std::string("expected \"frames\" or \"") + key + "\", not both");
            }
        }

        if (const Json *pFrames = Member(value, "frames"))
        {
            const std::string framesPointer = Child(pointer, "frames");
            if (!pFrames->is_array() || pFrames->size() != 2)
            {
                Error(*pFrames, framesPointer, "expected [first, last] frames");
                return;
            }
            std::array<uint32_t, 2> frames = {};
            const bool read = ReadInteger((*pFrames)[0], Child(framesPointer, size_t(0)), 0, &frames[0])
                & ReadInteger((*pFrames)[1], Child(framesPointer, size_t(1)), 0, &frames[1]);
            if (read)
                pClip->Frames = frames;
        }
    }

    void ReadNodeEdit(const Json &value, const std::string &pointer, SourceNodeEdit *pEdit)
    {
        pEdit->Where = At(value, pointer);
        if (!ExpectObject(value, pointer,
            { "translation", "rotation", "scale", "mobility", "camera", "light", "meshInstances", "meshFields", "placements", "children" }))
        {
            return;
        }

        ReadTransformMembers(value, pointer, &pEdit->Transform);
        ReadOptionalEnum(value, "mobility", pointer, kMobilities, &pEdit->Mobility);
        ReadAttachments(value, pointer, true, &pEdit->Attachments);
    }

    SourcePackage &m_Package;
    CDiagnostics &m_Diagnostics;
    uint32_t m_File = 0;
    fs::path m_Directory;
    std::vector<Finding> m_Findings;
};

} // namespace

//------------------------------------------------------------------------------------------------
bool LoadPackageSource(std::string_view packageFile, SourcePackage *pPackage, CDiagnostics &diagnostics)
{
    *pPackage = {};
    const size_t errorCount = diagnostics.GetErrorCount();
    CSourceLoader(*pPackage, diagnostics).Load(packageFile);
    return diagnostics.GetErrorCount() == errorCount;
}

//------------------------------------------------------------------------------------------------
void ReportAt(CDiagnostics &diagnostics, const SourcePackage &package, Severity severity, const SourceLocation &where, std::string_view message)
{
    const SourceFile &file = *package.Files[where.File];
    const Canvas::JsonPosition position = file.Lines->Locate(where.Offset);
    diagnostics.Report(severity, file.Path, position.Line, position.Column, message, where.Pointer);
}

} // namespace CpkgTool
