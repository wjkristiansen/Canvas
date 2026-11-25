//================================================================================================
// FbxImport - the reader of an import's FBX file
//
// Reads the file with ufbx, converts its content to CanvasPkg's conventions, and adds what the
// import builds from it through a CImportBuilder (see Import.h). See "Imports" in
// src/cpkg/README.md and "Exchange Formats" in PackageSource.md.
//================================================================================================

#include "Import.h"

#include <ufbx.h>

namespace CpkgTool
{

namespace fs = std::filesystem;

namespace
{

std::string_view View(const ufbx_string &text)
{
    return std::string_view(text.data, text.length);
}

Vec3 ToVec3(ufbx_vec3 v)
{
    return { float(v.x), float(v.y), float(v.z) };
}

struct SceneDeleter
{
    void operator()(ufbx_scene *pScene) const { ufbx_free_scene(pScene); }
};

using ScenePtr = std::unique_ptr<ufbx_scene, SceneDeleter>;

// ufbx's "front" is the direction a model faces, and its "right" is the viewer's right when
// facing the model, which is the model's left. Canvas's basis is +X forward, +Y left, +Z up, so
// a model's front maps to +X and its left to +Y.
constexpr ufbx_coordinate_axes kCanvasAxes = { UFBX_COORDINATE_AXIS_POSITIVE_Y, UFBX_COORDINATE_AXIS_POSITIVE_Z, UFBX_COORDINATE_AXIS_POSITIVE_X };

// Reads the FBX file bytes holds, whose path is path. Its geometry is scaled to meters; the
// change to Canvas's axes is in its nodes' transforms (see AxisChange).
ScenePtr LoadScene(std::string_view bytes, const std::string &path, std::string *pError)
{
    ufbx_load_opts options = {};
    options.generate_missing_normals = true;
    options.target_axes = kCanvasAxes;

    // A camera or light faces its local +X. ufbx's "front" is opposite the direction it faces.
    options.target_camera_axes = { UFBX_COORDINATE_AXIS_NEGATIVE_Y, UFBX_COORDINATE_AXIS_POSITIVE_Z, UFBX_COORDINATE_AXIS_NEGATIVE_X };
    options.target_light_axes = options.target_camera_axes;

    options.target_unit_meters = 1.0f;
    options.space_conversion = UFBX_SPACE_CONVERSION_MODIFY_GEOMETRY;
    options.geometry_transform_handling = UFBX_GEOMETRY_TRANSFORM_HANDLING_MODIFY_GEOMETRY;
    options.inherit_mode_handling = UFBX_INHERIT_MODE_HANDLING_HELPER_NODES;
    options.filename = ufbx_string{ path.data(), path.size() };

    ufbx_error error = {};
    ScenePtr pScene(ufbx_load_memory(bytes.data(), bytes.size(), &options, &error));
    if (!pScene)
        *pError = std::string(View(error.description));
    return pScene;
}

// The file name path ends in, without its extension, for either separator.
std::string_view FileStem(std::string_view path)
{
    const size_t slash = path.find_last_of("/\\");
    if (slash != std::string_view::npos)
        path.remove_prefix(slash + 1);
    const size_t dot = path.rfind('.');
    if (dot != std::string_view::npos && dot != 0)
        path = path.substr(0, dot);
    return path;
}

// A 3x3 matrix of row vectors: its rows are the images of +X, +Y, and +Z.
using Mat3 = std::array<Vec3, 3>;

constexpr Mat3 kIdentity = { Vec3{ 1.0f, 0.0f, 0.0f }, Vec3{ 0.0f, 1.0f, 0.0f }, Vec3{ 0.0f, 0.0f, 1.0f } };

Vec3 Transform(const Vec3 &v, const Mat3 &m)
{
    Vec3 result = {};
    for (size_t row = 0; row < 3; ++row)
    {
        for (size_t column = 0; column < 3; ++column)
            result[column] += v[row] * m[row][column];
    }
    return result;
}

float Determinant(const Mat3 &m)
{
    return m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1])
        - m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0])
        + m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]);
}

// The change of basis from the axes a file declares to Canvas's. ufbx applies it to a scene's
// nodes, not to the geometry they hold, so the geometry of an import that builds no nodes takes
// it here. The identity for axes that name an axis twice or are unknown.
Mat3 AxisChange(const ufbx_coordinate_axes &file)
{
    const ufbx_coordinate_axis from[] = { file.right, file.up, file.front };
    const ufbx_coordinate_axis to[] = { kCanvasAxes.right, kCanvasAxes.up, kCanvasAxes.front };
    Mat3 change = {};
    bool mapped[3] = {};
    for (size_t i = 0; i < 3; ++i)
    {
        if (from[i] >= UFBX_COORDINATE_AXIS_UNKNOWN)
            return kIdentity;
        const size_t axis = size_t(from[i]) / 2;
        if (mapped[axis])
            return kIdentity;
        mapped[axis] = true;

        // The file's direction from[i] becomes Canvas's to[i].
        const float sign = size_t(from[i]) % 2 == 0 ? 1.0f : -1.0f;
        change[axis][size_t(to[i]) / 2] = size_t(to[i]) % 2 == 0 ? sign : -sign;
    }
    return change;
}

