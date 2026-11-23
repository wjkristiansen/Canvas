//================================================================================================
// Unpack - writes a package source that builds a given .cpkg file
//
// Decodes each asset's payload into the source that builds it, writes the source to a temporary
// folder beside the output, builds that source, and moves its files into place only when the
// build reproduces the package byte for byte. See "unpack" in src/cpkg/README.md.
//================================================================================================

#include "CpkgTool.h"
#include "Stages.h"

namespace CpkgTool
{

using namespace Canvas;
namespace fs = std::filesystem;

namespace
{

// An asset whose large numeric arrays hold more numbers than this writes them to a binary array
// file.
constexpr uint64_t kInlineNumberLimit = 256;

//------------------------------------------------------------------------------------------------
// Files

std::string AsciiLower(std::string_view text)
{
    std::string lower(text);
    for (char &c : lower)
    {
        if (c >= 'A' && c <= 'Z')
            c = char(c - 'A' + 'a');
    }
    return lower;
}

// Whether name, ignoring ASCII case, is a device name Windows reserves, such as CON. Windows
// reserves it with any extension too.
bool IsReservedName(std::string_view name)
{
    const std::string lower = AsciiLower(name);
    if (lower == "con" || lower == "prn" || lower == "aux" || lower == "nul")
        return true;
    return lower.size() == 4 && (lower.starts_with("com") || lower.starts_with("lpt")) && lower[3] >= '0' && lower[3] <= '9';
}

// Appends c as '%' and its two hexadecimal digits.
void AppendEscaped(std::string *pText, char c)
{
    static constexpr char kDigits[] = "0123456789ABCDEF";
    const unsigned char byte = static_cast<unsigned char>(c);
    *pText += '%';
    *pText += kDigits[byte >> 4];
    *pText += kDigits[byte & 15];
}

// A file or folder a CFolder cannot create, for a reason other than its name being taken.
struct FolderError
{
    fs::path Path;
};

// Creates path as an empty file, or the folder path when folder. Returns false when the file
// system already holds a file or folder of that name, as its own rules for names compare them.
bool CreateNew(const fs::path &path, bool folder)
{
    switch (folder ? MakeDirectory(path) : MakeFile(path))
    {
    case FileResult::Success:
        return true;
    case FileResult::Exists:
        return false;
    default:
        throw FolderError{ path };
    }
}

// Names the files and folders of one folder, as "File names" in the README describes, creating
// each in the folder at path so that the file system's own rules decide which names are taken.
class CFolder
{
public:
    explicit CFolder(fs::path path) :
        m_Path(std::move(path))
    {
    }

    const fs::path &Path() const { return m_Path; }

    // Creates and reserves the file name, as it is.
    void Reserve(std::string_view name)
    {
        if (!CreateNew(m_Path / PathOf(name), false))
            throw FolderError{ m_Path / PathOf(name) };
    }

    // Creates, reserves and returns the name of a file named for name, with extension; an empty
    // extension names a folder.
    std::string Add(std::string_view name, std::string_view extension)
    {
        std::string base;
        for (char c : name)
        {
            if (static_cast<unsigned char>(c) < 0x20 || std::string_view("<>:\"/\\|?*%").find(c) != std::string_view::npos)
                AppendEscaped(&base, c);
            else
                base += c;
        }
        if (IsReservedName(std::string_view(base).substr(0, base.find('.'))))
        {
            std::string escaped;
            AppendEscaped(&escaped, base[0]);
            base = escaped + base.substr(1);
        }
        // A folder name cannot end in '.' or ' '.
        if (extension.empty() && (base.back() == '.' || base.back() == ' '))
        {
            const char last = base.back();
            base.pop_back();
            AppendEscaped(&base, last);
        }

        for (uint32_t suffix = 1;; ++suffix)
        {
            std::string candidate = base;
            if (suffix > 1)
                candidate += "~" + std::to_string(suffix);
            candidate += extension;
            if (CreateNew(m_Path / PathOf(candidate), extension.empty()))
                return candidate;
        }
    }

private:
    fs::path m_Path;
};

// One file of the source unpack writes.
struct OutputFile
{
    std::string Path;   // UTF-8, relative to the package file's folder, with '/' separators
    std::string Bytes;
};

//------------------------------------------------------------------------------------------------
// Payloads

struct alignas(kCpkgPayloadAlignment) PayloadBlock
{
    std::byte Bytes[kCpkgPayloadAlignment];
};

// One asset's payload, in a buffer aligned for the payload helpers.
class CPayload
{
public:
    // Reads the payload of the asset. Returns false when it cannot be read.
    bool Read(const CCpkgReader &reader, uint32_t bundleIndex, uint32_t assetIndex)
    {
        const uint64_t size = reader.GetAssetCompression(bundleIndex, assetIndex).RawSize;
        try
        {
            m_Blocks.assign(size_t((size + kCpkgPayloadAlignment - 1) / kCpkgPayloadAlignment), PayloadBlock{});
        }
        catch (const std::bad_alloc &)
        {
            return false;
        }
        catch (const std::length_error &)
        {
            return false;
        }
        m_Size = size_t(size);
        return size == 0 || Gem::Succeeded(reader.ReadAsset(bundleIndex, assetIndex, std::span(reinterpret_cast<std::byte *>(m_Blocks.data()), m_Size)));
    }

    std::span<const std::byte> Bytes() const
    {
        return { reinterpret_cast<const std::byte *>(m_Blocks.data()), m_Size };
    }

private:
    std::vector<PayloadBlock> m_Blocks;
    size_t m_Size = 0;
};

// A payload header or span that breaks the payload layout rules.
struct LayoutError
{
};

// Reads one payload, throwing LayoutError for what breaks the payload layout rules.
template <class THeader>
class CView
{
public:
    explicit CView(const CPayload &payload) :
        m_Payload(payload.Bytes())
    {
        if (Gem::Failed(CpkgGetPayloadHeader(m_Payload, &m_pHeader)))
            throw LayoutError{};
    }

    const THeader &Header() const { return *m_pHeader; }

    template <class T>
    std::span<const T> Elements(const CpkgSpan<T> &span) const
    {
        std::span<const T> elements;
        if (Gem::Failed(CpkgResolveSpan<THeader>(m_Payload, span, &elements)))
            throw LayoutError{};
        return elements;
    }

    std::string Text(const CpkgSpan<char> &span) const
    {
        const std::span<const char> chars = Elements(span);
        return std::string(chars.begin(), chars.end());
    }

private:
    std::span<const std::byte> m_Payload;
    const THeader *m_pHeader = nullptr;
};

// The numbers that elements hold, such as the 3 floats of each CpkgVertexPosition.
template <class TNumber, class TElement>
std::span<const TNumber> NumbersOf(std::span<const TElement> elements)
{
    static_assert(sizeof(TElement) % sizeof(TNumber) == 0);
    return { reinterpret_cast<const TNumber *>(elements.data()), elements.size() * (sizeof(TElement) / sizeof(TNumber)) };
}

bool SameBits(float a, float b)
{
    return std::bit_cast<uint32_t>(a) == std::bit_cast<uint32_t>(b);
}

bool SameBits(const float *pA, const float *pB, size_t count)
{
    return std::memcmp(pA, pB, count * sizeof(float)) == 0;
}

//------------------------------------------------------------------------------------------------
// The package

struct PackageAsset
{
    AssetType Type = AssetType::Unknown;
    std::string Name;
    CPayload Payload;
    bool Readable = false;
};

struct PackageBundle
{
    std::string Name;
    std::vector<PackageAsset> Assets;
};

// What an asset or a bundle holds that no source expresses.
struct Problem
{
    uint32_t Bundle = 0;
    uint32_t Asset = UINT32_MAX;    // UINT32_MAX for the bundle itself
    std::string Message;
};

// Collects problems, each once.
class CProblems
{
public:
    void Add(uint32_t bundle, uint32_t asset, std::string message)
    {
        const std::string key = std::to_string(bundle) + ":" + std::to_string(asset) + ":" + message;
        if (m_Keys.insert(key).second)
            m_Problems.push_back({ bundle, asset, std::move(message) });
    }

