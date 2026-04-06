// CanvasFbx.cpp : Scene import entry point
//

#include "pch.h"
#include "CanvasFbx.h"

#include <unordered_map>

namespace Canvas
{
namespace Fbx
{

namespace
{

constexpr size_t kMaxImportNodes = 1000000;
constexpr size_t kMaxImportWarnings = 1000000;

//------------------------------------------------------------------------------------------------
void AddDiag(_Inout_ ImportedScene *pScene, DiagLevel level, const std::string &message)
{
    pScene->Diagnostics.push_back({ level, message });
}

//------------------------------------------------------------------------------------------------
std::string ToUtf8(const wchar_t *pWide)
{
    if (!pWide)
        return std::string();
    auto u8path = std::filesystem::path(pWide).u8string();
    return std::string(reinterpret_cast<const char *>(u8path.data()), u8path.size());
}

#if CANVASFBX_HAS_UFBX

static_assert(sizeof(ufbx_real) == sizeof(float),
    "CanvasFbx expects UFBX_REAL_IS_FLOAT=1 so ufbx struct layouts match the linked ufbx library");

//------------------------------------------------------------------------------------------------
std::string ToStdString(ufbx_string s)
{
    if (!s.data || s.length == 0)
        return std::string();
    return std::string(s.data, s.length);
}

//------------------------------------------------------------------------------------------------
Math::FloatVector4 ToCanvasPosition(ufbx_vec3 v)
{
    return Math::FloatVector4(static_cast<float>(v.x), static_cast<float>(v.y), static_cast<float>(v.z), 1.0f);
}

//------------------------------------------------------------------------------------------------
Math::FloatVector4 ToCanvasNormal(ufbx_vec3 v)
{
    return Math::FloatVector4(static_cast<float>(v.x), static_cast<float>(v.y), static_cast<float>(v.z), 0.0f);
}

//------------------------------------------------------------------------------------------------
Math::FloatQuaternion ToCanvasQuaternion(ufbx_quat q)
{
    return Math::FloatQuaternion(
        static_cast<float>(q.x),
        static_cast<float>(q.y),
        static_cast<float>(q.z),
        static_cast<float>(q.w));
}

//------------------------------------------------------------------------------------------------
Math::FloatMatrix4x4 ToCanvasAffineMatrix(_In_ const ufbx_matrix &m)
{
    Math::FloatMatrix4x4 result = Math::FloatMatrix4x4::Identity();

    // ufbx stores affine basis vectors in columns; Canvas stores them in rows.
    result[0][0] = static_cast<float>(m.m00);
    result[0][1] = static_cast<float>(m.m10);
    result[0][2] = static_cast<float>(m.m20);
    result[1][0] = static_cast<float>(m.m01);
    result[1][1] = static_cast<float>(m.m11);
    result[1][2] = static_cast<float>(m.m21);
    result[2][0] = static_cast<float>(m.m02);
    result[2][1] = static_cast<float>(m.m12);
    result[2][2] = static_cast<float>(m.m22);
    result[3][0] = static_cast<float>(m.m03);
    result[3][1] = static_cast<float>(m.m13);
    result[3][2] = static_cast<float>(m.m23);
    return result;
}

//------------------------------------------------------------------------------------------------
Math::FloatVector4 ExtractCanvasScale(_In_ const Math::FloatMatrix4x4 &m)
{
    const Math::FloatVector4 row0(m[0][0], m[0][1], m[0][2], 0.0f);
    const Math::FloatVector4 row1(m[1][0], m[1][1], m[1][2], 0.0f);
    const Math::FloatVector4 row2(m[2][0], m[2][1], m[2][2], 0.0f);
    return Math::FloatVector4(
        std::sqrt(Math::DotProduct(row0, row0)),
        std::sqrt(Math::DotProduct(row1, row1)),
        std::sqrt(Math::DotProduct(row2, row2)),
        0.0f);
}

//------------------------------------------------------------------------------------------------
Math::FloatQuaternion ExtractCanvasRotation(_In_ const Math::FloatMatrix4x4 &m)
{
    Math::FloatMatrix4x4 rotation = m;

    auto NormalizeRow = [](Math::FloatMatrix4x4 &matrix, int row)
    {
        const float x = matrix[row][0];
        const float y = matrix[row][1];
        const float z = matrix[row][2];
        const float lenSq = x * x + y * y + z * z;
        if (lenSq > 1e-12f)
        {
            const float invLen = 1.0f / std::sqrt(lenSq);
            matrix[row][0] *= invLen;
            matrix[row][1] *= invLen;
            matrix[row][2] *= invLen;
        }
    };

    NormalizeRow(rotation, 0);
    NormalizeRow(rotation, 1);
    NormalizeRow(rotation, 2);

    rotation[0][3] = rotation[1][3] = rotation[2][3] = 0.0f;
    rotation[3][0] = rotation[3][1] = rotation[3][2] = 0.0f;
    rotation[3][3] = 1.0f;

    return Math::QuaternionFromRotationMatrix(rotation).Normalize();
}

//------------------------------------------------------------------------------------------------
Math::FloatQuaternion ToCanvasNodeRotation(_In_ const ufbx_node *pNode, _Inout_ ImportedScene *pScene)
{
    if (!pNode)
        return Math::FloatQuaternion();

    (void)pScene;

    const Math::FloatMatrix4x4 localMatrix = ToCanvasAffineMatrix(pNode->node_to_parent);
    return ExtractCanvasRotation(localMatrix);
}

//------------------------------------------------------------------------------------------------
Math::FloatVector4 ComputeFaceNormal(_In_ const Math::FloatVector4 &p0, _In_ const Math::FloatVector4 &p1, _In_ const Math::FloatVector4 &p2)
{
    const Math::FloatVector4 e1 = p1 - p0;
    const Math::FloatVector4 e2 = p2 - p0;
    Math::FloatVector4 n = Math::CrossProduct(e1, e2);
    const float lenSq = Math::DotProduct(n, n);
    if (lenSq <= 1e-20f)
        return Math::FloatVector4(0.0f, 0.0f, 1.0f, 0.0f);

    const float invLen = 1.0f / std::sqrt(lenSq);
    n.X *= invLen;
    n.Y *= invLen;
    n.Z *= invLen;
    n.W = 0.0f;
    return n;
}

//------------------------------------------------------------------------------------------------
bool ImportMesh(
    _In_ const ufbx_mesh *pMesh,
    _In_ bool triangulate,
    _Inout_ ImportedMesh *pOut,
    _Inout_ ImportedScene *pScene)
{
    pOut->Name = ToStdString(pMesh->name);
    pOut->Positions.clear();
    pOut->Normals.clear();
    pOut->Bounds.Reset();

    if (!pMesh->vertex_position.exists)
    {
        AddDiag(pScene, DiagLevel::Error, "Mesh '" + pOut->Name + "' has no vertex positions");
        return false;
    }

    const bool hasNormals = pMesh->vertex_normal.exists;
    bool warnedInvalidNormal = false;
    std::vector<uint32_t> triIndices(pMesh->max_face_triangles * 3);

    for (size_t fi = 0; fi < pMesh->faces.count; ++fi)
    {
        const ufbx_face face = pMesh->faces[fi];
        if (face.num_indices < 3)
        {
            AddDiag(pScene, DiagLevel::Warning, "Mesh '" + pOut->Name + "' has degenerate face with < 3 indices");
            continue;
        }

        uint32_t numTris = 0;
        if (triangulate)
        {
            numTris = ufbx_triangulate_face(triIndices.data(), triIndices.size(), pMesh, face);
        }
        else
        {
            if (face.num_indices != 3)
            {
                AddDiag(pScene, DiagLevel::Warning, "Mesh '" + pOut->Name + "' has non-triangle face while triangulation disabled");
                continue;
            }

            triIndices[0] = face.index_begin;
            triIndices[1] = face.index_begin + 1;
            triIndices[2] = face.index_begin + 2;
            numTris = 1;
        }

        for (uint32_t ti = 0; ti < numTris; ++ti)
        {
            const uint32_t i0 = triIndices[ti * 3 + 0];
            const uint32_t i1 = triIndices[ti * 3 + 1];
            const uint32_t i2 = triIndices[ti * 3 + 2];

            const Math::FloatVector4 p0 = ToCanvasPosition(ufbx_get_vertex_vec3(&pMesh->vertex_position, i0));
            const Math::FloatVector4 p1 = ToCanvasPosition(ufbx_get_vertex_vec3(&pMesh->vertex_position, i1));
            const Math::FloatVector4 p2 = ToCanvasPosition(ufbx_get_vertex_vec3(&pMesh->vertex_position, i2));

            Math::FloatVector4 n0;
            Math::FloatVector4 n1;
            Math::FloatVector4 n2;
            if (hasNormals)
            {
                n0 = ToCanvasNormal(ufbx_get_vertex_vec3(&pMesh->vertex_normal, i0));
                n1 = ToCanvasNormal(ufbx_get_vertex_vec3(&pMesh->vertex_normal, i1));
                n2 = ToCanvasNormal(ufbx_get_vertex_vec3(&pMesh->vertex_normal, i2));
            }
            else
            {
                const Math::FloatVector4 fn = ComputeFaceNormal(p0, p1, p2);
                n0 = fn;
                n1 = fn;
                n2 = fn;
            }

            auto SanitizeNormal = [&](Math::FloatVector4 &n)
            {
                n.W = 0.0f;
                const float lenSq = Math::DotProduct(n, n);
                if (lenSq <= 1e-20f)
                {
                    n = Math::FloatVector4(0.0f, 0.0f, 1.0f, 0.0f);
                    if (!warnedInvalidNormal)
                    {
                        AddDiag(pScene, DiagLevel::Warning,
                            "Mesh '" + pOut->Name + "' contains degenerate normals; replaced with +Z fallback");
                        warnedInvalidNormal = true;
                    }
                    return;
                }

                const float invLen = 1.0f / std::sqrt(lenSq);
                n.X *= invLen;
                n.Y *= invLen;
                n.Z *= invLen;
            };

            SanitizeNormal(n0);
            SanitizeNormal(n1);
            SanitizeNormal(n2);

            pOut->Positions.push_back(p0);
            pOut->Positions.push_back(p1);
            pOut->Positions.push_back(p2);

            pOut->Normals.push_back(n0);
            pOut->Normals.push_back(n1);
            pOut->Normals.push_back(n2);

            pOut->Bounds.ExpandToInclude(p0);
            pOut->Bounds.ExpandToInclude(p1);
            pOut->Bounds.ExpandToInclude(p2);
        }
    }

    if (pOut->Positions.empty())
    {
        AddDiag(pScene, DiagLevel::Warning, "Mesh '" + pOut->Name + "' produced no triangles");
        return false;
    }

    return true;
}

//------------------------------------------------------------------------------------------------
bool ImportLight(
    _In_ const ufbx_light *pLight,
    _Inout_ ImportedLight *pOut,
    _Inout_ ImportedScene *pScene)
{
    pOut->Name = ToStdString(pLight->name);
    pOut->Color = Math::FloatVector4(
        static_cast<float>(pLight->color.x),
        static_cast<float>(pLight->color.y),
        static_cast<float>(pLight->color.z),
        1.0f);
    pOut->Intensity = static_cast<float>(pLight->intensity);

    const float rawIntensityProperty = static_cast<float>(ufbx_find_real(&pLight->props, "Intensity", 100.0));
    const float decayStartProperty = static_cast<float>(ufbx_find_real(&pLight->props, "DecayStart", 0.0));
    const float farAttenuationEnd = static_cast<float>(ufbx_find_real(&pLight->props, "FarAttenuationEnd", 0.0));
    const float nearAttenuationStart = static_cast<float>(ufbx_find_real(&pLight->props, "NearAttenuationStart", 0.0));
    AddDiag(
        pScene,
        DiagLevel::Info,
        "Light '" + pOut->Name + "' props: rawIntensity=" + std::to_string(rawIntensityProperty) +
        " ufbxIntensity=" + std::to_string(pOut->Intensity) +
        " decayStart=" + std::to_string(decayStartProperty) +
        " nearAttenStart=" + std::to_string(nearAttenuationStart) +
        " farAttenEnd=" + std::to_string(farAttenuationEnd));

    switch (pLight->type)
    {
    case UFBX_LIGHT_POINT:
        pOut->Type = Canvas::LightType::Point;
        break;
    case UFBX_LIGHT_DIRECTIONAL:
        pOut->Type = Canvas::LightType::Directional;
        break;
    case UFBX_LIGHT_SPOT:
        pOut->Type = Canvas::LightType::Spot;
        pOut->SpotInnerAngle = static_cast<float>(pLight->inner_angle);
        pOut->SpotOuterAngle = static_cast<float>(pLight->outer_angle);
        break;
    case UFBX_LIGHT_AREA:
        pOut->Type = Canvas::LightType::Area;
        break;
    case UFBX_LIGHT_VOLUME:
        AddDiag(pScene, DiagLevel::Error,
            "Volume light '" + pOut->Name + "' is not supported; import aborted for this light");
        return false;
    default:
        AddDiag(pScene, DiagLevel::Error,
            "Unknown light type for '" + pOut->Name + "'; import aborted for this light");
        return false;
    }

    switch (pLight->decay)
    {
    case UFBX_LIGHT_DECAY_NONE:
        pOut->AttenuationConst = 1.0f;
        pOut->AttenuationLinear = 0.0f;
        pOut->AttenuationQuad = 0.0f;
        break;
    case UFBX_LIGHT_DECAY_LINEAR:
        pOut->AttenuationConst = 0.0f;
        pOut->AttenuationLinear = 1.0f;
        pOut->AttenuationQuad = 0.0f;
        break;
    case UFBX_LIGHT_DECAY_QUADRATIC:
        pOut->AttenuationConst = 0.0f;
        pOut->AttenuationLinear = 0.0f;
        pOut->AttenuationQuad = 1.0f;
        break;
    case UFBX_LIGHT_DECAY_CUBIC:
        pOut->AttenuationConst = 0.0f;
        pOut->AttenuationLinear = 0.0f;
        pOut->AttenuationQuad = 1.0f;
        AddDiag(pScene, DiagLevel::Warning, "Cubic decay for light '" + pOut->Name + "' approximated as quadratic");
        break;
    default:
        pOut->AttenuationConst = 1.0f;
        pOut->AttenuationLinear = 0.0f;
        pOut->AttenuationQuad = 0.0f;
        break;
    }

    return true;
}

//------------------------------------------------------------------------------------------------
bool ImportCamera(
    _In_ const ufbx_camera *pCamera,
    _In_ float distanceUnitScale,
    _Inout_ ImportedCamera *pOut,
    _Inout_ ImportedScene *pScene)
{
    pOut->Name = ToStdString(pCamera->name);

    const float nearPlane = static_cast<float>(pCamera->near_plane) * distanceUnitScale;
    const float farPlane = static_cast<float>(pCamera->far_plane) * distanceUnitScale;
    if (nearPlane > 0.0f)
        pOut->NearClip = nearPlane;
    if (farPlane > pOut->NearClip)
        pOut->FarClip = farPlane;

    float aspectRatio = static_cast<float>(pCamera->aspect_ratio);
    if (aspectRatio <= 0.0f && pCamera->resolution.y > 0.0)
        aspectRatio = static_cast<float>(pCamera->resolution.x / pCamera->resolution.y);
    if (aspectRatio > 0.0f)
        pOut->AspectRatio = aspectRatio;

    if (pCamera->projection_mode == UFBX_PROJECTION_MODE_PERSPECTIVE)
    {
        const float fovXDeg = static_cast<float>(pCamera->field_of_view_deg.x);
        const float fovYDeg = static_cast<float>(pCamera->field_of_view_deg.y);

        float verticalFovDeg = 0.0f;
        const bool useHorizontalFit =
            (pCamera->gate_fit == UFBX_GATE_FIT_HORIZONTAL) ||
            (pCamera->aperture_mode == UFBX_APERTURE_MODE_HORIZONTAL);

        if (useHorizontalFit && fovXDeg > 0.0f && pOut->AspectRatio > 0.0f)
        {
            const float fovXRadians = fovXDeg * (static_cast<float>(Math::Pi) / 180.0f);
            const float tanHalfX = tanf(fovXRadians * 0.5f);
            const float tanHalfY = tanHalfX / pOut->AspectRatio;
            verticalFovDeg = (2.0f * atanf(tanHalfY)) * (180.0f / static_cast<float>(Math::Pi));
        }
        else if (fovYDeg > 0.0f)
        {
            verticalFovDeg = fovYDeg;
        }
        else if (fovXDeg > 0.0f && pOut->AspectRatio > 0.0f)
        {
            const float fovXRadians = fovXDeg * (static_cast<float>(Math::Pi) / 180.0f);
            const float tanHalfX = tanf(fovXRadians * 0.5f);
            const float tanHalfY = tanHalfX / pOut->AspectRatio;
            verticalFovDeg = (2.0f * atanf(tanHalfY)) * (180.0f / static_cast<float>(Math::Pi));
        }

        if (verticalFovDeg > 0.0f)
            pOut->FovAngle = verticalFovDeg * (static_cast<float>(Math::Pi) / 180.0f);
    }
    else
    {
        AddDiag(pScene, DiagLevel::Error,
            "Orthographic camera '" + pOut->Name + "' is not supported; import aborted for this camera");
        return false;
    }

    return true;
}

#endif // CANVASFBX_HAS_UFBX

} // anonymous namespace

//------------------------------------------------------------------------------------------------
bool ImportedScene::HasErrors() const
{
    for (const auto &d : Diagnostics)
    {
        if (d.Level == DiagLevel::Error)
            return true;
    }
    return false;
}

//------------------------------------------------------------------------------------------------
#if CANVASFBX_HAS_UFBX

HRESULT ImportScene(
    _In_z_  const wchar_t  *pFilePath,
    _In_    const ImportOptions &options,
    _Out_   ImportedScene  *pScene)
{
    if (!pFilePath || !pScene)
        return E_INVALIDARG;

    *pScene = {};
    pScene->SceneBounds.Reset();

    const std::string utf8Path = ToUtf8(pFilePath);
    if (utf8Path.empty())
    {
        AddDiag(pScene, DiagLevel::Error, "Could not convert input file path to UTF-8");
        return E_INVALIDARG;
    }

    ufbx_load_opts loadOpts = { 0 };
    loadOpts.generate_missing_normals = options.GenerateNormals;
    // Scene/world conversion: preserve the original Canvas scene basis used elsewhere in the
    // renderer (X-right, Z-up, forward = +Y because `front` is opposite from forward in ufbx).
    loadOpts.target_axes = ufbx_coordinate_axes{
        UFBX_COORDINATE_AXIS_POSITIVE_X,
        UFBX_COORDINATE_AXIS_POSITIVE_Z,
        UFBX_COORDINATE_AXIS_NEGATIVE_Y,
    };
    // Keep source unit scale here; forcing target_unit_meters introduced a 100x shrink
    // for this asset's mesh geometry in runtime diagnostics.

    // Canvas cameras use a different local basis than world geometry:
    //   +X = forward, +Y = left, +Z = up
    // ufbx `front` is the opposite of forward, so camera target axes are:
    //   right = -Y, up = +Z, front = -X
    loadOpts.target_camera_axes = ufbx_coordinate_axes{
        UFBX_COORDINATE_AXIS_NEGATIVE_Y,
        UFBX_COORDINATE_AXIS_POSITIVE_Z,
        UFBX_COORDINATE_AXIS_NEGATIVE_X,
    };

    // Canvas directional/spot lights use +X as emission direction.
    // ufbx `front` is the opposite of forward, so use the same local basis mapping
    // as cameras for directed-light orientation.
    loadOpts.target_light_axes = ufbx_coordinate_axes{
        UFBX_COORDINATE_AXIS_NEGATIVE_Y,
        UFBX_COORDINATE_AXIS_POSITIVE_Z,
        UFBX_COORDINATE_AXIS_NEGATIVE_X,
    };
    loadOpts.space_conversion = UFBX_SPACE_CONVERSION_MODIFY_GEOMETRY;
    loadOpts.geometry_transform_handling = UFBX_GEOMETRY_TRANSFORM_HANDLING_MODIFY_GEOMETRY;
    loadOpts.inherit_mode_handling = UFBX_INHERIT_MODE_HANDLING_HELPER_NODES;

    ufbx_error err = { 0 };
    ufbx_scene *pLoaded = ufbx_load_file(utf8Path.c_str(), &loadOpts, &err);
    if (!pLoaded)
    {
        std::string message = "ufbx failed to load file: ";
        message += ToStdString(err.description);
        if (err.info_length > 0)
        {
            message += " (";
            message += std::string(err.info, err.info_length);
            message += ")";
        }
        AddDiag(pScene, DiagLevel::Error, message);
        return E_FAIL;
    }

    if (pLoaded->nodes.count > kMaxImportNodes)
    {
        AddDiag(pScene, DiagLevel::Error,
            "ufbx scene node count is unreasonably large (" + std::to_string(static_cast<unsigned long long>(pLoaded->nodes.count)) + ")");
        ufbx_free_scene(pLoaded);
        return E_FAIL;
    }

    if (pLoaded->metadata.warnings.count > kMaxImportWarnings)
    {
        AddDiag(pScene, DiagLevel::Error,
            "ufbx warning count is unreasonably large (" + std::to_string(static_cast<unsigned long long>(pLoaded->metadata.warnings.count)) + ")");
        ufbx_free_scene(pLoaded);
        return E_FAIL;
    }

    float sceneDistanceScale = 1.0f;
    if (pLoaded->settings.unit_meters > 0.0)
    {
        sceneDistanceScale = static_cast<float>(pLoaded->settings.unit_meters);
    }

    std::unordered_map<const ufbx_node*, int32_t> nodeMap;
    nodeMap.reserve(pLoaded->nodes.count);

    // Build Canvas node list first so parent indices are stable.
    for (size_t ni = 0; ni < pLoaded->nodes.count; ++ni)
    {
        const ufbx_node *pNode = pLoaded->nodes[ni];
        if (!pNode || pNode->is_root)
            continue;

        ImportedNode node;
        node.Name = ToStdString(pNode->name);
        if (node.Name.empty())
            node.Name = "Node_" + std::to_string(ni);

        const Math::FloatMatrix4x4 localMatrix = ToCanvasAffineMatrix(pNode->node_to_parent);
        node.Translation = Math::FloatVector4(
            localMatrix[3][0] * sceneDistanceScale,
            localMatrix[3][1] * sceneDistanceScale,
            localMatrix[3][2] * sceneDistanceScale,
            0.0f);
        node.Rotation = ToCanvasNodeRotation(pNode, pScene);
        node.Scale = ExtractCanvasScale(localMatrix);

        node.ParentIndex = -1;
        pScene->Nodes.push_back(node);
        nodeMap[pNode] = static_cast<int32_t>(pScene->Nodes.size() - 1);
    }

    // Fill parent indices and attach mesh/light/camera payloads.
    std::unordered_map<const ufbx_mesh*, int32_t> meshMap;
    std::unordered_map<const ufbx_light*, int32_t> lightMap;
    std::unordered_map<const ufbx_camera*, int32_t> cameraMap;
    for (size_t ni = 0; ni < pLoaded->nodes.count; ++ni)
    {
        const ufbx_node *pNode = pLoaded->nodes[ni];
        if (!pNode || pNode->is_root)
            continue;

        const auto itNode = nodeMap.find(pNode);
        if (itNode == nodeMap.end())
            continue;

        ImportedNode &outNode = pScene->Nodes[itNode->second];

        if (pNode->parent && !pNode->parent->is_root)
        {
            const auto itParent = nodeMap.find(pNode->parent);
            if (itParent != nodeMap.end())
                outNode.ParentIndex = itParent->second;
        }

        if (pNode->mesh)
        {
            const ufbx_mesh *pMesh = pNode->mesh;
            auto itMesh = meshMap.find(pMesh);
            if (itMesh == meshMap.end())
            {
                ImportedMesh importedMesh;
                if (ImportMesh(pMesh, options.Triangulate, &importedMesh, pScene))
                {
                    pScene->SceneBounds.ExpandToInclude(importedMesh.Bounds);
                    pScene->Meshes.push_back(std::move(importedMesh));
                    const int32_t meshIndex = static_cast<int32_t>(pScene->Meshes.size() - 1);
                    meshMap[pMesh] = meshIndex;
                    outNode.MeshIndex = meshIndex;
                }
            }
            else
            {
                outNode.MeshIndex = itMesh->second;
            }
        }

        if (pNode->light)
        {
            const ufbx_light *pLight = pNode->light;
            auto itLight = lightMap.find(pLight);
            if (itLight == lightMap.end())
            {
                ImportedLight importedLight;
                if (ImportLight(pLight, &importedLight, pScene))
                {
                    pScene->Lights.push_back(std::move(importedLight));
                    const int32_t lightIndex = static_cast<int32_t>(pScene->Lights.size() - 1);
                    lightMap[pLight] = lightIndex;
                    outNode.LightIndex = lightIndex;
                }
            }
            else
            {
                outNode.LightIndex = itLight->second;
            }
        }

        if (pNode->camera)
        {
            const ufbx_camera *pCamera = pNode->camera;

            auto itCamera = cameraMap.find(pCamera);
            if (itCamera == cameraMap.end())
            {
                ImportedCamera importedCamera;
                if (ImportCamera(pCamera, sceneDistanceScale, &importedCamera, pScene))
                {
                    pScene->Cameras.push_back(std::move(importedCamera));
                    const int32_t cameraIndex = static_cast<int32_t>(pScene->Cameras.size() - 1);
                    cameraMap[pCamera] = cameraIndex;
                    outNode.CameraIndex = cameraIndex;
                    if (pScene->ActiveCameraNodeIndex < 0)
                        pScene->ActiveCameraNodeIndex = itNode->second;
                }
            }
            else
            {
                outNode.CameraIndex = itCamera->second;
                if (pScene->ActiveCameraNodeIndex < 0)
                    pScene->ActiveCameraNodeIndex = itNode->second;
            }
        }
    }

    for (size_t wi = 0; wi < pLoaded->metadata.warnings.count; ++wi)
    {
        const ufbx_warning &w = pLoaded->metadata.warnings[wi];
        AddDiag(pScene, DiagLevel::Warning, ToStdString(w.description));
    }

    ufbx_free_scene(pLoaded);

    if (!pScene->HasMeshes())
    {
        AddDiag(pScene, DiagLevel::Warning, "No importable mesh geometry found in file");
    }

    return S_OK;
}

#else // stub when ufbx is not available

HRESULT ImportScene(
    _In_z_  const wchar_t  *pFilePath,
    _In_    const ImportOptions &options,
    _Out_   ImportedScene  *pScene)
{
    if (!pFilePath || !pScene)
        return E_INVALIDARG;

    (void)options;
    *pScene = {};
    pScene->Diagnostics.push_back({ DiagLevel::Error, "CanvasFbx built without ufbx — import unavailable" });
    return E_NOTIMPL;
}

#endif

} // namespace Fbx
} // namespace Canvas
