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
    loadOpts.target_axes = ufbx_coordinate_axes{ UFBX_COORDINATE_AXIS_POSITIVE_X, UFBX_COORDINATE_AXIS_POSITIVE_Z, UFBX_COORDINATE_AXIS_NEGATIVE_Y };
    loadOpts.target_light_axes = loadOpts.target_axes;
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

        node.Translation = ToCanvasPosition(pNode->local_transform.translation);
        node.Translation.W = 0.0f;
        node.Rotation = ToCanvasQuaternion(pNode->local_transform.rotation).Normalize();
        node.Scale = Math::FloatVector4(
            static_cast<float>(pNode->local_transform.scale.x),
            static_cast<float>(pNode->local_transform.scale.y),
            static_cast<float>(pNode->local_transform.scale.z),
            0.0f);

        node.ParentIndex = -1;
        pScene->Nodes.push_back(node);
        nodeMap[pNode] = static_cast<int32_t>(pScene->Nodes.size() - 1);
    }

    // Fill parent indices and attach mesh payloads.
    std::unordered_map<const ufbx_mesh*, int32_t> meshMap;
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

        if (!pNode->mesh)
            continue;

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
