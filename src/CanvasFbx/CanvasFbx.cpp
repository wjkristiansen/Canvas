// CanvasFbx.cpp : Scene import entry point
//

#include "pch.h"
#include "CanvasFbx.h"
#include "CanvasGfx.h"

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
    _Inout_ ImportedMesh *pOut,
    _Inout_ ImportedScene *pScene)
{
    pOut->Name = ToStdString(pMesh->name);
    pOut->Parts.clear();
    pOut->Bounds.Reset();

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

} // anonymous namespace

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

    // Import materials first so meshes can reference scene-wide indices.
    const std::unordered_map<const ufbx_material *, int32_t> materialMap = ImportMaterials(pLoaded, pScene);

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

//================================================================================================
// BuildModel — convert an ImportedScene into a fully populated XModel
//================================================================================================

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

    // Create the model shell
    Gem::TGemPtr<Canvas::XModel> pModel;
    Gem::Result gr = pCanvas->CreateModel(pDevice, &pModel, modelName);
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
                pModel->AddTexture(textures[i]);
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
        pModel->AddMaterial(pMaterial);
    }

    // -----------------------------------------------------------------
    // Mesh data: create GPU mesh data per imported mesh
    // -----------------------------------------------------------------
    std::vector<Gem::TGemPtr<Canvas::XGfxMeshData>> meshDataByIndex(scene.Meshes.size());
    for (size_t meshIndex = 0; meshIndex < scene.Meshes.size(); ++meshIndex)
    {
        const ImportedMesh &srcMesh = scene.Meshes[meshIndex];
        if (srcMesh.Parts.empty())
        {
            Canvas::LogWarn(pLogger, "BuildModel: skipping mesh '%s': no parts", srcMesh.Name.c_str());
            continue;
        }

        std::vector<Canvas::MeshDataGroupDesc> groups;
        groups.reserve(srcMesh.Parts.size());
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

        meshDataByIndex[meshIndex] = pMeshData;
        pModel->AddMeshData(pMeshData);
    }

    // -----------------------------------------------------------------
    // Lights
    // -----------------------------------------------------------------
    std::vector<Gem::TGemPtr<Canvas::XLight>> lightsByIndex(scene.Lights.size());
    for (size_t lightIndex = 0; lightIndex < scene.Lights.size(); ++lightIndex)
    {
        const ImportedLight &srcLight = scene.Lights[lightIndex];
        const std::string lightName = srcLight.Name.empty()
            ? ("ImportedLight_" + std::to_string(lightIndex))
            : srcLight.Name;

        Gem::TGemPtr<Canvas::XLight> pLight;
        gr = pCanvas->CreateLight(srcLight.Type, &pLight, lightName.c_str());
        if (Gem::Failed(gr))
        {
            Canvas::LogWarn(pLogger, "BuildModel: failed to create light '%s'", lightName.c_str());
            continue;
        }

        pLight->SetColor(srcLight.Color);
        pLight->SetIntensity(srcLight.Intensity);
        pLight->SetRange(srcLight.Range);
        pLight->SetAttenuation(srcLight.AttenuationConst, srcLight.AttenuationLinear, srcLight.AttenuationQuad);
        if (srcLight.Type == Canvas::LightType::Spot)
            pLight->SetSpotAngles(srcLight.SpotInnerAngle, srcLight.SpotOuterAngle);

        lightsByIndex[lightIndex].Attach(pLight.Detach());
    }

    // -----------------------------------------------------------------
    // Cameras
    // -----------------------------------------------------------------
    std::vector<Gem::TGemPtr<Canvas::XCamera>> camerasByIndex(scene.Cameras.size());
    for (size_t cameraIndex = 0; cameraIndex < scene.Cameras.size(); ++cameraIndex)
    {
        const ImportedCamera &srcCamera = scene.Cameras[cameraIndex];
        const std::string cameraName = srcCamera.Name.empty()
            ? ("ImportedCamera_" + std::to_string(cameraIndex))
            : srcCamera.Name;

        Gem::TGemPtr<Canvas::XCamera> pCamera;
        gr = pCanvas->CreateCamera(&pCamera, cameraName.c_str());
        if (Gem::Failed(gr))
        {
            Canvas::LogWarn(pLogger, "BuildModel: failed to create camera '%s'", cameraName.c_str());
            continue;
        }

        pCamera->SetNearClip(srcCamera.NearClip);
        pCamera->SetFarClip(srcCamera.FarClip);
        pCamera->SetFovAngle(srcCamera.FovAngle);
        pCamera->SetAspectRatio(srcCamera.AspectRatio);

        camerasByIndex[cameraIndex].Attach(pCamera.Detach());
    }

    // -----------------------------------------------------------------
    // Scene-graph nodes: create, set TRS, bind mesh/light/camera elements
    // -----------------------------------------------------------------
    Canvas::XSceneGraphNode *pModelRoot = pModel->GetRootNode();
    std::vector<Gem::TGemPtr<Canvas::XSceneGraphNode>> nodes(scene.Nodes.size());

    for (size_t nodeIndex = 0; nodeIndex < scene.Nodes.size(); ++nodeIndex)
    {
        const ImportedNode &srcNode = scene.Nodes[nodeIndex];
        const std::string nodeName = srcNode.Name.empty()
            ? ("ImportedNode_" + std::to_string(nodeIndex))
            : srcNode.Name;

        Gem::TGemPtr<Canvas::XSceneGraphNode> pNode;
        gr = pCanvas->CreateSceneGraphNode(&pNode, nodeName.c_str());
        if (Gem::Failed(gr))
        {
            Canvas::LogWarn(pLogger, "BuildModel: failed to create node '%s'", nodeName.c_str());
            continue;
        }

        pNode->SetName(nodeName.c_str());
        pNode->SetLocalTranslation(srcNode.Translation);
        pNode->SetLocalRotation(srcNode.Rotation);
        pNode->SetLocalScale(srcNode.Scale);

        // Bind mesh instance
        if (srcNode.MeshIndex >= 0 && static_cast<size_t>(srcNode.MeshIndex) < meshDataByIndex.size())
        {
            Canvas::XGfxMeshData *pMeshData = meshDataByIndex[static_cast<size_t>(srcNode.MeshIndex)].Get();
            if (pMeshData)
            {
                const std::string meshInstanceName = nodeName + "_Mesh";
                Gem::TGemPtr<Canvas::XMeshInstance> pMeshInstance;
                gr = pCanvas->CreateMeshInstance(&pMeshInstance, meshInstanceName.c_str());
                if (Gem::Succeeded(gr))
                {
                    pMeshInstance->SetMeshData(pMeshData);
                    pNode->BindElement(pMeshInstance);
                }
            }
        }

        // Bind light
        if (srcNode.LightIndex >= 0 && static_cast<size_t>(srcNode.LightIndex) < lightsByIndex.size())
        {
            Canvas::XLight *pLight = lightsByIndex[static_cast<size_t>(srcNode.LightIndex)].Get();
            if (pLight)
                pNode->BindElement(pLight);
        }

        // Bind camera
        if (srcNode.CameraIndex >= 0 && static_cast<size_t>(srcNode.CameraIndex) < camerasByIndex.size())
        {
            Canvas::XCamera *pCamera = camerasByIndex[static_cast<size_t>(srcNode.CameraIndex)].Get();
            if (pCamera)
                pNode->BindElement(pCamera);
        }

        nodes[nodeIndex].Attach(pNode.Detach());
    }

    // -----------------------------------------------------------------
    // Wire parent-child relationships
    // -----------------------------------------------------------------
    for (size_t nodeIndex = 0; nodeIndex < scene.Nodes.size(); ++nodeIndex)
    {
        if (!nodes[nodeIndex])
            continue;
        const int32_t parentIndex = scene.Nodes[nodeIndex].ParentIndex;
        if (parentIndex >= 0 && static_cast<size_t>(parentIndex) < nodes.size() && nodes[static_cast<size_t>(parentIndex)])
            nodes[static_cast<size_t>(parentIndex)]->AddChild(nodes[nodeIndex]);
        else
            pModelRoot->AddChild(nodes[nodeIndex]);
    }

    // -----------------------------------------------------------------
    // Active camera designation
    // -----------------------------------------------------------------
    if (scene.ActiveCameraNodeIndex >= 0 &&
        static_cast<size_t>(scene.ActiveCameraNodeIndex) < nodes.size() &&
        nodes[static_cast<size_t>(scene.ActiveCameraNodeIndex)])
    {
        pModel->SetActiveCameraNode(nodes[static_cast<size_t>(scene.ActiveCameraNodeIndex)].Get());
    }
    else if (!camerasByIndex.empty())
    {
        // Fall back: use the first node that carries a camera
        for (size_t nodeIndex = 0; nodeIndex < scene.Nodes.size(); ++nodeIndex)
        {
            if (scene.Nodes[nodeIndex].CameraIndex >= 0 && nodes[nodeIndex])
            {
                pModel->SetActiveCameraNode(nodes[nodeIndex].Get());
                Canvas::LogWarn(pLogger, "BuildModel: no explicit active camera; using first camera node");
                break;
            }
        }
    }

    *ppModel = pModel.Detach();
    return Gem::Result::Success;
}

} // namespace Fbx
} // namespace Canvas
