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
    macro(XCanvas, 1) \
    macro(XScene, 2) \
    macro(XSceneGraphNode, 3) \
    macro(XMeshInstance, 5) \
    macro(XLight, 6) \
    macro(XTransform, 7) \
    macro(XCamera, 8) \
    macro(XTexture, 9) \
    macro(XMaterial, 10) \
    macro(XMesh, 11) \
    macro(XAmination, 12) \
    macro(XSkeleton, 13) \
    macro(XIterator, 14) \
    macro(XNameTag, 15) \
    macro(XModel, 16) \
    macro(XGraphicsDevice, 17) \
    macro(XGraphicsContext, 18) \
    macro(XGraphicsResource, 19) \
    macro(XGraphicsBuffer, 20) \
    macro(XGraphicsTexture1D, 21) \
    macro(XGraphicsTexture2D, 22) \
    macro(XGraphicsTexture3D, 23) \
    macro(XGraphicsPipelineState, 24) \
    macro(XGraphicsShaderResourceView, 25) \
    macro(XGraphicsUnorderedAccessView, 26) \
    macro(XGraphicsConstantBufferView, 27) \
    macro(XGraphicsDepthStencilView, 28) \
    macro(XGraphicsRenderTargetView, 29) \
    macro(XGraphicsConstantBuffer, 30) \
    macro(XGraphicsUploadBuffer, 31) \
    macro(XGraphicsReadbackBuffer, 32) \

//------------------------------------------------------------------------------------------------
#define ENUM_INTERFACE_ID(iface, value) CanvasIId_##iface=value,
enum CanvasIId
{
    FOR_EACH_CANVAS_INTERFACE(ENUM_INTERFACE_ID)
};

//------------------------------------------------------------------------------------------------
#define INTERFACE_ID_STRING_CASE(iface, unused) case CanvasIId_##iface: return #iface;
inline const char* CanvasIIdToString(CanvasIId id)
{
	switch (id)
	{
		FOR_EACH_CANVAS_INTERFACE(INTERFACE_ID_STRING_CASE);
	}

	return nullptr;
}

#define CANVAS_INTERFACE_DECLARE(iface) GEM_INTERFACE_DECLARE(CanvasIId_##iface)

//------------------------------------------------------------------------------------------------
GEM_INTERFACE XIterator : public Gem::XGeneric
{
    CANVAS_INTERFACE_DECLARE(XIterator);

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
    CANVAS_INTERFACE_DECLARE(XNameTag);

    GEMMETHOD_(PCSTR, GetName)() = 0;
    GEMMETHOD(SetName)(PCSTR) = 0;
};

GEM_INTERFACE
XGraphicsContext : public Gem::XGeneric
{
    CANVAS_INTERFACE_DECLARE(XGraphicsContext);
};

GEM_INTERFACE
XGraphicsResource : public Gem::XGeneric
{
    CANVAS_INTERFACE_DECLARE(XGraphicsResource);
};

GEM_INTERFACE
XGraphicsBuffer : public XGraphicsResource
{
    CANVAS_INTERFACE_DECLARE(XGraphicsBuffer);
};

GEM_INTERFACE
XGraphicsUploadBuffer : public XGraphicsBuffer
{
    CANVAS_INTERFACE_DECLARE(XGraphicsUploadBuffer);
};

//------------------------------------------------------------------------------------------------
GEM_INTERFACE 
XGraphicsDevice : public Gem::XGeneric
{
    CANVAS_INTERFACE_DECLARE(XGraphicsDevice);

    GEMMETHOD(Present)() = 0;
    GEMMETHOD(Initialize)(HWND hWnd, bool Windowed) = 0;
    // GEMMETHOD(AllocateUploadBuffer)(UINT64 SizeInBytes, XGraphicsUploadBuffer **ppUploadBuffer) = 0;
};

//------------------------------------------------------------------------------------------------
GEM_INTERFACE
XCanvas : public Gem::XGeneric
{
    CANVAS_INTERFACE_DECLARE(XCanvas);

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
    CANVAS_INTERFACE_DECLARE(XMaterial);

    GEMMETHOD(Initialize)() = 0;
};

//------------------------------------------------------------------------------------------------
GEM_INTERFACE
XMesh : public Gem::XGeneric
{
    CANVAS_INTERFACE_DECLARE(XMesh);
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
    CANVAS_INTERFACE_DECLARE(XTransform);

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
    CANVAS_INTERFACE_DECLARE(XSceneGraphNode);
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
    CANVAS_INTERFACE_DECLARE(XScene);
};

//------------------------------------------------------------------------------------------------
GEM_INTERFACE
XMeshInstance : public XSceneGraphNode
{
    CANVAS_INTERFACE_DECLARE(XMeshInstance);

    GEMMETHOD_(void, SetMesh)(XMesh *pMesh) = 0;
};

//------------------------------------------------------------------------------------------------
GEM_INTERFACE
XCamera : public XSceneGraphNode
{
    CANVAS_INTERFACE_DECLARE(XCamera);
};

//------------------------------------------------------------------------------------------------
GEM_INTERFACE
XLight : public XSceneGraphNode
{
    CANVAS_INTERFACE_DECLARE(XLight);
};

}

extern Gem::Result GEMAPI CreateCanvas(Gem::InterfaceId iid, _Outptr_result_nullonfailure_ void **ppCanvas, QLog::CLogClient *pLogOutput = nullptr);

