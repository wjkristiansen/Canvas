//================================================================================================
// CpkgBuildTest - Tests for cpkg's Expand, Validate, and Write stages, through the build command
//================================================================================================

#include "pch.h"
#include "CanvasPkg.h"
#include "CpkgTool.h"
#include "ImageFiles.h"
#include "SchemaCheck.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace
{
    // Writes text to path as is, with no line-ending translation.
    void WriteText(const std::string &path, std::string_view text)
    {
        std::ofstream file(std::filesystem::path(path), std::ios::binary);
        file.write(text.data(), std::streamsize(text.size()));
    }

    // The bytes of the file at path; empty when it cannot be read.
    std::string ReadBytes(const char *path)
    {
        std::ifstream file(path, std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    }

    // What a cpkg run returned and printed.
    struct RunResult
    {
        int Status = -1;
        std::string Err;
    };

    // Runs cpkg with args, collecting its standard output and error into Err.
    RunResult RunCpkg(std::vector<std::string> args)
    {
        args.insert(args.begin(), "cpkg");
        std::vector<const char *> argv;
        for (const std::string &arg : args)
            argv.push_back(arg.c_str());
        std::ostringstream out;
        std::ostringstream err;
        RunResult result;
        result.Status = CpkgTool::Run(int(argv.size()), argv.data(), out, err);
        result.Err = out.str() + err.str();
        return result;
    }

    // One asset's payload, in a buffer aligned for the payload helpers.
    class CPayload
    {
    public:
        CPayload(const CCpkgReader &reader, uint32_t bundleIndex, uint32_t assetIndex)
        {
            m_Size = size_t(reader.GetAssetCompression(bundleIndex, assetIndex).RawSize);
            m_Blocks.resize((m_Size + sizeof(Block) - 1) / sizeof(Block));
            EXPECT_EQ(reader.ReadAsset(bundleIndex, assetIndex, std::as_writable_bytes(std::span(m_Blocks))), Result::Success);
        }

        std::span<const std::byte> Bytes() const { return std::as_bytes(std::span(m_Blocks)).first(m_Size); }

        template <class THeader>
        const THeader &Header() const
        {
            const THeader *pHeader = nullptr;
            EXPECT_EQ(CpkgGetPayloadHeader(Bytes(), &pHeader), Result::Success);
            return *pHeader;
        }

        template <class THeader, class T>
        std::span<const T> Elements(const CpkgSpan<T> &span) const
        {
            std::span<const T> elements;
            EXPECT_EQ((CpkgResolveSpan<THeader>(Bytes(), span, &elements)), Result::Success);
            return elements;
        }

        template <class THeader>
        std::string Text(const CpkgSpan<char> &span) const
        {
            const std::span<const char> chars = Elements<THeader>(span);
            return std::string(chars.begin(), chars.end());
        }

    private:
        struct alignas(kCpkgPayloadAlignment) Block
        {
            std::byte Bytes[kCpkgPayloadAlignment];
        };
        std::vector<Block> m_Blocks;
        size_t m_Size = 0;
    };

    //--------------------------------------------------------------------------------------------
    // FBX files

    // An ASCII FBX array member: name, the number of values, and values.
    std::string FbxArray(std::string_view name, std::string_view values)
    {
        const size_t count = size_t(std::count(values.begin(), values.end(), ',')) + 1;
        return "\t\t" + std::string(name) + ": *" + std::to_string(count) + " {\n\t\t\ta: " + std::string(values) + "\n\t\t}\n";
    }

    // An ASCII FBX file of objects and connections, Z up with its models facing -Y, as Blender
    // writes, in units of unitScale centimeters.
    std::string FbxFile(std::string_view objects, std::string_view connections, int unitScale = 100)
    {
        return "; FBX 7.5.0 project file\n"
            "FBXHeaderExtension:  {\n\tFBXHeaderVersion: 1003\n\tFBXVersion: 7500\n}\n"
            "GlobalSettings:  {\n\tVersion: 1000\n\tProperties70:  {\n"
            "\t\tP: \"UpAxis\", \"int\", \"Integer\", \"\",2\n"
            "\t\tP: \"UpAxisSign\", \"int\", \"Integer\", \"\",1\n"
            "\t\tP: \"FrontAxis\", \"int\", \"Integer\", \"\",1\n"
            "\t\tP: \"FrontAxisSign\", \"int\", \"Integer\", \"\",-1\n"
            "\t\tP: \"CoordAxis\", \"int\", \"Integer\", \"\",0\n"
            "\t\tP: \"CoordAxisSign\", \"int\", \"Integer\", \"\",1\n"
            "\t\tP: \"UnitScaleFactor\", \"double\", \"Number\", \"\"," + std::to_string(unitScale) + "\n"
            "\t}\n}\n"
            "Objects:  {\n" + std::string(objects) + "}\n"
            "Connections:  {\n" + std::string(connections) + "}\n";
    }

    // A property of an FBX object: name, its type, and values.
    std::string FbxProperty(std::string_view name, std::string_view type, std::string_view values)
    {
        return "\t\t\tP: \"" + std::string(name) + "\", \"" + std::string(type) + "\", \"\", \"A\"," + std::string(values) + "\n";
    }

    // A model named name, object id, of type, with properties, such as FbxProperty("Lcl Translation", ...) gives.
    std::string FbxModel(int id, std::string_view name, std::string_view type, std::string_view properties = "")
    {
        return "\tModel: " + std::to_string(id) + ", \"Model::" + std::string(name) + "\", \"" + std::string(type) + "\" {\n\t\tVersion: 232\n"
            + (properties.empty() ? std::string() : "\t\tProperties70:  {\n" + std::string(properties) + "\t\t}\n") + "\t}\n";
    }

    // A camera or light attribute, object id, of class "Camera" or "Light", with properties.
    std::string FbxAttribute(int id, std::string_view kind, std::string_view properties)
    {
        return "\tNodeAttribute: " + std::to_string(id) + ", \"NodeAttribute::\", \"" + std::string(kind) + "\" {\n"
            "\t\tProperties70:  {\n" + std::string(properties) + "\t\t}\n\t\tTypeFlags: \"" + std::string(kind) + "\"\n\t}\n";
    }

    // A connection of object child to object parent; 0 is the root.
    std::string FbxConnection(int child, int parent)
    {
        return "\tC: \"OO\"," + std::to_string(child) + "," + std::to_string(parent) + "\n";
    }

    // A connection of object child to the property named property of object parent.
    std::string FbxPropertyConnection(int child, int parent, std::string_view property)
    {
        return "\tC: \"OP\"," + std::to_string(child) + "," + std::to_string(parent) + ",\"" + std::string(property) + "\"\n";
    }

    // An animation stack, object id, named name, ending at stop, in FBX time.
    std::string FbxAnimationStack(int id, std::string_view name, std::string_view stop)
    {
        return "\tAnimationStack: " + std::to_string(id) + ", \"AnimStack::" + std::string(name) + "\", \"\" {\n\t\tProperties70:  {\n"
            + FbxProperty("LocalStart", "KTime", "0") + FbxProperty("LocalStop", "KTime", stop)
            + FbxProperty("ReferenceStart", "KTime", "0") + FbxProperty("ReferenceStop", "KTime", stop) + "\t\t}\n\t}\n";
    }

    // A curve node, object id, whose X, Y, and Z default to xyz.
    std::string FbxCurveNode(int id, std::string_view xyz)
    {
        const std::string values(xyz);
        const size_t first = values.find(',');
        const size_t second = values.find(',', first + 1);
        return "\tAnimationCurveNode: " + std::to_string(id) + ", \"AnimCurveNode::T\", \"\" {\n\t\tProperties70:  {\n"
            + FbxProperty("d|X", "Number", values.substr(0, first)) + FbxProperty("d|Y", "Number", values.substr(first + 1, second - first - 1))
            + FbxProperty("d|Z", "Number", values.substr(second + 1)) + "\t\t}\n\t}\n";
    }

    // A curve, object id, with keys at times, in FBX time, of values, every key with the
    // interpolation flags: 4 linear, 2 constant.
    std::string FbxCurve(int id, std::string_view times, std::string_view values, std::string_view flags)
    {
        return "\tAnimationCurve: " + std::to_string(id) + ", \"AnimCurve::\", \"\" {\n\t\tDefault: 0\n\t\tKeyVer: 4009\n"
            + FbxArray("KeyTime", times) + FbxArray("KeyValueFloat", values) + FbxArray("KeyAttrFlags", flags)
            + FbxArray("KeyAttrDataFloat", "0,0,0,0") + FbxArray("KeyAttrRefCount", "2") + "\t}\n";
    }

    // A mesh model named name, object id + 1, with model properties, and its geometry, object
    // id, which holds the members of layers after its vertices and polygons.
    std::string FbxMesh(int id, std::string_view name, std::string_view vertices, std::string_view polygons, std::string_view layers = "",
        std::string_view properties = "")
    {
        return "\tGeometry: " + std::to_string(id) + ", \"Geometry::" + std::string(name) + "\", \"Mesh\" {\n"
            + FbxArray("Vertices", vertices) + FbxArray("PolygonVertexIndex", polygons) + std::string(layers) + "\t}\n"
            + FbxModel(id + 1, name, "Mesh", properties);
    }

    // The connections of FbxMesh's model to the root and its geometry to the model.
    std::string FbxMeshConnections(int id)
    {
        return "\tC: \"OO\"," + std::to_string(id + 1) + ",0\n\tC: \"OO\"," + std::to_string(id) + "," + std::to_string(id + 1) + "\n";
    }

    // A phong material named name, object id, with a diffuse color.
    std::string FbxMaterial(int id, std::string_view name, std::string_view diffuse)
    {
        return "\tMaterial: " + std::to_string(id) + ", \"Material::" + std::string(name) + "\", \"\" {\n\t\tVersion: 102\n\t\tShadingModel: \"phong\"\n"
            "\t\tProperties70:  {\n\t\t\tP: \"DiffuseColor\", \"Color\", \"\", \"A\"," + std::string(diffuse) + "\n\t\t}\n\t}\n";
    }

    // A texture named name, object id, recording file as its relative file name.
    std::string FbxTexture(int id, std::string_view name, std::string_view file)
    {
        return "\tTexture: " + std::to_string(id) + ", \"Texture::" + std::string(name) + "\", \"\" {\n\t\tType: \"TextureVideoClip\"\n\t\tVersion: 202\n"
            "\t\tFileName: \"C:/nowhere/" + std::string(file) + "\"\n\t\tRelativeFilename: \"" + std::string(file) + "\"\n\t}\n";
    }

    std::string Base64(std::string_view bytes)
    {
        constexpr char kDigits[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        std::string text;
        for (size_t i = 0; i < bytes.size(); i += 3)
        {
            uint32_t group = uint32_t(uint8_t(bytes[i])) << 16;
            if (i + 1 < bytes.size())
                group |= uint32_t(uint8_t(bytes[i + 1])) << 8;
            if (i + 2 < bytes.size())
                group |= uint32_t(uint8_t(bytes[i + 2]));
            text += kDigits[(group >> 18) & 63];
            text += kDigits[(group >> 12) & 63];
            text += i + 1 < bytes.size() ? kDigits[(group >> 6) & 63] : '=';
            text += i + 2 < bytes.size() ? kDigits[group & 63] : '=';
        }
        return text;
    }

    // A video, object id, embedding bytes and recording file as its file name.
    std::string FbxVideo(int id, std::string_view file, std::string_view bytes)
    {
        return "\tVideo: " + std::to_string(id) + ", \"Video::" + std::string(file) + "\", \"Clip\" {\n\t\tType: \"Clip\"\n"
            "\t\tFileName: \"C:/nowhere/" + std::string(file) + "\"\n\t\tRelativeFilename: \"" + std::string(file) + "\"\n"
            "\t\tContent: , \"" + Base64(bytes) + "\"\n\t}\n";
    }

    // A quad of 1 m in the file's XZ plane, facing -Y, the direction its models face.
    const std::string kQuad = FbxFile(FbxMesh(100, "Quad", "0,0,0,1,0,0,1,0,1,0,0,1", "0,1,2,-4"), FbxMeshConnections(100));

    // A 4x4 FBX matrix that translates by x, y, z.
    std::string FbxTranslationMatrix(std::string_view xyz)
    {
        return "1,0,0,0,0,1,0,0,0,0,1,0," + std::string(xyz) + ",1";
    }

    // A bone model named name, object id + 1, translated by xyz, and its attribute, object id.
    std::string FbxBone(int id, std::string_view name, std::string_view xyz)
    {
        return "\tNodeAttribute: " + std::to_string(id) + ", \"NodeAttribute::\", \"LimbNode\" {\n\t\tTypeFlags: \"Skeleton\"\n\t}\n"
            + FbxModel(id + 1, name, "LimbNode", FbxProperty("Lcl Translation", "Lcl Translation", xyz));
    }

    // A skin cluster, object id, weighting the vertices indexes by weights to a bone bound at the
    // translation boneWorld, of a mesh bound at the origin. Its Transform is the mesh's space
    // relative to the bone's: the translation toBone, the negation of boneWorld.
    std::string FbxCluster(int id, std::string_view indexes, std::string_view weights, std::string_view boneWorld, std::string_view toBone)
    {
        return "\tDeformer: " + std::to_string(id) + ", \"SubDeformer::\", \"Cluster\" {\n\t\tVersion: 100\n"
            + FbxArray("Indexes", indexes) + FbxArray("Weights", weights)
            + FbxArray("Transform", FbxTranslationMatrix(toBone)) + FbxArray("TransformLink", FbxTranslationMatrix(boneWorld)) + "\t}\n";
    }

    // A file, in meters, whose root node Armature holds the bone Hips, 1 m up, its child bone
    // Hand, 0.5 m along +X from it, and the node Sword, on Hand, and whose mesh Body, a triangle
    // under Armature, is skinned to Hips and Hand, its last vertex by weights that sum to 0.75.
    // Its root node Tail is a bone of its own. With animated, it holds the action Wave as two
    // stacks, as Blender writes it: "Hips|Wave", which moves Hips 1 m along +X over 1 s, and
    // "Sword|Wave", which steps Sword from 0.25 to 0.5 along +Y at 1 s.
    std::string SkinnedFbx(bool animated = false)
    {
        constexpr const char *kSecond = "46186158000";
        const std::string animation = !animated ? std::string() :
            FbxAnimationStack(800, "Hips|Wave", kSecond) + FbxAnimationStack(810, "Sword|Wave", kSecond)
                + "\tAnimationLayer: 801, \"AnimLayer::\", \"\" {\n\t}\n\tAnimationLayer: 811, \"AnimLayer::\", \"\" {\n\t}\n"
                + FbxCurveNode(802, "0,0,1") + FbxCurve(803, std::string("0,") + kSecond, "0,1", "4")
                + FbxCurveNode(812, "0,0.25,0") + FbxCurve(813, std::string("0,") + kSecond, "0.25,0.5", "2");
        const std::string animationConnections = !animated ? std::string() :
            FbxConnection(801, 800) + FbxConnection(802, 801) + FbxPropertyConnection(802, 511, "Lcl Translation") + FbxPropertyConnection(803, 802, "d|X")
                + FbxConnection(811, 810) + FbxConnection(812, 811) + FbxPropertyConnection(812, 531, "Lcl Translation") + FbxPropertyConnection(813, 812, "d|Y");
        return FbxFile(
            FbxModel(501, "Armature", "Null")
                + FbxBone(510, "Hips", "0,0,1") + FbxBone(520, "Hand", "0.5,0,0")
                + FbxModel(531, "Sword", "Null", FbxProperty("Lcl Translation", "Lcl Translation", "0,0.25,0"))
                + FbxBone(700, "Tail", "0,2,0")
                + FbxMesh(100, "Body", "0,0,0,1,0,0,0,0,1", "0,1,-3")
                + "\tDeformer: 600, \"Deformer::\", \"Skin\" {\n\t\tVersion: 101\n\t}\n"
                + FbxCluster(610, "0,1,2", "1,1,0.25", "0,0,1", "0,0,-1") + FbxCluster(620, "2", "0.5", "0.5,0,1", "-0.5,0,-1")
                + animation,
            FbxConnection(501, 0) + FbxConnection(510, 511) + FbxConnection(511, 501) + FbxConnection(520, 521) + FbxConnection(521, 511)
                + FbxConnection(531, 521) + FbxConnection(700, 701) + FbxConnection(701, 0)
                + FbxConnection(100, 101) + FbxConnection(101, 501)
                + FbxConnection(600, 100) + FbxConnection(610, 600) + FbxConnection(620, 600) + FbxConnection(511, 610) + FbxConnection(521, 620)
                + animationConnections);
    }

    // A file in centimeters whose root node Body holds a quad 1 m across and the node Lamp, and
    // whose root node Eye holds a camera with a vertical field of view of 60 degrees. With folded, it holds its geometry and Lamp's
    // translation in meters, and its root nodes are scaled by 100, as Blender writes.
    std::string NodesFbx(bool folded)
    {
        const std::string rootScale = folded ? FbxProperty("Lcl Scaling", "Lcl Scaling", "100,100,100") : "";
        const std::string quad = folded ? "0,0,0,1,0,0,1,0,1,0,0,1" : "0,0,0,100,0,0,100,0,100,0,0,100";
        return FbxFile(
            FbxMesh(100, "Body", quad, "0,1,2,-4", "", FbxProperty("Lcl Translation", "Lcl Translation", "0,0,200") + rootScale)
                + FbxModel(201, "Lamp", "Light", FbxProperty("Lcl Translation", "Lcl Translation", folded ? "1,0,0" : "100,0,0"))
                + FbxAttribute(200, "Light", FbxProperty("LightType", "enum", "0") + FbxProperty("Intensity", "Number", "50")
                    + FbxProperty("Color", "Color", "1,0.5,0.25"))
                + FbxModel(301, "Eye", "Camera", rootScale)
                + FbxAttribute(300, "Camera", FbxProperty("NearPlane", "Number", "10") + FbxProperty("FarPlane", "Number", "1000")
                    + FbxProperty("ApertureMode", "enum", "2") + FbxProperty("FieldOfView", "FieldOfView", "60")),
            FbxMeshConnections(100) + FbxConnection(201, 101) + FbxConnection(200, 201) + FbxConnection(301, 0) + FbxConnection(300, 301), 1);
    }

    // The index of the asset of the bundle named name; the asset count when none is.
    uint32_t AssetIndex(const CCpkgReader &reader, uint32_t bundle, std::string_view name)
    {
        uint32_t i = 0;
        while (i < reader.GetAssetCount(bundle) && reader.GetAssetName(bundle, i) != name)
            ++i;
        return i;
    }

    // The index of the asset of type, of the bundle, that attaches to the node at nodeIndex; the
    // asset count when none does.
    template <class TData>
    uint32_t AttachedIndex(const CCpkgReader &reader, uint32_t bundle, AssetType type, uint32_t nodeIndex)
    {
        for (uint32_t i = 0; i < reader.GetAssetCount(bundle); ++i)
        {
            if (reader.GetAssetType(bundle, i) == type && CPayload(reader, bundle, i).Header<TData>().NodeIndex == int32_t(nodeIndex))
                return i;
        }
        return reader.GetAssetCount(bundle);
    }
}

//------------------------------------------------------------------------------------------------
// Runs each test in its own empty current directory, so paths in diagnostics are relative to it.
class CpkgBuild : public ::testing::Test
{
protected:
    void SetUp() override
    {
        const ::testing::TestInfo *pInfo = ::testing::UnitTest::GetInstance()->current_test_info();
        m_Dir = std::filesystem::temp_directory_path() / (std::string("CpkgBuild_") + pInfo->name());
        std::filesystem::remove_all(m_Dir);
        std::filesystem::create_directories(m_Dir);
        m_PreviousDir = std::filesystem::current_path();
        std::filesystem::current_path(m_Dir);
    }

    void TearDown() override
    {
        std::filesystem::current_path(m_PreviousDir);
        std::error_code ignored;
        std::filesystem::remove_all(m_Dir, ignored);
    }

private:
    std::filesystem::path m_Dir;
    std::filesystem::path m_PreviousDir;
};

//------------------------------------------------------------------------------------------------
TEST_F(CpkgBuild, WritesSharedAssets)
{
    WriteText("levels.cpkg.json", R"({
  "formatVersion": 1,
  "packageName": "Levels",
  "bundles": [
    {
      "name": "Looks",
      "materials": {
        "Brass": { "baseColor": [0.9, 0.7, 0.3, 1], "emissive": [0.1, 0.2, 0.3], "roughness": 0.35, "metallic": 1,
                   "textures": { "albedo": "Textures/Metals/BrassAlbedo" },
                   "displacement": { "map": "Textures/Metals/BrassHeight", "scale": 0.02 } },
        "Plain": {}
      },
      "skyboxes": { "Day": { "image": "Textures/Skies/Day", "orientation": [0, 0, 0.38268343, 0.92387953], "intensity": 1.5 } },
      "meshes": {
        "Tri": { "groups": [
          { "material": "Brass", "positions": [0, 0, 0, 1, 0, 0, 1, 1, 0], "normals": [0, 0, 1, 0, 0, 1, 0, 0, 1] },
          { "positions": [-1, 2, -3], "normals": [0, 0, 1], "uv0": [0.5, 0.5] } ] },
        "Boxed": { "bounds": { "min": [-1, -1, -1], "max": [1, 1, 1] }, "groups": [ { "positions": [0, 0, 0], "normals": [0, 0, 1] } ] },
        "Unbounded": { "bounds": "none", "groups": [ { "positions": [0, 0, 0], "normals": [0, 0, 1] } ] }
      }
    },
    {
      "name": "Rig",
      "skeletons": { "Goblin": { "bones": [
        { "name": "Hips", "rest": { "translation": [0, 0, 1] } },
        { "name": "Spine", "parent": "Hips", "rest": { "translation": [0, 0, 0.5], "rotation": [0, 0, 0, 1] } } ] } },
      "skins": {
        "GoblinSkin": { "skeleton": "Goblin", "bones": ["Spine", "Hips"], "bindPoses": [ { "translation": [0, 0, 1.5] }, { "translation": [0, 0, 1] } ] },
        "BorrowedSkin": { "skeleton": "Others/Rigs/Troll", "bones": ["Root"], "bindPoses": [ { "rotation": [1, 0, 0, 0], "scale": [2, 2, 2] } ] }
      },
      "skeletalAnimations": { "Walk": { "skeleton": "Goblin", "tracks": [
        { "bone": "Hips", "property": "rotation", "times": [0, 1], "values": [0, 0, 0, 1, 0, 0, 1, 0] },
        { "bone": "Spine", "property": "translation", "times": [0, 1], "values": [0, 0, 0, 0, 0, 1], "interpolation": "step" },
        { "bone": "Spine", "property": "scale", "times": [0, 1], "values": [1, 1, 1, 2, 2, 2],
          "interpolation": ["cubic", "linear"], "tangents": [0, 0, 0, 1, 1, 1, 1, 1, 1, 0, 0, 0] } ] } }
    },
    { "name": "Empty" }
  ]
}
)");

    const RunResult result = RunCpkg({ "build", "levels.cpkg.json" });
    EXPECT_TRUE(MatchesSchema(CanvasSchema::PackageSource, "levels.cpkg.json"));
    EXPECT_EQ(result.Err, "");
    ASSERT_EQ(result.Status, CpkgTool::kExitSuccess);

    CCpkgReader reader;
    ASSERT_EQ(reader.Open("levels.cpkg"), Result::Success);
    EXPECT_EQ(reader.GetPackageName(), "Levels");
    ASSERT_EQ(reader.GetBundleCount(), 3u);
    EXPECT_EQ(reader.GetBundleName(0), "Looks");
    EXPECT_EQ(reader.GetBundleName(2), "Empty");
    EXPECT_EQ(reader.GetAssetCount(2), 0u);

    // Canonical order: skyboxes, materials, then meshes, each in source order.
    ASSERT_EQ(reader.GetAssetCount(0), 6u);
    const std::pair<AssetType, std::string_view> looks[] = {
        { AssetType::Skybox, "Day" }, { AssetType::Material, "Brass" }, { AssetType::Material, "Plain" },
        { AssetType::MeshData, "Tri" }, { AssetType::MeshData, "Boxed" }, { AssetType::MeshData, "Unbounded" },
    };
    for (uint32_t i = 0; i < 6; ++i)
    {
        EXPECT_EQ(reader.GetAssetType(0, i), looks[i].first) << i;
        EXPECT_EQ(reader.GetAssetName(0, i), looks[i].second) << i;
        EXPECT_EQ(reader.GetAssetCompression(0, i).Codec, CpkgCodec::None) << i;
    }

    {
        const CPayload payload(reader, 0, 0);
        const CpkgSkyboxData &skybox = payload.Header<CpkgSkyboxData>();
        EXPECT_EQ(payload.Text<CpkgSkyboxData>(skybox.Image), "Textures/Skies/Day");
        EXPECT_EQ(skybox.Orientation[2], 0.38268343f);
        EXPECT_EQ(skybox.Orientation[3], 0.92387953f);
        EXPECT_EQ(skybox.Intensity, 1.5f);
    }
    {
        const CPayload payload(reader, 0, 1);
        const CpkgMaterialData &brass = payload.Header<CpkgMaterialData>();
        EXPECT_EQ(brass.BaseColorFactor[1], 0.7f);
        EXPECT_EQ(brass.EmissiveFactor[2], 0.3f);
        EXPECT_EQ(brass.EmissiveFactor[3], 0.0f);
        EXPECT_EQ(brass.RoughMetalAOFactor[0], 0.35f);
        EXPECT_EQ(brass.RoughMetalAOFactor[1], 1.0f);
        EXPECT_EQ(brass.RoughMetalAOFactor[2], 1.0f);
        EXPECT_EQ(payload.Text<CpkgMaterialData>(brass.AlbedoTexture), "Textures/Metals/BrassAlbedo");
        EXPECT_EQ(brass.NormalTexture.Count, 0u);
        EXPECT_EQ(brass.HasDisplacement, 1u);
        EXPECT_EQ(payload.Text<CpkgMaterialData>(brass.Displacement.DisplacementMap), "Textures/Metals/BrassHeight");
        EXPECT_EQ(brass.Displacement.MapScale, 0.02f);
        EXPECT_EQ(brass.Displacement.MaxTessFactor, 32.0f);
    }
    {
        const CPayload payload(reader, 0, 2);
        const CpkgMaterialData &plain = payload.Header<CpkgMaterialData>();
        EXPECT_EQ(plain.BaseColorFactor[0], 1.0f);
        EXPECT_EQ(plain.RoughMetalAOFactor[0], 1.0f);
        EXPECT_EQ(plain.HasDisplacement, 0u);
        EXPECT_EQ(plain.Displacement.DisplacementMap.Count, 0u);
    }
    {
        const CPayload payload(reader, 0, 3);
        const CpkgMeshData &tri = payload.Header<CpkgMeshData>();
        EXPECT_EQ(tri.Topology, GfxPrimitiveTopology::TriangleList);
        EXPECT_EQ(tri.LocalBounds.Min[0], -1.0f);
        EXPECT_EQ(tri.LocalBounds.Max[1], 2.0f);
        EXPECT_EQ(tri.LocalBounds.Min[2], -3.0f);
        EXPECT_EQ(tri.LocalBounds.Max[2], 0.0f);
        const std::span<const CpkgMeshDataGroup> groups = payload.Elements<CpkgMeshData>(tri.Groups);
        ASSERT_EQ(groups.size(), 2u);
        EXPECT_EQ(payload.Text<CpkgMeshData>(groups[0].Material), "Brass");
        const std::span<const CpkgVertexPosition> positions = payload.Elements<CpkgMeshData>(groups[0].Positions);
        ASSERT_EQ(positions.size(), 3u);
        EXPECT_EQ(positions[2].XYZ[1], 1.0f);
        EXPECT_EQ(groups[0].Normals.Count, 3u);
        EXPECT_EQ(groups[0].UV0.Count, 0u);
        EXPECT_EQ(groups[1].Material.Count, 0u);
        EXPECT_EQ(payload.Elements<CpkgMeshData>(groups[1].UV0)[0].UV[1], 0.5f);
    }
    {
        const CPayload payload(reader, 0, 4);
        EXPECT_EQ(payload.Header<CpkgMeshData>().LocalBounds.Max[0], 1.0f);
        const CPayload unbounded(reader, 0, 5);
        EXPECT_EQ(unbounded.Header<CpkgMeshData>().LocalBounds.Min[0], FLT_MAX);
        EXPECT_EQ(unbounded.Header<CpkgMeshData>().LocalBounds.Max[0], -FLT_MAX);
    }

    ASSERT_EQ(reader.GetAssetCount(1), 4u);
    EXPECT_EQ(reader.GetAssetType(1, 0), AssetType::Skeleton);
    EXPECT_EQ(reader.GetAssetType(1, 1), AssetType::MeshSkin);
    EXPECT_EQ(reader.GetAssetName(1, 2), "BorrowedSkin");
    EXPECT_EQ(reader.GetAssetType(1, 3), AssetType::SkeletalAnimation);
    {
        const CPayload payload(reader, 1, 0);
        const std::span<const CpkgSkeletonBone> bones = payload.Elements<CpkgSkeletonData>(payload.Header<CpkgSkeletonData>().Bones);
        ASSERT_EQ(bones.size(), 2u);
        EXPECT_EQ(bones[0].ParentIndex, -1);
        EXPECT_EQ(bones[1].ParentIndex, 0);
        EXPECT_EQ(payload.Text<CpkgSkeletonData>(bones[1].Name), "Spine");
        EXPECT_EQ(bones[1].RestTransform.Translation[2], 0.5f);
        EXPECT_EQ(bones[1].RestTransform.Rotation[3], 1.0f);
        EXPECT_EQ(bones[0].RestTransform.Scale[0], 1.0f);
    }
    {
        const CPayload payload(reader, 1, 1);
        const CpkgMeshSkinData &skin = payload.Header<CpkgMeshSkinData>();
        EXPECT_EQ(payload.Text<CpkgMeshSkinData>(skin.Skeleton), "Goblin");
        const std::span<const CpkgSpan<char>> bones = payload.Elements<CpkgMeshSkinData>(skin.Bones);
        ASSERT_EQ(bones.size(), 2u);
        EXPECT_EQ(payload.Text<CpkgMeshSkinData>(bones[0]), "Spine");
        const std::span<const CpkgTransform> poses = payload.Elements<CpkgMeshSkinData>(skin.BindPoses);
        ASSERT_EQ(poses.size(), 2u);
        EXPECT_EQ(poses[0].Translation[2], 1.5f);
        EXPECT_EQ(poses[0].Rotation[3], 1.0f);
        EXPECT_EQ(poses[0].Scale[0], 1.0f);
        EXPECT_EQ(poses[1].Translation[2], 1.0f);
    }
    {
        const CPayload payload(reader, 1, 2);
        const std::span<const CpkgTransform> poses = payload.Elements<CpkgMeshSkinData>(payload.Header<CpkgMeshSkinData>().BindPoses);
        ASSERT_EQ(poses.size(), 1u);
        EXPECT_EQ(poses[0].Rotation[0], 1.0f);
        EXPECT_EQ(poses[0].Rotation[3], 0.0f);
        EXPECT_EQ(poses[0].Scale[1], 2.0f);
    }
    {
        const CPayload payload(reader, 1, 3);
        const CpkgSkeletalAnimationData &walk = payload.Header<CpkgSkeletalAnimationData>();
        EXPECT_EQ(payload.Text<CpkgSkeletalAnimationData>(walk.Skeleton), "Goblin");
        const std::span<const CpkgBoneTrack> tracks = payload.Elements<CpkgSkeletalAnimationData>(walk.Tracks);
        ASSERT_EQ(tracks.size(), 3u);

        // Rotation keys are stored as written; every key is linear, so no interpolations are stored.
        EXPECT_EQ(payload.Text<CpkgSkeletalAnimationData>(tracks[0].Bone), "Hips");
        EXPECT_EQ(tracks[0].Property, CpkgAnimationProperty::Rotation);
        const std::span<const float> rotations = payload.Elements<CpkgSkeletalAnimationData>(tracks[0].Values);
        ASSERT_EQ(rotations.size(), 8u);
        EXPECT_EQ(rotations[3], 1.0f);
        EXPECT_EQ(rotations[6], 1.0f);
        EXPECT_EQ(tracks[0].Interpolations.Count, 0u);
        EXPECT_EQ(tracks[0].Tangents.Count, 0u);

        // One interpolation for every key is stored per key.
        const std::span<const AnimationInterpolation> steps = payload.Elements<CpkgSkeletalAnimationData>(tracks[1].Interpolations);
        EXPECT_EQ(steps.size(), 2u);
        EXPECT_EQ(steps[1], AnimationInterpolation::Step);

        EXPECT_EQ(tracks[2].Interpolations.Count, 2u);
        EXPECT_EQ(tracks[2].Tangents.Count, 12u);
        EXPECT_EQ(payload.Elements<CpkgSkeletalAnimationData>(tracks[2].Tangents)[3], 1.0f);
    }

    // -o names the output, and a second build replaces it.
    EXPECT_EQ(RunCpkg({ "build", "levels.cpkg.json", "-o", "out.cpkg" }).Status, CpkgTool::kExitSuccess);
    EXPECT_EQ(RunCpkg({ "build", "levels.cpkg.json", "--output", "out.cpkg" }).Status, CpkgTool::kExitSuccess);
    // Builds of the same source are byte for byte the same.
    reader.Close();
    EXPECT_FALSE(ReadBytes("out.cpkg").empty());
    EXPECT_EQ(ReadBytes("out.cpkg"), ReadBytes("levels.cpkg"));
}

//------------------------------------------------------------------------------------------------
TEST_F(CpkgBuild, ReplacesOutputOnlyWithNewCompletePackage)
{
    auto source = [](std::string_view brass)
    {
        const std::string text = R"({ "formatVersion": 1, "packageName": "Levels", "bundles": [ { "name": "Looks", "materials": { "Brass": )"
            + std::string(brass) + " } } ] }";
        EXPECT_TRUE(TextMatchesSchema(CanvasSchema::PackageSource, text));
        return text;
    };
    WriteText("levels.cpkg.json", source("{}"));
    ASSERT_EQ(RunCpkg({ "build", "levels.cpkg.json" }).Status, CpkgTool::kExitSuccess);
    const std::string first = ReadBytes("levels.cpkg");
    ASSERT_FALSE(first.empty());

    // A build of the same bytes leaves the output untouched.
    const std::filesystem::file_time_type old = std::filesystem::last_write_time("levels.cpkg") - std::chrono::hours(24);
    std::filesystem::last_write_time("levels.cpkg", old);
    ASSERT_EQ(RunCpkg({ "build", "levels.cpkg.json" }).Status, CpkgTool::kExitSuccess);
    EXPECT_EQ(std::filesystem::last_write_time("levels.cpkg"), old);

    // A build of different bytes replaces it.
    WriteText("levels.cpkg.json", source(R"({ "roughness": 0.5 })"));
    ASSERT_EQ(RunCpkg({ "build", "levels.cpkg.json" }).Status, CpkgTool::kExitSuccess);
    EXPECT_NE(std::filesystem::last_write_time("levels.cpkg"), old);
    const std::string second = ReadBytes("levels.cpkg");
    EXPECT_NE(second, first);

    // A failed build leaves it as it was.
    WriteText("levels.cpkg.json", source(R"({ "textures": { "albedo": "Missing" } })"));
    EXPECT_EQ(RunCpkg({ "build", "levels.cpkg.json" }).Status, CpkgTool::kExitError);
    EXPECT_EQ(ReadBytes("levels.cpkg"), second);

    // An output the build cannot replace is reported, and left as it was.
    WriteText("levels.cpkg.json", source("{}"));
    std::filesystem::create_directory("taken.cpkg");
    const RunResult result = RunCpkg({ "build", "levels.cpkg.json", "-o", "taken.cpkg" });
    EXPECT_EQ(result.Status, CpkgTool::kExitError);
    EXPECT_EQ(result.Err, "taken.cpkg: error: cannot replace the file\n");
    EXPECT_TRUE(std::filesystem::is_directory("taken.cpkg"));

    // Where the source lives does not enter the output.
    ASSERT_EQ(RunCpkg({ "build", "levels.cpkg.json" }).Status, CpkgTool::kExitSuccess);
    std::filesystem::create_directory("elsewhere");
    std::filesystem::copy_file("levels.cpkg.json", "elsewhere/levels.cpkg.json");
    ASSERT_EQ(RunCpkg({ "build", "elsewhere/levels.cpkg.json" }).Status, CpkgTool::kExitSuccess);
    EXPECT_EQ(ReadBytes("elsewhere/levels.cpkg"), ReadBytes("levels.cpkg"));

    // No build leaves a temporary file.
    for (const std::filesystem::directory_entry &entry : std::filesystem::directory_iterator("."))
        EXPECT_NE(entry.path().extension(), ".tmp") << entry.path();
}

//------------------------------------------------------------------------------------------------
TEST_F(CpkgBuild, WritesBundleStructure)
{
    WriteText("levels.cpkg.json", R"({
  "formatVersion": 1,
  "packageName": "Levels",
  "bundles": [
    {
      "name": "Goblin",
      "meshes": { "Body": { "groups": [ { "positions": [0, 0, 0], "normals": [0, 0, 1], "boneIndices": [0, 0, 0, 0], "boneWeights": [1, 0, 0, 0] } ] } },
      "skeletons": { "Skeleton": { "bones": [ { "name": "Hips" }, { "name": "Hand_R", "parent": "Hips" } ] } },
      "skins": { "Skin": { "skeleton": "Skeleton", "bones": ["Hips"], "bindPoses": [ {} ] } },
      "nodes": [
        {
          "name": "Goblin", "mobility": "movable",
          "skeletonInstance": { "skeleton": "Skeleton" },
          "meshInstances": [ { "mesh": "Body", "skin": "Skin", "skeletonInstance": "Goblin" } ],
          "children": [
            { "name": "Hand_R", "bone": "Hand_R", "rotation": [0, 0, 0, 1] },
            { "name": "Eyes", "translation": [0.1, 0, 1.6],
              "meshInstances": [ { "mesh": "Body", "skin": "Skin" }, { "mesh": "Body" } ],
              "camera": { "fovAngle": 1.2 },
              "light": { "type": "spot", "color": [1, 0.5, 0.25], "castsShadows": true } }
          ]
        }
      ],
      "exports": ["Hand_R"]
    },
    { "name": "Sword", "meshes": { "Blade": { "groups": [ { "positions": [0, 0, 0], "normals": [0, 0, 1] } ] } } },
    {
      "name": "Camp",
      "importNodes": [
        { "name": "Armed", "placement": "Goblin1", "export": "Hand_R", "rotation": { "degrees": [0, 0, 90] },
          "placements": [ { "name": "Sword1", "bundle": "Sword" } ] }
      ],
      "nodes": [
        { "name": "Fire", "meshFields": [ { "mesh": "Sword/Blade", "transforms": [ {}, { "translation": [1, 0, 0] } ] } ],
          "placements": [ { "name": "Lamp", "bundle": "Levels/Sword", "translation": [0, 0, 2] } ] }
      ],
      "placements": [
        { "name": "Goblin1", "bundle": "Goblin", "mobility": "movable", "translation": [12, 4, 0] },
        { "bundle": "Others/Trees" }
      ],
      "meshFields": [ { "name": "Grass", "mesh": "Sword/Blade", "cullDistance": 60, "transforms": [ { "translation": [1, 2, 0] } ] } ]
    }
  ]
}
)");

    const RunResult result = RunCpkg({ "build", "levels.cpkg.json" });
    EXPECT_TRUE(MatchesSchema(CanvasSchema::PackageSource, "levels.cpkg.json"));
    EXPECT_EQ(result.Err, "");
    ASSERT_EQ(result.Status, CpkgTool::kExitSuccess);

    CCpkgReader reader;
    ASSERT_EQ(reader.Open("levels.cpkg"), Result::Success);
    ASSERT_EQ(reader.GetBundleCount(), 3u);
    auto expectAssets = [&](uint32_t bundle, std::initializer_list<std::pair<AssetType, std::string_view>> assets)
    {
        ASSERT_EQ(reader.GetAssetCount(bundle), assets.size());
        uint32_t i = 0;
        for (const auto &[type, name] : assets)
        {
            EXPECT_EQ(reader.GetAssetType(bundle, i), type) << bundle << ": " << i;
            EXPECT_EQ(reader.GetAssetName(bundle, i), name) << bundle << ": " << i;
            ++i;
        }
    };

    // The shared assets, then the hierarchy in pre-order: a node, its skeleton instance, then its
    // children. Then each kind of attachment in the order of its nodes, then the exports.
    expectAssets(0, {
        { AssetType::MeshData, "Body" }, { AssetType::Skeleton, "Skeleton" }, { AssetType::MeshSkin, "Skin" },
        { AssetType::TransformNode, "Goblin" }, { AssetType::SkeletonInstance, "" }, { AssetType::TransformNode, "Hand_R" },
        { AssetType::TransformNode, "Eyes" }, { AssetType::MeshInstance, "" }, { AssetType::MeshInstance, "" },
        { AssetType::MeshInstance, "" }, { AssetType::Camera, "" }, { AssetType::Light, "" }, { AssetType::ExportList, "" },
    });
    {
        const CPayload payload(reader, 0, 3);
        const CpkgTransformNodeData &goblin = payload.Header<CpkgTransformNodeData>();
        EXPECT_EQ(goblin.ParentIndex, -1);
        EXPECT_EQ(goblin.Mobility, CpkgMobility::Movable);
        EXPECT_EQ(goblin.ParentBone.Count, 0u);
    }
    {
        const CPayload payload(reader, 0, 4);
        const CpkgSkeletonInstanceData &instance = payload.Header<CpkgSkeletonInstanceData>();
        EXPECT_EQ(instance.NodeIndex, 3);
        EXPECT_EQ(payload.Text<CpkgSkeletonInstanceData>(instance.Skeleton), "Skeleton");
    }
    {
        // A socket's parent is its parent node's skeleton instance.
        const CPayload payload(reader, 0, 5);
        const CpkgTransformNodeData &hand = payload.Header<CpkgTransformNodeData>();
        EXPECT_EQ(hand.ParentIndex, 4);
        EXPECT_EQ(hand.Mobility, CpkgMobility::Static);
        EXPECT_EQ(payload.Text<CpkgTransformNodeData>(hand.ParentBone), "Hand_R");
        EXPECT_EQ(hand.LocalTransform.Rotation[3], 1.0f);
    }
    {
        const CPayload payload(reader, 0, 6);
        const CpkgTransformNodeData &eyes = payload.Header<CpkgTransformNodeData>();
        EXPECT_EQ(eyes.ParentIndex, 3);
        EXPECT_EQ(eyes.LocalTransform.Translation[2], 1.6f);
    }
    {
        // A skinned mesh instance binds the skeleton instance it names, or the nearest one toward
        // its root.
        const std::pair<int32_t, int32_t> instances[] = { { 3, 4 }, { 6, 4 }, { 6, -1 } };
        for (uint32_t i = 0; i < 3; ++i)
        {
            const CPayload payload(reader, 0, 7 + i);
            const CpkgMeshInstanceData &instance = payload.Header<CpkgMeshInstanceData>();
            EXPECT_EQ(instance.NodeIndex, instances[i].first) << i;
            EXPECT_EQ(instance.SkeletonInstanceIndex, instances[i].second) << i;
            EXPECT_EQ(payload.Text<CpkgMeshInstanceData>(instance.MeshData), "Body") << i;
            EXPECT_EQ(payload.Text<CpkgMeshInstanceData>(instance.Skin), i < 2 ? "Skin" : "") << i;
        }
    }
    {
        const CPayload payload(reader, 0, 10);
        const CpkgCameraData &camera = payload.Header<CpkgCameraData>();
        EXPECT_EQ(camera.NodeIndex, 6);
        EXPECT_EQ(camera.FovAngle, 1.2f);
        EXPECT_EQ(camera.NearClip, 0.1f);
    }
    {
        const CPayload payload(reader, 0, 11);
        const CpkgLightData &light = payload.Header<CpkgLightData>();
        EXPECT_EQ(light.NodeIndex, 6);
        EXPECT_EQ(light.Type, LightType::Spot);
        EXPECT_EQ(light.Color[2], 0.25f);
        EXPECT_EQ(light.Color[3], 1.0f);
        EXPECT_EQ(light.Flags, LightFlags::Enabled | LightFlags::CastsShadows);
        EXPECT_EQ(light.Range, 100.0f);
    }
    {
        const CPayload payload(reader, 0, 12);
        const std::span<const int32_t> nodes = payload.Elements<CpkgExportListData>(payload.Header<CpkgExportListData>().NodeIndices);
        ASSERT_EQ(nodes.size(), 1u);
        EXPECT_EQ(nodes[0], 5);
    }

    expectAssets(1, { { AssetType::MeshData, "Blade" } });

    // The bundle's nodes, then its placements, each placement followed by the import nodes that
    // name it; then the mesh fields attached to nodes, then the bundle's own.
    expectAssets(2, {
        { AssetType::TransformNode, "Fire" }, { AssetType::Placement, "Lamp" }, { AssetType::Placement, "Goblin1" },
        { AssetType::ImportNode, "Armed" }, { AssetType::Placement, "Sword1" }, { AssetType::Placement, "" },
        { AssetType::MeshField, "" }, { AssetType::MeshField, "Grass" },
    });
    {
        const CPayload payload(reader, 2, 1);
        const CpkgPlacementData &lamp = payload.Header<CpkgPlacementData>();
        EXPECT_EQ(lamp.NodeIndex, 0);
        EXPECT_EQ(lamp.Mobility, CpkgMobility::Static);
        EXPECT_EQ(payload.Text<CpkgPlacementData>(lamp.Bundle), "Levels/Sword");
        EXPECT_EQ(lamp.Transform.Translation[2], 2.0f);
    }
    {
        const CPayload payload(reader, 2, 2);
        const CpkgPlacementData &goblin = payload.Header<CpkgPlacementData>();
        EXPECT_EQ(goblin.NodeIndex, -1);
        EXPECT_EQ(goblin.Mobility, CpkgMobility::Movable);
        EXPECT_EQ(goblin.Transform.Translation[0], 12.0f);
    }
    {
        const CPayload payload(reader, 2, 3);
        const CpkgImportNodeData &armed = payload.Header<CpkgImportNodeData>();
        EXPECT_EQ(armed.PlacementIndex, 2);
        EXPECT_EQ(payload.Text<CpkgImportNodeData>(armed.ExportName), "Hand_R");
        EXPECT_NEAR(armed.LocalTransform.Rotation[2], 0.70710678f, 1e-6f);
        EXPECT_NEAR(armed.LocalTransform.Rotation[3], 0.70710678f, 1e-6f);
    }
    {
        const CPayload payload(reader, 2, 4);
        EXPECT_EQ(payload.Header<CpkgPlacementData>().NodeIndex, 3);
        const CPayload trees(reader, 2, 5);
        EXPECT_EQ(trees.Header<CpkgPlacementData>().NodeIndex, -1);
        EXPECT_EQ(trees.Text<CpkgPlacementData>(trees.Header<CpkgPlacementData>().Bundle), "Others/Trees");
    }
    {
        const CPayload payload(reader, 2, 6);
        const CpkgMeshFieldData &fire = payload.Header<CpkgMeshFieldData>();
        EXPECT_EQ(fire.NodeIndex, 0);
        EXPECT_EQ(fire.CullDistance, 0.0f);
        EXPECT_EQ(payload.Text<CpkgMeshFieldData>(fire.MeshData), "Sword/Blade");
        const std::span<const CpkgTransform> transforms = payload.Elements<CpkgMeshFieldData>(fire.Transforms);
        ASSERT_EQ(transforms.size(), 2u);
        EXPECT_EQ(transforms[1].Translation[0], 1.0f);
        EXPECT_EQ(transforms[1].Scale[0], 1.0f);
    }
    {
        const CPayload payload(reader, 2, 7);
        const CpkgMeshFieldData &grass = payload.Header<CpkgMeshFieldData>();
        EXPECT_EQ(grass.NodeIndex, -1);
        EXPECT_EQ(grass.CullDistance, 60.0f);
    }
}