    bool Empty() const { return m_Problems.empty(); }

    // Reports each problem against package, in bundle and asset order.
    void Report(const std::vector<PackageBundle> &bundles, const std::string &package, CDiagnostics &diagnostics)
    {
        // A bundle's own problems, whose Asset UINT32_MAX wraps to 0, come before its assets'.
        std::stable_sort(m_Problems.begin(), m_Problems.end(), [](const Problem &a, const Problem &b)
        {
            return a.Bundle != b.Bundle ? a.Bundle < b.Bundle : uint32_t(a.Asset + 1) < uint32_t(b.Asset + 1);
        });
        for (const Problem &problem : m_Problems)
        {
            const PackageBundle &bundle = bundles[problem.Bundle];
            std::string where = "bundle " + Quoted(bundle.Name);
            if (problem.Asset != UINT32_MAX)
            {
                const PackageAsset &asset = bundle.Assets[problem.Asset];
                const char *pType = AssetTypeName(asset.Type);
                where += ", asset " + std::to_string(problem.Asset) + " ("
                    + (pType ? std::string(pType) : std::to_string(uint32_t(asset.Type)))
                    + (asset.Name.empty() ? std::string() : " " + Quoted(asset.Name)) + ")";
            }
            diagnostics.Report(Severity::Error, package, where + ": " + problem.Message);
        }
    }

private:
    std::vector<Problem> m_Problems;
    std::unordered_set<std::string> m_Keys;
};

//------------------------------------------------------------------------------------------------
// One bundle's source

// The large numeric arrays of one asset, written inline, or to one binary array file when they
// hold more than kInlineNumberLimit numbers.
struct ArrayFile
{
    std::string Name;           // the file's name in the bundle's folder; empty for inline arrays
    uint32_t Remaining = 0;     // non-empty arrays not yet written to the file
    std::string Bytes;
};

// A local asset of the bundle, and what attaches to it when it is a node.
struct Local
{
    int32_t Owner = -1;             // for an attachment, the node it attaches to
    int32_t TreeParent = -1;        // for a node, the node whose children hold it
    int32_t SkeletonInstance = -1;
    int32_t Camera = -1;
    int32_t Light = -1;
    std::vector<int32_t> Placements;
    std::vector<int32_t> MeshInstances;
    std::vector<int32_t> MeshFields;
    std::vector<int32_t> Children;
    Json Object;                    // the asset's own members
};

class CBundleUnpacker
{
public:
    CBundleUnpacker(const std::string &packageName, const std::vector<PackageBundle> &bundles, uint32_t bundleIndex,
        CProblems &problems) :
        m_PackageName(packageName),
        m_Bundles(bundles),
        m_Bundle(bundles[bundleIndex]),
        m_BundleIndex(bundleIndex),
        m_Problems(problems),
        m_Locals(m_Bundle.Assets.size())
    {
    }

    // Returns the bundle's source, and adds its binary array files, in folder, to *pFiles.
    Json Unpack(const std::string &folder, CFolder &files, std::vector<OutputFile> *pFiles)
    {
        m_pFolder = &files;
        if (m_Bundle.Name.starts_with('$'))
            m_Problems.Add(m_BundleIndex, UINT32_MAX, "its name begins with '$', which a source reserves for format keys");

        for (m_Asset = 0; m_Asset < m_Bundle.Assets.size(); ++m_Asset)
            Each(&CBundleUnpacker::Place);
        for (m_Asset = 0; m_Asset < m_Bundle.Assets.size(); ++m_Asset)
            Each(&CBundleUnpacker::Decode);

        for (ArrayFile &file : m_ArrayFiles)
            pFiles->push_back({ folder + "/" + file.Name, std::move(file.Bytes) });
        for (OutputFile &file : m_ImageFiles)
            pFiles->push_back({ folder + "/" + file.Path, std::move(file.Bytes) });
        return Assemble();
    }

private:
    const PackageAsset &Asset() const { return m_Bundle.Assets[m_Asset]; }
    Local &This() { return m_Locals[m_Asset]; }

    void Problem(std::string message)
    {
        m_Problems.Add(m_BundleIndex, uint32_t(m_Asset), std::move(message));
    }

    // Runs step on the current asset, reporting a payload that breaks the payload layout rules.
    void Each(void (CBundleUnpacker::*step)())
    {
        if (!Asset().Readable)
            return;
        try
        {
            (this->*step)();
        }
        catch (const LayoutError &)
        {
            Problem("its payload breaks the payload layout rules");
        }
    }

    bool IsType(int32_t index, AssetType type) const
    {
        return index >= 0 && size_t(index) < m_Bundle.Assets.size() && m_Bundle.Assets[size_t(index)].Type == type;
    }

    bool IsNode(int32_t index) const
    {
        return IsType(index, AssetType::TransformNode) || IsType(index, AssetType::ImportNode);
    }

    const std::string &NameOf(int32_t index) const { return m_Bundle.Assets[size_t(index)].Name; }

    // The name of the node at index, which a source refers to it by.
    Json NodeName(int32_t index, const char *pWhat)
    {
        if (NameOf(index).empty())
            Problem(std::string(pWhat) + " is an unnamed node, which a source cannot refer to");
        return NameOf(index);
    }

    //--------------------------------------------------------------------------------------------
    // Places each local asset in the hierarchy: what each node holds and attaches.

    void Place()
    {
        const AssetType type = Asset().Type;
        const int32_t index = int32_t(m_Asset);
        const bool named = !Asset().Name.empty();
        if (named && Asset().Name.starts_with('$'))
            Problem("its name begins with '$', which a source reserves for format keys");

        switch (type)
        {
        case AssetType::Image:
        case AssetType::Skybox:
        case AssetType::Material:
        case AssetType::MeshData:
        case AssetType::Skeleton:
        case AssetType::MeshSkin:
        case AssetType::SkeletalAnimation:
        case AssetType::AnimationClip:
            if (!named)
                Problem("it is unnamed, and a source names it by its key");
            break;

        case AssetType::TransformNode:
        {
            const CView<CpkgTransformNodeData> view(Asset().Payload);
            const CpkgTransformNodeData &node = view.Header();
            const int32_t parent = node.ParentIndex;
            int32_t treeParent = -1;
            if (node.ParentBone.Count != 0)
            {
                // A socket: its tree parent is the node its skeleton instance attaches to.
                if (!IsType(parent, AssetType::SkeletonInstance) || parent > index || m_Locals[size_t(parent)].Owner >= index
                    || m_Locals[size_t(parent)].Owner == -1)
                {
                    Problem("it names a parent bone, and its parent is not a skeleton instance on a node before it");
                    break;
                }
                treeParent = m_Locals[size_t(parent)].Owner;
            }
            else if (parent != -1)
            {
                if (!IsNode(parent) || parent > index)
                {
                    Problem("its parent is not a node before it");
                    break;
                }
                treeParent = parent;
            }
            This().TreeParent = treeParent;
            (treeParent == -1 ? m_Roots : m_Locals[size_t(treeParent)].Children).push_back(index);
            break;
        }

        case AssetType::ImportNode:
        {
            const CView<CpkgImportNodeData> view(Asset().Payload);
            const int32_t placement = view.Header().PlacementIndex;
            if (!IsType(placement, AssetType::Placement))
                Problem("its placement is not a placement of the bundle");
            else if (NameOf(placement).empty())
                Problem("its placement is unnamed, and a source names the placement an import node refers to");
            m_ImportNodes.push_back(index);
            break;
        }

        case AssetType::Placement:
        {
            const CView<CpkgPlacementData> view(Asset().Payload);
            Attach(view.Header().NodeIndex, &Local::Placements, &m_Placements);
            break;
        }

        case AssetType::SkeletonInstance:
        {
            const CView<CpkgSkeletonInstanceData> view(Asset().Payload);
            AttachOne(view.Header().NodeIndex, &Local::SkeletonInstance, "skeleton instance");
            break;
        }

        case AssetType::MeshInstance:
        {
            const CView<CpkgMeshInstanceData> view(Asset().Payload);
            Attach(view.Header().NodeIndex, &Local::MeshInstances, nullptr);
            break;
        }

        case AssetType::MeshField:
        {
            const CView<CpkgMeshFieldData> view(Asset().Payload);
            Attach(view.Header().NodeIndex, &Local::MeshFields, &m_MeshFields);
            break;
        }

        case AssetType::Camera:
        {
            const CView<CpkgCameraData> view(Asset().Payload);
            AttachOne(view.Header().NodeIndex, &Local::Camera, "camera");
            break;
        }

        case AssetType::Light:
        {
            const CView<CpkgLightData> view(Asset().Payload);
            AttachOne(view.Header().NodeIndex, &Local::Light, "light");
            break;
        }

        case AssetType::ExportList:
        case AssetType::WorldSettings:
        {
            int32_t &only = type == AssetType::ExportList ? m_Exports : m_World;
            if (only != -1)
                Problem(std::string("the bundle has another ") + (type == AssetType::ExportList ? "export list" : "WorldSettings") + ", and a source builds one");
            only = index;
            break;
        }

        default:
            Problem("its type is not one a source builds");
            return;
        }

        const bool sourceNames = type != AssetType::SkeletonInstance && type != AssetType::MeshInstance && type != AssetType::Camera
            && type != AssetType::Light && type != AssetType::ExportList && type != AssetType::WorldSettings;
        if (named && !sourceNames)
            Problem("it is named, and a source names no asset of its type");
    }