// Reads one import's FBX file and builds its content.
class CFbxReader
{
public:
    explicit CFbxReader(CImportBuilder &builder) :
        m_Builder(builder),
        m_Import(builder.GetImport())
    {
        builder.SetFormat("FBX");
    }

    bool Read()
    {
        const SourceFileRef &file = m_Import.File;
        RecordInput(m_Builder.GetPackage(), file.Path);
        std::string bytes;
        const FileResult read = ReadWholeFile(PathOf(file.Path), &bytes);
        if (read != FileResult::Success)
        {
            m_Builder.Error(file.Where, (read == FileResult::NotFound ? "file not found: " : "cannot read the file: ") + Quoted(file.Written));
            return false;
        }
        std::string error;
        m_pScene = LoadScene(bytes, file.Path, &error);
        if (!m_pScene)
        {
            m_Builder.Error(file.Where, "cannot read the FBX file (" + error + "): " + Quoted(file.Written));
            return false;
        }
        m_Directory = PathOf(file.Path).parent_path();
        m_AxisChange = AxisChange(m_pScene->settings.axes);

        if (m_pScene->bones.count != 0 || m_pScene->skin_deformers.count != 0)
            m_Builder.Unsupported(m_Import.Where, "the skeletons and skins of an FBX file");
        if (!m_Import.Clips && Animates())
            m_Builder.Unsupported(m_Import.Where, "the animation of an FBX file");
        BuildMaterials();
        BuildMeshes();
        return true;
    }

private:
    // Whether any animation stack of the file animates a property.
    bool Animates() const
    {
        for (const ufbx_anim_stack *pStack : m_pScene->anim_stacks)
        {
            for (const ufbx_anim_layer *pLayer : pStack->layers)
            {
                if (pLayer->anim_props.count != 0)
                    return true;
            }
        }
        return false;
    }

    //--------------------------------------------------------------------------------------------
    // Images

    // The path of the image a material's texture role uses: an image the import builds, or the
    // image "replace" names in its place. Empty when the texture builds no image, which it
    // reports.
    std::optional<SourceString> TexturePath(const ufbx_texture &texture, const ufbx_material &material)
    {
        if (auto found = m_TexturePaths.find(&texture); found != m_TexturePaths.end())
            return found->second;

        const std::string_view recorded = texture.relative_filename.length != 0 ? View(texture.relative_filename) : View(texture.absolute_filename);
        const std::string name = CImportBuilder::FileName(SourceImportKind::Images, recorded.empty() ? View(texture.name) : FileStem(recorded), texture.typed_id);
        std::optional<SourceString> path;
        if (const SourceString *pReplacement = m_Builder.ReplacementImage(name))
            path = *pReplacement;
        else
            path = BuildImage(texture, material, recorded, name);
        m_TexturePaths.emplace(&texture, path);
        return path;
    }

    // Builds the image of texture, which material uses, unless an image of the same bytes or
    // file is built already, and returns its path.
    std::optional<SourceString> BuildImage(const ufbx_texture &texture, const ufbx_material &material, std::string_view recorded, const std::string &name)
    {
        const std::string described = m_Builder.Describe(SourceImportKind::Images, name) + " of "
            + m_Builder.Describe(SourceImportKind::Materials, View(material.name));
        SourceImage image;
        image.Where = m_Import.Where;
        SourceFileRef &file = image.Files.emplace_back();
        file.Where = m_Import.Where;
        file.Written = std::string(recorded.empty() ? View(texture.name) : recorded);
        std::string key;
        if (texture.content.size != 0)
        {
            image.Embedded.emplace(static_cast<const char *>(texture.content.data), texture.content.size);
            key = "embedded:" + std::to_string(reinterpret_cast<uintptr_t>(texture.content.data));
        }
        else
        {
            std::optional<fs::path> found;
            if (texture.relative_filename.length != 0)
            {
                const fs::path relative = (m_Directory / PathOf(View(texture.relative_filename))).lexically_normal();
                if (Exists(relative))
                    found = relative;
            }
            if (!found && texture.absolute_filename.length != 0)
            {
                const fs::path absolute = PathOf(View(texture.absolute_filename)).lexically_normal();
                if (Exists(absolute))
                    found = absolute;
            }
            if (!found)
            {
                if (recorded.empty())
                    m_Builder.Error(m_Import.Where, described + " names no file and embeds no image");
                else
                    m_Builder.Error(m_Import.Where, described + " is not found: " + Quoted(recorded));
                return std::nullopt;
            }
            file.Path = Utf8Of(*found);
            key = "file:" + file.Path;
        }

        if (auto built = m_ImagesByKey.find(key); built != m_ImagesByKey.end())
            return built->second;
        SourceNamed<SourceImage> &asset = m_Builder.GetBundle().Images.emplace_back();
        asset.Name = m_Builder.BuiltName(SourceImportKind::Images, name);
        asset.Value = std::move(image);
        m_ImagesByKey.emplace(key, asset.Name);
        return asset.Name;
    }