//------------------------------------------------------------------------------------------------
TEST_F(CpkgBuild, WritesClipsAndWorlds)
{
    WriteText("levels.cpkg.json", R"({
  "formatVersion": 1,
  "packageName": "Levels",
  "bundles": [
    {
      "name": "Door",
      "skeletons": { "Rig": { "bones": [ { "name": "Root" } ] } },
      "skeletalAnimations": { "Open": { "skeleton": "Rig", "tracks": [ { "bone": "Root", "property": "translation", "times": [0, 2.5], "values": [0, 0, 0, 0, 0, 1] } ] } },
      "nodes": [
        { "name": "Frame", "children": [ { "name": "Hinge", "mobility": "movable", "children": [ { "name": "Panel" } ] } ] },
        { "name": "Latch", "skeletonInstance": { "skeleton": "Rig" } }
      ],
      "clips": {
        "Swing": {
          "tracks": [
            { "node": "Hinge", "property": "rotation", "times": [0, 1.5], "values": [0, 0, 0, 1, 0, 0, 1, 0] },
            { "node": "Panel", "property": "scale", "times": [0.5], "values": [1, 1, 1] }
          ],
          "skeletalAnimations": [ { "skeletonInstance": "Latch", "animation": "Open" } ]
        },
        "Fixed": { "duration": 4, "tracks": [ { "node": "Hinge", "property": "translation", "times": [0, 1], "values": [0, 0, 0, 1, 0, 0] } ] },
        "Remote": { "duration": 3, "skeletalAnimations": [ { "skeletonInstance": "Latch", "animation": "Others/Rigs/Wave" } ] }
      }
    },
    {
      "name": "Arena",
      "skyboxes": { "Day": { "image": "Others/Skies/DayCube" } },
      "nodes": [ { "name": "Overview", "translation": [0, -10, 5], "camera": {} } ],
      "world": {
        "regions": [
          { "region": "Door", "bounds": { "min": [0, 0, -10], "max": [64, 64, 50] } },
          { "region": "Others/Hills", "bounds": { "min": [64, 0, -10], "max": [128, 64, 50] } }
        ],
        "skyboxes": ["Day", "Others/Skies/Night"],
        "defaultSkybox": "Others/Skies/Night",
        "activeCamera": "Overview",
        "backgroundColor": [0.1, 0.2, 0.3, 1]
      }
    },
    { "name": "Empty", "world": {} }
  ]
}
)");

    const RunResult result = RunCpkg({ "build", "levels.cpkg.json" });
    EXPECT_TRUE(MatchesSchema(CanvasSchema::PackageSource, "levels.cpkg.json"));
    EXPECT_EQ(result.Err, "");
    ASSERT_EQ(result.Status, CpkgTool::kExitSuccess);

    CCpkgReader reader;
    ASSERT_EQ(reader.Open("levels.cpkg"), Result::Success);
    ASSERT_EQ(reader.GetBundleCount(), 3u);

    // Clips follow the attachments, in source order.
    const std::pair<AssetType, std::string_view> door[] = {
        { AssetType::Skeleton, "Rig" }, { AssetType::SkeletalAnimation, "Open" }, { AssetType::TransformNode, "Frame" },
        { AssetType::TransformNode, "Hinge" }, { AssetType::TransformNode, "Panel" }, { AssetType::TransformNode, "Latch" },
        { AssetType::SkeletonInstance, "" }, { AssetType::AnimationClip, "Swing" }, { AssetType::AnimationClip, "Fixed" },
        { AssetType::AnimationClip, "Remote" },
    };
    ASSERT_EQ(reader.GetAssetCount(0), std::size(door));
    for (uint32_t i = 0; i < std::size(door); ++i)
    {
        EXPECT_EQ(reader.GetAssetType(0, i), door[i].first) << i;
        EXPECT_EQ(reader.GetAssetName(0, i), door[i].second) << i;
    }
    {
        // The default duration is the latest key time of the clip's tracks and the skeletal
        // animations it binds.
        const CPayload payload(reader, 0, 7);
        const CpkgAnimationClipData &swing = payload.Header<CpkgAnimationClipData>();
        EXPECT_EQ(swing.Duration, 2.5f);
        const std::span<const CpkgNodeTrack> tracks = payload.Elements<CpkgAnimationClipData>(swing.Tracks);
        ASSERT_EQ(tracks.size(), 2u);
        EXPECT_EQ(tracks[0].NodeIndex, 3);
        EXPECT_EQ(tracks[0].Property, CpkgAnimationProperty::Rotation);
        EXPECT_EQ(payload.Elements<CpkgAnimationClipData>(tracks[0].Values)[6], 1.0f);
        EXPECT_EQ(tracks[1].NodeIndex, 4);
        EXPECT_EQ(payload.Elements<CpkgAnimationClipData>(tracks[1].Times)[0], 0.5f);
        const std::span<const CpkgSkeletalAnimationBinding> bindings = payload.Elements<CpkgAnimationClipData>(swing.SkeletalAnimations);
        ASSERT_EQ(bindings.size(), 1u);
        EXPECT_EQ(bindings[0].SkeletonInstanceIndex, 6);
        EXPECT_EQ(payload.Text<CpkgAnimationClipData>(bindings[0].Animation), "Open");
    }
    {
        const CPayload fixed(reader, 0, 8);
        EXPECT_EQ(fixed.Header<CpkgAnimationClipData>().Duration, 4.0f);
        EXPECT_EQ(fixed.Header<CpkgAnimationClipData>().SkeletalAnimations.Count, 0u);
        const CPayload remote(reader, 0, 9);
        EXPECT_EQ(remote.Header<CpkgAnimationClipData>().Duration, 3.0f);
        EXPECT_EQ(remote.Header<CpkgAnimationClipData>().Tracks.Count, 0u);
    }

    // The world settings come last.
    ASSERT_EQ(reader.GetAssetCount(1), 4u);
    EXPECT_EQ(reader.GetAssetType(1, 2), AssetType::Camera);
    EXPECT_EQ(reader.GetAssetType(1, 3), AssetType::WorldSettings);
    {
        const CPayload payload(reader, 1, 3);
        const CpkgWorldSettingsData &world = payload.Header<CpkgWorldSettingsData>();
        const std::span<const CpkgWorldRegionRef> regions = payload.Elements<CpkgWorldSettingsData>(world.Regions);
        ASSERT_EQ(regions.size(), 2u);
        EXPECT_EQ(payload.Text<CpkgWorldSettingsData>(regions[0].Region), "Door");
        EXPECT_EQ(regions[0].StreamingBounds.Min[2], -10.0f);
        EXPECT_EQ(regions[1].StreamingBounds.Max[0], 128.0f);
        const std::span<const CpkgSpan<char>> skyboxes = payload.Elements<CpkgWorldSettingsData>(world.Skyboxes);
        ASSERT_EQ(skyboxes.size(), 2u);
        EXPECT_EQ(payload.Text<CpkgWorldSettingsData>(skyboxes[1]), "Others/Skies/Night");
        EXPECT_EQ(world.DefaultSkybox, 1);
        EXPECT_EQ(world.ActiveCameraIndex, 2);
        EXPECT_EQ(world.BackgroundColor[2], 0.3f);
    }
    {
        const CPayload payload(reader, 2, 0);
        const CpkgWorldSettingsData &world = payload.Header<CpkgWorldSettingsData>();
        EXPECT_EQ(world.Regions.Count, 0u);
        EXPECT_EQ(world.DefaultSkybox, -1);
        EXPECT_EQ(world.ActiveCameraIndex, -1);
        EXPECT_EQ(world.BackgroundColor[3], 1.0f);
    }
}