    // Attaches the current asset to node, or for node -1 to the bundle's own list pBundleList.
    void Attach(int32_t node, std::vector<int32_t> Local::*pList, std::vector<int32_t> *pBundleList)
    {
        if (node == -1 && pBundleList)
        {
            pBundleList->push_back(int32_t(m_Asset));
            return;
        }
        if (!IsNode(node))
        {
            Problem("it attaches to no node of the bundle");
            return;
        }
        This().Owner = node;
        (m_Locals[size_t(node)].*pList).push_back(int32_t(m_Asset));
    }

    // Attaches the current asset to node, which holds at most one of its kind.
    void AttachOne(int32_t node, int32_t Local::*pOnly, const char *pKind)
    {
        if (!IsNode(node))
        {
            Problem("it attaches to no node of the bundle");
            return;
        }
        int32_t &only = m_Locals[size_t(node)].*pOnly;
        if (only != -1)
            Problem(std::string("its node has another ") + pKind + ", and a source attaches one to a node");
        only = int32_t(m_Asset);
        This().Owner = node;
    }

    //--------------------------------------------------------------------------------------------
    // Values

    Json Number(float value)
    {
        if (!std::isfinite(value))
        {
            Problem("it holds a number that is not finite, which JSON cannot express");
            return 0;
        }

        return FloatJson(value);
    }

    Json Number(uint32_t value) { return uint64_t(value); }

    Json Vector(const float *pValues, size_t count)
    {
        Json vector = Json::array();
        for (size_t i = 0; i < count; ++i)
            vector.push_back(Number(pValues[i]));
        return vector;
    }

    Json Bounds(const CpkgAABB &bounds)
    {
        Json json = Json::object();
        json["min"] = Vector(bounds.Min, 3);
        json["max"] = Vector(bounds.Max, 3);
        return json;
    }

    // A rotation, which a source writes as a unit quaternion.
    Json Rotation(const float *pQuaternion)
    {
        if (!Math::FloatQuaternion(pQuaternion[0], pQuaternion[1], pQuaternion[2], pQuaternion[3]).IsNormalized())
            Problem("it holds a rotation that is not a unit quaternion, which no source expresses");
        return Vector(pQuaternion, 4);
    }

    // Adds the parts of transform other than the identity's to object.
    void AddTransform(Json &object, const CpkgTransform &transform)
    {
        const CpkgTransform identity;
        if (!SameBits(transform.Translation, identity.Translation, 3))
            object["translation"] = Vector(transform.Translation, 3);
        if (!SameBits(transform.Rotation, identity.Rotation, 4))
            object["rotation"] = Rotation(transform.Rotation);
        if (!SameBits(transform.Scale, identity.Scale, 3))
            object["scale"] = Vector(transform.Scale, 3);
    }

    Json Transform(const CpkgTransform &transform)
    {
        Json object = Json::object();
        AddTransform(object, transform);
        return object;
    }

    template <class T, size_t N>
    Json Enum(const EnumName<T> (&names)[N], T value, const char *pWhat)
    {
        const std::string_view name = EnumNameOf(names, value);
        if (name.empty())
            Problem(std::string("its ") + pWhat + ", " + std::to_string(uint32_t(value)) + ", is not one a source names");
        return std::string(name);
    }

    // Adds a mobility other than static to object.
    void AddMobility(Json &object, CpkgMobility mobility)
    {
        if (mobility != CpkgMobility::Static)
            object["mobility"] = Enum(kMobilities, mobility, "mobility");
    }

    //--------------------------------------------------------------------------------------------
    // Large numeric arrays

    // Begins the arrays of the current asset, which hold numbers numbers in arrays non-empty
    // arrays, written to a file with extension when there are more than kInlineNumberLimit.
    ArrayFile *BeginArrays(uint64_t numbers, uint32_t arrays, std::string_view extension)
    {
        if (numbers <= kInlineNumberLimit)
            return nullptr;
        ArrayFile &file = m_ArrayFiles.emplace_back();
        file.Name = m_pFolder->Add(Asset().Name, extension);
        file.Remaining = arrays;
        return &file;
    }

    // An array of numbers whose elements each hold components of them: inline, or the range of
    // pFile that holds them.
    template <class T>
    Json Array(ArrayFile *pFile, std::span<const T> numbers, size_t components)
    {
        if (!pFile || numbers.empty())
        {
            Json array = Json::array();
            for (T number : numbers)
                array.push_back(Number(number));
            return array;
        }

        Json range = Json::object();
        range["file"] = pFile->Name;
        if (!pFile->Bytes.empty())
            range["offset"] = uint64_t(pFile->Bytes.size());
        if (--pFile->Remaining > 0)
            range["count"] = uint64_t(numbers.size() / components);
        pFile->Bytes.append(reinterpret_cast<const char *>(numbers.data()), numbers.size_bytes());
        return range;
    }

    static void CountArray(uint64_t count, uint64_t *pNumbers, uint32_t *pArrays)
    {
        if (count == 0)
            return;
        *pNumbers += count;
        ++*pArrays;
    }

    template <class TTrack>
    static void CountTrack(const TTrack &track, uint64_t *pNumbers, uint32_t *pArrays)
    {
        CountArray(track.Times.Count, pNumbers, pArrays);
        CountArray(track.Values.Count, pNumbers, pArrays);
        CountArray(track.Tangents.Count, pNumbers, pArrays);
    }

    // A skeletal animation's or a clip's track, whose target is targetKey: target.
    template <class THeader, class TTrack>
    Json Track(const CView<THeader> &view, const TTrack &track, const char *targetKey, Json target, ArrayFile *pFile)
    {
        Json json = Json::object();
        json[targetKey] = std::move(target);
        json["property"] = Enum(kProperties, track.Property, "track property");

        const std::span<const float> times = view.Elements(track.Times);
        const std::span<const float> values = view.Elements(track.Values);
        json["times"] = Array(pFile, times, 1);
        json["values"] = Array(pFile, values, 1);
        if (track.Property == CpkgAnimationProperty::Rotation)
        {
            for (size_t i = 0; i + 3 < values.size(); i += 4)
                Rotation(&values[i]);
        }

        const std::span<const AnimationInterpolation> interpolations = view.Elements(track.Interpolations);
        bool cubic = false;
        if (!interpolations.empty())
        {
            const bool same = std::all_of(interpolations.begin(), interpolations.end(),
                [&](AnimationInterpolation i) { return i == interpolations[0]; });
            if (same && interpolations[0] == AnimationInterpolation::Linear)
                Problem("a track stores an interpolation for each key, all linear, and a build stores none for such a track");
            cubic = std::find(interpolations.begin(), interpolations.end(), AnimationInterpolation::Cubic) != interpolations.end();
            if (same && interpolations.size() == times.size())
                json["interpolation"] = Enum(kInterpolations, interpolations[0], "interpolation");
            else
            {
                Json list = Json::array();
                for (AnimationInterpolation interpolation : interpolations)
                    list.push_back(Enum(kInterpolations, interpolation, "interpolation"));
                json["interpolation"] = std::move(list);
            }
        }

        const std::span<const float> tangents = view.Elements(track.Tangents);
        if (!tangents.empty())
        {
            if (!cubic)
                Problem("a track stores tangents but no cubic key, and a build stores tangents only for a track with one");
            json["tangents"] = Array(pFile, tangents, 1);
        }
        return json;
    }