    //--------------------------------------------------------------------------------------------
    // Materials

    // The texture of a material role: the PBR map's, or else the classic FBX map's.
    static const ufbx_texture *RoleTexture(const ufbx_material_map &pbr, const ufbx_material_map &fbx)
    {
        if (pbr.texture && pbr.texture_enabled)
            return pbr.texture;
        if (fbx.texture && fbx.texture_enabled)
            return fbx.texture;
        return pbr.texture ? pbr.texture : fbx.texture;
    }

    void BuildMaterials()
    {
        for (const ufbx_material *pMaterial : m_pScene->materials)
        {
            const ufbx_material &material = *pMaterial;
            const std::string name = CImportBuilder::FileName(SourceImportKind::Materials, View(material.name), material.typed_id);
            if (const SourceString *pReplacement = m_Builder.ReplacementMaterial(name))
            {
                m_MaterialPaths.emplace(pMaterial, *pReplacement);
                continue;
            }

            SourceMaterial built;
            built.Where = m_Import.Where;
            const ufbx_material_pbr_maps &pbr = material.pbr;
            const ufbx_material_fbx_maps &fbx = material.fbx;
            const ufbx_material_map &baseColor = pbr.base_color.has_value ? pbr.base_color : fbx.diffuse_color;
            if (baseColor.has_value)
            {
                const float factor = pbr.base_factor.has_value ? float(pbr.base_factor.value_real) : 1.0f;
                for (size_t c = 0; c < 3; ++c)
                    built.BaseColor[c] = float(baseColor.value_vec4.v[c]) * factor;
            }
            if (pbr.opacity.has_value)
                built.BaseColor[3] = float(pbr.opacity.value_real);
            const ufbx_material_map &emission = pbr.emission_color.has_value ? pbr.emission_color : fbx.emission_color;
            if (emission.has_value)
            {
                const float factor = pbr.emission_factor.has_value ? float(pbr.emission_factor.value_real) : 1.0f;
                for (size_t c = 0; c < 3; ++c)
                    built.Emissive[c] = float(emission.value_vec4.v[c]) * factor;
            }
            if (pbr.roughness.has_value)
                built.Roughness = float(pbr.roughness.value_real);
            if (pbr.metalness.has_value)
                built.Metallic = float(pbr.metalness.value_real);
            if (pbr.ambient_occlusion.has_value)
                built.AmbientOcclusion = float(pbr.ambient_occlusion.value_real);

            const ufbx_texture *textures[] = {
                RoleTexture(pbr.base_color, fbx.diffuse_color),
                RoleTexture(pbr.normal_map, fbx.normal_map),
                RoleTexture(pbr.roughness, pbr.roughness),
                RoleTexture(pbr.metalness, pbr.metalness),
                RoleTexture(pbr.ambient_occlusion, pbr.ambient_occlusion),
                RoleTexture(pbr.emission_color, fbx.emission_color),
            };
            static_assert(std::size(textures) == size_t(SourceTextureRole::Count));
            for (size_t role = 0; role < std::size(textures); ++role)
            {
                if (textures[role])
                    built.Textures[role] = TexturePath(*textures[role], material);
            }

            SourceNamed<SourceMaterial> &asset = m_Builder.GetBundle().Materials.emplace_back();
            asset.Name = m_Builder.BuiltName(SourceImportKind::Materials, name);
            asset.Value = std::move(built);
            m_MaterialPaths.emplace(pMaterial, asset.Name);
        }
    }

    //--------------------------------------------------------------------------------------------
    // Meshes

    Vec3 Position(ufbx_vec3 v) const
    {
        return m_Builder.Position(Transform(ToVec3(v), m_AxisChange));
    }

    Vec3 Direction(ufbx_vec3 v) const
    {
        return m_Builder.Direction(Transform(ToVec3(v), m_AxisChange));
    }

