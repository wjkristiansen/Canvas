//================================================================================================
// Light
//================================================================================================

#pragma once

using UintVector3 = TVector<UINT, 3>;

//------------------------------------------------------------------------------------------------
class CMeshInstance :
    public XMeshInstance,
    public CInnerGenericBase
{
public:
    CMeshInstance(XGeneric *pOuterObj) :
        CInnerGenericBase(pOuterObj) {}
    GEMMETHOD(InternalQueryInterface)(InterfaceId iid, _Outptr_ void **ppObj) final
    {
        if (XMeshInstance::IId == iid)
        {
            *ppObj = this;
            AddRef(); // This will actually AddRef the outer generic
            return Result::Success;
        }

        return __super::InternalQueryInterface(iid, ppObj);
    }
};

//------------------------------------------------------------------------------------------------
template <>
class TCanvasObject<ObjectType::MeshInstance> :
    public XGeneric,
    public CCanvasObjectBase
{
public:
    TInnerGeneric<CMeshInstance> m_MeshInstance;
    TInnerGeneric<CTransform> m_Transform;
    TInnerGeneric<CSceneGraphNode> m_SceneGraphNode;
    TInnerGeneric<CObjectName> m_ObjectName;

    TCanvasObject(CCanvas *pCanvas, PCWSTR szName) :
        CCanvasObjectBase(pCanvas),
        m_MeshInstance(this),
        m_Transform(this),
        m_SceneGraphNode(this),
        m_ObjectName(this, szName, pCanvas)
    {}

    GEMMETHOD_(ObjectType, GetType)() const { return ObjectType::MeshInstance; }

    GEMMETHOD(InternalQueryInterface)(InterfaceId iid, _Outptr_ void **ppObj)
    {
        if (XMeshInstance::IId == iid)
        {
            return m_MeshInstance.InternalQueryInterface(iid, ppObj);
        }
        if (XObjectName::IId == iid)
        {
            return m_ObjectName.InternalQueryInterface(iid, ppObj);
        }
        if (XTransform::IId == iid)
        {
            return m_Transform.InternalQueryInterface(iid, ppObj);
        }
        if (XSceneGraphNode::IId == iid)
        {
            return m_SceneGraphNode.InternalQueryInterface(iid, ppObj);
        }

        return __super::InternalQueryInterface(iid, ppObj);
    }
};

//------------------------------------------------------------------------------------------------
// An indexed triangle list with common material and texture attributes
// The actual layout of pixels depends on the material
struct TriangleGroupData
{
    std::vector<UINT> m_Indices;

    void Load() {}
    void Store() {}
};

//------------------------------------------------------------------------------------------------
struct MeshData
{
    std::vector<FloatVector3> m_Vertices;
    std::vector<FloatVector3> m_Normals;
    std::vector<FloatVector2> m_TextureUVs;
    std::vector<UintVector3> m_BoneIndices;
    std::vector<FloatVector4> m_BoneWeights;
    std::vector<TriangleGroupData> m_TriangleGroups;

    void Load() {}
    void Store() {}

    // Returns the index of the added vertex
    UINT AddVertex(const FloatVector3 &vertex);

    // Returns the index of the triangle group
    UINT AddTriangleGroup();

    // Returns the primitive index of the triangle
    UINT AddTriangle(UINT groupId, const UintVector3 &indices);

    // Sets the normal for a given vertex.
    // If no normals have been set then all normals are [0,0,0].
    // First call allocates storage for the full array or normals.
    void SetNormal(UINT index, const FloatVector3 &normal);
};

//------------------------------------------------------------------------------------------------
struct MaterialData
{

};

//------------------------------------------------------------------------------------------------
struct TextureData
{

};

//------------------------------------------------------------------------------------------------
struct CameraData
{
    float m_NearClip;
    float m_FarClip;
    float m_FovAngle;
};

//------------------------------------------------------------------------------------------------
struct LightData
{
    LightType m_Type;
    float m_Intensity;
    FloatVector4 m_Color;
    float m_InnerAngle; // For spot light
    float m_OuterAngle; // For spot light

};

