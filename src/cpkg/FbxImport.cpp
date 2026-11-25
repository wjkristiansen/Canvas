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
namespace Math = Canvas::Math;

namespace
{

std::string_view View(const ufbx_string &text)
{
    return std::string_view(text.data, text.length);
}

Math::FloatVector3 ToVector(ufbx_vec3 v)
{
    return { float(v.x), float(v.y), float(v.z) };
}

Math::FloatQuaternion ToQuaternion(ufbx_quat q)
{
    return { float(q.x), float(q.y), float(q.z), float(q.w) };
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

// Reads the FBX file bytes holds, whose path is path. Its geometry and node transforms stay in
// the file's units and axes; the conversion to meters and Canvas's axes is the root node's
// transform, which the reader applies (see CFbxReader::Read).
ScenePtr LoadScene(std::string_view bytes, const std::string &path, std::string *pError)
{
    ufbx_load_opts options = {};
    options.generate_missing_normals = true;
    options.target_axes = kCanvasAxes;

    // A camera or light faces its local +X. ufbx's "front" is opposite the direction it faces.
    options.target_camera_axes = { UFBX_COORDINATE_AXIS_NEGATIVE_Y, UFBX_COORDINATE_AXIS_POSITIVE_Z, UFBX_COORDINATE_AXIS_NEGATIVE_X };
    options.target_light_axes = options.target_camera_axes;

    options.target_unit_meters = 1.0f;
    options.space_conversion = UFBX_SPACE_CONVERSION_TRANSFORM_ROOT;
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

// Whether the change of basis m, for row vectors, mirrors: whether its determinant is negative.
bool Mirrors(const Math::FloatMatrix3x3 &m)
{
    return Math::DotProduct(m[0], Math::CrossProduct(m[1], m[2])) < 0.0f;
}

// The change of basis from the axes a file declares to Canvas's, which the geometry of an
// import that builds no nodes takes. The identity for axes that name an axis twice or are
// unknown. Its rows, for row vectors, are the images of the file's +X, +Y, and +Z.
Math::FloatMatrix3x3 AxisChange(const ufbx_coordinate_axes &file)
{
    const ufbx_coordinate_axis from[] = { file.right, file.up, file.front };
    const ufbx_coordinate_axis to[] = { kCanvasAxes.right, kCanvasAxes.up, kCanvasAxes.front };
    Math::FloatMatrix3x3 change;
    bool mapped[3] = {};
    for (size_t i = 0; i < 3; ++i)
    {
        if (from[i] >= UFBX_COORDINATE_AXIS_UNKNOWN)
            return Math::FloatMatrix3x3::Identity();
        const size_t axis = size_t(from[i]) / 2;
        if (mapped[axis])
            return Math::FloatMatrix3x3::Identity();
        mapped[axis] = true;

        // The file's direction from[i] becomes Canvas's to[i].
        const float sign = size_t(from[i]) % 2 == 0 ? 1.0f : -1.0f;
        change[int(axis)][int(to[i]) / 2] = size_t(to[i]) % 2 == 0 ? sign : -sign;
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
        const ufbx_transform &root = m_pScene->root_node->local_transform;
        m_RootRotation = ToQuaternion(root.rotation);
        m_Unit = root.scale.x;
        m_Folds = Folds();
        m_PushedScale = m_Folds ? ufbx_real(1) : m_Unit;

        if (m_pScene->bones.count != 0 || m_pScene->skin_deformers.count != 0)
            m_Builder.Unsupported(m_Import.Where, "the skeletons and skins of an FBX file");
        if (!m_Import.Clips && Animates())
            m_Builder.Unsupported(m_Import.Where, "the animation of an FBX file");
        if (m_Import.Nodes && Mirrors(m_AxisChange))
            m_Builder.Unsupported(m_Import.Where, "the nodes of an FBX file in a left-handed axis system");
        BuildMaterials();
        BuildMeshes();
        if (m_Import.Nodes)
            BuildNodes();
        return true;
    }

private:
    // Whether the conversion to meters folds into the scale of the file's root nodes. Either way
    // the built content is the file's: a folded unit leaves the geometry and every node below the
    // root nodes unscaled, and otherwise the unit scales the geometry and every translation, and
    // the root nodes keep their scale. The unit folds when that leaves the root nodes' scales
    // nearer 1, measured as the sum of the magnitudes of their scales' logarithms, as for a file
    // that holds geometry in meters under root nodes scaled to its declared unit.
    bool Folds() const
    {
        if (m_Unit == 1)
            return false;
        double kept = 0.0;
        double folded = 0.0;
        for (const ufbx_node *pNode : m_pScene->root_node->children)
        {
            const ufbx_vec3 &s = pNode->local_transform.scale;
            for (const ufbx_real axis : { s.x, s.y, s.z })
            {
                if (axis == 0)
                    return false;
                kept += std::abs(std::log(std::abs(double(axis))));
                folded += std::abs(std::log(std::abs(double(axis) * m_Unit)));
            }
        }
        return folded < kept;
    }

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
                for (int c = 0; c < 3; ++c)
                    built.BaseColor[c] = float(baseColor.value_vec4.v[c]) * factor;
            }
            if (pbr.opacity.has_value)
                built.BaseColor[3] = float(pbr.opacity.value_real);
            const ufbx_material_map &emission = pbr.emission_color.has_value ? pbr.emission_color : fbx.emission_color;
            if (emission.has_value)
            {
                const float factor = pbr.emission_factor.has_value ? float(pbr.emission_factor.value_real) : 1.0f;
                for (int c = 0; c < 3; ++c)
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

    // A vertex position or direction of the file's geometry, in its node's space when the import
    // builds nodes, whose transforms then hold the change of axes and "rotation", and otherwise in
    // Canvas space.
    Math::FloatVector3 Position(ufbx_vec3 v) const
    {
        const Math::FloatVector3 p = ToVector(v) * float(m_PushedScale);
        if (m_Import.Nodes)
            return p * m_Import.Scale;
        return m_Builder.Position(p * m_AxisChange);
    }

    Math::FloatVector3 Direction(ufbx_vec3 v) const
    {
        if (m_Import.Nodes)
            return ToVector(v);
        return m_Builder.Direction(ToVector(v) * m_AxisChange);
    }

    // Appends the components of v to values.
    static void Append(std::vector<float> &values, const Math::FloatVector3 &v)
    {
        values.insert(values.end(), std::begin(v.V), std::end(v.V));
    }

    void BuildMeshes()
    {
        const bool mirrored = !m_Import.Nodes && Mirrors(m_AxisChange);
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
                        const Math::FloatVector3 normal = Direction(ufbx_get_vertex_vec3(&mesh.vertex_normal, index));
                        Append(group.Positions.Values, Position(ufbx_get_vertex_vec3(&mesh.vertex_position, index)));
                        Append(group.Normals->Values, normal);
                        if (hasUV)
                        {
                            // FBX's V runs up from the bottom of a texture, and Canvas's down from the top.
                            const ufbx_vec2 uv = ufbx_get_vertex_vec2(&mesh.vertex_uv, index);
                            group.UV0->Values.push_back(float(uv.x));
                            group.UV0->Values.push_back(1.0f - float(uv.y));
                        }
                        if (hasTangents)
                        {
                            const Math::FloatVector3 tangent = Direction(ufbx_get_vertex_vec3(&mesh.vertex_tangent, index));
                            const Math::FloatVector3 bitangent = Direction(ufbx_get_vertex_vec3(&mesh.vertex_bitangent, index));
                            const float handedness = Math::DotProduct(Math::CrossProduct(normal, tangent), bitangent);
                            Append(group.Tangents->Values, tangent);
                            group.Tangents->Values.push_back(handedness < 0.0f ? -1.0f : 1.0f);
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
            m_MeshPaths.emplace(pMesh, asset.Name);
        }
    }

    //--------------------------------------------------------------------------------------------
    // Nodes

    void BuildNodes()
    {
        std::vector<SourceNode> roots;
        for (const ufbx_node *pNode : m_pScene->root_node->children)
            roots.push_back(BuildNode(*pNode, true));
        m_Builder.AddNodes(std::move(roots));
    }

    SourceNode BuildNode(const ufbx_node &node, bool isRoot)
    {
        SourceNode built;
        built.Where = m_Import.Where;
        if (node.name.length != 0)
            built.Name = m_Builder.BuiltName(SourceImportKind::Nodes, std::string(View(node.name)));
        const std::string described = m_Builder.Describe(SourceImportKind::Nodes, View(node.name));

        // A root node takes the conversion to meters and Canvas's axes, the root's transform; the
        // unit's scale folds into its scale, or else scales its translation and everything below.
        const ufbx_transform &local = node.local_transform;
        if (isRoot)
        {
            const Math::FloatVector3 t = Math::RotateVectorByQuaternion(m_RootRotation, ToVector(local.translation) * float(m_Unit));
            const Math::FloatQuaternion r = (m_RootRotation * ToQuaternion(local.rotation)).Normalize();
            const float s = m_Folds ? float(m_Unit) : 1.0f;
            built.Transform = m_Builder.RootTransform(t, r, ToVector(local.scale) * s);
        }
        else
        {
            const ufbx_real scale = m_PushedScale * m_Import.Scale;
            built.Transform.Translation = ToVector(local.translation) * float(scale);
            SourceRotation &rotation = built.Transform.Rotation.emplace();
            rotation.Quaternion = ToQuaternion(local.rotation);
            rotation.Where = m_Import.Where;
            built.Transform.Scale = ToVector(local.scale);
        }

        if (node.mesh)
        {
            if (auto mesh = m_MeshPaths.find(node.mesh); mesh != m_MeshPaths.end())
            {
                for (size_t i = 0; i < node.materials.count; ++i)
                {
                    if (i >= node.mesh->materials.count || node.materials.data[i] != node.mesh->materials.data[i])
                    {
                        m_Builder.Unsupported(m_Import.Where, "the materials of " + described + " that differ from its mesh's");
                        break;
                    }
                }
                SourceMeshInstance &instance = built.Attachments.MeshInstances.emplace_back();
                instance.Mesh = mesh->second;
                instance.Where = m_Import.Where;
            }
        }
        if (node.camera)
            built.Attachments.Camera = BuildCamera(*node.camera, described);
        if (node.light)
            built.Attachments.Light = BuildLight(*node.light, described);

        for (const ufbx_node *pChild : node.children)
            built.Attachments.Children.push_back(BuildNode(*pChild, false));
        return built;
    }

    // A distance in the file's units, in meters with the import's "scale" applied.
    float Distance(double distance) const
    {
        return float(distance * m_Unit * m_Import.Scale);
    }

    SourceCamera BuildCamera(const ufbx_camera &camera, const std::string &described)
    {
        SourceCamera built;
        built.Where = m_Import.Where;
        if (camera.projection_mode != UFBX_PROJECTION_MODE_PERSPECTIVE)
            m_Builder.Unsupported(m_Import.Where, "the orthographic camera of " + described);
        built.NearClip = Distance(camera.near_plane);
        built.FarClip = Distance(camera.far_plane);
        if (camera.field_of_view_deg.y > 0)
            built.FovAngle = float(camera.field_of_view_deg.y * (kPi / 180.0));
        else if (camera.projection_mode == UFBX_PROJECTION_MODE_PERSPECTIVE)
            m_Builder.Error(m_Import.Where, "the camera of " + described + " has no field of view");
        return built;
    }

    SourceLight BuildLight(const ufbx_light &light, const std::string &described)
    {
        SourceLight built;
        built.Where = m_Import.Where;
        switch (light.type)
        {
        case UFBX_LIGHT_POINT: built.Type = Canvas::LightType::Point; break;
        case UFBX_LIGHT_DIRECTIONAL: built.Type = Canvas::LightType::Directional; break;
        case UFBX_LIGHT_SPOT: built.Type = Canvas::LightType::Spot; break;
        case UFBX_LIGHT_AREA: built.Type = Canvas::LightType::Area; break;
        default:
            built.Type = Canvas::LightType::Point;
            m_Builder.Unsupported(m_Import.Where, "the volume light of " + described);
            break;
        }
        built.Color = ToVector(light.color);
        built.Intensity = float(light.intensity);
        built.CastsShadows = light.cast_shadows;
        if (light.type == UFBX_LIGHT_SPOT)
        {
            built.SpotInnerAngle = float(light.inner_angle * (kPi / 180.0));
            built.SpotOuterAngle = float(light.outer_angle * (kPi / 180.0));
        }
        switch (light.decay)
        {
        case UFBX_LIGHT_DECAY_NONE: break;
        case UFBX_LIGHT_DECAY_LINEAR: built.AttenuationConstant = 0.0f; built.AttenuationLinear = 1.0f; break;
        case UFBX_LIGHT_DECAY_QUADRATIC: built.AttenuationConstant = 0.0f; built.AttenuationQuadratic = 1.0f; break;
        default: m_Builder.Unsupported(m_Import.Where, "the cubic decay of the light of " + described); break;
        }
        return built;
    }

    static constexpr double kPi = 3.14159265358979323846;

    CImportBuilder &m_Builder;
    const SourceImport &m_Import;
    ScenePtr m_pScene;
    fs::path m_Directory;                   // the FBX file's
    Math::FloatMatrix3x3 m_AxisChange = Math::FloatMatrix3x3::Identity();         // from the file's axes to Canvas's
    Math::FloatQuaternion m_RootRotation;  // the change to Canvas's axes
    ufbx_real m_Unit = 1;                     // meters per unit of the file
    bool m_Folds = false;                   // see Folds
    ufbx_real m_PushedScale = 1;              // the unit's scale on geometry and nodes below the root nodes

    std::unordered_map<const ufbx_texture *, std::optional<SourceString>> m_TexturePaths;
    std::unordered_map<std::string, SourceString> m_ImagesByKey;    // by embedded bytes or file
    std::unordered_map<const ufbx_material *, SourceString> m_MaterialPaths;
    std::unordered_map<const ufbx_mesh *, SourceString> m_MeshPaths;
};

} // namespace

//------------------------------------------------------------------------------------------------
bool ReadFbxImport(CImportBuilder &builder)
{
    return CFbxReader(builder).Read();
}

} // namespace CpkgTool