//------------------------------------------------------------------------------------------------
TEST_F(CpkgBuild, ReportsClipAndWorldErrors)
{
    WriteText("levels.cpkg.json", R"({
  "formatVersion": 1,
  "packageName": "Levels",
  "bundles": [
    {
      "name": "Door",
      "skeletons": { "Rig": { "bones": [ { "name": "Root" } ] }, "Other": { "bones": [ { "name": "Root" } ] } },
      "skeletalAnimations": { "Open": { "skeleton": "Other", "tracks": [ { "bone": "Root", "property": "scale", "times": [0], "values": [1, 1, 1] } ] } },
      "placements": [ { "name": "Pin1", "bundle": "Pin", "mobility": "movable" }, { "name": "Pin2", "bundle": "Pin" }, { "name": "Peg1", "bundle": "Peg" } ],
      "importNodes": [
        { "name": "Handle", "placement": "Pin1", "export": "Tip", "children": [ { "name": "Knob1" } ] },
        { "placement": "Pin2", "export": "Tip", "children": [ { "name": "Knob2" } ] },
        { "placement": "Peg1", "export": "Tip", "children": [ { "name": "Knob3" } ] }
      ],
      "nodes": [
        { "name": "Frame", "skeletonInstance": { "skeleton": "Rig" }, "children": [ { "name": "Sock", "bone": "Root", "mobility": "movable" } ] },
        { "name": "Still" }
      ],
      "clips": {
        "Swing": {
          "tracks": [
            { "node": "Still", "property": "rotation", "times": [0], "values": [0, 0, 0, 1] },
            { "node": "Sock", "property": "rotation", "times": [0], "values": [0, 0, 0, 1] },
            { "node": "Handle", "property": "rotation", "times": [0], "values": [0, 0, 0, 1] },
            { "node": "Pin1", "property": "rotation", "times": [0], "values": [0, 0, 0, 1] },
            { "node": "Gone", "property": "scale", "times": [1, 0], "values": [1, 1, 1, 1, 1, 1] },
            { "node": "Knob1", "property": "scale", "times": [0], "values": [1, 1, 1] },
            { "node": "Knob2", "property": "scale", "times": [0], "values": [1, 1, 1] },
            { "node": "Knob3", "property": "scale", "times": [0], "values": [1, 1, 1] },
            { "node": "Knob1", "property": "scale", "times": [0], "values": [1, 1, 1] }
          ],
          "skeletalAnimations": [
            { "skeletonInstance": "Frame", "animation": "Open" },
            { "skeletonInstance": "Frame", "animation": "Open" },
            { "skeletonInstance": "Still", "animation": "Rig" }
          ]
        },
        "Remote": { "skeletalAnimations": [ { "skeletonInstance": "Frame", "animation": "Others/Rigs/Wave" } ] }
      }
    },
    { "name": "Pin", "nodes": [ { "name": "Tip", "mobility": "movable" } ], "exports": ["Tip"] },
    { "name": "Peg", "nodes": [ { "name": "Tip" } ], "exports": ["Tip"] },
    {
      "name": "Arena",
      "nodes": [ { "name": "Plain" } ],
      "world": {
        "regions": [ { "region": "Arena", "bounds": { "min": [1, 0, 0], "max": [0, 1, 1] } }, { "region": "Nowhere", "bounds": { "min": [0, 0, 0], "max": [1, 1, 1] } } ],
        "skyboxes": ["Door/Open"],
        "defaultSkybox": "Night",
        "activeCamera": "Plain"
      }
    }
  ]
}
)");

    const RunResult result = RunCpkg({ "validate", "levels.cpkg.json" });
    EXPECT_TRUE(MatchesSchema(CanvasSchema::PackageSource, "levels.cpkg.json"));
    EXPECT_EQ(result.Status, CpkgTool::kExitError);

    // Knob1 moves with its placement and Knob2 with the node it is under, which its placed
    // bundle exports; Knob3 moves with neither.
    EXPECT_EQ(result.Err,
        "levels.cpkg.json:22:23: error: the clip animates \"Still\", whose effective mobility is not \"movable\" [/bundles/0/clips/Swing/tracks/0/node]\n"
        "levels.cpkg.json:23:23: error: \"Sock\" is a socket, which a clip track cannot animate [/bundles/0/clips/Swing/tracks/1/node]\n"
        "levels.cpkg.json:24:23: error: \"Handle\" is an import node, which a clip track cannot animate [/bundles/0/clips/Swing/tracks/2/node]\n"
        "levels.cpkg.json:25:23: error: \"Pin1\" is a placement, not a node [/bundles/0/clips/Swing/tracks/3/node]\n"
        "levels.cpkg.json:26:23: error: the bundle has no node \"Gone\" [/bundles/0/clips/Swing/tracks/4/node]\n"
        "levels.cpkg.json:26:61: error: expected nondecreasing times; key 1 is earlier than key 0 [/bundles/0/clips/Swing/tracks/4/times]\n"
        "levels.cpkg.json:29:23: error: the clip animates \"Knob3\", whose effective mobility is not \"movable\" [/bundles/0/clips/Swing/tracks/7/node]\n"
        "levels.cpkg.json:30:23: error: the clip already has a \"scale\" track for \"Knob1\" [/bundles/0/clips/Swing/tracks/8/node]\n"
        "levels.cpkg.json:33:57: error: the animation's skeleton \"Other\" is not the skeleton instance's \"Rig\" [/bundles/0/clips/Swing/skeletalAnimations/0/animation]\n"
        "levels.cpkg.json:34:35: error: the clip already binds the skeleton instance of \"Frame\" [/bundles/0/clips/Swing/skeletalAnimations/1/skeletonInstance]\n"
        "levels.cpkg.json:35:35: error: the node \"Still\" has no \"skeletonInstance\" [/bundles/0/clips/Swing/skeletalAnimations/2/skeletonInstance]\n"
        "levels.cpkg.json:35:57: error: \"Rig\" is a skeleton, not a skeletal animation [/bundles/0/clips/Swing/skeletalAnimations/2/animation]\n"
        "levels.cpkg.json:38:19: error: a clip that binds a skeletal animation in another package needs \"duration\" [/bundles/0/clips/Remote]\n"
        "levels.cpkg.json:47:34: error: \"Arena\" is a world bundle, which a bundle path cannot name [/bundles/3/world/regions/0/region]\n"
        "levels.cpkg.json:47:53: error: expected \"min\" <= \"max\" on every axis [/bundles/3/world/regions/0/bounds]\n"
        "levels.cpkg.json:47:107: error: the package has no bundle \"Nowhere\" [/bundles/3/world/regions/1/region]\n"
        "levels.cpkg.json:48:22: error: \"Door/Open\" is a skeletal animation, not a skybox [/bundles/3/world/skyboxes/0]\n"
        "levels.cpkg.json:49:26: error: \"Night\" is not one of the world's \"skyboxes\" [/bundles/3/world/defaultSkybox]\n"
        "levels.cpkg.json:50:25: error: the node \"Plain\" has no \"camera\" [/bundles/3/world/activeCamera]\n");
}

