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

#define FOR_EACH_CANVAS_INTERFACE(macro) \
    macro(XCanvas) \
    macro(XScene) \
    macro(XSceneGraphNode) \
    macro(XSceneGraphIterator) \
    macro(XMeshInstance) \
    macro(XLight) \
    macro(XTransform) \
    macro(XCamera) \
    macro(XTexture) \
    macro(XMaterial) \
    macro(XMesh) \
    macro(XAmination) \
    macro(XSkeleton) \
    macro(XIterator) \
    macro(XNameTag) \
    macro(XModel) \
    macro(XGraphicsDevice) \
    macro(XGraphicsContext) \
    macro(XGraphicsResource) \
    macro(XGraphicsBuffer) \
    macro(XGraphicsTexture1D) \
    macro(XGraphicsTexture2D) \
    macro(XGraphicsTexture3D) \
    macro(XGraphicsPipelineState) \
    macro(XGraphicsShaderResourceView) \
    macro(XGraphicsUnorderedAccessView) \
    macro(XGraphicsConstantBufferView) \
    macro(XGraphicsDepthStencilView) \
    macro(XGraphicsRenderTargetView) \
    macro(XGraphicsConstantBuffer) \
    macro(XGraphicsUploadBuffer) \
    macro(XGraphicsReadbackBuffer) \

#define ENUM_INTERFACE_ID(iface) CanvasIId_##iface,

enum CanvasIId
{
    FOR_EACH_CANVAS_INTERFACE(ENUM_INTERFACE_ID)
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

GEM_INTERFACE
XGraphicsResource : public Gem::XGeneric
{
    GEM_INTERFACE_DECLARE(CanvasIId_XGraphicsResource);
};

GEM_INTERFACE
XGraphicsBuffer : public XGraphicsResource
{
    GEM_INTERFACE_DECLARE(CanvasIId_XGraphicsBuffer);
};

GEM_INTERFACE
XGraphicsUploadBuffer : public XGraphicsBuffer
{
    GEM_INTERFACE_DECLARE(CanvasIId_XGraphicsUploadBuffer);
};

//------------------------------------------------------------------------------------------------
GEM_INTERFACE 
XGraphicsDevice : public Gem::XGeneric
{
    GEM_INTERFACE_DECLARE(CanvasIId_XGraphicsDevice);

    GEMMETHOD(Present)() = 0;
    GEMMETHOD(Initialize)(HWND hWnd, bool Windowed) = 0;
    // GEMMETHOD(AllocateUploadBuffer)(UINT64 SizeInBytes, XGraphicsUploadBuffer **ppUploadBuffer) = 0;
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

