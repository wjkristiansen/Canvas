//================================================================================================
// Canvas
//================================================================================================

#pragma once
#include <QLog.h>
#include "CanvasGS.h"
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

//------------------------------------------------------------------------------------------------
#define ENUM_INTERFACE_ID(iface, value) CanvasIId_##iface=value,
enum CanvasIId
{
    FOR_EACH_CANVAS_INTERFACE(ENUM_INTERFACE_ID)
};

//------------------------------------------------------------------------------------------------
#define INTERFACE_ID_STRING_CASE(iface, unused) case CanvasIId_##iface: return #iface;
#define GS_INTERFACE_ID_STRING_CASE(iface, unused) case CanvasGSIId_##iface: return #iface;
inline const char* IIdToString(Gem::InterfaceId id)
{
	switch (id.Value)
	{
        FOR_EACH_CANVAS_INTERFACE(INTERFACE_ID_STRING_CASE);
        FOR_EACH_CANVAS_GS_INTERFACE(GS_INTERFACE_ID_STRING_CASE);
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

    GEMMETHOD(CreateGraphicsDevice)(PCSTR szDLLPath, HWND hWnd, _Outptr_opt_result_nullonfailure_ XCanvasGSDevice **ppGraphicsDevice) = 0;
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
enum class RotationType
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