//------------------------------------------------------------------------------------------------
TEST_F(CpkgBuild, ReportsBundleStructureErrors)
{
    WriteText("levels.cpkg.json", R"({
  "formatVersion": 1,
  "packageName": "Levels",
  "bundles": [
    {
      "name": "Goblin",
      "skeletons": { "Skeleton": { "bones": [ { "name": "Hips" } ] }, "Other": { "bones": [ { "name": "Root" } ] } },
      "skins": { "Skin": { "skeleton": "Other", "bones": ["Root"], "bindPoses": [ {} ] } },
      "meshes": { "Body": { "groups": [ { "positions": [0, 0, 0], "normals": [0, 0, 1] } ] } },
      "nodes": [
        { "name": "Root", "bone": "Hips" },
        { "name": "Body", "children": [ { "bone": "Hips" } ] },
        { "name": "Rig", "skeletonInstance": { "skeleton": "Skeleton" }, "meshInstances": [ { "mesh": "Body", "skin": "Skin" } ],
          "children": [ { "bone": "Tail", "rotation": [0, 0, 0, 0] } ] },
        { "name": "Plain", "meshInstances": [ { "mesh": "Skin", "skin": "Skin" }, { "mesh": "Body", "skin": "Skin", "skeletonInstance": "Plain" },
                                              { "mesh": "Body", "skin": "Skin", "skeletonInstance": "Spot" } ] }
      ],
      "placements": [ { "name": "Spot", "bundle": "Goblin" }, { "bundle": "Arena" }, { "bundle": "Nowhere" }, { "bundle": "Levels/Camp" } ],
      "meshFields": [ { "mesh": "Skeleton", "transforms": [ { "rotation": [0, 0, 0.7071, 0.7071] } ] } ],
      "exports": ["Rig", "Spot", "Missing"]
    },
    {
      "name": "Camp",
      "placements": [ { "name": "G", "bundle": "Goblin" } ],
      "importNodes": [
        { "placement": "G", "export": "Hand" },
        { "placement": "Fire", "export": "Rig" },
        { "placement": "Nope", "export": "Rig" },
        { "name": "Loop", "placement": "Inner", "export": "Rig", "placements": [ { "name": "Inner", "bundle": "Sword" } ] }
      ],
      "nodes": [ { "name": "Fire" } ]
    },
    { "name": "Sword" },
    { "name": "Arena", "world": {} }
  ]
}
)");

    const RunResult result = RunCpkg({ "validate", "levels.cpkg.json" });
    EXPECT_TRUE(MatchesSchema(CanvasSchema::PackageSource, "levels.cpkg.json"));
    EXPECT_EQ(result.Status, CpkgTool::kExitError);
    EXPECT_EQ(result.Err,
        "levels.cpkg.json:11:35: error: a node with \"bone\" needs a parent node with a \"skeletonInstance\" [/bundles/0/nodes/0/bone]\n"
        "levels.cpkg.json:12:19: error: the bundle already uses the name \"Body\" [/bundles/0/nodes/1/name]\n"
        "levels.cpkg.json:12:51: error: a node with \"bone\" needs a parent node with a \"skeletonInstance\" [/bundles/0/nodes/1/children/0/bone]\n"
        "levels.cpkg.json:13:119: error: the skin's skeleton \"Other\" is not the skeleton instance's \"Skeleton\" [/bundles/0/nodes/2/meshInstances/0/skin]\n"
        "levels.cpkg.json:14:35: error: the skeleton \"Skeleton\" has no bone \"Tail\" [/bundles/0/nodes/2/children/0/bone]\n"
        "levels.cpkg.json:14:55: error: expected a unit quaternion; its squared length is 0 [/bundles/0/nodes/2/children/0/rotation]\n"
        "levels.cpkg.json:15:47: error: no node from this mesh instance's node to its root has a \"skeletonInstance\" for the skin to bind to [/bundles/0/nodes/3/meshInstances/0]\n"
        "levels.cpkg.json:15:57: error: \"Skin\" is a skin, not a mesh [/bundles/0/nodes/3/meshInstances/0/mesh]\n"
        "levels.cpkg.json:15:137: error: the node \"Plain\" has no \"skeletonInstance\" [/bundles/0/nodes/3/meshInstances/1/skeletonInstance]\n"
        "levels.cpkg.json:16:101: error: \"Spot\" is a placement, not a node [/bundles/0/nodes/3/meshInstances/2/skeletonInstance]\n"
        "levels.cpkg.json:18:51: error: the bundle places itself [/bundles/0/placements/0/bundle]\n"
        "levels.cpkg.json:18:75: error: \"Arena\" is a world bundle, which a bundle path cannot name [/bundles/0/placements/1/bundle]\n"
        "levels.cpkg.json:18:98: error: the package has no bundle \"Nowhere\" [/bundles/0/placements/2/bundle]\n"
        "levels.cpkg.json:18:123: error: \"Levels/Camp\" places this bundle, directly or through the bundles it places [/bundles/0/placements/3/bundle]\n"
        "levels.cpkg.json:19:33: error: \"Skeleton\" is a skeleton, not a mesh [/bundles/0/meshFields/0/mesh]\n"
        "levels.cpkg.json:19:75: error: expected a unit quaternion; its squared length is 0.999981 [/bundles/0/meshFields/0/transforms/0/rotation]\n"
        "levels.cpkg.json:20:26: error: \"Spot\" is a placement, not a node [/bundles/0/exports/1]\n"
        "levels.cpkg.json:20:34: error: the bundle has no node \"Missing\" [/bundles/0/exports/2]\n"
        "levels.cpkg.json:24:48: error: \"Goblin\" places this bundle, directly or through the bundles it places [/bundles/1/placements/0/bundle]\n"
        "levels.cpkg.json:26:39: error: the bundle \"Goblin\" exports no node \"Hand\" [/bundles/1/importNodes/0/export]\n"
        "levels.cpkg.json:27:24: error: \"Fire\" is a node, not a placement [/bundles/1/importNodes/1/placement]\n"
        "levels.cpkg.json:28:24: error: the bundle has no placement \"Nope\" [/bundles/1/importNodes/2/placement]\n"
        "levels.cpkg.json:29:40: error: the placement \"Inner\" attaches within this import node [/bundles/1/importNodes/3/placement]\n");
}

