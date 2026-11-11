//================================================================================================
// Model
//
// ModelContent  : the tables of one model template - shared GPU resources, model nodes, the
//                 elements on them, skeletons, sockets, and animation clips.  Indices between
//                 tables are validated when entries are added, so a model's content is always
//                 consistent.
//
// CModel        : an immutable template.  Its content is fixed when XModelBuilder::Build
//                 creates it, so any number of instances (and threads) may read it at once.
//
// CModelBuilder : the mutable side.  Accumulates one model's content, then moves it into a
//                 new CModel.
//================================================================================================

#pragma once

#include "pch.h"
#include "Canvas.h"
#include "CanvasElement.h"
#include "Animation.h"
#include "Skeleton.h"

namespace Canvas
{

//------------------------------------------------------------------------------------------------
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4324) // structure padded due to alignment specifier
#endif
struct ModelNodeData
{
    std::string         Name;
    uint32_t            ParentIndex = InvalidIndex;   // less than the node's own index
    Transform           LocalTransform;
};

struct ModelSocketData
{
    uint32_t            NodeIndex     = InvalidIndex;
    uint32_t            SkeletonIndex = InvalidIndex;
    uint32_t            BoneIndex     = InvalidIndex;
    Transform           LocalTransform;
};
#ifdef _MSC_VER
#pragma warning(pop)
#endif

struct ModelMeshData
{
    std::string                         Name;
    uint32_t                            NodeIndex     = InvalidIndex;
    uint32_t                            MeshDataIndex = InvalidIndex;
    uint32_t                            SkeletonIndex = InvalidIndex;
    std::vector<uint32_t>               SkeletonBoneIndices;
    std::vector<Math::FloatMatrix4x4>   InvBindPoses;
};

struct ModelCameraData
{
    std::string         Name;
    uint32_t            NodeIndex = InvalidIndex;
    CameraParams        Params;
};

struct ModelLightData
{
    std::string         Name;
    uint32_t            NodeIndex = InvalidIndex;
    LightType           Type      = LightType::Point;
    LightParams         Params;
};

struct ModelContent
{
    std::vector<Gem::TGemPtr<XGfxMeshData>> MeshData;
    std::vector<Gem::TGemPtr<XGfxMaterial>> Materials;
    std::vector<Gem::TGemPtr<XGfxSurface>>  Textures;

    std::vector<ModelNodeData>          Nodes;          // parents first
    std::vector<ModelMeshData>          Meshes;
    std::vector<ModelCameraData>        Cameras;
    std::vector<ModelLightData>         Lights;
    std::vector<ModelSkeletonData>      Skeletons;
    std::vector<ModelSocketData>        Sockets;
    std::vector<AnimTarget>             AnimTargets;
    std::vector<CAnimationClip>         AnimClips;

    Math::AABB                          Bounds;     // meshes at rest, relative to the instance root
};

//------------------------------------------------------------------------------------------------
class CModel :
    public TCanvasElement<XModel>
{
    Gem::TGemPtr<XGfxDevice> m_pDevice;
    ModelContent m_Content;     // immutable after construction

public:
    BEGIN_GEM_INTERFACE_MAP()
        GEM_INTERFACE_ENTRY(XNamedElement)
        GEM_INTERFACE_ENTRY(XCanvasElement)
        GEM_INTERFACE_ENTRY(XModel)
    END_GEM_INTERFACE_MAP()

    // Moves content into the model.
    CModel(XCanvas *pCanvas, XGfxDevice *pDevice, ModelContent &content) :
        TCanvasElement(pCanvas),
        m_pDevice(pDevice),
        m_Content(std::move(content))
    {}

    Gem::Result Initialize() { return Gem::Result::Success; }
    void Uninitialize() {}

    // Gives the content back to a builder whose Build could not complete.
    ModelContent TakeContent() { return std::move(m_Content); }

public: // XModel methods
    GEMMETHOD_(XGfxDevice *, GetDevice)() final
    {
        return m_pDevice.Get();
    }

    GEMMETHOD_(uint32_t, GetMeshDataCount)() final
    {
        return static_cast<uint32_t>(m_Content.MeshData.size());
    }

    GEMMETHOD_(XGfxMeshData *, GetMeshData)(uint32_t index) final
    {
        return (index < m_Content.MeshData.size()) ? m_Content.MeshData[index].Get() : nullptr;
    }

    GEMMETHOD_(uint32_t, GetMaterialCount)() final
    {
        return static_cast<uint32_t>(m_Content.Materials.size());
    }

    GEMMETHOD_(XGfxMaterial *, GetMaterial)(uint32_t index) final
    {
        return (index < m_Content.Materials.size()) ? m_Content.Materials[index].Get() : nullptr;
    }

    GEMMETHOD_(uint32_t, GetTextureCount)() final
    {
        return static_cast<uint32_t>(m_Content.Textures.size());
    }

    GEMMETHOD_(XGfxSurface *, GetTexture)(uint32_t index) final
    {
        return (index < m_Content.Textures.size()) ? m_Content.Textures[index].Get() : nullptr;
    }

    GEMMETHOD_(uint32_t, GetNodeCount)() final
    {
        return static_cast<uint32_t>(m_Content.Nodes.size());
    }

    GEMMETHOD_(const char *, GetNodeName)(uint32_t index) final
    {
        return (index < m_Content.Nodes.size()) ? m_Content.Nodes[index].Name.c_str() : nullptr;
    }

    GEMMETHOD_(uint32_t, GetNodeParent)(uint32_t index) final
    {
        return (index < m_Content.Nodes.size()) ? m_Content.Nodes[index].ParentIndex : InvalidIndex;
    }

    GEMMETHOD(GetNodeTransform)(uint32_t index, Transform *pTransform) final
    {
        if (!pTransform)
            return Gem::Result::BadPointer;
        if (index >= m_Content.Nodes.size())
            return Gem::Result::InvalidArg;
        *pTransform = m_Content.Nodes[index].LocalTransform;
        return Gem::Result::Success;
    }

    GEMMETHOD_(uint32_t, GetSkeletonCount)() final
    {
        return static_cast<uint32_t>(m_Content.Skeletons.size());
    }

    GEMMETHOD_(uint32_t, GetCameraCount)() final
    {
        return static_cast<uint32_t>(m_Content.Cameras.size());
    }

    GEMMETHOD_(uint32_t, GetAnimationClipCount)() final
    {
        return static_cast<uint32_t>(m_Content.AnimClips.size());
    }

    GEMMETHOD_(const char *, GetAnimationClipName)(uint32_t index) final
    {
        return (index < m_Content.AnimClips.size()) ? m_Content.AnimClips[index].Name.c_str() : nullptr;
    }

    // Creates the nodes and elements of an instance under a new instance root, which has no
    // parent and holds the instance's animation controller when the model has clips.
    Gem::Result CreateInstance(XSceneNode **ppInstanceRoot);

    GEMMETHOD_(Math::AABB, GetBounds)() final
    {
        return m_Content.Bounds;
    }
};