    //--------------------------------------------------------------------------------------------
    // Decodes each asset into the source that builds it.

    void Decode()
    {
        const std::string &name = Asset().Name;
        switch (Asset().Type)
        {
        case AssetType::Image:
            m_Images[name] = Image();
            break;
        case AssetType::Skybox:
            m_Skyboxes[name] = Skybox();
            break;
        case AssetType::Material:
            m_Materials[name] = Material();
            break;
        case AssetType::MeshData:
            m_Meshes[name] = Mesh();
            break;
        case AssetType::Skeleton:
            m_Skeletons[name] = Skeleton();
            break;
        case AssetType::MeshSkin:
            m_Skins[name] = Skin();
            break;
        case AssetType::SkeletalAnimation:
            m_SkeletalAnimations[name] = SkeletalAnimation();
            break;
        case AssetType::TransformNode:
            This().Object = TransformNode();
            break;
        case AssetType::ImportNode:
            This().Object = ImportNode();
            break;
        case AssetType::Placement:
            This().Object = Placement();
            break;
        case AssetType::SkeletonInstance:
        {
            const CView<CpkgSkeletonInstanceData> view(Asset().Payload);
            This().Object["skeleton"] = view.Text(view.Header().Skeleton);
            break;
        }
        case AssetType::MeshInstance:
            This().Object = MeshInstance();
            break;
        case AssetType::MeshField:
            This().Object = MeshField();
            break;
        case AssetType::Camera:
            This().Object = Camera();
            break;
        case AssetType::Light:
            This().Object = Light();
            break;
        case AssetType::AnimationClip:
            m_Clips[name] = Clip();
            break;
        case AssetType::ExportList:
            m_ExportsJson = Exports();
            break;
        case AssetType::WorldSettings:
            m_WorldJson = World();
            break;
        default:
            break;
        }
    }

    // An image, in the file form when it is one 2D image, the cubemap form when it is one cube of
    // single-mip faces, and otherwise the general form, each of its encoded images written
    // verbatim to a file of its own.
    Json Image()
    {
        const CView<CpkgImageData> view(Asset().Payload);
        const CpkgImageData &image = view.Header();
        const std::span<const CpkgSpan<uint8_t>> entries = view.Elements(image.Images);
        const char *pExtension = ImageExtension(image.Encoding);
        if (!pExtension)
        {
            Problem("it holds images in an encoding no source's image files hold");
            return Json();
        }

        auto file = [&](const std::string &suffix, const CpkgSpan<uint8_t> &entry)
        {
            const std::span<const uint8_t> bytes = view.Elements(entry);
            const std::string name = m_pFolder->Add(Asset().Name, suffix + pExtension);
            m_ImageFiles.push_back({ name, std::string(reinterpret_cast<const char *>(bytes.data()), bytes.size()) });
            return Json(name);
        };

        const bool single = image.MipLevels == 1;
        if (image.Dimension == CpkgImageDimension::Texture2D && image.ArraySize == 1 && single && entries.size() == 1)
            return file("", entries[0]);

        Json json = Json::object();
        if (image.Dimension == CpkgImageDimension::Cube && image.ArraySize == 6 && single && entries.size() == 6)
        {
            Json faces = Json::object();
            for (size_t face = 0; face < 6; ++face)
                faces[kCubeFaces[face]] = file(std::string(".") + kCubeFaces[face], entries[face]);
            json["cubemap"] = std::move(faces);
            return json;
        }

        json["dimension"] = Enum(kDimensions, image.Dimension, "dimension");
        if (image.ArraySize != 1)
            json["arraySize"] = Number(image.ArraySize);
        if (image.MipLevels != 1)
            json["mipLevels"] = Number(image.MipLevels);
        Json files = Json::array();
        for (size_t i = 0; i < entries.size(); ++i)
            files.push_back(file("." + std::to_string(i), entries[i]));
        json["files"] = std::move(files);
        return json;
    }

    Json Skybox()
    {
        const CView<CpkgSkyboxData> view(Asset().Payload);
        const CpkgSkyboxData &skybox = view.Header();
        const CpkgSkyboxData defaults;
        Json json = Json::object();
        json["image"] = view.Text(skybox.Image);
        if (!SameBits(skybox.Orientation, defaults.Orientation, 4))
            json["orientation"] = Rotation(skybox.Orientation);
        if (!SameBits(skybox.Intensity, defaults.Intensity))
            json["intensity"] = Number(skybox.Intensity);
        return json;
    }

    Json Material()
    {
        const CView<CpkgMaterialData> view(Asset().Payload);
        const CpkgMaterialData &material = view.Header();
        const CpkgMaterialData defaults;
        Json json = Json::object();
        if (!SameBits(material.BaseColorFactor, defaults.BaseColorFactor, 4))
            json["baseColor"] = Vector(material.BaseColorFactor, 4);
        if (!SameBits(material.EmissiveFactor, defaults.EmissiveFactor, 3))
            json["emissive"] = Vector(material.EmissiveFactor, 3);
        if (!SameBits(material.EmissiveFactor[3], 0.0f))
            Problem("its emissive factor has an alpha other than 0, which no source expresses");
        const char *factors[] = { "roughness", "metallic", "ambientOcclusion" };
        for (size_t i = 0; i < 3; ++i)
        {
            if (!SameBits(material.RoughMetalAOFactor[i], defaults.RoughMetalAOFactor[i]))
                json[factors[i]] = Number(material.RoughMetalAOFactor[i]);
        }
        if (!SameBits(material.RoughMetalAOFactor[3], 0.0f))
            Problem("its RoughMetalAOFactor has a fourth part other than 0, which no source expresses");

        const CpkgSpan<char> *textures[] = {
            &material.AlbedoTexture, &material.NormalTexture, &material.RoughnessTexture,
            &material.MetallicTexture, &material.AmbientOcclusionTexture, &material.EmissiveTexture,
        };
        static_assert(std::size(textures) == std::size(kTextureRoles));
        Json texturesJson = Json::object();
        for (size_t role = 0; role < std::size(textures); ++role)
        {
            if (textures[role]->Count != 0)
                texturesJson[kTextureRoles[role]] = view.Text(*textures[role]);
        }
        if (!texturesJson.empty())
            json["textures"] = std::move(texturesJson);

        const CpkgDisplacementData &displacement = material.Displacement;
        const CpkgDisplacementData none;
        if (material.HasDisplacement == 0)
        {
            if (std::memcmp(&displacement, &none, sizeof(none)) != 0)
                Problem("it holds displacement fields but HasDisplacement 0, which no source expresses");
        }
        else if (material.HasDisplacement == 1)
        {
            Json displacementJson = Json::object();
            displacementJson["map"] = view.Text(displacement.DisplacementMap);
            auto field = [&](const char *pKey, float value, float defaultValue)
            {
                if (!SameBits(value, defaultValue))
                    displacementJson[pKey] = Number(value);
            };
            field("scale", displacement.MapScale, none.MapScale);
            field("bias", displacement.MapBias, none.MapBias);
            field("minTessFactor", displacement.MinTessFactor, none.MinTessFactor);
            field("maxTessFactor", displacement.MaxTessFactor, none.MaxTessFactor);
            field("distanceLodScale", displacement.DistanceLodScale, none.DistanceLodScale);
            field("curvatureLodScale", displacement.CurvatureLodScale, none.CurvatureLodScale);
            json["displacement"] = std::move(displacementJson);
        }
        else
            Problem("its HasDisplacement is " + std::to_string(material.HasDisplacement) + ", and a source builds 0 or 1");
        return json;
    }