//------------------------------------------------------------------------------------------------
TEST_F(CpkgBuild, ReportsValidationErrorsInDocumentOrder)
{
    WriteText("a.png", ImageFiles::Png(4, 4));
    WriteText("parts.cbundle.json", R"({
  "formatVersion": 1,
  "bundle": { "name": "Looks", "materials": { "Steel": { "textures": { "normal": "Sky" } } }, "meshes": { "Steel": { "groups": [ { "positions": [0, 0, 0] } ] } } }
})");
    WriteText("levels.cpkg.json", R"({
  "formatVersion": 1,
  "packageName": "Levels",
  "bundles": [
    {
      "name": "Looks",
      "images": { "Sky": { "cubemap": { "+X": "a.png", "-X": "a.png", "+Y": "a.png", "-Y": "a.png", "+Z": "a.png", "-Z": "a.png" } } },
      "materials": { "Brass": { "textures": { "albedo": "Sky" } } },
      "skyboxes": { "Day": { "image": "Brass", "orientation": [0, 0, 0, 0] }, "Night": { "image": "Missing/Sky" } },
      "meshes": {
        "Tri": { "topology": "patchList4CP", "bounds": { "min": [0, 0, 1], "max": [1, 1, 0] },
                 "groups": [ { "material": "Plastic", "positions": [0, 0, 0, 1, 0, 0], "normals": [0, 0, 1], "boneWeights": [1, 0, 0, 0, 0.75, 0.5, 0, 0] },
                             { "positions": [], "normals": [] } ] }
      }
    },
    {
      "name": "Rig",
      "skeletons": { "Goblin": { "bones": [ { "name": "Hips", "parent": "Spine" }, { "name": "Spine" }, { "name": "Spine", "parent": "Tail" } ] } },
      "skins": {
        "Skin": { "skeleton": "Goblin", "bones": ["Hips", "Head"], "bindPoses": [ {} ] },
        "Borrowed": { "skeleton": "Others/Rigs/Troll", "bones": ["Root"], "bindPoses": [ { "rotation": [0, 0, 0, 0] } ] }
      },
      "skeletalAnimations": { "Walk": { "skeleton": "Goblin", "tracks": [
        { "bone": "Hips", "property": "rotation", "times": [1, 0], "values": [0, 0, 0, 1, 0, 0, 0, 0] },
        { "bone": "Hips", "property": "rotation", "times": [0], "values": [0, 0, 0, 1, 0], "interpolation": ["cubic", "step"] },
        { "bone": "Neck", "property": "scale", "times": [], "values": [], "tangents": [1, 1, 1] } ] } }
    },
    { "include": "parts.cbundle.json" }
  ]
}
)");

    const RunResult result = RunCpkg({ "validate", "levels.cpkg.json" });
    EXPECT_TRUE(MatchesSchema(CanvasSchema::PackageSource, "levels.cpkg.json"));
    EXPECT_TRUE(MatchesSchema(CanvasSchema::PackageSource, "parts.cbundle.json"));
    EXPECT_EQ(result.Status, CpkgTool::kExitError);
    EXPECT_EQ(result.Err,
        "levels.cpkg.json:8:57: error: \"Sky\" is not a 2D image with one array slice [/bundles/0/materials/Brass/textures/albedo]\n"
        "levels.cpkg.json:9:39: error: \"Brass\" is a material, not an image [/bundles/0/skyboxes/Day/image]\n"
        "levels.cpkg.json:9:63: error: expected a unit quaternion; its squared length is 0 [/bundles/0/skyboxes/Day/orientation]\n"
        "levels.cpkg.json:9:99: error: the package has no bundle \"Missing\" [/bundles/0/skyboxes/Night/image]\n"
        "levels.cpkg.json:11:56: error: expected \"min\" <= \"max\" on every axis [/bundles/0/meshes/Tri/bounds]\n"
        "levels.cpkg.json:12:30: error: a group of a \"patchList4CP\" mesh needs \"uv0\" [/bundles/0/meshes/Tri/groups/0]\n"
        "levels.cpkg.json:12:30: error: a group of a skinned mesh needs \"boneIndices\" and \"boneWeights\" [/bundles/0/meshes/Tri/groups/0]\n"
        "levels.cpkg.json:12:44: error: the bundle has no asset \"Plastic\" [/bundles/0/meshes/Tri/groups/0/material]\n"
        "levels.cpkg.json:12:99: error: expected 2 vertices, as \"positions\" holds; found 1 [/bundles/0/meshes/Tri/groups/0/normals]\n"
        "levels.cpkg.json:12:125: error: expected bone weights of at least 0 with a sum of at most 1; vertex 1's weights sum to 1.25 [/bundles/0/meshes/Tri/groups/0/boneWeights]\n"
        "levels.cpkg.json:13:30: error: a group of a \"patchList4CP\" mesh needs \"uv0\" [/bundles/0/meshes/Tri/groups/1]\n"
        "levels.cpkg.json:13:30: error: a group of a skinned mesh needs \"boneIndices\" and \"boneWeights\" [/bundles/0/meshes/Tri/groups/1]\n"
        "levels.cpkg.json:13:45: error: expected at least one vertex [/bundles/0/meshes/Tri/groups/1/positions]\n"
        "levels.cpkg.json:18:73: error: \"Spine\" is not an earlier bone [/bundles/1/skeletons/Goblin/bones/0/parent]\n"
        "levels.cpkg.json:18:115: error: the skeleton already has a bone named \"Spine\" [/bundles/1/skeletons/Goblin/bones/2/name]\n"
        "levels.cpkg.json:18:134: error: the skeleton has no bone \"Tail\" [/bundles/1/skeletons/Goblin/bones/2/parent]\n"
        "levels.cpkg.json:20:59: error: the skeleton \"Goblin\" has no bone \"Head\" [/bundles/1/skins/Skin/bones/1]\n"
        "levels.cpkg.json:20:81: error: expected 2 bind poses, one per bone; found 1 [/bundles/1/skins/Skin/bindPoses]\n"
        "levels.cpkg.json:21:104: error: expected a unit quaternion; its squared length is 0 [/bundles/1/skins/Borrowed/bindPoses/0/rotation]\n"
        "levels.cpkg.json:24:60: error: expected nondecreasing times; key 1 is earlier than key 0 [/bundles/1/skeletalAnimations/Walk/tracks/0/times]\n"
        "levels.cpkg.json:24:78: error: expected unit quaternions; key 1 has squared length 0 [/bundles/1/skeletalAnimations/Walk/tracks/0/values]\n"
        "levels.cpkg.json:25:9: error: a track with a \"cubic\" key needs \"tangents\" [/bundles/1/skeletalAnimations/Walk/tracks/1]\n"
        "levels.cpkg.json:25:19: error: the animation already has a \"rotation\" track for \"Hips\" [/bundles/1/skeletalAnimations/Walk/tracks/1/bone]\n"
        "levels.cpkg.json:25:75: error: expected 4 numbers, 4 for each of 1 key; found 5 [/bundles/1/skeletalAnimations/Walk/tracks/1/values]\n"
        "levels.cpkg.json:25:109: error: expected 1 interpolation, one per key; found 2 [/bundles/1/skeletalAnimations/Walk/tracks/1/interpolation]\n"
        "levels.cpkg.json:26:19: error: the skeleton \"Goblin\" has no bone \"Neck\" [/bundles/1/skeletalAnimations/Walk/tracks/2/bone]\n"
        "levels.cpkg.json:26:57: error: expected at least one key [/bundles/1/skeletalAnimations/Walk/tracks/2/times]\n"
        "levels.cpkg.json:26:87: error: expected no tangents, since no key is \"cubic\"; found 3 [/bundles/1/skeletalAnimations/Walk/tracks/2/tangents]\n"
        "parts.cbundle.json:3:23: error: the package already has a bundle named \"Looks\" [/bundle/name]\n"
        "parts.cbundle.json:3:82: error: the bundle has no asset \"Sky\" [/bundle/materials/Steel/textures/normal]\n"
        "parts.cbundle.json:3:107: error: the bundle already uses the name \"Steel\" [/bundle/meshes/Steel]\n"
        "parts.cbundle.json:3:130: error: a group needs \"normals\" [/bundle/meshes/Steel/groups/0]\n");
    EXPECT_FALSE(std::filesystem::exists("levels.cpkg"));
}

