//================================================================================================
// CanvasModel.h
//================================================================================================

#pragma once

namespace Canvas
{
    namespace Model
    {
        //------------------------------------------------------------------------------------------------
        enum LIGHT_TYPE
        {
            LIGHT_TYPE_NULL = 0,
            LIGHT_TYPE_POINT,
            LIGHT_TYPE_DIRECTIONAL,
            LIGHT_TYPE_SPOT,
            LIGHT_TYPE_AREA,
            LIGHT_TYPE_VOLUME
        };

        //------------------------------------------------------------------------------------------------
        struct TEXTURE_DATA
        {

        };

        //------------------------------------------------------------------------------------------------
        struct MATERIAL_DATA
        {
            Math::UIntVector4 TextureIndices;
            Math::FloatVector3 AmbientColor;
            Math::FloatVector3 DiffuseColor;
            Math::FloatVector3 SpecularColor;
        };

        //------------------------------------------------------------------------------------------------
        // An indexed triangle list with common material and texture attributes
        // The actual layout of pixels depends on the material
        struct MATERIAL_GROUP_DATA
        {
            UINT NumTriangles = 0;
            _In_count_(NumTriangles)  Math::UIntVector3 *pTriangles = nullptr;
            UINT MaterialIndex = 0;
        };

        //------------------------------------------------------------------------------------------------
        struct MESH_DATA
        {
            UINT NumVertices = 0;
            _In_count_(NumVertices) Math::FloatVector3 *pVertices = nullptr;
            _In_opt_count_(NumVertices) Math::FloatVector3 *pNormals = nullptr;
            _In_opt_count_(NumVertices) Math::FloatVector2 *pTextureUVs[4] = { 0 };
            _In_opt_count_(NumVertices) Math::UIntVector4 *pBoneIndices = nullptr;
            _In_opt_count_(NumVertices) Math::FloatVector4 *pBoneWeights = nullptr;
            UINT NumMaterialGroups = 0;
            _In_count_(NumMaterialGroups) MATERIAL_GROUP_DATA *pMaterialGroups = nullptr;
        };

        //------------------------------------------------------------------------------------------------
        struct CAMERA_DATA
        {
            float NearClip;
            float FarClip;
            float FovAngle;
        };

        //------------------------------------------------------------------------------------------------
        struct LIGHT_DATA
        {
            LIGHT_TYPE Type;
            float Intensity;
            Math::FloatVector4 Color;
            float InnerAngle; // For spot light
            float OuterAngle; // For spot light

        };

    }
}