    Json Mesh()
    {
        const CView<CpkgMeshData> view(Asset().Payload);
        const CpkgMeshData &mesh = view.Header();
        const std::span<const CpkgMeshDataGroup> groups = view.Elements(mesh.Groups);

        uint64_t numbers = 0;
        uint32_t arrays = 0;
        for (const CpkgMeshDataGroup &group : groups)
        {
            CountArray(group.Positions.Count * 3, &numbers, &arrays);
            CountArray(group.Normals.Count * 3, &numbers, &arrays);
            CountArray(group.UV0.Count * 2, &numbers, &arrays);
            CountArray(group.Tangents.Count * 4, &numbers, &arrays);
            CountArray(group.BoneIndices.Count * 4, &numbers, &arrays);
            CountArray(group.BoneWeights.Count * 4, &numbers, &arrays);
        }
        ArrayFile *pFile = BeginArrays(numbers, arrays, ".mesh.bin");

        Json json = Json::object();
        if (mesh.Topology != GfxPrimitiveTopology::TriangleList)
            json["topology"] = Enum(kTopologies, mesh.Topology, "topology");

        // The bounds a build computes from the positions, as it computes them.
        CpkgAABB computed;
        Json groupsJson = Json::array();
        for (const CpkgMeshDataGroup &group : groups)
        {
            Json groupJson = Json::object();
            if (group.Material.Count != 0)
                groupJson["material"] = view.Text(group.Material);
            const std::span<const float> positions = NumbersOf<float>(view.Elements(group.Positions));
            for (size_t i = 0; i + 2 < positions.size(); i += 3)
            {
                for (size_t axis = 0; axis < 3; ++axis)
                {
                    computed.Min[axis] = std::min(computed.Min[axis], positions[i + axis]);
                    computed.Max[axis] = std::max(computed.Max[axis], positions[i + axis]);
                }
            }
            groupJson["positions"] = Array(pFile, positions, 3);
            auto stream = [&](const char *pKey, const auto &span, size_t components)
            {
                using TNumber = std::conditional_t<std::is_same_v<std::decay_t<decltype(span)>, CpkgSpan<CpkgVertexBoneIndices>>, uint32_t, float>;
                if (span.Count != 0)
                    groupJson[pKey] = Array(pFile, NumbersOf<TNumber>(view.Elements(span)), components);
            };
            stream("normals", group.Normals, 3);
            stream("uv0", group.UV0, 2);
            stream("tangents", group.Tangents, 4);
            stream("boneIndices", group.BoneIndices, 4);
            stream("boneWeights", group.BoneWeights, 4);
            groupsJson.push_back(std::move(groupJson));
        }

        const CpkgAABB none;
        const auto sameBounds = [](const CpkgAABB &a, const CpkgAABB &b) { return SameBits(a.Min, b.Min, 3) && SameBits(a.Max, b.Max, 3); };
        if (!sameBounds(mesh.LocalBounds, computed))
            json["bounds"] = sameBounds(mesh.LocalBounds, none) ? Json("none") : Bounds(mesh.LocalBounds);
        json["groups"] = std::move(groupsJson);
        return json;
    }

    Json Skeleton()
    {
        const CView<CpkgSkeletonData> view(Asset().Payload);
        const std::span<const CpkgSkeletonBone> bones = view.Elements(view.Header().Bones);
        Json bonesJson = Json::array();
        for (size_t i = 0; i < bones.size(); ++i)
        {
            const CpkgSkeletonBone &bone = bones[i];
            Json json = Json::object();
            json["name"] = view.Text(bone.Name);
            if (bone.ParentIndex != -1)
            {
                if (bone.ParentIndex < 0 || size_t(bone.ParentIndex) >= i)
                    Problem("a bone's parent is not an earlier bone");
                else
                    json["parent"] = view.Text(bones[size_t(bone.ParentIndex)].Name);
            }
            Json rest = Transform(bone.RestTransform);
            if (!rest.empty())
                json["rest"] = std::move(rest);
            bonesJson.push_back(std::move(json));
        }
        Json json = Json::object();
        json["bones"] = std::move(bonesJson);
        return json;
    }

    Json Skin()
    {
        const CView<CpkgMeshSkinData> view(Asset().Payload);
        const CpkgMeshSkinData &skin = view.Header();
        Json json = Json::object();
        json["skeleton"] = view.Text(skin.Skeleton);
        Json bones = Json::array();
        for (const CpkgSpan<char> &bone : view.Elements(skin.Bones))
            bones.push_back(view.Text(bone));
        json["bones"] = std::move(bones);
        Json poses = Json::array();
        for (const CpkgTransform &pose : view.Elements(skin.BindPoses))
            poses.push_back(Transform(pose));
        json["bindPoses"] = std::move(poses);
        return json;
    }

    Json SkeletalAnimation()
    {
        const CView<CpkgSkeletalAnimationData> view(Asset().Payload);
        const std::span<const CpkgBoneTrack> tracks = view.Elements(view.Header().Tracks);
        uint64_t numbers = 0;
        uint32_t arrays = 0;
        for (const CpkgBoneTrack &track : tracks)
            CountTrack(track, &numbers, &arrays);
        ArrayFile *pFile = BeginArrays(numbers, arrays, ".anim.bin");

        Json json = Json::object();
        json["skeleton"] = view.Text(view.Header().Skeleton);
        Json tracksJson = Json::array();
        for (const CpkgBoneTrack &track : tracks)
            tracksJson.push_back(Track(view, track, "bone", view.Text(track.Bone), pFile));
        json["tracks"] = std::move(tracksJson);
        return json;
    }

    Json TransformNode()
    {
        const CView<CpkgTransformNodeData> view(Asset().Payload);
        const CpkgTransformNodeData &node = view.Header();
        Json json = Json::object();
        if (!Asset().Name.empty())
            json["name"] = Asset().Name;
        AddTransform(json, node.LocalTransform);
        AddMobility(json, node.Mobility);
        if (node.ParentBone.Count != 0)
            json["bone"] = view.Text(node.ParentBone);
        return json;
    }

    Json ImportNode()
    {
        const CView<CpkgImportNodeData> view(Asset().Payload);
        const CpkgImportNodeData &node = view.Header();
        Json json = Json::object();
        if (!Asset().Name.empty())
            json["name"] = Asset().Name;
        json["placement"] = IsType(node.PlacementIndex, AssetType::Placement) ? NameOf(node.PlacementIndex) : std::string();
        json["export"] = view.Text(node.ExportName);
        AddTransform(json, node.LocalTransform);
        return json;
    }

    Json Placement()
    {
        const CView<CpkgPlacementData> view(Asset().Payload);
        const CpkgPlacementData &placement = view.Header();
        Json json = Json::object();
        if (!Asset().Name.empty())
            json["name"] = Asset().Name;
        json["bundle"] = view.Text(placement.Bundle);
        AddMobility(json, placement.Mobility);
        AddTransform(json, placement.Transform);
        return json;
    }

    // The skeleton instance a build binds a skinned mesh instance on node to by default: the one
    // on the nearest node, from node toward its root node, that has one.
    int32_t DefaultSkeletonInstance(int32_t node) const
    {
        for (int32_t at = node; at != -1; at = m_Locals[size_t(at)].TreeParent)
        {
            if (m_Locals[size_t(at)].SkeletonInstance != -1)
                return m_Locals[size_t(at)].SkeletonInstance;
        }
        return -1;
    }

    Json MeshInstance()
    {
        const CView<CpkgMeshInstanceData> view(Asset().Payload);
        const CpkgMeshInstanceData &instance = view.Header();
        Json json = Json::object();
        json["mesh"] = view.Text(instance.MeshData);
        if (instance.Skin.Count == 0)
        {
            if (instance.SkeletonInstanceIndex != -1)
                Problem("it binds a skeleton instance but has no skin, which no source expresses");
            return json;
        }

        json["skin"] = view.Text(instance.Skin);
        const int32_t bound = instance.SkeletonInstanceIndex;
        if (!IsType(bound, AssetType::SkeletonInstance))
            Problem("it has a skin but binds no skeleton instance");
        else if (This().Owner != -1 && bound != DefaultSkeletonInstance(This().Owner) && m_Locals[size_t(bound)].Owner != -1)
            json["skeletonInstance"] = NodeName(m_Locals[size_t(bound)].Owner, "the node of the skeleton instance it binds");
        return json;
    }