//------------------------------------------------------------------------------------------------
TEST_F(CpkgBuild, ImportsMeshesInCanvasSpace)
{
    WriteText("quad.fbx", kQuad);
    auto build = [](std::string_view settings)
    {
        WriteText("levels.cpkg.json", R"({ "formatVersion": 1, "packageName": "Levels", "bundles": [ { "name": "Props", "imports": [ { "file": "quad.fbx", "nodes": false)"
            + std::string(settings) + " } ] } ] }");
        return RunCpkg({ "build", "levels.cpkg.json" });
    };

    // The file's -Y, the direction its model faces, is Canvas's +X, and its +X, the model's left,
    // is Canvas's +Y. Its polygon is triangulated, and its normals generated.
    RunResult result = build("");
    ASSERT_EQ(result.Err, "");
    {
        CCpkgReader reader;
        ASSERT_EQ(reader.Open("levels.cpkg"), Result::Success);
        ASSERT_EQ(reader.GetAssetCount(0), 1u);
        EXPECT_EQ(reader.GetAssetName(0, 0), "Quad.mesh");
        const CPayload payload(reader, 0, 0);
        const CpkgMeshData &mesh = payload.Header<CpkgMeshData>();
        for (size_t axis = 0; axis < 3; ++axis)
        {
            EXPECT_NEAR(mesh.LocalBounds.Min[axis], 0.0f, 1e-6f) << axis;
            EXPECT_NEAR(mesh.LocalBounds.Max[axis], axis == 0 ? 0.0f : 1.0f, 1e-6f) << axis;
        }
        const std::span<const CpkgMeshDataGroup> groups = payload.Elements<CpkgMeshData>(mesh.Groups);
        ASSERT_EQ(groups.size(), 1u);
        EXPECT_EQ(groups[0].Material.Count, 0u);
        EXPECT_EQ(groups[0].Positions.Count, 6u);
        EXPECT_EQ(groups[0].UV0.Count, 0u);
        for (const CpkgVertexNormal &normal : payload.Elements<CpkgMeshData>(groups[0].Normals))
        {
            EXPECT_NEAR(normal.XYZ[0], 1.0f, 1e-6f);
            EXPECT_NEAR(normal.XYZ[1], 0.0f, 1e-6f);
            EXPECT_NEAR(normal.XYZ[2], 0.0f, 1e-6f);
        }
    }

    // "rotation" and "scale" apply after the conversion: a quarter turn about +Z turns +Y to -X,
    // and the normal +X to +Y.
    result = build(R"(, "scale": 2, "rotation": { "degrees": [0, 0, 90] })");
    ASSERT_EQ(result.Err, "");
    {
        CCpkgReader reader;
        ASSERT_EQ(reader.Open("levels.cpkg"), Result::Success);
        const CPayload payload(reader, 0, 0);
        const CpkgMeshData &mesh = payload.Header<CpkgMeshData>();
        const float min[] = { -2.0f, 0.0f, 0.0f };
        const float max[] = { 0.0f, 0.0f, 2.0f };
        for (size_t axis = 0; axis < 3; ++axis)
        {
            EXPECT_NEAR(mesh.LocalBounds.Min[axis], min[axis], 1e-6f) << axis;
            EXPECT_NEAR(mesh.LocalBounds.Max[axis], max[axis], 1e-6f) << axis;
        }
        const CpkgMeshDataGroup &group = payload.Elements<CpkgMeshData>(mesh.Groups)[0];
        EXPECT_NEAR(payload.Elements<CpkgMeshData>(group.Normals)[0].XYZ[1], 1.0f, 1e-6f);
    }

    // A file in centimeters is converted to meters.
    WriteText("quad.fbx", FbxFile(FbxMesh(100, "Quad", "0,0,0,1,0,0,1,0,1,0,0,1", "0,1,2,-4"), FbxMeshConnections(100), 1));
    result = build("");
    ASSERT_EQ(result.Err, "");
    {
        CCpkgReader reader;
        ASSERT_EQ(reader.Open("levels.cpkg"), Result::Success);
        const CPayload payload(reader, 0, 0);
        EXPECT_NEAR(payload.Header<CpkgMeshData>().LocalBounds.Max[2], 0.01f, 1e-8f);
    }
}

//------------------------------------------------------------------------------------------------
TEST_F(CpkgBuild, ImportsMaterialsAndTextures)
{
    const std::string bump = ImageFiles::Png(8, 8, 'b');
    const std::string wood = ImageFiles::Png(4, 2, 'w');
    const std::string layers =
        "\t\tLayerElementMaterial: 0 {\n\t\t\tVersion: 101\n\t\t\tMappingInformationType: \"ByPolygon\"\n"
        "\t\t\tReferenceInformationType: \"IndexToDirect\"\n\t\t\tMaterials: *2 {\n\t\t\t\ta: 0,1\n\t\t\t}\n\t\t}\n"
        "\t\tLayerElementUV: 0 {\n\t\t\tVersion: 101\n\t\t\tName: \"UVMap\"\n\t\t\tMappingInformationType: \"ByPolygonVertex\"\n"
        "\t\t\tReferenceInformationType: \"Direct\"\n\t\t\tUV: *12 {\n\t\t\t\ta: 0,0,1,0,1,1,0,0,1,1,0,1\n\t\t\t}\n\t\t}\n"
        "\t\tLayer: 0 {\n\t\t\tVersion: 100\n"
        "\t\t\tLayerElement:  {\n\t\t\t\tType: \"LayerElementMaterial\"\n\t\t\t\tTypedIndex: 0\n\t\t\t}\n"
        "\t\t\tLayerElement:  {\n\t\t\t\tType: \"LayerElementUV\"\n\t\t\t\tTypedIndex: 0\n\t\t\t}\n\t\t}\n";
    std::filesystem::create_directory("art");
    WriteText("art/tiles.fbx", FbxFile(
        FbxMesh(100, "Tiles", "0,0,0,1,0,0,1,0,1,0,0,1", "0,1,-3,0,2,-4", layers)
            + FbxMaterial(200, "Red", "1,0,0") + FbxMaterial(201, "Wood", "0.5,0.25,0")
            + FbxVideo(300, "bump.png", bump) + FbxTexture(301, "Bump", "bump.png") + FbxTexture(302, "Wood", "wood.png"),
        FbxMeshConnections(100)
            + "\tC: \"OO\",200,101\n\tC: \"OO\",201,101\n"
            + "\tC: \"OP\",301,200,\"NormalMap\"\n\tC: \"OO\",300,301\n\tC: \"OP\",302,201,\"DiffuseColor\"\n"));
    WriteText("art/wood.png", wood);
    WriteText("levels.cpkg.json", R"({
  "formatVersion": 1,
  "packageName": "Levels",
  "bundles": [ { "name": "Props", "imports": [ { "file": "art/tiles.fbx", "nodes": false, "rename": { "materials": { "Red": "Brick" } } } ] } ]
}
)");

    // The embedded texture's bytes and the external texture's file are stored unchanged, and each
    // polygon's material builds a group. The FBX file and the external texture are inputs.
    RunResult result = RunCpkg({ "build", "levels.cpkg.json", "--depfile", "levels.d" });
    ASSERT_EQ(result.Err, "");
    const std::string inputs = ReadBytes("levels.d");
    EXPECT_NE(inputs.find("/art/tiles.fbx"), std::string::npos) << inputs;
    EXPECT_NE(inputs.find("/art/wood.png"), std::string::npos) << inputs;
    {
        CCpkgReader reader;
        ASSERT_EQ(reader.Open("levels.cpkg"), Result::Success);
        ASSERT_EQ(reader.GetAssetCount(0), 5u);
        const char *names[] = { "bump.image", "wood.image", "Brick", "Wood.material", "Tiles.mesh" };
        for (uint32_t i = 0; i < std::size(names); ++i)
            EXPECT_EQ(reader.GetAssetName(0, i), names[i]) << i;

        const std::string *pBytes[] = { &bump, &wood };
        for (uint32_t i = 0; i < 2; ++i)
        {
            const CPayload payload(reader, 0, i);
            const CpkgImageData &image = payload.Header<CpkgImageData>();
            EXPECT_EQ(image.Encoding, CpkgImageEncoding::Png);
            EXPECT_EQ(image.Width, i == 0 ? 8u : 4u);
            const std::span<const CpkgSpan<uint8_t>> entries = payload.Elements<CpkgImageData>(image.Images);
            ASSERT_EQ(entries.size(), 1u);
            const std::span<const uint8_t> stored = payload.Elements<CpkgImageData>(entries[0]);
            EXPECT_EQ(std::string(stored.begin(), stored.end()), *pBytes[i]) << i;
        }
        {
            const CPayload payload(reader, 0, 2);
            const CpkgMaterialData &brick = payload.Header<CpkgMaterialData>();
            EXPECT_EQ(brick.BaseColorFactor[0], 1.0f);
            EXPECT_EQ(brick.BaseColorFactor[1], 0.0f);
            EXPECT_EQ(brick.BaseColorFactor[3], 1.0f);
            EXPECT_EQ(payload.Text<CpkgMaterialData>(brick.NormalTexture), "bump.image");
            EXPECT_EQ(brick.AlbedoTexture.Count, 0u);
        }
        {
            const CPayload payload(reader, 0, 3);
            const CpkgMaterialData &woodMaterial = payload.Header<CpkgMaterialData>();
            EXPECT_EQ(woodMaterial.BaseColorFactor[1], 0.25f);
            EXPECT_EQ(payload.Text<CpkgMaterialData>(woodMaterial.AlbedoTexture), "wood.image");
        }
        {
            const CPayload payload(reader, 0, 4);
            const std::span<const CpkgMeshDataGroup> groups = payload.Elements<CpkgMeshData>(payload.Header<CpkgMeshData>().Groups);
            ASSERT_EQ(groups.size(), 2u);
            EXPECT_EQ(payload.Text<CpkgMeshData>(groups[0].Material), "Brick");
            EXPECT_EQ(payload.Text<CpkgMeshData>(groups[1].Material), "Wood.material");
            EXPECT_EQ(groups[0].Positions.Count, 3u);
            ASSERT_EQ(groups[0].UV0.Count, 3u);

            // FBX's V runs up from the bottom, and Canvas's down from the top.
            EXPECT_EQ(payload.Elements<CpkgMeshData>(groups[0].UV0)[0].UV[1], 1.0f);
        }
    }

    // A replaced material is not built, and neither is the image only it uses, whose file the
    // build then does not need.
    std::filesystem::remove("art/wood.png");
    WriteText("levels.cpkg.json", R"({
  "formatVersion": 1,
  "packageName": "Levels",
  "bundles": [ { "name": "Props", "materials": { "Oak": {} }, "imports": [ { "file": "art/tiles.fbx", "nodes": false, "replace": { "materials": { "Wood": "Oak" } } } ] } ]
}
)");
    result = RunCpkg({ "build", "levels.cpkg.json" });
    ASSERT_EQ(result.Err, "");
    {
        CCpkgReader reader;
        ASSERT_EQ(reader.Open("levels.cpkg"), Result::Success);
        ASSERT_EQ(reader.GetAssetCount(0), 4u);
        EXPECT_EQ(AssetIndex(reader, 0, "wood.image"), 4u);
        EXPECT_EQ(AssetIndex(reader, 0, "Wood.material"), 4u);
        const CPayload payload(reader, 0, AssetIndex(reader, 0, "Tiles.mesh"));
        const std::span<const CpkgMeshDataGroup> groups = payload.Elements<CpkgMeshData>(payload.Header<CpkgMeshData>().Groups);
        ASSERT_EQ(groups.size(), 2u);
        EXPECT_EQ(payload.Text<CpkgMeshData>(groups[1].Material), "Oak");
    }

    // An external texture found nowhere is reported.
    WriteText("levels.cpkg.json", R"({
  "formatVersion": 1,
  "packageName": "Levels",
  "bundles": [ { "name": "Props", "imports": [ { "file": "art/tiles.fbx", "nodes": false } ] } ]
}
)");
    result = RunCpkg({ "build", "levels.cpkg.json" });
    EXPECT_EQ(result.Status, CpkgTool::kExitError);
    EXPECT_EQ(result.Err,
        "levels.cpkg.json:4:48: error: the FBX texture \"wood\" of the FBX material \"Wood\" is not found: \"wood.png\" [/bundles/0/imports/0]\n");
}

