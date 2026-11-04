// CanvasFbx.cpp : Scene import entry point
//

#include "pch.h"
#include "CanvasFbx.h"
#include "CanvasGfx.h"

#include <map>
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
Math::FloatVector4 ToCanvasVector(ufbx_vec3 v)
{
    return Math::FloatVector4(static_cast<float>(v.x), static_cast<float>(v.y), static_cast<float>(v.z), 0.0f);
}

Math::FloatVector4 ToCanvasPoint(ufbx_vec3 v)
{
    return Math::FloatVector4(static_cast<float>(v.x), static_cast<float>(v.y), static_cast<float>(v.z), 1.0f);
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
// Space conversion applied above the top-level nodes (see ImportScene). Folding it into a
// top-level node's local TRS leaves world transforms unchanged; it is exact because the
// root scale is uniform, so it commutes with the node's rotation.
#pragma warning(push)
#pragma warning(disable: 4324) // padded due to alignas(16) math types
struct RootFold
{
    bool                  Enabled = false;
    Math::FloatVector4    Translation = { 0.0f, 0.0f, 0.0f, 0.0f };
    Math::FloatQuaternion Rotation = {};
    Math::FloatMatrix4x4  RotationMatrix = Math::FloatMatrix4x4::Identity();
    float                 Scale = 1.0f;

    void Apply(_Inout_ Math::FloatVector4 &t, _Inout_ Math::FloatQuaternion &r, _Inout_ Math::FloatVector4 &s) const
    {
        t = Fold(ImportedTransformProperty::Translation, t);
        r = Fold(ImportedTransformProperty::Rotation, r);
        s = Fold(ImportedTransformProperty::Scale, s);
    }

    // One property's value, in the form ImportedTransformProperty describes. Each property folds
    // independently of the others.
    Math::FloatVector4 Fold(ImportedTransformProperty property, Math::FloatVector4 v) const
    {
        if (!Enabled)
            return v;
        switch (property)
        {
        case ImportedTransformProperty::Translation:
            v = (v * Scale) * RotationMatrix + Translation;
            v.W = 1.0f;
            break;
        case ImportedTransformProperty::Rotation:
            v = (Rotation * Math::FloatQuaternion(v)).Normalize();
            break;
        case ImportedTransformProperty::Scale:
            v = v * Scale;
            v.W = 0.0f;
            break;
        }
        return v;
    }
};
#pragma warning(pop)

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
// FBX stores UVs with the OpenGL convention (origin at bottom-left).  D3D
// samples textures with the origin at top-left, so flip V on import to keep
// imported textures right-side-up.
Math::FloatVector2 ToCanvasUV(ufbx_vec2 v)
{
    return Math::FloatVector2(static_cast<float>(v.x), 1.0f - static_cast<float>(v.y));
}

//------------------------------------------------------------------------------------------------
// Build the bitangent sign for a tangent vector by comparing the supplied
// bitangent against the right-handed reconstruction (cross(N, T)). Returns +1
// when the bitangent matches the right-handed convention, -1 otherwise.
float ComputeBitangentSign(_In_ const Math::FloatVector4 &n, _In_ const Math::FloatVector4 &t, _In_ const Math::FloatVector4 &b)
{
    const Math::FloatVector4 reconstructed = Math::CrossProduct(n, t);
    const float dot = Math::DotProduct(reconstructed, b);
    return (dot < 0.0f) ? -1.0f : 1.0f;
}

//------------------------------------------------------------------------------------------------
// Compute one tangent per triangle from positions + UVs (returns identity-X
// when degenerate). The result is suitable for assigning to all three corners
// of the triangle when no per-vertex tangent is available from ufbx.
Math::FloatVector4 ComputeTriangleTangent(
    _In_ const Math::FloatVector4 &p0, _In_ const Math::FloatVector4 &p1, _In_ const Math::FloatVector4 &p2,
    _In_ const Math::FloatVector2 &uv0, _In_ const Math::FloatVector2 &uv1, _In_ const Math::FloatVector2 &uv2,
    _In_ const Math::FloatVector4 &faceNormal)
{
    const Math::FloatVector4 dp1 = p1 - p0;
    const Math::FloatVector4 dp2 = p2 - p0;
    const float du1 = uv1.X - uv0.X;
    const float dv1 = uv1.Y - uv0.Y;
    const float du2 = uv2.X - uv0.X;
    const float dv2 = uv2.Y - uv0.Y;

    const float det = du1 * dv2 - du2 * dv1;
    if (std::abs(det) <= 1e-20f)
    {
        // Degenerate UV mapping: fall back to a deterministic axis perpendicular to the normal.
        const Math::FloatVector4 axis = (std::abs(faceNormal.X) > 0.9f)
            ? Math::FloatVector4(0.0f, 1.0f, 0.0f, 0.0f)
            : Math::FloatVector4(1.0f, 0.0f, 0.0f, 0.0f);
        Math::FloatVector4 t = axis - faceNormal * Math::DotProduct(faceNormal, axis);
        const float lenSq = Math::DotProduct(t, t);
        if (lenSq > 1e-20f)
            t = t * (1.0f / std::sqrt(lenSq));
        t.W = 1.0f;
        return t;
    }

    const float invDet = 1.0f / det;
    Math::FloatVector4 t = (dp1 * dv2 - dp2 * dv1) * invDet;

    // Orthonormalize against the face normal.
    t = t - faceNormal * Math::DotProduct(faceNormal, t);
    const float lenSq = Math::DotProduct(t, t);
    if (lenSq > 1e-20f)
        t = t * (1.0f / std::sqrt(lenSq));
    else
        t = Math::FloatVector4(1.0f, 0.0f, 0.0f, 0.0f);

    // Bitangent sign: compare cross(N, T) against (dp1*du2 - dp2*du1) * invDet (the source bitangent).
    const Math::FloatVector4 bitangent = (dp2 * du1 - dp1 * du2) * invDet;
    t.W = ComputeBitangentSign(faceNormal, t, bitangent);
    return t;
}

//------------------------------------------------------------------------------------------------
bool ImportMesh(
    _In_ const ufbx_mesh *pMesh,
    _In_ bool triangulate,
    _In_ const std::unordered_map<const ufbx_node*, int32_t>& nodeMap,
    _Inout_ ImportedMesh *pOut,
    _Inout_ ImportedScene *pScene)
{
    pOut->Name = ToStdString(pMesh->name);
    pOut->Parts.clear();
    pOut->Bounds.Reset();
    pOut->Skin = ImportedSkin{};

    if (!pMesh->vertex_position.exists)
    {
        AddDiag(pScene, DiagLevel::Error, "Mesh '" + pOut->Name + "' has no vertex positions");
        return false;
    }

    const bool hasNormals   = pMesh->vertex_normal.exists;
    const bool hasUVs       = pMesh->vertex_uv.exists;
    const bool hasTangents  = pMesh->vertex_tangent.exists && pMesh->vertex_bitangent.exists && hasUVs;
    bool warnedInvalidNormal = false;
    std::vector<uint32_t> triIndices(pMesh->max_face_triangles * 3);

    // Skin deformer: build bone list and per-vertex influence data.
    // We use only the first skin deformer; multiple deformers on one mesh are rare.
    const ufbx_skin_deformer* pSkin = nullptr;
    if (pMesh->skin_deformers.count > 0)
        pSkin = pMesh->skin_deformers.data[0];

    if (pSkin)
    {
        pOut->Skin.HasSkin = true;
        pOut->Skin.BoneNodeIndices.reserve(pSkin->clusters.count);
        pOut->Skin.InvBindPoses.reserve(pSkin->clusters.count);

        for (size_t ci = 0; ci < pSkin->clusters.count; ++ci)
        {
            const ufbx_skin_cluster* pCluster = pSkin->clusters.data[ci];
            int32_t nodeIdx = -1;
            if (pCluster->bone_node)
            {
                auto it = nodeMap.find(pCluster->bone_node);
                if (it != nodeMap.end())
                    nodeIdx = it->second;
            }
            pOut->Skin.BoneNodeIndices.push_back(nodeIdx);
            pOut->Skin.InvBindPoses.push_back(ToCanvasAffineMatrix(pCluster->geometry_to_bone));
        }
    }

    // ufbx always produces material_parts (one entry per material partition; a
    // single dummy entry covering all faces when the mesh has no materials).
    const ufbx_mesh_part_list &partList = pMesh->material_parts;
    if (partList.count == 0)
    {
        AddDiag(pScene, DiagLevel::Error, "Mesh '" + pOut->Name + "' has no material parts");
        return false;
    }

    pOut->Parts.reserve(partList.count);

    for (size_t pi = 0; pi < partList.count; ++pi)
    {
        const ufbx_mesh_part &part = partList.data[pi];
        if (part.num_faces == 0)
            continue;

        ImportedMeshPart outPart;

        // Map ufbx mesh-part -> Canvas material index. The mesh's materials list
        // mirrors the partition order via face_material indices; for a no-
        // material mesh, partList.count == 1 and pMesh->materials.count == 0.
        if (pi < pMesh->materials.count && pMesh->materials.data[pi] != nullptr)
        {
            // Resolved later by the per-scene material map (ImportMaterials).
            outPart.MaterialIndex = static_cast<int32_t>(pi);  // placeholder (mesh-local)
        }

        const size_t estimatedVertexCount = part.num_faces * 3;
        outPart.Positions.reserve(estimatedVertexCount);
        outPart.Normals.reserve(estimatedVertexCount);
        if (hasUVs)
            outPart.UV0.reserve(estimatedVertexCount);
        if (hasUVs)  // tangent stream needs UVs to be meaningful
            outPart.Tangents.reserve(estimatedVertexCount);
        if (pSkin)
            outPart.SkinVertices.reserve(estimatedVertexCount);

        for (size_t fii = 0; fii < part.num_faces; ++fii)
        {
            const uint32_t faceIdx = part.face_indices.data[fii];
            const ufbx_face face = pMesh->faces[faceIdx];
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

                outPart.Positions.push_back(p0);
                outPart.Positions.push_back(p1);
                outPart.Positions.push_back(p2);

                outPart.Normals.push_back(n0);
                outPart.Normals.push_back(n1);
                outPart.Normals.push_back(n2);

                if (hasUVs)
                {
                    const Math::FloatVector2 uv0 = ToCanvasUV(ufbx_get_vertex_vec2(&pMesh->vertex_uv, i0));
                    const Math::FloatVector2 uv1 = ToCanvasUV(ufbx_get_vertex_vec2(&pMesh->vertex_uv, i1));
                    const Math::FloatVector2 uv2 = ToCanvasUV(ufbx_get_vertex_vec2(&pMesh->vertex_uv, i2));
                    outPart.UV0.push_back(uv0);
                    outPart.UV0.push_back(uv1);
                    outPart.UV0.push_back(uv2);

                    if (hasTangents)
                    {
                        const Math::FloatVector4 t0 = ToCanvasNormal(ufbx_get_vertex_vec3(&pMesh->vertex_tangent, i0));
                        const Math::FloatVector4 t1 = ToCanvasNormal(ufbx_get_vertex_vec3(&pMesh->vertex_tangent, i1));
                        const Math::FloatVector4 t2 = ToCanvasNormal(ufbx_get_vertex_vec3(&pMesh->vertex_tangent, i2));
                        const Math::FloatVector4 b0 = ToCanvasNormal(ufbx_get_vertex_vec3(&pMesh->vertex_bitangent, i0));
                        const Math::FloatVector4 b1 = ToCanvasNormal(ufbx_get_vertex_vec3(&pMesh->vertex_bitangent, i1));
                        const Math::FloatVector4 b2 = ToCanvasNormal(ufbx_get_vertex_vec3(&pMesh->vertex_bitangent, i2));

                        Math::FloatVector4 tt0 = t0; tt0.W = ComputeBitangentSign(n0, t0, b0);
                        Math::FloatVector4 tt1 = t1; tt1.W = ComputeBitangentSign(n1, t1, b1);
                        Math::FloatVector4 tt2 = t2; tt2.W = ComputeBitangentSign(n2, t2, b2);
                        outPart.Tangents.push_back(tt0);
                        outPart.Tangents.push_back(tt1);
                        outPart.Tangents.push_back(tt2);
                    }
                    else
                    {
                        // Generate one tangent per triangle and replicate to all three corners.
                        const Math::FloatVector4 fn = ComputeFaceNormal(p0, p1, p2);
                        const Math::FloatVector4 t = ComputeTriangleTangent(p0, p1, p2, uv0, uv1, uv2, fn);
                        outPart.Tangents.push_back(t);
                        outPart.Tangents.push_back(t);
                        outPart.Tangents.push_back(t);
                    }
                }

                pOut->Bounds.ExpandToInclude(p0);
                pOut->Bounds.ExpandToInclude(p1);
                pOut->Bounds.ExpandToInclude(p2);

                if (pSkin)
                {
                    const uint32_t corners[3] = { i0, i1, i2 };
                    for (uint32_t ci = 0; ci < 3; ++ci)
                    {
                        const uint32_t vertIdx = pMesh->vertex_position.indices[corners[ci]];
                        ImportedSkinVertex sv;

                        if (vertIdx < pSkin->vertices.count)
                        {
                            const ufbx_skin_vertex& sv_src = pSkin->vertices.data[vertIdx];
                            const uint32_t numW = std::min(sv_src.num_weights, (uint32_t)4);

                            float totalWeight = 0.0f;
                            for (uint32_t wi = 0; wi < numW; ++wi)
                            {
                                const ufbx_skin_weight& w = pSkin->weights.data[sv_src.weight_begin + wi];
                                sv.BoneIndices[wi] = w.cluster_index;
                                sv.Weights[wi] = static_cast<float>(w.weight);
                                totalWeight += sv.Weights[wi];
                            }
                            // Normalise
                            if (totalWeight > 1e-6f)
                            {
                                const float invTotal = 1.0f / totalWeight;
                                for (uint32_t wi = 0; wi < numW; ++wi)
                                    sv.Weights[wi] *= invTotal;
                            }
                        }
                        outPart.SkinVertices.push_back(sv);
                    }
                }
            }
        }

        if (!outPart.Positions.empty())
            pOut->Parts.push_back(std::move(outPart));
    }

    if (pOut->Parts.empty())
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
    _In_ float distanceScale,
    _Inout_ ImportedCamera *pOut,
    _Inout_ ImportedScene *pScene)
{
    pOut->Name = ToStdString(pCamera->name);

    const float nearPlane = static_cast<float>(pCamera->near_plane) * distanceScale;
    const float farPlane = static_cast<float>(pCamera->far_plane) * distanceScale;
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

//------------------------------------------------------------------------------------------------
// Bind (rest) pose. A node's stored local transform is the pose the file was saved in, which
// for an animated skeleton is often a posed frame rather than the rest pose. The bind pose is
// recorded separately, as world matrices: per skin cluster (the matrix its inverse bind pose
// was built from) and in the scene's BindPose objects. ufbx converts both into the same world
// space as node_to_world. A skinned mesh node is placed where its skin was bound.
//
// A node with a bind matrix takes its local transform from it, relative to its parent's bind
// world; every other node keeps its stored local transform.
//------------------------------------------------------------------------------------------------
class BindPose
{
public:
    explicit BindPose(_In_ const ufbx_scene *pScene)
    {
        // Cluster matrices first: they are what the skin's inverse bind poses were built from.
        for (const ufbx_skin_cluster *pCluster : pScene->skin_clusters)
            if (pCluster->bone_node)
                m_BindWorld.emplace(pCluster->bone_node, pCluster->bind_to_world);

        // A skinned mesh node is placed where its skin was bound, recovered from the cluster
        // matrices its geometry_to_bone was built from. This takes precedence over the mesh
        // node's BindPose entry, which some exporters write inconsistently with the clusters
        // (e.g. Blender 2.79 and MotionBuilder), so the node always sits where the skin
        // renders. Uses the first skin deformer (as ImportMesh does) and the mesh's first
        // instance (as ufbx does).
        for (const ufbx_node *pNode : pScene->nodes)
        {
            const ufbx_mesh *pMesh = pNode->mesh;
            if (!pMesh || pMesh->skin_deformers.count == 0 || pMesh->instances.count == 0 ||
                pMesh->instances.data[0] != pNode)
                continue;

            for (const ufbx_skin_cluster *pCluster : pMesh->skin_deformers.data[0]->clusters)
            {
                if (!pCluster->bone_node)
                    continue;
                const ufbx_matrix geometryToWorld = ufbx_matrix_mul(&pCluster->bind_to_world, &pCluster->geometry_to_bone);
                const ufbx_matrix nodeToGeometry = ufbx_matrix_invert(&pNode->geometry_to_node);
                m_BindWorld.emplace(pNode, ufbx_matrix_mul(&geometryToWorld, &nodeToGeometry));
                break;
            }
        }

        // BindPose entries for everything else.
        for (const ufbx_pose *pPose : pScene->poses)
        {
            if (!pPose->is_bind_pose)
                continue;
            for (const ufbx_bone_pose &bonePose : pPose->bone_poses)
                if (bonePose.bone_node)
                    m_BindWorld.emplace(bonePose.bone_node, bonePose.bone_to_world);
        }
    }

    // The node's local transform in the bind pose. pExact is set false when the bind matrix,
    // relative to the parent's, has shear (e.g. a rotated child under a non-uniformly scaled
    // parent): translation, rotation, and scale cannot represent it, so the result
    // approximates it.
    ufbx_transform LocalTransform(_In_ const ufbx_node *pNode, _Out_ bool *pExact)
    {
        *pExact = true;
        const auto it = m_BindWorld.find(pNode);
        if (it == m_BindWorld.end() || !pNode->parent)
            return pNode->local_transform;

        const ufbx_matrix parentToWorld = WorldMatrix(pNode->parent);
        const ufbx_matrix worldToParent = ufbx_matrix_invert(&parentToWorld);
        const ufbx_matrix local = ufbx_matrix_mul(&worldToParent, &it->second);
        const ufbx_transform transform = ufbx_matrix_to_transform(&local);

        // Rebuild the matrix from the decomposition; shear is whatever fails to round-trip.
        // Translation always round-trips exactly, so only the 3x3 linear block is compared
        // (a large translation must not loosen the tolerance).
        const ufbx_matrix rebuilt = ufbx_transform_to_matrix(&transform);
        ufbx_real diff = 0.0, magnitude = 0.0;
        for (int col = 0; col < 3; ++col)
            for (int row = 0; row < 3; ++row)
            {
                diff = std::max(diff, std::abs(local.cols[col].v[row] - rebuilt.cols[col].v[row]));
                magnitude = std::max(magnitude, std::abs(local.cols[col].v[row]));
            }
        *pExact = diff <= 1e-5 * magnitude;
        return transform;
    }

private:
    // The node's world matrix in the bind pose.
    ufbx_matrix WorldMatrix(_In_ const ufbx_node *pNode)
    {
        const auto it = m_BindWorld.find(pNode);
        if (it != m_BindWorld.end())
            return it->second;
        if (!pNode->parent)
            return pNode->node_to_world;

        const auto itDerived = m_DerivedWorld.find(pNode);
        if (itDerived != m_DerivedWorld.end())
            return itDerived->second;

        const ufbx_matrix parentToWorld = WorldMatrix(pNode->parent);
        const ufbx_matrix world = ufbx_matrix_mul(&parentToWorld, &pNode->node_to_parent);
        m_DerivedWorld.emplace(pNode, world);
        return world;
    }

    std::unordered_map<const ufbx_node *, ufbx_matrix> m_BindWorld;     // authored bind matrices
    std::unordered_map<const ufbx_node *, ufbx_matrix> m_DerivedWorld;  // nodes without one, memoized
};

//------------------------------------------------------------------------------------------------
// Global matrix of every imported node: its local transform's matrix times its parent's
// global matrix (row vectors), as a Canvas scene-graph node composes it.
std::vector<Math::FloatMatrix4x4> ImportedGlobalMatrices(_In_ const std::vector<ImportedNode> &nodes)
{
    std::vector<Math::FloatMatrix4x4> globals(nodes.size());
    std::vector<bool> done(nodes.size(), false);

    const auto compute = [&](auto &self, size_t i) -> const Math::FloatMatrix4x4 &
    {
        if (done[i])
            return globals[i];

        const ImportedNode &node = nodes[i];
        const Math::FloatMatrix4x4 local = node.LocalTransform.ToMatrix();
        globals[i] = node.ParentIndex >= 0 ? local * self(self, static_cast<size_t>(node.ParentIndex)) : local;
        done[i] = true;
        return globals[i];
    };

    for (size_t i = 0; i < nodes.size(); ++i)
        compute(compute, i);
    return globals;
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

namespace
{

//------------------------------------------------------------------------------------------------
// Resolve (or create) the scene-wide texture index for a ufbx texture. Returns
// -1 when the texture is absent or has no usable file/embedded payload. Dedupes
// by absolute filename when present, falling back to the ufbx pointer identity
// for embedded-only textures.
int32_t AcquireTextureIndex(
    _In_opt_ const ufbx_texture *pTexture,
    _Inout_ ImportedScene *pScene,
    _Inout_ std::unordered_map<std::string, int32_t> &textureByPath,
    _Inout_ std::unordered_map<const ufbx_texture *, int32_t> &textureByPtr)
{
    if (!pTexture)
        return -1;

    auto itPtr = textureByPtr.find(pTexture);
    if (itPtr != textureByPtr.end())
        return itPtr->second;

    std::string absPath = ToStdString(pTexture->absolute_filename);
    if (absPath.empty())
        absPath = ToStdString(pTexture->filename);

    if (!absPath.empty())
    {
        auto itPath = textureByPath.find(absPath);
        if (itPath != textureByPath.end())
        {
            textureByPtr[pTexture] = itPath->second;
            return itPath->second;
        }
    }

    const bool hasEmbedded = pTexture->content.size > 0 && pTexture->content.data != nullptr;
    if (absPath.empty() && !hasEmbedded)
        return -1;

    ImportedTextureRef ref;
    ref.AbsoluteFilePath = absPath;
    ref.Embedded = hasEmbedded;
    if (hasEmbedded)
    {
        const uint8_t *pBytes = static_cast<const uint8_t *>(pTexture->content.data);
        ref.EmbeddedBytes.assign(pBytes, pBytes + pTexture->content.size);
    }

    pScene->Textures.push_back(std::move(ref));
    const int32_t index = static_cast<int32_t>(pScene->Textures.size() - 1);
    if (!absPath.empty())
        textureByPath[absPath] = index;
    textureByPtr[pTexture] = index;
    return index;
}

//------------------------------------------------------------------------------------------------
// Pick the texture pointer for a material role: prefer the PBR map, fall back
// to the FBX classic map when the PBR slot has no texture / value.
const ufbx_texture *PickRoleTexture(_In_ const ufbx_material_map &pbrMap, _In_ const ufbx_material_map &fbxMap)
{
    if (pbrMap.texture_enabled && pbrMap.texture)
        return pbrMap.texture;
    if (fbxMap.texture_enabled && fbxMap.texture)
        return fbxMap.texture;
    if (pbrMap.texture)
        return pbrMap.texture;
    if (fbxMap.texture)
        return fbxMap.texture;
    return nullptr;
}

//------------------------------------------------------------------------------------------------
Math::FloatVector4 PickRoleFactor(
    _In_ const ufbx_material_map &pbrMap,
    _In_ const ufbx_material_map &fbxMap,
    _In_ const Math::FloatVector4 &fallback)
{
    const ufbx_material_map *pSrc = nullptr;
    if (pbrMap.has_value)
        pSrc = &pbrMap;
    else if (fbxMap.has_value)
        pSrc = &fbxMap;

    if (!pSrc)
        return fallback;

    return Math::FloatVector4(
        static_cast<float>(pSrc->value_vec4.x),
        static_cast<float>(pSrc->value_vec4.y),
        static_cast<float>(pSrc->value_vec4.z),
        static_cast<float>(pSrc->value_vec4.w));
}

//------------------------------------------------------------------------------------------------
// Walk pLoaded->materials, populate pScene->Materials and pScene->Textures.
// Returns a map from the ufbx material pointer to the scene-wide index, used
// downstream to translate mesh-local material indices.
std::unordered_map<const ufbx_material *, int32_t> ImportMaterials(
    _In_ const ufbx_scene *pLoaded,
    _Inout_ ImportedScene *pScene)
{
    std::unordered_map<const ufbx_material *, int32_t> materialMap;
    materialMap.reserve(pLoaded->materials.count);

    std::unordered_map<std::string, int32_t> textureByPath;
    std::unordered_map<const ufbx_texture *, int32_t> textureByPtr;

    pScene->Materials.reserve(pLoaded->materials.count);

    for (size_t mi = 0; mi < pLoaded->materials.count; ++mi)
    {
        const ufbx_material *pMat = pLoaded->materials.data[mi];
        if (!pMat)
            continue;

        ImportedMaterial outMat;
        outMat.Name = ToStdString(pMat->name);
        if (outMat.Name.empty())
            outMat.Name = "Material_" + std::to_string(mi);

        outMat.BaseColorFactor = PickRoleFactor(
            pMat->pbr.base_color, pMat->fbx.diffuse_color,
            Math::FloatVector4(1.0f, 1.0f, 1.0f, 1.0f));
        outMat.EmissiveFactor = PickRoleFactor(
            pMat->pbr.emission_color, pMat->fbx.emission_color,
            Math::FloatVector4(0.0f, 0.0f, 0.0f, 0.0f));

        // Roughness, metallic and AO are scalar PBR maps in ufbx. The classic
        // FBX side has no real equivalent, so use the same map for both
        // arguments to PickRoleFactor and rely on the fallback when absent.
        const Math::FloatVector4 roughness = PickRoleFactor(
            pMat->pbr.roughness, pMat->pbr.roughness,
            Math::FloatVector4(1.0f, 0.0f, 0.0f, 0.0f));
        const Math::FloatVector4 metallic = PickRoleFactor(
            pMat->pbr.metalness, pMat->pbr.metalness,
            Math::FloatVector4(0.0f, 0.0f, 0.0f, 0.0f));
        const Math::FloatVector4 ao = PickRoleFactor(
            pMat->pbr.ambient_occlusion, pMat->pbr.ambient_occlusion,
            Math::FloatVector4(1.0f, 0.0f, 0.0f, 0.0f));
        outMat.RoughMetalAOFactor = Math::FloatVector4(roughness.X, metallic.X, ao.X, 0.0f);

        const ufbx_texture *pAlbedoTex    = PickRoleTexture(pMat->pbr.base_color,     pMat->fbx.diffuse_color);
        const ufbx_texture *pNormalTex    = PickRoleTexture(pMat->pbr.normal_map,     pMat->fbx.normal_map);
        const ufbx_texture *pEmissiveTex  = PickRoleTexture(pMat->pbr.emission_color, pMat->fbx.emission_color);
        const ufbx_texture *pRoughnessTex = PickRoleTexture(pMat->pbr.roughness,         pMat->pbr.roughness);
        const ufbx_texture *pMetallicTex  = PickRoleTexture(pMat->pbr.metalness,         pMat->pbr.metalness);
        const ufbx_texture *pAOTex        = PickRoleTexture(pMat->pbr.ambient_occlusion, pMat->pbr.ambient_occlusion);

        outMat.AlbedoTextureIndex           = AcquireTextureIndex(pAlbedoTex,    pScene, textureByPath, textureByPtr);
        outMat.NormalTextureIndex           = AcquireTextureIndex(pNormalTex,    pScene, textureByPath, textureByPtr);
        outMat.EmissiveTextureIndex         = AcquireTextureIndex(pEmissiveTex,  pScene, textureByPath, textureByPtr);
        outMat.RoughnessTextureIndex        = AcquireTextureIndex(pRoughnessTex, pScene, textureByPath, textureByPtr);
        outMat.MetallicTextureIndex         = AcquireTextureIndex(pMetallicTex,  pScene, textureByPath, textureByPtr);
        outMat.AmbientOcclusionTextureIndex = AcquireTextureIndex(pAOTex,        pScene, textureByPath, textureByPtr);

        pScene->Materials.push_back(std::move(outMat));
        materialMap[pMat] = static_cast<int32_t>(pScene->Materials.size() - 1);
    }

    return materialMap;
}

//------------------------------------------------------------------------------------------------
// Extract the "action name" from a Blender-style "ObjectName|ActionName" AnimationStack name.
// Returns the full name when no '|' separator is present (non-Blender exporters).
static std::string ExtractActionName(const std::string& stackName)
{
    const auto pos = stackName.find('|');
    return (pos != std::string::npos) ? stackName.substr(pos + 1) : stackName;
}

//------------------------------------------------------------------------------------------------
constexpr ImportedTransformProperty kTransformProperties[] = {
    ImportedTransformProperty::Translation,
    ImportedTransformProperty::Rotation,
    ImportedTransformProperty::Scale,
};

//------------------------------------------------------------------------------------------------
// A transform's value for one property, in the form ImportedTransformProperty describes.
static Math::FloatVector4 PropertyValue(
    _In_ const Transform&           transform,
    _In_ ImportedTransformProperty  property)
{
    switch (property)
    {
    case ImportedTransformProperty::Rotation: return transform.Rotation;
    case ImportedTransformProperty::Scale:    return transform.Scale;
    default:                                  return transform.Translation;
    }
}

//------------------------------------------------------------------------------------------------
// True when two values of a property are the same pose.
//
// The tolerance is absolute (meters for translation), independent of distance from the origin,
// so a small offset far from the origin still counts as a different pose. Far out, float
// rounding can make equal poses compare different; that only costs a redundant track.
static bool IsSamePropertyValue(
    _In_ ImportedTransformProperty  property,
    _In_ const Math::FloatVector4&  a,
    _In_ const Math::FloatVector4&  b)
{
    constexpr float kEpsilon = 1e-5f;

    // q and -q are the same rotation, so compare against whichever sign is nearer. The
    // component distance is about half the angle between them (in radians).
    const float sign = property == ImportedTransformProperty::Rotation && Math::DotProduct(a, b) < 0.0f ? -1.0f : 1.0f;
    const Math::FloatVector4 d = a - b * sign;
    return std::sqrt(Math::DotProduct(d, d)) <= kEpsilon;
}

//------------------------------------------------------------------------------------------------
// Convert one property of a ufbx_baked_node into an ImportedAnimationTrack, keeping ufbx's
// key times. A property that holds one value for the whole clip (within IsSamePropertyValue;
// exporters leave float noise on properties they did not animate) becomes a single key at t = 0.
static ImportedAnimationTrack BakedPropertyToTrack(
    _In_     const ufbx_baked_node&     bn,
    _In_     ImportedTransformProperty  property,
    _In_     double                     clipBegin,
    _In_opt_ const RootFold*            pRootFold)  // non-null for top-level nodes
{
    ImportedAnimationTrack track;
    track.Property = property;

    const auto AddKey = [&](double absTime, Math::FloatVector4 value)
    {
        if (pRootFold)
            value = pRootFold->Fold(property, value);
        // Keep each rotation key in the hemisphere of the one before it, so interpolation
        // between them takes the short way.
        if (property == ImportedTransformProperty::Rotation && !track.Values.empty() &&
            Math::DotProduct(value, track.Values.back()) < 0.0f)
            value = -value;
        track.Times.push_back(static_cast<float>(absTime - clipBegin));
        track.Values.push_back(value);
    };

    switch (property)
    {
    case ImportedTransformProperty::Translation:
        for (const ufbx_baked_vec3& key : bn.translation_keys)
            AddKey(key.time, ToCanvasPoint(key.value));
        break;
    case ImportedTransformProperty::Rotation:
        for (const ufbx_baked_quat& key : bn.rotation_keys)
            AddKey(key.time, ToCanvasQuaternion(key.value));
        break;
    case ImportedTransformProperty::Scale:
        for (const ufbx_baked_vec3& key : bn.scale_keys)
            AddKey(key.time, ToCanvasVector(key.value));
        break;
    }

    const bool constant = std::all_of(track.Values.begin(), track.Values.end(),
        [&](const Math::FloatVector4& v) { return IsSamePropertyValue(property, v, track.Values.front()); });
    if (constant && !track.Values.empty())
    {
        track.Times.assign(1, 0.0f);
        track.Values.resize(1);
    }
    return track;
}

//------------------------------------------------------------------------------------------------
// The node's stored (file) local transform.
static Transform StoredPose(
    _In_ const ufbx_node& node,
    _In_ const RootFold&  rootFold)
{
    Transform pose;
    pose.Translation = ToCanvasPoint(node.local_transform.translation);
    pose.Rotation    = ToCanvasQuaternion(node.local_transform.rotation);
    pose.Scale       = ToCanvasVector(node.local_transform.scale);
    if (node.parent && node.parent->is_root)
        rootFold.Apply(pose.Translation, pose.Rotation, pose.Scale);
    return pose;
}

//------------------------------------------------------------------------------------------------
// The property's name in import diagnostics.
static const char* PropertyName(_In_ ImportedTransformProperty property)
{
    switch (property)
    {
    case ImportedTransformProperty::Rotation: return "rotation";
    case ImportedTransformProperty::Scale:    return "scale";
    default:                                  return "translation";
    }
}

//------------------------------------------------------------------------------------------------
// True when a layer of the stack animates the node's property, as opposed to the bake holding
// it at its stored value.
static bool StackAnimatesProperty(
    _In_ const ufbx_anim_stack&     stack,
    _In_ const ufbx_node&           node,
    _In_ ImportedTransformProperty  property)
{
    const char* fbxProperty =
        property == ImportedTransformProperty::Rotation ? UFBX_Lcl_Rotation :
        property == ImportedTransformProperty::Scale    ? UFBX_Lcl_Scaling  : UFBX_Lcl_Translation;
    for (const ufbx_anim_layer* pLayer : stack.layers)
        if (ufbx_find_anim_prop(pLayer, &node.element, fbxProperty))
            return true;
    return false;
}

//------------------------------------------------------------------------------------------------
// Bake all FBX AnimationStacks and append to pScene->AnimationClips.  Fails if any stack
// cannot be baked, or if two stacks of one action animate the same property of a node.
//
// Blender (and some DCC tools) exports each object's action as a separate AnimationStack
// named "ObjectName|ActionName".  Stacks sharing the same action-name suffix are merged
// into a single ImportedAnimationClip so e.g. "Cube|Walk" + "Sphere|Walk" become one
// "Walk" clip that drives both nodes simultaneously.
//
// Uses ufbx_bake_anim: rotation keyframes are stored as quaternions (ufbx_baked_quat)
// so all rotation orders (including UFBX_ROTATION_ORDER_SPHERIC) are handled correctly.
Gem::Result ImportAnimationClips(
    _In_ const ufbx_scene* pLoaded,
    _In_ const RootFold& rootFold,
    _In_ const std::unordered_map<const ufbx_node*, int32_t>& nodeMap,
    _Inout_ ImportedScene* pScene)
{
    // Pass 1 - group stacks by action name and create one ImportedAnimationClip per action.
    std::unordered_map<std::string, size_t> clipIndexByAction;

    AddDiag(pScene, DiagLevel::Info,
        "Anim: " + std::to_string(pLoaded->anim_stacks.count) + " anim_stack(s) in file");

    for (size_t si = 0; si < pLoaded->anim_stacks.count; ++si)
    {
        const ufbx_anim_stack* pStack = pLoaded->anim_stacks.data[si];
        if (!pStack) continue;

        const std::string actionName = ExtractActionName(ToStdString(pStack->name));
        AddDiag(pScene, DiagLevel::Info,
            "Anim:   stack[" + std::to_string(si) + "] name='" + ToStdString(pStack->name) +
            "' -> action='" + actionName + "'");
        if (clipIndexByAction.find(actionName) == clipIndexByAction.end())
        {
            clipIndexByAction[actionName] = pScene->AnimationClips.size();
            pScene->AnimationClips.emplace_back();
            pScene->AnimationClips.back().Name = actionName;
        }
    }

    // Pass 2 - bake each stack and collect its tracks for the matching clip.  The bake covers
    // every property of the nodes it lists, including properties the stack does not animate;
    // those come out as constant tracks.  Each (node, property) of a clip keeps one track: a
    // property a stack animates replaces another stack's constant, and two stacks animating the
    // same property fail the import.  Each clip also records which nodes its stacks baked.
    struct ClipTrack
    {
        bool                    Animated = false;   // a stack of the clip animates the property
        bool                    HasTrack = false;   // false: the property holds the imported pose
        ImportedAnimationTrack  Track;
    };
    using ClipTrackKey = std::pair<int32_t, ImportedTransformProperty>;
    std::vector<std::map<ClipTrackKey, ClipTrack>> clipTracks(pScene->AnimationClips.size());
    std::vector<bool> clipBaked(pScene->AnimationClips.size(), false);
    std::vector<std::vector<bool>> nodeBaked(pScene->AnimationClips.size(), std::vector<bool>(pScene->Nodes.size(), false));
    for (size_t si = 0; si < pLoaded->anim_stacks.count; ++si)
    {
        const ufbx_anim_stack* pStack = pLoaded->anim_stacks.data[si];
        if (!pStack || !pStack->anim) continue;

        const std::string actionName = ExtractActionName(ToStdString(pStack->name));
        const size_t clipIndex = clipIndexByAction.at(actionName);
        ImportedAnimationClip& clip = pScene->AnimationClips[clipIndex];

        const float duration = static_cast<float>(pStack->time_end - pStack->time_begin);
        if (duration <= 0.0f)
        {
            AddDiag(pScene, DiagLevel::Warning,
                "AnimationStack '" + ToStdString(pStack->name) + "' has zero/negative duration; skipping");
            continue;
        }
        clip.DurationSeconds = std::max(clip.DurationSeconds, duration);

        ufbx_bake_opts bakeOpts = { 0 };
        ufbx_error bakeErr = { 0 };
        ufbx_baked_anim* pBaked = ufbx_bake_anim(pLoaded, pStack->anim, &bakeOpts, &bakeErr);
        if (!pBaked)
        {
            AddDiag(pScene, DiagLevel::Error,
                "Failed to bake '" + ToStdString(pStack->name) + "': " +
                std::string(bakeErr.description.data, bakeErr.description.length));
            return Gem::Result::Fail;
        }
        clipBaked[clipIndex] = true;

        for (size_t ni = 0; ni < pBaked->nodes.count; ++ni)
        {
            const ufbx_baked_node& bn = pBaked->nodes.data[ni];
            if (bn.typed_id >= pLoaded->nodes.count) continue;
            const ufbx_node* pNode = pLoaded->nodes.data[bn.typed_id];
            if (!pNode || pNode->is_root) continue;

            const auto itNode = nodeMap.find(pNode);
            if (itNode == nodeMap.end()) continue;
            const int32_t nodeIndex = itNode->second;
            const ImportedNode& importedNode = pScene->Nodes[static_cast<size_t>(nodeIndex)];
            const bool topLevel = pNode->parent && pNode->parent->is_root;
            nodeBaked[clipIndex][static_cast<size_t>(nodeIndex)] = true;

            for (ImportedTransformProperty property : kTransformProperties)
            {
                const bool animated = StackAnimatesProperty(*pStack, *pNode, property);
                ClipTrack& entry = clipTracks[clipIndex][{ nodeIndex, property }];
                if (entry.Animated && animated)
                {
                    AddDiag(pScene, DiagLevel::Error,
                        "Clip '" + clip.Name + "' has more than one stack animating the " +
                        PropertyName(property) + " of node '" + ToStdString(pNode->name) +
                        "'. Check for a stray action on this object.");
                    ufbx_free_baked_anim(pBaked);
                    return Gem::Result::Fail;
                }
                if (entry.Animated || (entry.HasTrack && !animated))
                    continue;    // keep the animated track, or the first stack's constant

                // A property held still at the node's imported pose needs no track. Held
                // anywhere else (e.g. a bone posed away from the bind pose for the whole clip),
                // it keeps one.
                ImportedAnimationTrack track = BakedPropertyToTrack(bn, property, pBaked->playback_time_begin, topLevel ? &rootFold : nullptr);
                entry.Animated = animated;
                entry.HasTrack = !track.Values.empty() &&
                    !(track.Values.size() == 1 && IsSamePropertyValue(property, track.Values.front(), PropertyValue(importedNode.LocalTransform, property)));
                if (entry.HasTrack)
                {
                    track.NodeIndex = nodeIndex;
                    entry.Track = std::move(track);
                }
            }
        }

        ufbx_free_baked_anim(pBaked);
    }
    for (size_t ci = 0; ci < pScene->AnimationClips.size(); ++ci)
        for (auto& [key, entry] : clipTracks[ci])
            if (entry.HasTrack)
                pScene->AnimationClips[ci].Tracks.push_back(std::move(entry.Track));

    // A node a clip does not bake holds its stored (file) transform while the clip plays, so
    // each of its properties whose stored value differs from the imported pose gets a single-key
    // track holding the stored value.
    for (size_t ci = 0; ci < pScene->AnimationClips.size(); ++ci)
    {
        if (!clipBaked[ci])
            continue;

        ImportedAnimationClip& clip = pScene->AnimationClips[ci];
        for (const ufbx_node* pNode : pLoaded->nodes)
        {
            const auto itNode = nodeMap.find(pNode);
            if (itNode == nodeMap.end()) continue;

            const int32_t nodeIndex = itNode->second;
            if (nodeBaked[ci][static_cast<size_t>(nodeIndex)])
                continue;

            const Transform stored = StoredPose(*pNode, rootFold);
            const Transform& imported = pScene->Nodes[static_cast<size_t>(nodeIndex)].LocalTransform;
            for (ImportedTransformProperty property : kTransformProperties)
            {
                const Math::FloatVector4 value = PropertyValue(stored, property);
                if (IsSamePropertyValue(property, value, PropertyValue(imported, property)))
                    continue;

                ImportedAnimationTrack track;
                track.NodeIndex = nodeIndex;
                track.Property  = property;
                track.Times     = { 0.0f };
                track.Values    = { value };
                clip.Tracks.push_back(std::move(track));
            }
        }
    }

    // Remove clips that no stack baked. A baked clip with no tracks holds every node at its
    // imported pose.
    size_t kept = 0;
    for (size_t ci = 0; ci < pScene->AnimationClips.size(); ++ci)
        if (clipBaked[ci])
        {
            if (kept != ci)
                pScene->AnimationClips[kept] = std::move(pScene->AnimationClips[ci]);
            ++kept;
        }
    pScene->AnimationClips.resize(kept);
    return Gem::Result::Success;
}

} // anonymous namespace

Gem::Result ImportScene(
    _In_z_  const wchar_t  *pFilePath,
    _In_    const ImportOptions &options,
    _Out_   ImportedScene  *pScene)
{
    if (!pFilePath || !pScene)
        return Gem::Result::InvalidArg;

    *pScene = {};
    pScene->SceneBounds.Reset();

    const std::string utf8Path = ToUtf8(pFilePath);
    if (utf8Path.empty())
    {
        AddDiag(pScene, DiagLevel::Error, "Could not convert input file path to UTF-8");
        return Gem::Result::InvalidArg;
    }

    ufbx_load_opts loadOpts = { 0 };
    loadOpts.generate_missing_normals = options.GenerateNormals;
    // Scene/world conversion to the Canvas local basis: +X = forward, +Y = left, +Z = up.
    // ufbx `front` is the direction a model faces (Maya content faces +Z, Blender -Y), and
    // ufbx `right` is the viewer's right when facing the model, i.e. the model's left. So a
    // model's front maps to +X and its left to +Y.
    loadOpts.target_axes = ufbx_coordinate_axes{
        UFBX_COORDINATE_AXIS_POSITIVE_Y,
        UFBX_COORDINATE_AXIS_POSITIVE_Z,
        UFBX_COORDINATE_AXIS_POSITIVE_X,
    };
    // Canvas world units are meters.
    loadOpts.target_unit_meters = 1.0;

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

    // Unit conversion. Most exporters (Maya and others using the FBX SDK) write vertices
    // and translations in the declared unit with unit node scale, so ufbx converts the
    // geometry and translations to meters (MODIFY_GEOMETRY) and nodes stay unscaled.
    //
    // Blender writes vertices in meters but declares centimeters, and puts a x100 scale on
    // every top-level object to make up the difference. Converting its geometry would
    // shrink the vertices and leave the x100 on the nodes. Instead the file is reloaded
    // with the conversion (x0.01 plus the axis change) on the ufbx root, and that root
    // transform is folded into the top-level nodes: their x100 becomes x1, vertices and
    // child nodes are untouched, and every node comes out with unit scale.
    RootFold rootFold;
    if (pLoaded && (pLoaded->metadata.exporter == UFBX_EXPORTER_BLENDER_BINARY ||
                    pLoaded->metadata.exporter == UFBX_EXPORTER_BLENDER_ASCII))
    {
        ufbx_load_opts rootOpts = loadOpts;
        rootOpts.space_conversion = UFBX_SPACE_CONVERSION_TRANSFORM_ROOT;
        ufbx_scene *pRootLoaded = ufbx_load_file(utf8Path.c_str(), &rootOpts, &err);
        const ufbx_vec3 rootScale = pRootLoaded ? pRootLoaded->root_node->local_transform.scale : ufbx_vec3{};
        if (pRootLoaded && rootScale.x > 0.0f && rootScale.x == rootScale.y && rootScale.x == rootScale.z)
        {
            ufbx_free_scene(pLoaded);
            pLoaded = pRootLoaded;

            const ufbx_transform &root = pLoaded->root_node->local_transform;
            rootFold.Enabled        = true;
            rootFold.Translation    = ToCanvasVector(root.translation);
            rootFold.Rotation       = ToCanvasQuaternion(root.rotation);
            rootFold.RotationMatrix = Math::QuaternionToRotationMatrix(rootFold.Rotation);
            rootFold.Scale          = static_cast<float>(rootScale.x);
        }
        else if (pRootLoaded)
        {
            ufbx_free_scene(pRootLoaded);
        }
    }

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
        return Gem::Result::Fail;
    }

    if (pLoaded->nodes.count > kMaxImportNodes)
    {
        AddDiag(pScene, DiagLevel::Error,
            "ufbx scene node count is unreasonably large (" + std::to_string(static_cast<unsigned long long>(pLoaded->nodes.count)) + ")");
        ufbx_free_scene(pLoaded);
        return Gem::Result::Fail;
    }

    if (pLoaded->metadata.warnings.count > kMaxImportWarnings)
    {
        AddDiag(pScene, DiagLevel::Error,
            "ufbx warning count is unreasonably large (" + std::to_string(static_cast<unsigned long long>(pLoaded->metadata.warnings.count)) + ")");
        ufbx_free_scene(pLoaded);
        return Gem::Result::Fail;
    }

    std::unordered_map<const ufbx_node*, int32_t> nodeMap;
    nodeMap.reserve(pLoaded->nodes.count);

    // Import materials first so meshes can reference scene-wide indices.
    const std::unordered_map<const ufbx_material *, int32_t> materialMap = ImportMaterials(pLoaded, pScene);

    // Build Canvas node list first so parent indices are stable. Nodes start in the bind pose.
    BindPose bindPose(pLoaded);
    for (size_t ni = 0; ni < pLoaded->nodes.count; ++ni)
    {
        const ufbx_node *pNode = pLoaded->nodes[ni];
        if (!pNode || pNode->is_root)
            continue;

        ImportedNode node;
        node.Name = ToStdString(pNode->name);
        if (node.Name.empty())
            node.Name = "Node_" + std::to_string(ni);

        // ufbx's local TRS composes the same way as a Canvas node (S * R * T, then the
        // parent), so it carries over unchanged.
        bool exact = true;
        const ufbx_transform local = bindPose.LocalTransform(pNode, &exact);
        if (!exact)
            AddDiag(pScene, DiagLevel::Warning,
                "Node '" + node.Name + "' has a bind pose with shear relative to its parent (e.g. a rotated "
                "bone under a non-uniformly scaled parent); its bind transform is approximated, so skinned "
                "geometry may be deformed in the bind pose");
        Transform &nodeTransform = node.LocalTransform;
        nodeTransform.Translation = ToCanvasPoint(local.translation);
        nodeTransform.Rotation    = ToCanvasQuaternion(local.rotation);
        nodeTransform.Scale       = ToCanvasVector(local.scale);
        if (pNode->parent && pNode->parent->is_root)
            rootFold.Apply(nodeTransform.Translation, nodeTransform.Rotation, nodeTransform.Scale);

        // Unit conversion leaves every node unscaled, so remaining scale was authored (or
        // comes from an export setting this importer does not recognize).
        const Math::FloatVector4 &scale = nodeTransform.Scale;
        const float maxScaleError = std::max({ std::abs(scale.X - 1.0f), std::abs(scale.Y - 1.0f), std::abs(scale.Z - 1.0f) });
        if (maxScaleError > 1e-4f)
            AddDiag(pScene, DiagLevel::Info,
                "Node '" + node.Name + "' has scale (" + std::to_string(scale.X) + ", " +
                std::to_string(scale.Y) + ", " + std::to_string(scale.Z) + ")");

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
                if (ImportMesh(pMesh, options.Triangulate, nodeMap, &importedMesh, pScene))
                {
                    // Remap mesh-local material indices (= mesh part index) into
                    // scene-wide indices via materialMap. ImportMesh stored the
                    // mesh-local index as a placeholder.
                    for (ImportedMeshPart &part : importedMesh.Parts)
                    {
                        if (part.MaterialIndex < 0)
                            continue;

                        const size_t localIdx = static_cast<size_t>(part.MaterialIndex);
                        if (localIdx < pMesh->materials.count)
                        {
                            const ufbx_material *pMat = pMesh->materials.data[localIdx];
                            const auto itMat = materialMap.find(pMat);
                            part.MaterialIndex = (itMat != materialMap.end()) ? itMat->second : -1;
                        }
                        else
                        {
                            part.MaterialIndex = -1;
                        }
                    }

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
                if (ImportCamera(pCamera, rootFold.Scale, &importedCamera, pScene))
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

    // Scene bounds from the imported hierarchy itself, so they enclose the model exactly as
    // built (including any approximated bind transforms).
    const std::vector<Math::FloatMatrix4x4> globals = ImportedGlobalMatrices(pScene->Nodes);
    for (size_t i = 0; i < pScene->Nodes.size(); ++i)
    {
        const int32_t meshIndex = pScene->Nodes[i].MeshIndex;
        if (meshIndex >= 0)
            pScene->SceneBounds.ExpandToInclude(pScene->Meshes[static_cast<size_t>(meshIndex)].Bounds.Transform(globals[i]));
    }

    for (size_t wi = 0; wi < pLoaded->metadata.warnings.count; ++wi)
    {
        const ufbx_warning &w = pLoaded->metadata.warnings[wi];
        AddDiag(pScene, DiagLevel::Warning, ToStdString(w.description));
    }

    // Extract animation clips (FBX AnimationStacks / Blender Actions).
    // Must happen before ufbx_free_scene while the ufbx data is still live.
    const Gem::Result clipsResult = ImportAnimationClips(pLoaded, rootFold, nodeMap, pScene);

    ufbx_free_scene(pLoaded);
    if (Gem::Failed(clipsResult))
        return clipsResult;

    if (!pScene->HasMeshes())
    {
        AddDiag(pScene, DiagLevel::Warning, "No importable mesh geometry found in file");
    }

    return Gem::Result::Success;
}

#else // stub when ufbx is not available

Gem::Result ImportScene(
    _In_z_  const wchar_t  *pFilePath,
    _In_    const ImportOptions &options,
    _Out_   ImportedScene  *pScene)
{
    if (!pFilePath || !pScene)
        return Gem::Result::InvalidArg;

    (void)options;
    *pScene = {};
    pScene->Diagnostics.push_back({ DiagLevel::Error, "CanvasFbx built without ufbx - import unavailable" });
    return Gem::Result::NotImplemented;
}

#endif

//================================================================================================
// BuildModel - convert an ImportedScene into a fully populated XModel
//================================================================================================

namespace
{

//------------------------------------------------------------------------------------------------
// How BuildModel divides the imported hierarchy between model nodes and skeleton joints.
//
// The bones of skins that share a bone form one skeleton.  A skeleton is in the space of its
// owner: the nearest common ancestor of its bones' parents, or the instance root when they
// have none.  Its joints are its bones and every node between a bone and the owner, so each
// root joint is a child of the owner.  Skeletons whose joints overlap, or whose owner is a
// joint of another skeleton, are merged.  Every other node is a model node.
//------------------------------------------------------------------------------------------------
struct SkeletonLayout
{
    struct Skeleton
    {
        int32_t                 OwnerNode = -1;     // imported node index; -1: the instance root
        std::vector<int32_t>    Joints;             // imported node indices, parents first
    };

    std::vector<Skeleton>   Skeletons;
    std::vector<int32_t>    SkeletonOfNode;         // per imported node; -1 for a model node
    std::vector<uint32_t>   JointOfNode;            // per imported node; its joint index
};

// The animation target type for one property of a scene node's or joint's transform.
AnimationTargetType TargetType(ImportedTransformProperty property, bool joint)
{
    switch (property)
    {
    case ImportedTransformProperty::Rotation:
        return joint ? AnimationTargetType::JointRotation : AnimationTargetType::SceneNodeRotation;
    case ImportedTransformProperty::Scale:
        return joint ? AnimationTargetType::JointScale : AnimationTargetType::SceneNodeScale;
    default:
        return joint ? AnimationTargetType::JointTranslation : AnimationTargetType::SceneNodeTranslation;
    }
}

// Imported node indices, parents first, with siblings in index order.
std::vector<int32_t> ParentsFirstOrder(_In_ const std::vector<int32_t> &parents)
{
    std::vector<std::vector<int32_t>> children(parents.size());
    std::vector<int32_t> roots;
    for (size_t i = 0; i < parents.size(); ++i)
        (parents[i] < 0 ? roots : children[static_cast<size_t>(parents[i])]).push_back(static_cast<int32_t>(i));

    std::vector<int32_t> order;
    order.reserve(parents.size());
    std::vector<int32_t> stack(roots.rbegin(), roots.rend());
    while (!stack.empty())
    {
        const int32_t n = stack.back();
        stack.pop_back();
        order.push_back(n);
        const std::vector<int32_t> &c = children[static_cast<size_t>(n)];
        stack.insert(stack.end(), c.rbegin(), c.rend());
    }
    return order;
}

SkeletonLayout ComputeSkeletonLayout(
    _In_ const std::vector<int32_t> &parents,
    _In_ const std::vector<int32_t> &order,
    _In_ const std::vector<std::vector<int32_t>> &skinBones)    // the bones of each usable skin
{
    const size_t nodeCount = parents.size();

    std::vector<int32_t> depth(nodeCount, 0);
    for (int32_t n : order)
        if (parents[static_cast<size_t>(n)] >= 0)
            depth[static_cast<size_t>(n)] = depth[static_cast<size_t>(parents[static_cast<size_t>(n)])] + 1;

    // Nearest common ancestor-or-self; -1 stands above every root.
    const auto CommonAncestor = [&](int32_t a, int32_t b)
    {
        while (a >= 0 && b >= 0 && a != b)
        {
            if (depth[static_cast<size_t>(a)] >= depth[static_cast<size_t>(b)])
                a = parents[static_cast<size_t>(a)];
            else
                b = parents[static_cast<size_t>(b)];
        }
        return (a >= 0 && b >= 0) ? a : -1;
    };

    // Union-find over bones: bones that end up in one skeleton share a root.
    std::vector<int32_t> uf(nodeCount);
    for (size_t i = 0; i < nodeCount; ++i)
        uf[i] = static_cast<int32_t>(i);
    const auto Find = [&](int32_t n)
    {
        while (uf[static_cast<size_t>(n)] != n)
            n = uf[static_cast<size_t>(n)] = uf[static_cast<size_t>(uf[static_cast<size_t>(n)])];
        return n;
    };
    const auto Unite = [&](int32_t a, int32_t b)
    {
        a = Find(a);
        b = Find(b);
        if (a == b)
            return false;
        uf[static_cast<size_t>(std::max(a, b))] = std::min(a, b);
        return true;
    };

    std::vector<bool> isBone(nodeCount, false);
    for (const std::vector<int32_t> &bones : skinBones)
        for (int32_t bone : bones)
        {
            isBone[static_cast<size_t>(bone)] = true;
            Unite(bones.front(), bone);
        }

    std::vector<int32_t> owner(nodeCount, -1);      // per group root
    std::vector<int32_t> groupOfNode(nodeCount, -1);
    for (bool changed = true; changed;)
    {
        changed = false;

        // Each group's owner: the common ancestor of its bones' parents.
        std::vector<bool> seen(nodeCount, false);
        for (int32_t n : order)
        {
            if (!isBone[static_cast<size_t>(n)])
                continue;
            const int32_t g = Find(n);
            const int32_t p = parents[static_cast<size_t>(n)];
            owner[static_cast<size_t>(g)] = seen[static_cast<size_t>(g)] ? CommonAncestor(owner[static_cast<size_t>(g)], p) : p;
            seen[static_cast<size_t>(g)] = true;
        }

        // Each group's joints: every node from a bone up to, not including, the owner.  A
        // node claimed by two groups merges them.
        std::fill(groupOfNode.begin(), groupOfNode.end(), -1);
        for (int32_t n : order)
        {
            if (!isBone[static_cast<size_t>(n)])
                continue;
            const int32_t g = Find(n);
            for (int32_t j = n; j >= 0 && j != owner[static_cast<size_t>(g)]; j = parents[static_cast<size_t>(j)])
            {
                int32_t &claim = groupOfNode[static_cast<size_t>(j)];
                if (claim >= 0 && claim != g)
                    changed |= Unite(claim, g);
                claim = g;
            }
        }

        // An owner is a model node: one that is another group's joint merges the two.
        for (int32_t n : order)
        {
            if (!isBone[static_cast<size_t>(n)])
                continue;
            const int32_t g = Find(n);
            const int32_t o = owner[static_cast<size_t>(g)];
            if (o >= 0 && groupOfNode[static_cast<size_t>(o)] >= 0)
                changed |= Unite(groupOfNode[static_cast<size_t>(o)], g);
        }
    }

    // Number the skeletons, and their joints, in hierarchy order.
    SkeletonLayout layout;
    layout.SkeletonOfNode.assign(nodeCount, -1);
    layout.JointOfNode.assign(nodeCount, 0);
    std::unordered_map<int32_t, int32_t> skeletonOfGroup;
    for (int32_t n : order)
    {
        const int32_t claim = groupOfNode[static_cast<size_t>(n)];
        if (claim < 0)
            continue;
        const int32_t g = Find(claim);
        auto [it, inserted] = skeletonOfGroup.try_emplace(g, static_cast<int32_t>(layout.Skeletons.size()));
        if (inserted)
        {
            layout.Skeletons.emplace_back();
            layout.Skeletons.back().OwnerNode = owner[static_cast<size_t>(g)];
        }
        SkeletonLayout::Skeleton &skeleton = layout.Skeletons[static_cast<size_t>(it->second)];
        layout.SkeletonOfNode[static_cast<size_t>(n)] = it->second;
        layout.JointOfNode[static_cast<size_t>(n)] = static_cast<uint32_t>(skeleton.Joints.size());
        skeleton.Joints.push_back(n);
    }
    return layout;
}

} // anonymous namespace

Gem::Result BuildModel(
    _In_    Canvas::XCanvas        *pCanvas,
    _In_    Canvas::XGfxDevice     *pDevice,
    _In_    const ImportedScene    &scene,
    _In_    const BuildModelOptions &options,
    _Out_   Canvas::XModel        **ppModel)
{
    if (!pCanvas || !pDevice || !ppModel)
        return Gem::Result::InvalidArg;

    *ppModel = nullptr;

    Canvas::XLogger *pLogger = options.pLogger;
    const char *modelName = (options.pModelName && options.pModelName[0])
        ? options.pModelName
        : "ImportedModel";

    Gem::TGemPtr<Canvas::XModelBuilder> pBuilder;
    Gem::Result gr = pCanvas->CreateModelBuilder(pDevice, &pBuilder, "CanvasFbx.BuildModel");
    if (Gem::Failed(gr))
        return gr;
    gr = pBuilder->Begin(modelName);
    if (Gem::Failed(gr))
        return gr;

    // -----------------------------------------------------------------
    // Textures: invoke the caller's loader for each referenced image.
    // Failed loads remain null; downstream material binding checks.
    // -----------------------------------------------------------------
    std::vector<Gem::TGemPtr<Canvas::XGfxSurface>> textures(scene.Textures.size());
    if (options.TextureLoader)
    {
        for (size_t i = 0; i < scene.Textures.size(); ++i)
        {
            Canvas::XGfxSurface *pSurface = nullptr;
            if (options.TextureLoader(pDevice, scene.Textures[i], &pSurface))
            {
                textures[i].Attach(pSurface);
                pBuilder->AddTexture(textures[i], nullptr);
            }
            else
            {
                Canvas::LogWarn(pLogger,
                    "BuildModel: texture %zu failed to load (path='%s', embedded=%s); slot will be unbound",
                    i, scene.Textures[i].AbsoluteFilePath.c_str(),
                    scene.Textures[i].Embedded ? "true" : "false");
            }
        }
    }

    // -----------------------------------------------------------------
    // Helper: bind a texture to a material role when the index is valid
    // -----------------------------------------------------------------
    auto BindRole = [&](Canvas::XGfxMaterial *pMaterial,
                        Canvas::MaterialLayerRole role,
                        int32_t textureIndex)
    {
        if (textureIndex < 0)
            return;
        if (static_cast<size_t>(textureIndex) >= textures.size())
            return;
        Canvas::XGfxSurface *pSurface = textures[static_cast<size_t>(textureIndex)].Get();
        if (!pSurface)
            return;
        pMaterial->SetTexture(role, pSurface);
    };

    // -----------------------------------------------------------------
    // Materials: one XGfxMaterial per ImportedMaterial
    // -----------------------------------------------------------------
    std::vector<Gem::TGemPtr<Canvas::XGfxMaterial>> materials(scene.Materials.size());
    for (size_t i = 0; i < scene.Materials.size(); ++i)
    {
        const ImportedMaterial &srcMat = scene.Materials[i];
        Gem::TGemPtr<Canvas::XGfxMaterial> pMaterial;
        gr = pDevice->CreateMaterial(&pMaterial);
        if (Gem::Failed(gr))
        {
            Canvas::LogWarn(pLogger, "BuildModel: failed to create material %zu '%s'", i, srcMat.Name.c_str());
            continue;
        }

        pMaterial->SetBaseColorFactor(srcMat.BaseColorFactor);
        pMaterial->SetEmissiveFactor(srcMat.EmissiveFactor);
        pMaterial->SetRoughMetalAOFactor(srcMat.RoughMetalAOFactor);
        BindRole(pMaterial.Get(), Canvas::MaterialLayerRole::Albedo,           srcMat.AlbedoTextureIndex);
        BindRole(pMaterial.Get(), Canvas::MaterialLayerRole::Normal,           srcMat.NormalTextureIndex);
        BindRole(pMaterial.Get(), Canvas::MaterialLayerRole::Emissive,         srcMat.EmissiveTextureIndex);
        BindRole(pMaterial.Get(), Canvas::MaterialLayerRole::Roughness,        srcMat.RoughnessTextureIndex);
        BindRole(pMaterial.Get(), Canvas::MaterialLayerRole::Metallic,         srcMat.MetallicTextureIndex);
        BindRole(pMaterial.Get(), Canvas::MaterialLayerRole::AmbientOcclusion, srcMat.AmbientOcclusionTextureIndex);
        materials[i] = pMaterial;
        pBuilder->AddMaterial(pMaterial, nullptr);
    }

    // -----------------------------------------------------------------
    // Mesh data: create GPU mesh data per imported mesh
    // -----------------------------------------------------------------
    std::vector<uint32_t> meshDataIndex(scene.Meshes.size(), Canvas::InvalidIndex);
    for (size_t meshIndex = 0; meshIndex < scene.Meshes.size(); ++meshIndex)
    {
        const ImportedMesh &srcMesh = scene.Meshes[meshIndex];
        if (srcMesh.Parts.empty())
        {
            Canvas::LogWarn(pLogger, "BuildModel: skipping mesh '%s': no parts", srcMesh.Name.c_str());
            continue;
        }

        std::vector<Canvas::MeshDataGroupDesc> groups;
        // Per-part bone index/weight arrays; must outlive the CreateMeshData call.
        std::vector<std::vector<Canvas::Math::UIntVector4>>  allBoneIndices;
        std::vector<std::vector<Canvas::Math::FloatVector4>> allBoneWeights;
        groups.reserve(srcMesh.Parts.size());
        allBoneIndices.reserve(srcMesh.Parts.size());
        allBoneWeights.reserve(srcMesh.Parts.size());

        for (size_t partIndex = 0; partIndex < srcMesh.Parts.size(); ++partIndex)
        {
            const ImportedMeshPart &part = srcMesh.Parts[partIndex];
            const uint32_t vertexCount = part.GetVertexCount();
            if (vertexCount == 0 || part.Normals.size() != part.Positions.size())
            {
                Canvas::LogWarn(pLogger,
                    "BuildModel: mesh '%s' part %zu: invalid vertex streams (positions=%zu normals=%zu); skipping",
                    srcMesh.Name.c_str(), partIndex, part.Positions.size(), part.Normals.size());
                continue;
            }

            Canvas::MeshDataGroupDesc group{};
            group.VertexCount = vertexCount;
            group.pPositions  = part.Positions.data();
            group.pNormals    = part.Normals.data();
            group.pUV0        = (part.UV0.size() == vertexCount)      ? part.UV0.data()      : nullptr;
            group.pTangents   = (part.Tangents.size() == vertexCount) ? part.Tangents.data() : nullptr;
            if (part.MaterialIndex >= 0 &&
                static_cast<size_t>(part.MaterialIndex) < materials.size())
            {
                group.pMaterial = materials[static_cast<size_t>(part.MaterialIndex)].Get();
            }

            // Bone weight streams - convert uint8 indices to uint32 for GPU upload
            allBoneIndices.emplace_back();
            allBoneWeights.emplace_back();
            if (srcMesh.Skin.HasSkin && part.SkinVertices.size() == vertexCount)
            {
                auto& bi = allBoneIndices.back();
                auto& bw = allBoneWeights.back();
                bi.resize(vertexCount);
                bw.resize(vertexCount);
                for (uint32_t v = 0; v < vertexCount; ++v)
                {
                    const ImportedSkinVertex& sv = part.SkinVertices[v];
                    bi[v] = { sv.BoneIndices[0], sv.BoneIndices[1],
                              sv.BoneIndices[2], sv.BoneIndices[3] };
                    bw[v] = { sv.Weights[0], sv.Weights[1], sv.Weights[2], sv.Weights[3] };
                }
                group.pBoneIndices = bi.data();
                group.pBoneWeights = bw.data();
            }

            groups.push_back(group);
        }

        if (groups.empty())
        {
            Canvas::LogWarn(pLogger, "BuildModel: mesh '%s': all parts skipped", srcMesh.Name.c_str());
            continue;
        }

        Canvas::MeshDataDesc desc{};
        desc.pGroups    = groups.data();
        desc.GroupCount = static_cast<uint32_t>(groups.size());
        desc.pName      = srcMesh.Name.c_str();

        Gem::TGemPtr<Canvas::XGfxMeshData> pMeshData;
        gr = pDevice->CreateMeshData(desc, &pMeshData);
        if (Gem::Failed(gr))
        {
            Canvas::LogWarn(pLogger, "BuildModel: failed to create mesh data for '%s'", srcMesh.Name.c_str());
            continue;
        }

        gr = pBuilder->AddMeshData(pMeshData, &meshDataIndex[meshIndex]);
        if (Gem::Failed(gr))
            return gr;
    }

    // -----------------------------------------------------------------
    // Hierarchy: which imported nodes are model nodes and which are joints
    // -----------------------------------------------------------------
    const size_t nodeCount = scene.Nodes.size();
    std::vector<int32_t> parents(nodeCount, -1);
    for (size_t ni = 0; ni < nodeCount; ++ni)
    {
        const int32_t parentIndex = scene.Nodes[ni].ParentIndex;
        if (parentIndex >= 0 && static_cast<size_t>(parentIndex) < nodeCount && static_cast<size_t>(parentIndex) != ni)
            parents[ni] = parentIndex;
        else if (parentIndex >= 0)
            Canvas::LogWarn(pLogger, "BuildModel: node %zu has an invalid parent index %d; treating it as a root", ni, parentIndex);
    }
    const std::vector<int32_t> order = ParentsFirstOrder(parents);

    // A skin applies to the first node that carries its mesh; its other instances draw
    // unskinned.  A skin with a bone outside the hierarchy is dropped.  skinNode is -1 for
    // a mesh drawn without a skin.
    std::vector<int32_t> skinNode(scene.Meshes.size(), -1);
    for (size_t ni = 0; ni < nodeCount; ++ni)
    {
        const int32_t meshIndex = scene.Nodes[ni].MeshIndex;
        if (meshIndex >= 0 && static_cast<size_t>(meshIndex) < scene.Meshes.size() && skinNode[static_cast<size_t>(meshIndex)] < 0)
            skinNode[static_cast<size_t>(meshIndex)] = static_cast<int32_t>(ni);
    }
    std::vector<std::vector<int32_t>> skinBones;
    for (size_t meshIndex = 0; meshIndex < scene.Meshes.size(); ++meshIndex)
    {
        const ImportedSkin &skin = scene.Meshes[meshIndex].Skin;
        if (!skin.HasSkin || skinNode[meshIndex] < 0 || meshDataIndex[meshIndex] == Canvas::InvalidIndex)
        {
            skinNode[meshIndex] = -1;
            continue;
        }
        const bool bonesValid = !skin.BoneNodeIndices.empty() &&
            skin.BoneNodeIndices.size() == skin.InvBindPoses.size() &&
            std::all_of(skin.BoneNodeIndices.begin(), skin.BoneNodeIndices.end(),
                [nodeCount](int32_t b) { return b >= 0 && static_cast<size_t>(b) < nodeCount; });
        if (!bonesValid)
        {
            Canvas::LogWarn(pLogger, "BuildModel: mesh '%s' has a skin bone outside the hierarchy; drawing it unskinned",
                scene.Meshes[meshIndex].Name.c_str());
            skinNode[meshIndex] = -1;
            continue;
        }
        skinBones.push_back(skin.BoneNodeIndices);
    }
    const SkeletonLayout layout = ComputeSkeletonLayout(parents, order, skinBones);

    const auto NodeName = [&](size_t ni)
    {
        return scene.Nodes[ni].Name.empty() ? ("ImportedNode_" + std::to_string(ni)) : scene.Nodes[ni].Name;
    };

    try
    {
        std::vector<uint32_t> modelNodeOf(nodeCount, Canvas::InvalidIndex);
        std::vector<uint32_t> skeletonIndex(layout.Skeletons.size(), Canvas::InvalidIndex);
        std::vector<std::vector<uint32_t>> socketOf(layout.Skeletons.size());

        // Adds the skeletons in the space of an imported node (or, for -1, the instance root).
        const auto AddSkeletonsOwnedBy = [&](int32_t ownerNode)
        {
            for (size_t s = 0; s < layout.Skeletons.size(); ++s)
            {
                const SkeletonLayout::Skeleton &src = layout.Skeletons[s];
                if (src.OwnerNode != ownerNode)
                    continue;

                std::vector<std::string> names;
                std::vector<Canvas::ModelJointDesc> joints(src.Joints.size());
                names.reserve(src.Joints.size());
                for (size_t j = 0; j < src.Joints.size(); ++j)
                {
                    const size_t ni = static_cast<size_t>(src.Joints[j]);
                    names.push_back(NodeName(ni));
                }
                for (size_t j = 0; j < src.Joints.size(); ++j)
                {
                    // A root joint's parent is the owner; every other joint's is a joint.
                    const size_t ni = static_cast<size_t>(src.Joints[j]);
                    const int32_t p = parents[ni];
                    const bool parentIsJoint = p >= 0 && layout.SkeletonOfNode[static_cast<size_t>(p)] == static_cast<int32_t>(s);
                    joints[j].Name          = names[j].c_str();
                    joints[j].ParentIndex   = parentIsJoint ? layout.JointOfNode[static_cast<size_t>(p)] : Canvas::InvalidIndex;
                    joints[j].RestTransform = scene.Nodes[ni].LocalTransform;
                }

                Canvas::ModelSkeletonDesc desc{};
                const std::string name = ownerNode >= 0 ? NodeName(static_cast<size_t>(ownerNode)) + "_Skeleton" : std::string("Skeleton");
                desc.Name       = name.c_str();
                desc.NodeIndex  = ownerNode >= 0 ? modelNodeOf[static_cast<size_t>(ownerNode)] : Canvas::InvalidIndex;
                desc.JointCount = static_cast<uint32_t>(joints.size());
                desc.pJoints    = joints.data();
                Gem::ThrowGemError(pBuilder->AddSkeleton(&desc, &skeletonIndex[s]));
                socketOf[s].assign(joints.size(), Canvas::InvalidIndex);
            }
        };

        // The socket node that carries a joint's content, added when first needed.
        const auto SocketFor = [&](size_t jointNode)
        {
            const size_t s = static_cast<size_t>(layout.SkeletonOfNode[jointNode]);
            const uint32_t j = layout.JointOfNode[jointNode];
            if (socketOf[s][j] == Canvas::InvalidIndex)
            {
                const std::string name = NodeName(jointNode) + "_Socket";
                Canvas::ModelSocketDesc desc{};
                desc.Name          = name.c_str();
                desc.SkeletonIndex = skeletonIndex[s];
                desc.JointIndex    = j;
                Gem::ThrowGemError(pBuilder->AddSocket(&desc, &socketOf[s][j]));
            }
            return socketOf[s][j];
        };

        // Model nodes, parents first.  A model node under a joint hangs from the joint's socket.
        AddSkeletonsOwnedBy(-1);
        for (int32_t n : order)
        {
            const size_t ni = static_cast<size_t>(n);
            if (layout.SkeletonOfNode[ni] >= 0)
                continue;

            const std::string name = NodeName(ni);
            Canvas::ModelNodeDesc desc{};
            desc.Name      = name.c_str();
            desc.LocalTransform = scene.Nodes[ni].LocalTransform;
            const int32_t p = parents[ni];
            if (p >= 0)
                desc.ParentIndex = layout.SkeletonOfNode[static_cast<size_t>(p)] >= 0
                    ? SocketFor(static_cast<size_t>(p))
                    : modelNodeOf[static_cast<size_t>(p)];
            Gem::ThrowGemError(pBuilder->AddNode(&desc, &modelNodeOf[ni]));
            AddSkeletonsOwnedBy(n);
        }

        // Meshes, lights, and cameras.  Content on a joint goes on the joint's socket.
        std::vector<uint32_t> cameraOfNode(nodeCount, Canvas::InvalidIndex);
        for (int32_t n : order)
        {
            const size_t ni = static_cast<size_t>(n);
            const ImportedNode &srcNode = scene.Nodes[ni];
            const bool hasMesh = srcNode.MeshIndex >= 0 && static_cast<size_t>(srcNode.MeshIndex) < scene.Meshes.size() &&
                                 meshDataIndex[static_cast<size_t>(srcNode.MeshIndex)] != Canvas::InvalidIndex;
            const bool hasLight  = srcNode.LightIndex >= 0 && static_cast<size_t>(srcNode.LightIndex) < scene.Lights.size();
            const bool hasCamera = srcNode.CameraIndex >= 0 && static_cast<size_t>(srcNode.CameraIndex) < scene.Cameras.size();
            if (!hasMesh && !hasLight && !hasCamera)
                continue;

            const uint32_t node = layout.SkeletonOfNode[ni] >= 0 ? SocketFor(ni) : modelNodeOf[ni];
            const std::string nodeName = NodeName(ni);

            if (hasMesh)
            {
                const size_t meshIndex = static_cast<size_t>(srcNode.MeshIndex);
                const std::string name = nodeName + "_Mesh";
                Canvas::ModelMeshDesc desc{};
                desc.Name          = name.c_str();
                desc.NodeIndex     = node;
                desc.MeshDataIndex = meshDataIndex[meshIndex];

                std::vector<uint32_t> jointIndices;
                const ImportedSkin &skin = scene.Meshes[meshIndex].Skin;
                if (skinNode[meshIndex] == n)
                {
                    jointIndices.reserve(skin.BoneNodeIndices.size());
                    for (int32_t bone : skin.BoneNodeIndices)
                        jointIndices.push_back(layout.JointOfNode[static_cast<size_t>(bone)]);
                    desc.SkeletonIndex = skeletonIndex[static_cast<size_t>(layout.SkeletonOfNode[static_cast<size_t>(skin.BoneNodeIndices.front())])];
                    desc.BoneCount     = static_cast<uint32_t>(jointIndices.size());
                    desc.pJointIndices = jointIndices.data();
                    desc.pInvBindPoses = skin.InvBindPoses.data();
                }
                Gem::ThrowGemError(pBuilder->AddMesh(&desc, nullptr));
            }

            if (hasLight)
            {
                const ImportedLight &srcLight = scene.Lights[static_cast<size_t>(srcNode.LightIndex)];
                const std::string name = srcLight.Name.empty()
                    ? ("ImportedLight_" + std::to_string(srcNode.LightIndex))
                    : srcLight.Name;
                Canvas::ModelLightDesc desc{};
                desc.Name                        = name.c_str();
                desc.NodeIndex                   = node;
                desc.Type                        = srcLight.Type;
                desc.Params.Color                = srcLight.Color;
                desc.Params.Intensity            = srcLight.Intensity;
                desc.Params.Range                = srcLight.Range;
                desc.Params.AttenuationConstant  = srcLight.AttenuationConst;
                desc.Params.AttenuationLinear    = srcLight.AttenuationLinear;
                desc.Params.AttenuationQuadratic = srcLight.AttenuationQuad;
                if (srcLight.Type == Canvas::LightType::Spot)
                {
                    desc.Params.SpotInnerAngle = srcLight.SpotInnerAngle;
                    desc.Params.SpotOuterAngle = srcLight.SpotOuterAngle;
                }
                Gem::ThrowGemError(pBuilder->AddLight(&desc, nullptr));
            }

            if (hasCamera)
            {
                const ImportedCamera &srcCamera = scene.Cameras[static_cast<size_t>(srcNode.CameraIndex)];
                const std::string name = srcCamera.Name.empty()
                    ? ("ImportedCamera_" + std::to_string(srcNode.CameraIndex))
                    : srcCamera.Name;
                Canvas::ModelCameraDesc desc{};
                desc.Name               = name.c_str();
                desc.NodeIndex          = node;
                desc.Params.NearClip    = srcCamera.NearClip;
                desc.Params.FarClip     = srcCamera.FarClip;
                desc.Params.FovAngle    = srcCamera.FovAngle;
                desc.Params.AspectRatio = srcCamera.AspectRatio;
                Gem::ThrowGemError(pBuilder->AddCamera(&desc, &cameraOfNode[ni]));
            }
        }

        // -----------------------------------------------------------------
        // Active camera designation
        // -----------------------------------------------------------------
        if (scene.ActiveCameraNodeIndex >= 0 &&
            static_cast<size_t>(scene.ActiveCameraNodeIndex) < nodeCount &&
            cameraOfNode[static_cast<size_t>(scene.ActiveCameraNodeIndex)] != Canvas::InvalidIndex)
        {
            Gem::ThrowGemError(pBuilder->SetActiveCamera(cameraOfNode[static_cast<size_t>(scene.ActiveCameraNodeIndex)]));
        }
        else
        {
            // Fall back: use the first node that carries a camera
            for (size_t ni = 0; ni < nodeCount; ++ni)
            {
                if (cameraOfNode[ni] != Canvas::InvalidIndex)
                {
                    Gem::ThrowGemError(pBuilder->SetActiveCamera(cameraOfNode[ni]));
                    Canvas::LogWarn(pLogger, "BuildModel: no explicit active camera; using first camera node");
                    break;
                }
            }
        }

        // -----------------------------------------------------------------
        // Animation targets and clips.  An imported node's tracks animate its joint when the
        // node is a joint, and its model node otherwise.  Each node property that any clip
        // animates becomes one animation target, added before the first clip that drives it.
        // -----------------------------------------------------------------
        std::map<std::pair<size_t, ImportedTransformProperty>, uint32_t> targetOf;
        for (const ImportedAnimationClip& srcClip : scene.AnimationClips)
        {
            std::vector<std::vector<float>> packedValues;
            std::vector<Canvas::AnimationTrackDesc> trackDescs;
            std::vector<uint32_t> trackTargets;
            packedValues.reserve(srcClip.Tracks.size());
            trackDescs.reserve(srcClip.Tracks.size());
            trackTargets.reserve(srcClip.Tracks.size());

            for (const ImportedAnimationTrack& srcTrack : srcClip.Tracks)
            {
                if (srcTrack.NodeIndex < 0 || static_cast<size_t>(srcTrack.NodeIndex) >= nodeCount)
                    continue;
                if (srcTrack.Times.size() != srcTrack.Values.size())
                    continue;
                const size_t ni = static_cast<size_t>(srcTrack.NodeIndex);

                Canvas::AnimationTargetDesc target{};
                const bool joint = layout.SkeletonOfNode[ni] >= 0;
                if (joint)
                {
                    target.SkeletonIndex = skeletonIndex[static_cast<size_t>(layout.SkeletonOfNode[ni])];
                    target.Index         = layout.JointOfNode[ni];
                }
                else if (modelNodeOf[ni] != Canvas::InvalidIndex)
                {
                    target.Index = modelNodeOf[ni];
                }
                else
                {
                    continue;
                }
                target.Type = TargetType(srcTrack.Property, joint);
                const bool rotation = srcTrack.Property == ImportedTransformProperty::Rotation;

                auto [it, inserted] = targetOf.try_emplace({ ni, srcTrack.Property }, Canvas::InvalidIndex);
                if (inserted)
                    Gem::ThrowGemError(pBuilder->AddAnimationTarget(&target, &it->second));

                const AnimationValueType valueType = rotation ? AnimationValueType::Quaternion : AnimationValueType::Float3;
                const size_t components = Canvas::AnimationValueComponents(valueType);
                std::vector<float>& packed = packedValues.emplace_back();
                packed.reserve(srcTrack.Values.size() * components);
                for (const Math::FloatVector4& v : srcTrack.Values)
                    for (size_t c = 0; c < components; ++c)
                        packed.push_back(v[static_cast<int>(c)]);

                Canvas::AnimationTrackDesc td{};
                td.ValueType     = valueType;
                td.Interpolation = srcTrack.Interpolation;
                td.pTimes        = srcTrack.Times.data();
                td.pValues       = packed.data();
                td.KeyCount      = static_cast<uint32_t>(srcTrack.Times.size());
                trackDescs.push_back(td);
                trackTargets.push_back(it->second);
            }

            Canvas::AnimationClipDesc desc{};
            desc.Name          = srcClip.Name.c_str();
            desc.Duration      = srcClip.DurationSeconds;
            desc.pTracks       = trackDescs.data();
            desc.pTrackTargets = trackTargets.data();
            desc.TrackCount    = static_cast<uint32_t>(trackDescs.size());

            gr = pBuilder->AddAnimationClip(&desc, nullptr);
            if (Gem::Failed(gr))
                Canvas::LogWarn(pLogger, "BuildModel: failed to add animation clip '%s'",
                    srcClip.Name.c_str());
        }

        Gem::ThrowGemError(pBuilder->Build(ppModel));
    }
    catch (const Gem::GemError &e)
    {
        Canvas::LogError(pLogger, "BuildModel: model '%s' failed: %s", modelName, GemResultString(e.Result()));
        return e.Result();
    }

    return Gem::Result::Success;
}

} // namespace Fbx
} // namespace Canvas
