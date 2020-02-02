//================================================================================================
// CanvasModel.h
//================================================================================================

#pragma once

namespace Canvas
{
    namespace ModelData
    {
        struct ModelHeader
        {
            UINT NumStrings;
            UINT NumMeshes;
            UINT NumLights;
            UINT NumCameras;
            UINT NumFunctions;
            UINT NumNodes;
        };

        enum NodeType
        {
            Null,
            Bone,
            Mesh,
            Light,
            Camera,
        };

        struct NodeData
        {
            UINT NameIndex; // Index in string table of node name
            UINT NodeType;
            float Rotation[4];
            float Translation[3];
        };

        //------------------------------------------------------------------------------------------------
        enum LIGHT_TYPE
        {
            LIGHT_TYPE_NULL = 0,
            LIGHT_TYPE_AMBIENT,
            LIGHT_TYPE_POINT,
            LIGHT_TYPE_DIRECTIONAL,
            LIGHT_TYPE_SPOT,
//            LIGHT_TYPE_AREA,
//            LIGHT_TYPE_VOLUME
        };

        //------------------------------------------------------------------------------------------------
        struct LIGHT_DATA
        {
            LIGHT_TYPE Type;
            Math::FloatVector4 Color;
        };

        //------------------------------------------------------------------------------------------------
        struct SPOT_LIGHT_INFO
        {
            float InnerAngle;
            float OuterAngle;
        };

        //------------------------------------------------------------------------------------------------
        struct CAMERA_DATA
        {
            float NearClip;
            float FarClip;
            float FovAngle;
        };

        //------------------------------------------------------------------------------------------------
        struct TRIANGLE_GROUP_DATA
        {
            UINT MaterialId = 0;
            UINT NumTriangles = 0;
            Math::UIntVector3 *pTriangles = nullptr;
        };

        //------------------------------------------------------------------------------------------------
        struct STATIC_MESH_DATA
        {
            UINT NumVertices = 0;
            _In_count_(NumVertices) Math::FloatVector3 *pVertices = nullptr;
            _In_opt_count_(NumVertices) Math::FloatVector3 *pNormals = nullptr;
            _In_opt_count_(NumVertices) Math::FloatVector2 *pTextureUVs[4] = { 0 };
            UINT NumTriangleGroups = 0;
            _In_count_(NumTriangleGroups) TRIANGLE_GROUP_DATA *pTriangleGroups = nullptr;
        };

        //------------------------------------------------------------------------------------------------
        struct SKIN_MESH_DATA
        {
            UINT NumVertices = 0;
            _In_count_(NumVertices) Math::FloatVector3 *pVertices = nullptr;
            _In_opt_count_(NumVertices) Math::FloatVector3 *pNormals = nullptr;
            _In_opt_count_(NumVertices) Math::FloatVector2 *pTextureUVs[4] = { 0 };
            _In_opt_count_(NumVertices) Math::UIntVector4 *pBoneIndices = nullptr;
            _In_opt_count_(NumVertices) Math::FloatVector4 *pBoneWeights = nullptr;
            UINT NumTriangleGroups = 0;
            _In_count_(NumTriangleGroups) TRIANGLE_GROUP_DATA *pTriangleGroups = nullptr;
        };

        //------------------------------------------------------------------------------------------------
        struct TEXTURE2D_DATA
        {
            UINT Width;
            UINT Height;
            void *pData;
        };

        //------------------------------------------------------------------------------------------------
        struct MATERIAL_DATA
        {
            Math::UIntVector4 TextureIds;
            Math::FloatVector3 AmbientColor;
            Math::FloatVector3 DiffuseColor;
            Math::FloatVector3 SpecularColor;
            Math::FloatVector3 EmissiveColor;
        };
    }
}