//------------------------------------------------------------------------------------------------
TEST_F(CpkgBuild, ReportsImportNames)
{
    WriteText("walls.fbx", FbxFile(FbxMesh(100, "wall/left", "0,0,0,1,0,0,1,0,1", "0,1,-3") + FbxMesh(200, "Crate", "0,0,0,1,0,0,1,0,1", "0,1,-3"),
        FbxMeshConnections(100) + FbxMeshConnections(200)));

    // A name in the file that cannot name an asset is an error, and so is a key of "rename" or
    // "replace" that names nothing in the file.
    WriteText("levels.cpkg.json", R"({
  "formatVersion": 1,
  "packageName": "Levels",
  "bundles": [
    {
      "name": "Props",
      "materials": { "Crate.mesh": {} },
      "imports": [ { "file": "walls.fbx", "nodes": false, "rename": { "meshes": { "Barrel": "B" } }, "replace": { "materials": { "Skin": "Crate.mesh" } } } ]
    }
  ]
}
)");
    RunResult result = RunCpkg({ "build", "levels.cpkg.json" });
    EXPECT_EQ(result.Status, CpkgTool::kExitError);
    EXPECT_EQ(result.Err,
        "levels.cpkg.json:8:20: error: the FBX mesh \"wall/left\" builds the name \"wall/left.mesh\", and it holds a '/'; name it with \"rename\" [/bundles/0/imports/0]\n"
        "levels.cpkg.json:8:83: error: the FBX file has no mesh \"Barrel\" [/bundles/0/imports/0/rename/meshes/Barrel]\n"
        "levels.cpkg.json:8:130: error: the FBX file has no material \"Skin\" [/bundles/0/imports/0/replace/materials/Skin]\n");

    // A built name the bundle already uses is an error.
    WriteText("levels.cpkg.json", R"({
  "formatVersion": 1,
  "packageName": "Levels",
  "bundles": [
    {
      "name": "Props",
      "materials": { "Crate.mesh": {} },
      "imports": [ { "file": "walls.fbx", "nodes": false, "rename": { "meshes": { "wall/left": "LeftWall" } } } ]
    }
  ]
}
)");
    result = RunCpkg({ "build", "levels.cpkg.json" });
    EXPECT_EQ(result.Status, CpkgTool::kExitError);
    EXPECT_EQ(result.Err, "levels.cpkg.json:8:20: error: the bundle already uses the name \"Crate.mesh\" [/bundles/0/imports/0]\n");

    // "rename" and "names" give each a name.
    WriteText("levels.cpkg.json", R"({
  "formatVersion": 1,
  "packageName": "Levels",
  "bundles": [
    {
      "name": "Props",
      "materials": { "Crate.mesh": {} },
      "imports": [ { "file": "walls.fbx", "nodes": false, "names": { "meshes": "Wall_{name}" }, "rename": { "meshes": { "wall/left": "LeftWall" } } } ]
    }
  ]
}
)");
    result = RunCpkg({ "build", "levels.cpkg.json" });
    ASSERT_EQ(result.Err, "");
    CCpkgReader reader;
    ASSERT_EQ(reader.Open("levels.cpkg"), Result::Success);
    ASSERT_EQ(reader.GetAssetCount(0), 3u);
    EXPECT_EQ(reader.GetAssetName(0, 1), "LeftWall");
    EXPECT_EQ(reader.GetAssetName(0, 2), "Wall_Crate");
}

//------------------------------------------------------------------------------------------------
TEST_F(CpkgBuild, ReportsUnreadableImports)
{
    WriteText("junk.fbx", "not an FBX file");
    WriteText("levels.cpkg.json", R"({
  "formatVersion": 1,
  "packageName": "Levels",
  "bundles": [ { "name": "Props", "imports": [ { "file": "missing.fbx", "nodes": false }, { "file": "junk.fbx", "nodes": false }, { "file": "tree.obj", "nodes": false } ] } ]
}
)");
    const RunResult result = RunCpkg({ "validate", "levels.cpkg.json" });
    EXPECT_EQ(result.Status, CpkgTool::kExitError);
    EXPECT_EQ(result.Err,
        "levels.cpkg.json:4:58: error: file not found: \"missing.fbx\" [/bundles/0/imports/0/file]\n"
        "levels.cpkg.json:4:101: error: cannot read the FBX file (Unsupported version): \"junk.fbx\" [/bundles/0/imports/1/file]\n"
        "levels.cpkg.json:4:141: error: expected an exchange file in a format cpkg reads, \".fbx\": \"tree.obj\" [/bundles/0/imports/2/file]\n");
}

//------------------------------------------------------------------------------------------------
TEST_F(CpkgBuild, ImportsNodes)
{
    WriteText("levels.cpkg.json", R"({
  "formatVersion": 1,
  "packageName": "Levels",
  "bundles": [
    {
      "name": "Props",
      "nodes": [ { "name": "Root" } ],
      "imports": [
        {
          "file": "room.fbx",
          "parent": "Root",
          "rename": { "nodes": { "Eye": "Camera1" } },
          "edits": {
            "Body": { "mobility": "movable", "children": [ { "name": "Grip" } ] },
            "Lamp": { "light": { "intensity": 2 } }
          }
        }
      ]
    }
  ]
}
)");

    // Whether the file folds its unit into its root nodes or not, the nodes build alike: in
    // meters, with unit scale, and turned from the file's axes to Canvas's, a quarter turn
    // about +Z.
    for (const bool folded : { false, true })
    {
        WriteText("room.fbx", NodesFbx(folded));
        const RunResult result = RunCpkg({ "build", "levels.cpkg.json" });
        ASSERT_EQ(result.Err, "") << folded;
        CCpkgReader reader;
        ASSERT_EQ(reader.Open("levels.cpkg"), Result::Success);
        const uint32_t root = AssetIndex(reader, 0, "Root");
        const uint32_t body = AssetIndex(reader, 0, "Body");
        const uint32_t lamp = AssetIndex(reader, 0, "Lamp");
        const uint32_t eye = AssetIndex(reader, 0, "Camera1");
        const uint32_t grip = AssetIndex(reader, 0, "Grip");
        const uint32_t count = reader.GetAssetCount(0);
        ASSERT_TRUE(root < count && body < count && lamp < count && eye < count && grip < count) << folded;

        {
            const CPayload payload(reader, 0, body);
            const CpkgTransformNodeData &node = payload.Header<CpkgTransformNodeData>();
            EXPECT_EQ(node.ParentIndex, int32_t(root));
            EXPECT_EQ(node.Mobility, CpkgMobility::Movable);
            const CpkgTransform &transform = node.LocalTransform;
            const float translation[] = { 0.0f, 0.0f, 2.0f };
            const float rotation[] = { 0.0f, 0.0f, 0.70710678f, 0.70710678f };
            for (size_t i = 0; i < 3; ++i)
            {
                EXPECT_NEAR(transform.Translation[i], translation[i], 1e-5f) << folded << i;
                EXPECT_NEAR(transform.Scale[i], 1.0f, 1e-5f) << folded << i;
            }
            for (size_t i = 0; i < 4; ++i)
                EXPECT_NEAR(transform.Rotation[i], rotation[i], 1e-5f) << folded << i;
        }
        {
            const CPayload payload(reader, 0, lamp);
            const CpkgTransformNodeData &node = payload.Header<CpkgTransformNodeData>();
            EXPECT_EQ(node.ParentIndex, int32_t(body));
            EXPECT_NEAR(node.LocalTransform.Translation[0], 1.0f, 1e-5f) << folded;
            EXPECT_EQ(CPayload(reader, 0, grip).Header<CpkgTransformNodeData>().ParentIndex, int32_t(body));
        }
        {
            const CPayload payload(reader, 0, AssetIndex(reader, 0, "Body.mesh"));
            EXPECT_NEAR(payload.Header<CpkgMeshData>().LocalBounds.Max[2], 1.0f, 1e-5f) << folded;
        }

        const uint32_t instance = AttachedIndex<CpkgMeshInstanceData>(reader, 0, AssetType::MeshInstance, body);
        ASSERT_LT(instance, count);
        {
            const CPayload payload(reader, 0, instance);
            EXPECT_EQ(payload.Text<CpkgMeshInstanceData>(payload.Header<CpkgMeshInstanceData>().MeshData), "Body.mesh");
        }

        // The edit sets the light's intensity, and keeps the rest of what the file gives it.
        const uint32_t light = AttachedIndex<CpkgLightData>(reader, 0, AssetType::Light, lamp);
        ASSERT_LT(light, count);
        {
            const CPayload payload(reader, 0, light);
            const CpkgLightData &data = payload.Header<CpkgLightData>();
            EXPECT_EQ(data.Type, LightType::Point);
            EXPECT_EQ(data.Intensity, 2.0f);
            EXPECT_EQ(data.Color[1], 0.5f);
        }

        // The camera's clip planes are distances, converted to meters, and its vertical field of
        // view is in radians.
        const uint32_t camera = AttachedIndex<CpkgCameraData>(reader, 0, AssetType::Camera, eye);
        ASSERT_LT(camera, count);
        {
            const CPayload payload(reader, 0, camera);
            const CpkgCameraData &data = payload.Header<CpkgCameraData>();
            EXPECT_NEAR(data.NearClip, 0.1f, 1e-6f) << folded;
            EXPECT_NEAR(data.FarClip, 10.0f, 1e-5f) << folded;
            EXPECT_NEAR(data.FovAngle, 1.04719755f, 1e-5f);
        }
    }
}

//------------------------------------------------------------------------------------------------
TEST_F(CpkgBuild, ImportsSkeletonsAndSkins)
{
    WriteText("goblin.fbx", SkinnedFbx());
    WriteText("levels.cpkg.json", R"({
  "formatVersion": 1,
  "packageName": "Levels",
  "bundles": [
    {
      "name": "Goblin",
      "imports": [
        { "file": "goblin.fbx" },
        { "file": "goblin.fbx", "nodes": false, "skeleton": "Hips.skeleton", "names": { "meshes": "B_{name}.mesh", "skins": "B_{name}.skin" } }
      ]
    }
  ]
}
)");
    const RunResult result = RunCpkg({ "build", "levels.cpkg.json" });
    ASSERT_EQ(result.Err, "");
    CCpkgReader reader;
    ASSERT_EQ(reader.Open("levels.cpkg"), Result::Success);
    const uint32_t count = reader.GetAssetCount(0);

    // Each tree of bones builds a skeleton, named after its root bone, with its bones' rest
    // transforms where the skin bound them. The second import builds none, and its skin uses
    // the first's skeleton.
    const uint32_t hips = AssetIndex(reader, 0, "Hips.skeleton");
    ASSERT_LT(hips, count);
    ASSERT_LT(AssetIndex(reader, 0, "Tail.skeleton"), count);
    {
        const CPayload payload(reader, 0, hips);
        const std::span<const CpkgSkeletonBone> bones = payload.Elements<CpkgSkeletonData>(payload.Header<CpkgSkeletonData>().Bones);
        ASSERT_EQ(bones.size(), 2u);
        EXPECT_EQ(payload.Text<CpkgSkeletonData>(bones[0].Name), "Hips");
        EXPECT_EQ(bones[0].ParentIndex, -1);
        EXPECT_NEAR(bones[0].RestTransform.Translation[2], 1.0f, 1e-6f);
        EXPECT_EQ(payload.Text<CpkgSkeletonData>(bones[1].Name), "Hand");
        EXPECT_EQ(bones[1].ParentIndex, 0);
        EXPECT_NEAR(bones[1].RestTransform.Translation[0], 0.5f, 1e-6f);
    }

    // A skin's bind poses are its bones' transforms in the mesh's space when bound, and its
    // mesh's bone streams name its bones, strongest first, with the weights the file gives them.
    // Without nodes, the mesh's space is
    // turned to Canvas's axes, so the file's +X is +Y.
    for (const bool nodes : { true, false })
    {
        const char *pSkin = nodes ? "Body.skin" : "B_Body.skin";
        const uint32_t skin = AssetIndex(reader, 0, pSkin);
        ASSERT_LT(skin, count) << pSkin;
        const CPayload payload(reader, 0, skin);
        const CpkgMeshSkinData &data = payload.Header<CpkgMeshSkinData>();
        EXPECT_EQ(payload.Text<CpkgMeshSkinData>(data.Skeleton), "Hips.skeleton");
        const std::span<const CpkgSpan<char>> bones = payload.Elements<CpkgMeshSkinData>(data.Bones);
        ASSERT_EQ(bones.size(), 2u);
        EXPECT_EQ(payload.Text<CpkgMeshSkinData>(bones[1]), "Hand");
        const std::span<const CpkgTransform> poses = payload.Elements<CpkgMeshSkinData>(data.BindPoses);
        ASSERT_EQ(poses.size(), 2u);
        EXPECT_NEAR(poses[1].Translation[nodes ? 0 : 1], 0.5f, 1e-6f) << pSkin;
        EXPECT_NEAR(poses[1].Translation[2], 1.0f, 1e-6f) << pSkin;
    }
    {
        const CPayload payload(reader, 0, AssetIndex(reader, 0, "Body.mesh"));
        const CpkgMeshDataGroup &group = payload.Elements<CpkgMeshData>(payload.Header<CpkgMeshData>().Groups)[0];
        const std::span<const CpkgVertexBoneIndices> indices = payload.Elements<CpkgMeshData>(group.BoneIndices);
        const std::span<const CpkgVertexBoneWeights> weights = payload.Elements<CpkgMeshData>(group.BoneWeights);
        ASSERT_EQ(indices.size(), 3u);
        ASSERT_EQ(weights.size(), 3u);
        EXPECT_EQ(indices[0].Index[0], 0u);
        EXPECT_EQ(weights[0].Weight[0], 1.0f);
        EXPECT_EQ(weights[0].Weight[1], 0.0f);
        EXPECT_EQ(indices[2].Index[0], 1u);
        EXPECT_NEAR(weights[2].Weight[0], 0.5f, 1e-6f);
        EXPECT_EQ(indices[2].Index[1], 0u);
        EXPECT_NEAR(weights[2].Weight[1], 0.25f, 1e-6f);
    }

    // A skeleton instance attaches to the root bone's parent node, or to a node built to carry
    // it, and the skinned mesh instance binds to it. Bones build no nodes; the bone that holds
    // Sword has a socket, under the skeleton instance's node, that holds it.
    const uint32_t armature = AssetIndex(reader, 0, "Armature");
    const uint32_t tail = AssetIndex(reader, 0, "Tail");
    const uint32_t body = AssetIndex(reader, 0, "Body");
    const uint32_t socket = AssetIndex(reader, 0, "Hand_Socket");
    const uint32_t sword = AssetIndex(reader, 0, "Sword");
    ASSERT_TRUE(armature < count && tail < count && body < count && socket < count && sword < count);
    EXPECT_EQ(AssetIndex(reader, 0, "Hips"), count);
    EXPECT_EQ(AssetIndex(reader, 0, "Hand"), count);

    const uint32_t instance = AttachedIndex<CpkgSkeletonInstanceData>(reader, 0, AssetType::SkeletonInstance, armature);
    ASSERT_LT(instance, count);
    {
        const CPayload payload(reader, 0, instance);
        EXPECT_EQ(payload.Text<CpkgSkeletonInstanceData>(payload.Header<CpkgSkeletonInstanceData>().Skeleton), "Hips.skeleton");
    }
    ASSERT_LT((AttachedIndex<CpkgSkeletonInstanceData>(reader, 0, AssetType::SkeletonInstance, tail)), count);
    {
        const CPayload payload(reader, 0, tail);
        EXPECT_EQ(payload.Header<CpkgTransformNodeData>().ParentIndex, -1);
    }
    {
        const CPayload payload(reader, 0, socket);
        const CpkgTransformNodeData &node = payload.Header<CpkgTransformNodeData>();
        EXPECT_EQ(node.ParentIndex, int32_t(instance));
        EXPECT_EQ(payload.Text<CpkgTransformNodeData>(node.ParentBone), "Hand");
    }
    {
        const CPayload payload(reader, 0, sword);
        const CpkgTransformNodeData &node = payload.Header<CpkgTransformNodeData>();
        EXPECT_EQ(node.ParentIndex, int32_t(socket));
        EXPECT_NEAR(node.LocalTransform.Translation[1], 0.25f, 1e-6f);
    }
    const uint32_t meshInstance = AttachedIndex<CpkgMeshInstanceData>(reader, 0, AssetType::MeshInstance, body);
    ASSERT_LT(meshInstance, count);
    {
        const CPayload payload(reader, 0, meshInstance);
        const CpkgMeshInstanceData &data = payload.Header<CpkgMeshInstanceData>();
        EXPECT_EQ(data.SkeletonInstanceIndex, int32_t(instance));
        EXPECT_EQ(payload.Text<CpkgMeshInstanceData>(data.Skin), "Body.skin");
    }
}