    Json MeshField()
    {
        const CView<CpkgMeshFieldData> view(Asset().Payload);
        const CpkgMeshFieldData &field = view.Header();
        Json json = Json::object();
        if (!Asset().Name.empty())
            json["name"] = Asset().Name;
        json["mesh"] = view.Text(field.MeshData);
        if (!SameBits(field.CullDistance, 0.0f))
            json["cullDistance"] = Number(field.CullDistance);
        Json transforms = Json::array();
        for (const CpkgTransform &transform : view.Elements(field.Transforms))
            transforms.push_back(Transform(transform));
        json["transforms"] = std::move(transforms);
        return json;
    }

    Json Camera()
    {
        const CView<CpkgCameraData> view(Asset().Payload);
        const CpkgCameraData &camera = view.Header();
        const CpkgCameraData defaults;
        Json json = Json::object();
        auto field = [&](const char *pKey, float value, float defaultValue)
        {
            if (!SameBits(value, defaultValue))
                json[pKey] = Number(value);
        };
        field("nearClip", camera.NearClip, defaults.NearClip);
        field("farClip", camera.FarClip, defaults.FarClip);
        field("fovAngle", camera.FovAngle, defaults.FovAngle);
        field("exposureStops", camera.ExposureStops, defaults.ExposureStops);
        return json;
    }

    Json Light()
    {
        const CView<CpkgLightData> view(Asset().Payload);
        const CpkgLightData &light = view.Header();
        const CpkgLightData defaults;
        Json json = Json::object();
        auto field = [&](const char *pKey, float value, float defaultValue)
        {
            if (!SameBits(value, defaultValue))
                json[pKey] = Number(value);
        };

        json["type"] = Enum(kLightTypes, light.Type, "light type");
        if (!SameBits(light.Color, defaults.Color, 3))
            json["color"] = Vector(light.Color, 3);
        if (!SameBits(light.Color[3], 1.0f))
            Problem("its color has an alpha other than 1, which no source expresses");
        field("intensity", light.Intensity, defaults.Intensity);

        const uint32_t flags = uint32_t(light.Flags);
        const uint32_t enabled = uint32_t(LightFlags::Enabled);
        const uint32_t castsShadows = uint32_t(LightFlags::CastsShadows);
        if ((flags & ~(enabled | castsShadows)) != 0)
            Problem("its flags hold bits other than Enabled and CastsShadows, which no source expresses");
        if ((flags & enabled) == 0)
            json["enabled"] = false;
        if ((flags & castsShadows) != 0)
            json["castsShadows"] = true;

        field("range", light.Range, defaults.Range);
        field("attenuationConstant", light.AttenuationConstant, defaults.AttenuationConstant);
        field("attenuationLinear", light.AttenuationLinear, defaults.AttenuationLinear);
        field("attenuationQuadratic", light.AttenuationQuadratic, defaults.AttenuationQuadratic);
        field("spotInnerAngle", light.SpotInnerAngle, defaults.SpotInnerAngle);
        field("spotOuterAngle", light.SpotOuterAngle, defaults.SpotOuterAngle);
        if (light.ShadowResolution != defaults.ShadowResolution)
            json["shadowResolution"] = Number(light.ShadowResolution);
        field("shadowConstantBias", light.ShadowConstantBias, defaults.ShadowConstantBias);
        field("shadowSlopeScaleBias", light.ShadowSlopeScaleBias, defaults.ShadowSlopeScaleBias);
        field("shadowNormalOffset", light.ShadowNormalOffset, defaults.ShadowNormalOffset);
        return json;
    }

    // The latest key time of the skeletal animation at path, written in this bundle, when it is
    // in this package and readable.
    std::optional<float> AnimationEnd(std::string_view path) const
    {
        const std::vector<std::string_view> names = PathNames(path);
        const PackageBundle *pBundle = &m_Bundle;
        if (names.size() > 3 || (names.size() == 3 && names[0] != m_PackageName))
            return std::nullopt;
        if (names.size() > 1)
        {
            const auto found = std::find_if(m_Bundles.begin(), m_Bundles.end(),
                [&](const PackageBundle &bundle) { return bundle.Name == names[names.size() - 2]; });
            if (found == m_Bundles.end())
                return std::nullopt;
            pBundle = &*found;
        }

        for (const PackageAsset &asset : pBundle->Assets)
        {
            if (asset.Type != AssetType::SkeletalAnimation || asset.Name != names.back() || !asset.Readable)
                continue;
            try
            {
                const CView<CpkgSkeletalAnimationData> view(asset.Payload);
                float end = 0.0f;
                for (const CpkgBoneTrack &track : view.Elements(view.Header().Tracks))
                {
                    const std::span<const float> times = view.Elements(track.Times);
                    if (!times.empty())
                        end = std::max(end, times.back());
                }
                return end;
            }
            catch (const LayoutError &)
            {
                return std::nullopt;
            }
        }
        return std::nullopt;
    }

    Json Clip()
    {
        const CView<CpkgAnimationClipData> view(Asset().Payload);
        const CpkgAnimationClipData &clip = view.Header();
        const std::span<const CpkgNodeTrack> tracks = view.Elements(clip.Tracks);
        uint64_t numbers = 0;
        uint32_t arrays = 0;
        for (const CpkgNodeTrack &track : tracks)
            CountTrack(track, &numbers, &arrays);
        ArrayFile *pFile = BeginArrays(numbers, arrays, ".clip.bin");

        // The duration a build computes: the latest key time of the clip's tracks and of the
        // skeletal animations it binds, when the package holds them.
        float duration = 0.0f;
        bool computable = true;
        Json tracksJson = Json::array();
        for (const CpkgNodeTrack &track : tracks)
        {
            Json node;
            if (!IsType(track.NodeIndex, AssetType::TransformNode))
                Problem("a track's node is not a TransformNode of the bundle");
            else
                node = NodeName(track.NodeIndex, "a track's node");
            tracksJson.push_back(Track(view, track, "node", std::move(node), pFile));
            const std::span<const float> times = view.Elements(track.Times);
            if (!times.empty())
                duration = std::max(duration, times.back());
        }

        Json bindings = Json::array();
        for (const CpkgSkeletalAnimationBinding &binding : view.Elements(clip.SkeletalAnimations))
        {
            Json json = Json::object();
            const int32_t instance = binding.SkeletonInstanceIndex;
            if (!IsType(instance, AssetType::SkeletonInstance) || m_Locals[size_t(instance)].Owner == -1)
                Problem("a skeletal animation binding's skeleton instance is not a skeleton instance on a node of the bundle");
            else
                json["skeletonInstance"] = NodeName(m_Locals[size_t(instance)].Owner, "the node of a binding's skeleton instance");
            const std::string animation = view.Text(binding.Animation);
            json["animation"] = animation;
            if (const std::optional<float> end = AnimationEnd(animation))
                duration = std::max(duration, *end);
            else
                computable = false;
            bindings.push_back(std::move(json));
        }

        Json json = Json::object();
        if (!computable || !SameBits(clip.Duration, duration))
            json["duration"] = Number(clip.Duration);
        if (!tracksJson.empty())
            json["tracks"] = std::move(tracksJson);
        if (!bindings.empty())
            json["skeletalAnimations"] = std::move(bindings);
        return json;
    }

    Json Exports()
    {
        const CView<CpkgExportListData> view(Asset().Payload);
        Json json = Json::array();
        for (int32_t node : view.Elements(view.Header().NodeIndices))
        {
            if (!IsNode(node))
                Problem("an exported node is not a node of the bundle");
            else
                json.push_back(NodeName(node, "an exported node"));
        }
        return json;
    }