    void BuildMeshes()
    {
        const bool mirrored = Determinant(m_AxisChange) < 0.0f;
        for (const ufbx_mesh *pMesh : m_pScene->meshes)
        {
            const ufbx_mesh &mesh = *pMesh;
            if (mesh.skin_deformers.count != 0)
                continue;   // Read reported the file's skins

            const bool hasUV = mesh.vertex_uv.exists;
            const bool hasTangents = hasUV && mesh.vertex_tangent.exists && mesh.vertex_bitangent.exists;
            SourceMesh built;
            built.Where = m_Import.Where;
            std::vector<uint32_t> corners(mesh.max_face_triangles * 3);
            for (size_t part = 0; part < mesh.material_parts.count; ++part)
            {
                const ufbx_mesh_part &faces = mesh.material_parts.data[part];
                if (faces.num_triangles == 0)
                    continue;

                SourceMeshGroup &group = built.Groups.emplace_back();
                group.Where = m_Import.Where;
                if (part < mesh.materials.count && mesh.materials.data[part])
                {
                    if (auto material = m_MaterialPaths.find(mesh.materials.data[part]); material != m_MaterialPaths.end())
                        group.Material = material->second;
                }
                group.Normals.emplace();
                if (hasUV)
                    group.UV0.emplace();
                if (hasTangents)
                    group.Tangents.emplace();

                for (const uint32_t faceIndex : faces.face_indices)
                {
                    const uint32_t triangles = ufbx_triangulate_face(corners.data(), corners.size(), pMesh, mesh.faces.data[faceIndex]);
                    if (mirrored)
                    {
                        // A mirroring change of axes reverses each triangle's winding, which this restores.
                        for (size_t t = 0; t < triangles; ++t)
                            std::swap(corners[t * 3 + 1], corners[t * 3 + 2]);
                    }
                    for (size_t c = 0; c < size_t(triangles) * 3; ++c)
                    {
                        const uint32_t index = corners[c];
                        const Vec3 position = Position(ufbx_get_vertex_vec3(&mesh.vertex_position, index));
                        const Vec3 normal = Direction(ufbx_get_vertex_vec3(&mesh.vertex_normal, index));
                        group.Positions.Values.insert(group.Positions.Values.end(), position.begin(), position.end());
                        group.Normals->Values.insert(group.Normals->Values.end(), normal.begin(), normal.end());
                        if (hasUV)
                        {
                            // FBX's V runs up from the bottom of a texture, and Canvas's down from the top.
                            const ufbx_vec2 uv = ufbx_get_vertex_vec2(&mesh.vertex_uv, index);
                            group.UV0->Values.push_back(float(uv.x));
                            group.UV0->Values.push_back(1.0f - float(uv.y));
                        }
                        if (hasTangents)
                        {
                            const Vec3 tangent = Direction(ufbx_get_vertex_vec3(&mesh.vertex_tangent, index));
                            const Vec3 bitangent = Direction(ufbx_get_vertex_vec3(&mesh.vertex_bitangent, index));
                            const Vec3 cross = {
                                normal[1] * tangent[2] - normal[2] * tangent[1],
                                normal[2] * tangent[0] - normal[0] * tangent[2],
                                normal[0] * tangent[1] - normal[1] * tangent[0],
                            };
                            const float dot = cross[0] * bitangent[0] + cross[1] * bitangent[1] + cross[2] * bitangent[2];
                            group.Tangents->Values.insert(group.Tangents->Values.end(), tangent.begin(), tangent.end());
                            group.Tangents->Values.push_back(dot < 0.0f ? -1.0f : 1.0f);
                        }
                    }
                }
            }

            const std::string name = CImportBuilder::FileName(SourceImportKind::Meshes, View(mesh.name), mesh.typed_id);
            const SourceString builtName = m_Builder.BuiltName(SourceImportKind::Meshes, name);
            if (built.Groups.empty())
            {
                m_Builder.Warning(m_Import.Where, m_Builder.Describe(SourceImportKind::Meshes, name) + " has no faces, and builds no mesh");
                continue;
            }
            SourceNamed<SourceMesh> &asset = m_Builder.GetBundle().Meshes.emplace_back();
            asset.Name = builtName;
            asset.Value = std::move(built);
        }
    }

    CImportBuilder &m_Builder;
    const SourceImport &m_Import;
    ScenePtr m_pScene;
    fs::path m_Directory;                   // the FBX file's
    Mat3 m_AxisChange = kIdentity;          // from the file's axes to Canvas's

    std::unordered_map<const ufbx_texture *, std::optional<SourceString>> m_TexturePaths;
    std::unordered_map<std::string, SourceString> m_ImagesByKey;    // by embedded bytes or file
    std::unordered_map<const ufbx_material *, SourceString> m_MaterialPaths;
};

} // namespace

//------------------------------------------------------------------------------------------------
bool ReadFbxImport(CImportBuilder &builder)
{
    return CFbxReader(builder).Read();
}

} // namespace CpkgTool