//------------------------------------------------------------------------------------------------
TEST_F(CpkgBuild, ImportsClips)
{
    WriteText("goblin.fbx", SkinnedFbx(true));
    WriteText("levels.cpkg.json", R"({
  "formatVersion": 1,
  "packageName": "Levels",
  "bundles": [
    {
      "name": "Goblin",
      "imports": [
        { "file": "goblin.fbx" },
        { "file": "goblin.fbx", "nodes": false, "skeleton": "Hips.skeleton",
          "names": { "meshes": "B_{name}.mesh", "skins": "B_{name}.skin", "skeletalAnimations": "B_{name}.anim" } }
      ]
    }
  ]
}
)");

    // Without nodes, the import builds the skeletal animation alone, and reports the node it
    // cannot animate.
    const RunResult result = RunCpkg({ "build", "levels.cpkg.json" });
    EXPECT_EQ(result.Status, CpkgTool::kExitSuccess);
    EXPECT_EQ(result.Err,
        "levels.cpkg.json:9:9: warning: the FBX clip \"Wave\" animates the FBX node \"Sword\", which builds no track, since the import builds no nodes [/bundles/0/imports/1]\n");
    CCpkgReader reader;
    ASSERT_EQ(reader.Open("levels.cpkg"), Result::Success);
    const uint32_t count = reader.GetAssetCount(0);

    // The two stacks of the action Wave build one clip, named after the action. Its bone
    // track builds a skeletal animation for the skeleton it animates, which the clip binds to
    // the skeleton's instance; its node track is the clip's, and the node it animates is movable.
    const uint32_t clip = AssetIndex(reader, 0, "Wave");
    const uint32_t wave = AssetIndex(reader, 0, "Wave.anim");
    ASSERT_LT(clip, count);
    ASSERT_LT(wave, count);
    ASSERT_LT(AssetIndex(reader, 0, "B_Wave.anim"), count);
    {
        const CPayload payload(reader, 0, wave);
        const CpkgSkeletalAnimationData &animation = payload.Header<CpkgSkeletalAnimationData>();
        EXPECT_EQ(payload.Text<CpkgSkeletalAnimationData>(animation.Skeleton), "Hips.skeleton");
        const std::span<const CpkgBoneTrack> tracks = payload.Elements<CpkgSkeletalAnimationData>(animation.Tracks);
        ASSERT_EQ(tracks.size(), 1u);
        EXPECT_EQ(payload.Text<CpkgSkeletalAnimationData>(tracks[0].Bone), "Hips");
        EXPECT_EQ(tracks[0].Property, CpkgAnimationProperty::Translation);
        const std::span<const float> times = payload.Elements<CpkgSkeletalAnimationData>(tracks[0].Times);
        const std::span<const float> values = payload.Elements<CpkgSkeletalAnimationData>(tracks[0].Values);
        ASSERT_EQ(times.size(), 2u);
        EXPECT_NEAR(times[1], 1.0f, 1e-6f);
        ASSERT_EQ(values.size(), 6u);
        EXPECT_NEAR(values[3], 1.0f, 1e-6f);
        EXPECT_NEAR(values[5], 1.0f, 1e-6f);
    }
    {
        const CPayload payload(reader, 0, clip);
        const CpkgAnimationClipData &data = payload.Header<CpkgAnimationClipData>();
        EXPECT_NEAR(data.Duration, 1.0f, 1e-6f);
        const std::span<const CpkgNodeTrack> tracks = payload.Elements<CpkgAnimationClipData>(data.Tracks);
        ASSERT_EQ(tracks.size(), 1u);
        EXPECT_EQ(tracks[0].Property, CpkgAnimationProperty::Translation);

        // The file's step builds one key that holds until the next.
        const std::span<const float> values = payload.Elements<CpkgAnimationClipData>(tracks[0].Values);
        const std::span<const AnimationInterpolation> interpolations = payload.Elements<CpkgAnimationClipData>(tracks[0].Interpolations);
        ASSERT_EQ(values.size(), 6u);
        EXPECT_NEAR(values[1], 0.25f, 1e-6f);
        EXPECT_NEAR(values[4], 0.5f, 1e-6f);
        ASSERT_EQ(interpolations.size(), 2u);
        EXPECT_EQ(interpolations[0], AnimationInterpolation::Step);

        const std::span<const CpkgSkeletalAnimationBinding> bindings = payload.Elements<CpkgAnimationClipData>(data.SkeletalAnimations);
        ASSERT_EQ(bindings.size(), 1u);
        EXPECT_EQ(payload.Text<CpkgAnimationClipData>(bindings[0].Animation), "Wave.anim");
    }
    {
        const CPayload payload(reader, 0, AssetIndex(reader, 0, "Sword"));
        EXPECT_EQ(payload.Header<CpkgTransformNodeData>().Mobility, CpkgMobility::Movable);
    }
    {
        const CPayload payload(reader, 0, AssetIndex(reader, 0, "Armature"));
        EXPECT_EQ(payload.Header<CpkgTransformNodeData>().Mobility, CpkgMobility::Static);
    }
}

//------------------------------------------------------------------------------------------------
// With "skeleton", a track on a bone the named skeleton lacks is left out, wherever in the package
// the skeleton is. The skin's bone the skeleton lacks is an error, as for any skin.
TEST_F(CpkgBuild, ReportsTracksASharedSkeletonLacks)
{
    WriteText("goblin.fbx", SkinnedFbx(true));
    WriteText("levels.cpkg.json", R"({
  "formatVersion": 1,
  "packageName": "Levels",
  "bundles": [
    { "name": "Rig", "skeletons": { "Arm": { "bones": [ { "name": "Hand" } ] } } },
    { "name": "Moves", "imports": [ { "file": "goblin.fbx", "nodes": false, "skeleton": "Rig/Arm" } ] }
  ]
}
)");
    const RunResult result = RunCpkg({ "build", "levels.cpkg.json" });
    EXPECT_EQ(result.Status, CpkgTool::kExitError);
    EXPECT_EQ(result.Err,
        "levels.cpkg.json:6:37: warning: the FBX clip \"Wave\" animates the FBX node \"Sword\", which builds no track, since the import builds no nodes [/bundles/1/imports/0]\n"
        "levels.cpkg.json:6:37: warning: the FBX clip \"Wave\" animates the FBX bone \"Hips\", which the skeleton \"Rig/Arm\" lacks; its track is left out [/bundles/1/imports/0]\n"
        "levels.cpkg.json:6:37: error: the skeleton \"Rig/Arm\" has no bone \"Hips\" [/bundles/1/imports/0]\n");
}

//------------------------------------------------------------------------------------------------
TEST_F(CpkgBuild, ReportsImportNodeErrors)
{
    WriteText("room.fbx", NodesFbx(false));
    WriteText("levels.cpkg.json", R"({
  "formatVersion": 1,
  "packageName": "Levels",
  "bundles": [
    {
      "name": "Props",
      "imports": [
        { "file": "room.fbx", "parent": "Nowhere", "edits": { "Lid": {}, "Body": { "light": { "intensity": 2 } } } },
        { "file": "room.fbx", "nodes": false, "names": { "meshes": "B_{name}" }, "parent": "Root",
          "rename": { "nodes": { "Body": "B" } }, "edits": { "Body": {} } }
      ]
    }
  ]
}
)");
    const RunResult result = RunCpkg({ "build", "levels.cpkg.json" });
    EXPECT_EQ(result.Status, CpkgTool::kExitError);
    EXPECT_EQ(result.Err,
        "levels.cpkg.json:8:41: error: the bundle has no node \"Nowhere\" [/bundles/0/imports/0/parent]\n"
        "levels.cpkg.json:8:63: error: the import builds no node \"Lid\" [/bundles/0/imports/0/edits/Lid]\n"
        "levels.cpkg.json:8:93: error: the node \"Body\" has no light, so the edit's light needs a \"type\" [/bundles/0/imports/0/edits/Body/light]\n"
        "levels.cpkg.json:9:92: error: \"parent\" applies only to an import that builds nodes, and this one has \"nodes\": false [/bundles/0/imports/1/parent]\n"
        "levels.cpkg.json:10:34: error: \"rename\" of \"nodes\" applies only to an import that builds nodes, and this one has \"nodes\": false [/bundles/0/imports/1/rename/nodes/Body]\n"
        "levels.cpkg.json:10:62: error: \"edits\" applies only to an import that builds nodes, and this one has \"nodes\": false [/bundles/0/imports/1/edits/Body]\n");
    EXPECT_FALSE(std::filesystem::exists("levels.cpkg"));
}

//------------------------------------------------------------------------------------------------
// A name in "clips", and a "rename" key of a kind of animation, names a clip in the file.
TEST_F(CpkgBuild, ReportsImportClipNames)
{
    WriteText("quad.fbx", kQuad);
    WriteText("levels.cpkg.json", R"({
  "formatVersion": 1,
  "packageName": "Levels",
  "bundles": [
    {
      "name": "Props",
      "imports": [
        { "file": "quad.fbx", "rename": { "skeletalAnimations": { "Walk": "W" } }, "clips": [ "Idle" ] }
      ]
    }
  ]
}
)");
    const RunResult result = RunCpkg({ "build", "levels.cpkg.json" });
    EXPECT_EQ(result.Status, CpkgTool::kExitError);
    EXPECT_EQ(result.Err,
        "levels.cpkg.json:8:67: error: the FBX file has no skeletal animation \"Walk\" [/bundles/0/imports/0/rename/skeletalAnimations/Walk]\n"
        "levels.cpkg.json:8:95: error: the FBX file has no clip \"Idle\" [/bundles/0/imports/0/clips/0]\n");
    EXPECT_FALSE(std::filesystem::exists("levels.cpkg"));
}

//------------------------------------------------------------------------------------------------
// Each FBX file of deps/FbxAssets builds, with its nodes and every clip. The assets are a
// submodule of their own; without them checked out, the test is skipped.
TEST_F(CpkgBuild, ImportsTheFbxAssets)
{
    std::error_code error;
    std::filesystem::directory_iterator files(CANVAS_FBX_ASSET_DIR, error);
    size_t built = 0;
    for (const std::filesystem::directory_entry &entry : error ? std::filesystem::directory_iterator() : files)
    {
        if (entry.path().extension() != ".fbx")
            continue;
        ++built;
        const std::string file = entry.path().generic_string();
        WriteText("assets.cpkg.json", R"({ "formatVersion": 1, "packageName": "Assets", "bundles": [ { "name": "A", "imports": [ { "file": ")"
            + file + R"(" } ] } ] })");
        const RunResult result = RunCpkg({ "build", "assets.cpkg.json" });
        EXPECT_EQ(result.Err, "") << file;
        if (result.Status == CpkgTool::kExitSuccess)
        {
            CCpkgReader reader;
            ASSERT_EQ(reader.Open("assets.cpkg"), Result::Success) << file;
            EXPECT_GT(reader.GetAssetCount(0), 0u) << file;

            // No root node carries a scale for the file's unit, only the scales authored in Blender.
            for (uint32_t i = 0; i < reader.GetAssetCount(0); ++i)
            {
                if (reader.GetAssetType(0, i) != AssetType::TransformNode)
                    continue;
                const CPayload payload(reader, 0, i);
                const CpkgTransformNodeData &node = payload.Header<CpkgTransformNodeData>();
                for (size_t axis = 0; node.ParentIndex == -1 && axis < 3; ++axis)
                    EXPECT_LT(std::abs(node.LocalTransform.Scale[axis]), 10.0f) << file << ": " << reader.GetAssetName(0, i);
            }
        }
    }
    if (built == 0)
        GTEST_SKIP() << "No FBX assets available in " << CANVAS_FBX_ASSET_DIR;
}

//------------------------------------------------------------------------------------------------
TEST_F(CpkgBuild, UsageErrors)
{
    RunResult result = RunCpkg({ "build" });
    EXPECT_EQ(result.Status, CpkgTool::kExitUsage);
    EXPECT_EQ(result.Err, "cpkg: error: build: missing argument <file>\nRun 'cpkg --help' for usage.\n");

    result = RunCpkg({ "build", "levels.json" });
    EXPECT_EQ(result.Status, CpkgTool::kExitUsage);
    EXPECT_EQ(result.Err, "cpkg: error: build: expected a .cpkg.json or .cproj.json file: levels.json\nRun 'cpkg --help' for usage.\n");

    result = RunCpkg({ "build", "missing.cpkg.json" });
    EXPECT_EQ(result.Status, CpkgTool::kExitError);
    EXPECT_EQ(result.Err, "missing.cpkg.json: error: file not found\n");
}