//------------------------------------------------------------------------------------------------
class CModelBuilder :
    public TCanvasElement<XModelBuilder>
{
    Gem::TGemPtr<XGfxDevice> m_pDevice;
    bool m_Building = false;
    std::string m_ModelName;
    ModelContent m_Content;

    // Rest pose of each skeleton, for placing sockets; parallel to m_Content.Skeletons.
    std::vector<SkeletonPose> m_RestPoses;

    Gem::Result ValidateAnimationTargets() const;

public:
    BEGIN_GEM_INTERFACE_MAP()
        GEM_INTERFACE_ENTRY(XNamedElement)
        GEM_INTERFACE_ENTRY(XCanvasElement)
        GEM_INTERFACE_ENTRY(XModelBuilder)
    END_GEM_INTERFACE_MAP()

    CModelBuilder(XCanvas *pCanvas, XGfxDevice *pDevice) :
        TCanvasElement(pCanvas),
        m_pDevice(pDevice)
    {}

    Gem::Result Initialize() { return Gem::Result::Success; }
    void Uninitialize() {}

public: // XModelBuilder methods
    GEMMETHOD_(XGfxDevice *, GetDevice)() final { return m_pDevice.Get(); }
    GEMMETHOD(Begin)(const char *name) final;
    GEMMETHOD_(void, Reset)() final;
    GEMMETHOD_(bool, IsBuilding)() const final { return m_Building; }

    GEMMETHOD(AddMeshData)(_In_ XGfxMeshData *pMeshData, _Out_opt_ uint32_t *pIndex) final;
    GEMMETHOD(AddMaterial)(_In_ XGfxMaterial *pMaterial, _Out_opt_ uint32_t *pIndex) final;
    GEMMETHOD(AddTexture)(_In_ XGfxSurface *pTexture, _Out_opt_ uint32_t *pIndex) final;
    GEMMETHOD(AddNode)(_In_ const ModelNodeDesc *pDesc, _Out_opt_ uint32_t *pIndex) final;
    GEMMETHOD(AddMesh)(_In_ const ModelMeshDesc *pDesc, _Out_opt_ uint32_t *pIndex) final;
    GEMMETHOD(AddCamera)(_In_ const ModelCameraDesc *pDesc, _Out_opt_ uint32_t *pIndex) final;
    GEMMETHOD(AddLight)(_In_ const ModelLightDesc *pDesc, _Out_opt_ uint32_t *pIndex) final;
    GEMMETHOD(AddSkeleton)(_In_ const ModelSkeletonDesc *pDesc, _Out_opt_ uint32_t *pIndex) final;
    GEMMETHOD(AddSocket)(_In_ const ModelSocketDesc *pDesc, _Out_opt_ uint32_t *pNodeIndex) final;
    GEMMETHOD(AddAnimationTarget)(_In_ const AnimationTargetDesc *pDesc, _Out_opt_ uint32_t *pIndex) final;
    GEMMETHOD(AddAnimationClip)(_In_ const AnimationClipDesc *pDesc, _Out_opt_ uint32_t *pIndex) final;
    GEMMETHOD(Build)(_Out_ XModel **ppModel) final;
};

}
