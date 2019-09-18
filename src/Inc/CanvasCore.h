//================================================================================================
// CanvasCore
//================================================================================================

#pragma once
#include <QLog.h>
#include "CanvasModel.h"

namespace Canvas
{
// Canvas core interfaces
GEM_INTERFACE XCanvas;

// Scene GEM_INTERFACE
GEM_INTERFACE XScene;

// Scene graph node
GEM_INTERFACE XSceneGraphNode;

// Camera GEM_INTERFACEs
GEM_INTERFACE XCamera;

// Light GEM_INTERFACEs
GEM_INTERFACE XLight;

// Transform GEM_INTERFACEs
GEM_INTERFACE XTransform;

// Assets
GEM_INTERFACE XTexture;
GEM_INTERFACE XMaterial;
GEM_INTERFACE XMesh;
GEM_INTERFACE XAmination;
GEM_INTERFACE XSkeleton;

enum CanvasIId
{
    CanvasIId_XCanvas = 1U,
    CanvasIId_XScene = 2U,
    CanvasIId_XSceneGraphNode = 3U,
    CanvasIId_XSceneGraphIterator = 4U,
    CanvasIId_XMeshInstance = 5U,
    CanvasIId_XCamera = 6U,
    CanvasIId_XLight = 7U,
    CanvasIId_XTransform = 8U,
    CanvasIId_XTexture = 9U,
    CanvasIId_XMaterial = 10U,
    CanvasIId_XMesh = 11U,
    CanvasIId_XAmination = 12U,
    CanvasIId_XSkeleton = 13U,
    CanvasIId_XIterator = 14U,
    CanvasIId_XNameTag = 15U,
    CanvasIId_XModel = 16U,
    CanvasIId_XGraphicsDevice = 17U,
    CanvasIId_XGraphicsContext = 18U,
    CanvasIId_XGraphicsResource = 19U,
    CanvasIId_XGraphicsBuffer = 20U,
    CanvasIId_XGraphicsTexture1D = 21U,
    CanvasIId_XGraphicsTexture2D = 22U,
    CanvasIId_XGraphicsTexture3D = 23U,
    CanvasIId_XGraphicsPipelineState = 24U,
    CanvasIId_XGraphicsShaderResourceView = 25U,
    CanvasIId_XGraphicsUnorderedAccessView = 26U,
    CanvasIId_XGraphicsConstantBufferView = 27U,
    CanvasIId_XGraphicsDepthStencilView = 28U,
    CanvasIId_XGraphicsRenderTargetView = 29U,
    CanvasIId_XConstantBuffer = 30U,
    CanvasIId_XUploadBuffer = 31U,
    CanvasIId_XReadbackBuffer = 32U,
};

//------------------------------------------------------------------------------------------------
GEM_INTERFACE XIterator : public Gem::XGeneric
{
    GEM_INTERFACE_DECLARE(CanvasIId_XIterator);

    // Resets the iterator to the start of the collection
    GEMMETHOD(Reset)() = 0;

    // Returns true if the the iterator is at the end of the collection
    GEMMETHOD_(bool, IsAtEnd)() = 0;

    // Moves the iterator to the next element
    GEMMETHOD(MoveNext)() = 0;

    // Moves the iterator to the previous element
    GEMMETHOD(MovePrev)() = 0;

    // QI's the current element (if exists)
    GEMMETHOD(Select)(Gem::InterfaceId iid, _Outptr_result_nullonfailure_ void **ppObj) = 0;
    
    // Removes the current element and the iterator to the next element
    GEMMETHOD(Prune)() = 0;
};

//------------------------------------------------------------------------------------------------
GEM_INTERFACE
XNameTag : public Gem::XGeneric
{
    GEM_INTERFACE_DECLARE(CanvasIId_XNameTag);

    GEMMETHOD_(PCSTR, GetName)() = 0;
    GEMMETHOD(SetName)(PCSTR) = 0;
};

GEM_INTERFACE
XGraphicsContext : public Gem::XGeneric
{
    GEM_INTERFACE_DECLARE(CanvasIId_XGraphicsContext);
};

//------------------------------------------------------------------------------------------------
GEM_INTERFACE 
XGraphicsDevice : public Gem::XGeneric
{
    GEM_INTERFACE_DECLARE(CanvasIId_XGraphicsDevice);

    GEMMETHOD(CreateStaticMesh)(const ModelData::STATIC_MESH_DATA *pMeshData, XMesh **ppMesh) = 0;
    GEMMETHOD(CreateMaterial)(const ModelData::MATERIAL_DATA *pMaterialData, XMaterial **ppMaterial) = 0;
};

//------------------------------------------------------------------------------------------------
GEM_INTERFACE
XCanvas : public Gem::XGeneric
{
    GEM_INTERFACE_DECLARE(CanvasIId_XCanvas);

    GEMMETHOD(CreateScene)(Gem::InterfaceId iid, _Outptr_result_nullonfailure_ void **ppObj) = 0;
    GEMMETHOD(GetNamedObject)(_In_z_ PCSTR szName, Gem::InterfaceId iid, _Outptr_result_nullonfailure_ void **ppObj) = 0;
    GEMMETHOD(CreateNullSceneGraphNode)(Gem::InterfaceId iid, _Outptr_result_nullonfailure_ void **ppObj, _In_z_ PCSTR szName = nullptr) = 0;
    GEMMETHOD(CreateCameraNode)(_In_ const ModelData::CAMERA_DATA *pCameraData, _Outptr_result_nullonfailure_ XCamera **ppCamera, _In_z_ PCSTR szName = nullptr) = 0;
    GEMMETHOD(CreateLightNode)(const ModelData::LIGHT_DATA *pLightData, _Outptr_result_nullonfailure_ XLight **ppLight, _In_z_ PCSTR szName = nullptr) = 0;

    GEMMETHOD(CreateGraphicsDevice)(PCSTR szDLLPath, HWND hWnd, _Outptr_opt_result_nullonfailure_ XGraphicsDevice **ppGraphicsDevice) = 0;
    GEMMETHOD(FrameTick)() = 0;
};

//------------------------------------------------------------------------------------------------
GEM_INTERFACE
XMaterial : public Gem::XGeneric
{
    GEM_INTERFACE_DECLARE(CanvasIId_XMaterial);

    GEMMETHOD(Initialize)() = 0;
};

//------------------------------------------------------------------------------------------------
GEM_INTERFACE
XMesh : public Gem::XGeneric
{
    GEM_INTERFACE_DECLARE(CanvasIId_XMesh);
};

//------------------------------------------------------------------------------------------------
enum RotationType
{
    EulerXYZ,
    EulerXZY,
    EulerYXZ,
    EulerYZX,
    EulerZXY,
    EulerZYX,
    QuaternionWXYZ,
    AxisAngle,
};

//------------------------------------------------------------------------------------------------
GEM_INTERFACE
XTransform : public Gem::XGeneric
{
    GEM_INTERFACE_DECLARE(CanvasIId_XTransform);

    GEMMETHOD_(RotationType, GetRotationType)() const = 0;
    GEMMETHOD_(const Math::FloatVector4 &, GetRotation)() const = 0;
    GEMMETHOD_(const Math::FloatVector4 &, GetTranslation)() const = 0;
    GEMMETHOD_(void, SetRotation)(RotationType Type, _In_ const Math::FloatVector4 &Rotation) = 0;
    GEMMETHOD_(void, SetTranslation)(_In_ const Math::FloatVector4 &Translation) = 0;
    GEMMETHOD(LookAt)(_In_ const Math::FloatVector4 &Location) = 0;
};

//------------------------------------------------------------------------------------------------
GEM_INTERFACE
XSceneGraphNode : public XTransform
{
    GEM_INTERFACE_DECLARE(CanvasIId_XSceneGraphNode);
    GEMMETHOD(AddChild)(_In_ XSceneGraphNode *pChild) = 0;
    GEMMETHOD(CreateChildIterator)(_Outptr_result_nullonfailure_ XIterator **ppIterator) = 0;

    //GEMMETHOD_(void, SetMesh)(XMesh *pMesh) = 0;
    //GEMMETHOD_(void, SetCamera)(XCamera *pCamera) = 0;
    //GEMMETHOD_(void, SetLight)(XLight *pLight) = 0;
};

//------------------------------------------------------------------------------------------------
GEM_INTERFACE
XScene : public XSceneGraphNode
{
    GEM_INTERFACE_DECLARE(CanvasIId_XScene);
};

//------------------------------------------------------------------------------------------------
GEM_INTERFACE
XMeshInstance : public XSceneGraphNode
{
    GEM_INTERFACE_DECLARE(CanvasIId_XMeshInstance);

    GEMMETHOD_(void, SetMesh)(XMesh *pMesh) = 0;
};

//------------------------------------------------------------------------------------------------
GEM_INTERFACE
XCamera : public XSceneGraphNode
{
    GEM_INTERFACE_DECLARE(CanvasIId_XCamera);
};

//------------------------------------------------------------------------------------------------
GEM_INTERFACE
XLight : public XSceneGraphNode
{
    GEM_INTERFACE_DECLARE(CanvasIId_XLight);
};

}

extern Gem::Result GEMAPI CreateCanvas(Gem::InterfaceId iid, _Outptr_result_nullonfailure_ void **ppCanvas, QLog::CLogClient *pLogOutput = nullptr);