    Json World()
    {
        const CView<CpkgWorldSettingsData> view(Asset().Payload);
        const CpkgWorldSettingsData &world = view.Header();
        const CpkgWorldSettingsData defaults;
        Json json = Json::object();

        Json regions = Json::array();
        for (const CpkgWorldRegionRef &region : view.Elements(world.Regions))
        {
            Json regionJson = Json::object();
            regionJson["region"] = view.Text(region.Region);
            regionJson["bounds"] = Bounds(region.StreamingBounds);
            regions.push_back(std::move(regionJson));
        }
        if (!regions.empty())
            json["regions"] = std::move(regions);

        const std::span<const CpkgSpan<char>> skyboxes = view.Elements(world.Skyboxes);
        if (!skyboxes.empty())
        {
            Json skyboxesJson = Json::array();
            for (const CpkgSpan<char> &skybox : skyboxes)
                skyboxesJson.push_back(view.Text(skybox));
            json["skyboxes"] = std::move(skyboxesJson);
        }
        if (world.DefaultSkybox != -1)
        {
            if (world.DefaultSkybox < 0 || size_t(world.DefaultSkybox) >= skyboxes.size())
                Problem("its DefaultSkybox is not a position in its Skyboxes");
            else
                json["defaultSkybox"] = view.Text(skyboxes[size_t(world.DefaultSkybox)]);
        }

        if (world.ActiveCameraIndex != -1)
        {
            const int32_t camera = world.ActiveCameraIndex;
            if (!IsType(camera, AssetType::Camera) || m_Locals[size_t(camera)].Owner == -1)
                Problem("its active camera is not a camera on a node of the bundle");
            else
                json["activeCamera"] = NodeName(m_Locals[size_t(camera)].Owner, "the node of its active camera");
        }
        if (!SameBits(world.BackgroundColor, defaults.BackgroundColor, 4))
            json["backgroundColor"] = Vector(world.BackgroundColor, 4);
        return json;
    }

    //--------------------------------------------------------------------------------------------
    // Assembles the bundle's source from what Decode found.

    // Moves the objects of the local assets at indices into an array.
    Json TakeObjects(const std::vector<int32_t> &indices)
    {
        Json array = Json::array();
        for (int32_t index : indices)
            array.push_back(std::move(m_Locals[size_t(index)].Object));
        return array;
    }

    // The node at index, with what it holds and attaches.
    Json AssembleNode(int32_t index)
    {
        Local &local = m_Locals[size_t(index)];
        Json node = std::move(local.Object);
        if (local.SkeletonInstance != -1)
            node["skeletonInstance"] = std::move(m_Locals[size_t(local.SkeletonInstance)].Object);
        if (!local.MeshInstances.empty())
            node["meshInstances"] = TakeObjects(local.MeshInstances);
        if (!local.MeshFields.empty())
            node["meshFields"] = TakeObjects(local.MeshFields);
        if (local.Camera != -1)
            node["camera"] = std::move(m_Locals[size_t(local.Camera)].Object);
        if (local.Light != -1)
            node["light"] = std::move(m_Locals[size_t(local.Light)].Object);
        if (!local.Placements.empty())
            node["placements"] = TakeObjects(local.Placements);
        if (!local.Children.empty())
        {
            Json children = Json::array();
            for (int32_t child : local.Children)
                children.push_back(AssembleNode(child));
            node["children"] = std::move(children);
        }
        return node;
    }

    Json Assemble()
    {
        Json bundle = Json::object();
        bundle["name"] = m_Bundle.Name;
        auto map = [&](const char *pKey, Json &assets)
        {
            if (!assets.empty())
                bundle[pKey] = std::move(assets);
        };
        map("images", m_Images);
        map("materials", m_Materials);
        map("meshes", m_Meshes);
        map("skeletons", m_Skeletons);
        map("skins", m_Skins);
        map("skeletalAnimations", m_SkeletalAnimations);
        map("skyboxes", m_Skyboxes);

        auto nodes = [&](const char *pKey, const std::vector<int32_t> &indices)
        {
            if (indices.empty())
                return;
            Json array = Json::array();
            for (int32_t index : indices)
                array.push_back(AssembleNode(index));
            bundle[pKey] = std::move(array);
        };
        nodes("nodes", m_Roots);
        if (!m_Placements.empty())
            bundle["placements"] = TakeObjects(m_Placements);
        if (!m_MeshFields.empty())
            bundle["meshFields"] = TakeObjects(m_MeshFields);
        nodes("importNodes", m_ImportNodes);
        map("clips", m_Clips);
        if (m_Exports != -1)
            bundle["exports"] = std::move(m_ExportsJson);
        if (m_World != -1)
            bundle["world"] = std::move(m_WorldJson);
        return bundle;
    }

    const std::string &m_PackageName;
    const std::vector<PackageBundle> &m_Bundles;
    const PackageBundle &m_Bundle;
    uint32_t m_BundleIndex;
    CProblems &m_Problems;
    CFolder *m_pFolder = nullptr;
    size_t m_Asset = 0;

    std::vector<Local> m_Locals;    // per asset of the bundle
    std::vector<int32_t> m_Roots;
    std::vector<int32_t> m_Placements;
    std::vector<int32_t> m_MeshFields;
    std::vector<int32_t> m_ImportNodes;
    int32_t m_Exports = -1;
    int32_t m_World = -1;

    Json m_Images = Json::object();
    Json m_Materials = Json::object();
    Json m_Meshes = Json::object();
    Json m_Skeletons = Json::object();
    Json m_Skins = Json::object();
    Json m_SkeletalAnimations = Json::object();
    Json m_Skyboxes = Json::object();
    Json m_Clips = Json::object();
    Json m_ExportsJson;
    Json m_WorldJson;
    std::vector<ArrayFile> m_ArrayFiles;
    std::vector<OutputFile> m_ImageFiles;   // each Path in the bundle's folder
};

//------------------------------------------------------------------------------------------------
// The command

class CUnpacker
{
public:
    CUnpacker(const UnpackOptions &options, CDiagnostics &diagnostics) :
        m_Options(options),
        m_Diagnostics(diagnostics)
    {
    }

    void Run()
    {
        if (!Read())
            return;

        const std::string output = m_Options.Output.value_or(m_Options.Package + ".json");
        const fs::path outputPath = PathOf(output);
        m_Folder = outputPath.has_parent_path() ? outputPath.parent_path() : fs::path(".");
        m_PackageFile = Utf8Of(outputPath.filename());

        // A failure leaves no folder the output's folder did not have.
        fs::path created;
        if (MakeDirectories(m_Folder, &created) != FileResult::Success)
        {
            m_Diagnostics.Report(Severity::Error, Utf8Of(m_Folder), "cannot create the folder");
            return;
        }

        // The files are named in the temporary folder, beside the output and so on its file
        // system, which decides which names collide.
        bool moved = false;
        {
            const CScopedDirectory temporary(outputPath);
            if (!temporary.IsCreated())
                m_Diagnostics.Report(Severity::Error, Utf8Of(temporary.GetPath()), "cannot create the folder");
            else
            {
                const fs::path &directory = temporary.GetPath();
                std::vector<OutputFile> files;
                moved = Generate(directory, &files) && CheckExisting(files) && WriteFiles(directory, files)
                    && Verify(directory) && MoveFiles(directory, files);
            }
        }
        if (!moved && !created.empty())
            Remove(created);
    }

private:
    bool Read()
    {
        const Gem::Result result = m_Reader.Open(m_Options.Package.c_str());
        if (Gem::Failed(result))
        {
            m_Diagnostics.Report(Severity::Error, m_Options.Package, OpenFailure(result, m_Options.Package));
            return false;
        }

        m_PackageName = m_Reader.GetPackageName();
        m_Bundles.resize(m_Reader.GetBundleCount());
        for (uint32_t b = 0; b < m_Bundles.size(); ++b)
        {
            PackageBundle &bundle = m_Bundles[b];
            bundle.Name = m_Reader.GetBundleName(b);
            bundle.Assets.resize(m_Reader.GetAssetCount(b));
            for (uint32_t i = 0; i < bundle.Assets.size(); ++i)
            {
                PackageAsset &asset = bundle.Assets[i];
                asset.Type = m_Reader.GetAssetType(b, i);
                asset.Name = m_Reader.GetAssetName(b, i);
                if (m_Reader.GetAssetCompression(b, i).Codec != CpkgCodec::None)
                    m_Problems.Add(b, i, "its payload is compressed, and a build stores payloads uncompressed");
                else if (!(asset.Readable = asset.Payload.Read(m_Reader, b, i)))
                    m_Problems.Add(b, i, "its payload could not be read");
            }
        }
        return true;
    }

    // Generates the package source's files. Returns false, having reported each problem, when
    // the package holds what no source expresses.
    // Generates the source's files, naming them in folder.
    bool Generate(const fs::path &folder, std::vector<OutputFile> *pFiles)
    {
        try
        {
            return GenerateIn(folder, pFiles);
        }
        catch (const FolderError &e)
        {
            m_Diagnostics.Report(Severity::Error, Utf8Of(e.Path), "cannot create the file");
            return false;
        }
    }

    bool GenerateIn(const fs::path &folder, std::vector<OutputFile> *pFiles)
    {
        if (m_PackageName.starts_with('$'))
            m_Diagnostics.Report(Severity::Error, m_Options.Package, "the package's name begins with '$', which a source reserves for format keys");

        CFolder root(folder);
        root.Reserve(m_PackageFile);
        Json bundles = Json::array();
        std::vector<OutputFile> bundleFiles;
        for (uint32_t b = 0; b < m_Bundles.size(); ++b)
        {
            const std::string bundleFolder = root.Add(m_Bundles[b].Name, "");
            CFolder files(folder / PathOf(bundleFolder));
            const std::string bundleFile = bundleFolder + "/" + files.Add(m_Bundles[b].Name, ".cbundle.json");

            Json document = Json::object();
            document["formatVersion"] = 1;
            document["bundle"] = CBundleUnpacker(m_PackageName, m_Bundles, b, m_Problems).Unpack(bundleFolder, files, &bundleFiles);
            bundleFiles.push_back({ bundleFile, JsonFormat(document) });

            Json include = Json::object();
            include["include"] = bundleFile;
            bundles.push_back(std::move(include));
        }

        Json document = Json::object();
        document["formatVersion"] = 1;
        document["packageName"] = m_PackageName;
        document["bundles"] = std::move(bundles);
        pFiles->push_back({ m_PackageFile, JsonFormat(document) });
        for (OutputFile &file : bundleFiles)
            pFiles->push_back(std::move(file));

        if (m_Problems.Empty())
            return m_Diagnostics.GetErrorCount() == 0;
        m_Problems.Report(m_Bundles, m_Options.Package, m_Diagnostics);
        return false;
    }

    std::string Destination(const OutputFile &file) const
    {
        return Utf8Of(m_Folder / PathOf(file.Path));
    }

    // Without --force, reports each file that exists. Returns true when unpack may write them.
    bool CheckExisting(const std::vector<OutputFile> &files)
    {
        if (m_Options.Force)
            return true;
        bool clear = true;
        for (const OutputFile &file : files)
        {
            if (Exists(m_Folder / PathOf(file.Path)))
            {
                m_Diagnostics.Report(Severity::Error, Destination(file), "the file exists; --force replaces it");
                clear = false;
            }
        }
        return clear;
    }

    bool WriteFiles(const fs::path &folder, const std::vector<OutputFile> &files)
    {
        for (const OutputFile &file : files)
        {
            const fs::path path = folder / PathOf(file.Path);
            if (MakeDirectories(path.parent_path()) != FileResult::Success || WriteWholeFile(path, file.Bytes) != FileResult::Success)
            {
                m_Diagnostics.Report(Severity::Error, Utf8Of(path), "cannot write the file");
                return false;
            }
        }
        return true;
    }

    // Builds the source in folder and checks that it reproduces the package.
    bool Verify(const fs::path &folder)
    {
        const std::string packageFile = Utf8Of(folder / PathOf(m_PackageFile));
        const std::string built = packageFile.substr(0, packageFile.size() - 5);

        std::ostringstream findings;
        CDiagnostics buildDiagnostics(findings, false, true);
        SourcePackage source;
        const bool succeeded = LoadPackageSource(packageFile, &source, buildDiagnostics)
            && TranslatePackageSource(source, buildDiagnostics)
            && ValidatePackageSource(source, buildDiagnostics)
            && buildDiagnostics.GetErrorCount() == 0
            && WritePackage(source, built, buildDiagnostics);
        if (!succeeded)
        {
            // The findings name the temporary folder's files; they are reported at the files'
            // places in the output's folder.
            const std::string from = DisplayPath(Utf8Of(folder)) + "/";
            const std::string folderText = DisplayPath(Utf8Of(m_Folder));
            const std::string to = folderText == "." || folderText.empty() ? std::string() : folderText + "/";
            std::istringstream lines(findings.str());
            for (std::string line; std::getline(lines, line);)
            {
                if (line.starts_with(from))
                    line = to + line.substr(from.size());
                m_Diagnostics.Report(Severity::Error, m_Options.Package, "the source written for the package does not build: " + line);
            }
            return false;
        }

        if (SameBytes(PathOf(m_Options.Package), PathOf(built)))
            return true;
        ReportDifferences(built);
        return false;
    }

    // Reports each asset the source built from the package does not reproduce.
    void ReportDifferences(const std::string &built)
    {
        CCpkgReader reader;
        if (Gem::Failed(reader.Open(built.c_str())))
        {
            m_Diagnostics.Report(Severity::Error, m_Options.Package, "the package the source builds cannot be read");
            return;
        }

        const uint32_t bundleCount = std::min<uint32_t>(reader.GetBundleCount(), uint32_t(m_Bundles.size()));
        for (uint32_t b = 0; b < bundleCount; ++b)
        {
            const std::vector<PackageAsset> &assets = m_Bundles[b].Assets;
            const uint32_t count = std::min<uint32_t>(reader.GetAssetCount(b), uint32_t(assets.size()));
            bool ordered = true;
            for (uint32_t i = 0; i < count && ordered; ++i)
            {
                if (reader.GetAssetType(b, i) != assets[i].Type || reader.GetAssetName(b, i) != assets[i].Name)
                {
                    m_Problems.Add(b, i, "it is not in the order a build appends the bundle's assets");
                    ordered = false;
                    break;
                }
                CPayload payload;
                const std::span<const std::byte> original = assets[i].Payload.Bytes();
                if (!payload.Read(reader, b, i) || payload.Bytes().size() != original.size()
                    || std::memcmp(payload.Bytes().data(), original.data(), original.size()) != 0)
                {
                    m_Problems.Add(b, i, "its payload holds what no source expresses, and a build of its source writes another");
                }
            }
            if (ordered && reader.GetAssetCount(b) != assets.size())
                m_Problems.Add(b, UINT32_MAX, "a build of its source appends another number of assets");
        }

        if (m_Problems.Empty())
            m_Diagnostics.Report(Severity::Error, m_Options.Package, "a build of its source reproduces each asset, but not the file's layout");
        else
            m_Problems.Report(m_Bundles, m_Options.Package, m_Diagnostics);
    }

    bool MoveFiles(const fs::path &folder, const std::vector<OutputFile> &files)
    {
        for (const OutputFile &file : files)
        {
            const fs::path destination = m_Folder / PathOf(file.Path);
            if (MakeDirectories(destination.parent_path()) != FileResult::Success
                || Rename(folder / PathOf(file.Path), destination) != FileResult::Success)
            {
                m_Diagnostics.Report(Severity::Error, Destination(file), "cannot replace the file");
                return false;
            }
        }
        return true;
    }

    const UnpackOptions &m_Options;
    CDiagnostics &m_Diagnostics;
    CCpkgReader m_Reader;
    std::string m_PackageName;
    std::vector<PackageBundle> m_Bundles;
    CProblems m_Problems;
    fs::path m_Folder;              // the folder of the package file
    std::string m_PackageFile;      // the package file's name
};

} // namespace

//------------------------------------------------------------------------------------------------
void Unpack(const UnpackOptions &options, CDiagnostics &diagnostics)
{
    CUnpacker(options, diagnostics).Run();
}

} // namespace CpkgTool
