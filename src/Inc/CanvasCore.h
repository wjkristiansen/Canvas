//================================================================================================
// CanvasCore
//================================================================================================

#pragma once
#include <QLog.h>
#include "CanvasGfx.h"
#include "CanvasModel.h"

namespace Canvas
{
// Canvas core interfaces
struct XCanvas;

// Scene struct
struct XScene;

// Scene graph node
struct XSceneGraphNode;

// Camera GEM_INTERFACEs
struct XCamera;

// Light GEM_INTERFACEs
struct XLight;

// Transform GEM_INTERFACEs
struct XTransform;

// Assets
struct XTexture;
struct XMaterial;
struct XMesh;
struct XAmination;
struct XSkeleton;

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

#define CANVAS_INTERFACE_DECLARE(iface) GEM_INTERFACE_DECLARE(CanvasIId_##iface)

//------------------------------------------------------------------------------------------------
struct XIterator : public Gem::XGeneric
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
struct
XNameTag : public Gem::XGeneric
{
    CANVAS_INTERFACE_DECLARE(XNameTag);

    GEMMETHOD_(PCSTR, GetName)() = 0;
    GEMMETHOD(SetName)(PCSTR) = 0;
};

//------------------------------------------------------------------------------------------------
struct
XCanvas : public Gem::XGeneric
{
    CANVAS_INTERFACE_DECLARE(XCanvas);

    GEMMETHOD(CreateScene)(Gem::InterfaceId iid, _Outptr_result_nullonfailure_ void **ppObj) = 0;
    GEMMETHOD(GetNamedObject)(_In_z_ PCSTR szName, Gem::InterfaceId iid, _Outptr_result_nullonfailure_ void **ppObj) = 0;
    GEMMETHOD(CreateNullSceneGraphNode)(Gem::InterfaceId iid, _Outptr_result_nullonfailure_ void **ppObj, _In_z_ PCSTR szName = nullptr) = 0;
    GEMMETHOD(CreateCameraNode)(_In_ const ModelData::CAMERA_DATA *pCameraData, _Outptr_result_nullonfailure_ XCamera **ppCamera, _In_z_ PCSTR szName = nullptr) = 0;
    GEMMETHOD(CreateLightNode)(const ModelData::LIGHT_DATA *pLightData, _Outptr_result_nullonfailure_ XLight **ppLight, _In_z_ PCSTR szName = nullptr) = 0;

    GEMMETHOD(InitCanvasGfx)(PCSTR szDLLPath, _Outptr_opt_result_nullonfailure_ XGfxInstance **ppCanvasGfx) = 0;
    GEMMETHOD(FrameTick)() = 0;
};

//------------------------------------------------------------------------------------------------
struct
XMaterial : public Gem::XGeneric
{
    CANVAS_INTERFACE_DECLARE(XMaterial);

    GEMMETHOD(Initialize)() = 0;
};

//------------------------------------------------------------------------------------------------
struct
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
struct
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
struct
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
struct
XScene : public XSceneGraphNode
{
    CANVAS_INTERFACE_DECLARE(XScene);
};

//------------------------------------------------------------------------------------------------
struct
XMeshInstance : public XSceneGraphNode
{
    CANVAS_INTERFACE_DECLARE(XMeshInstance);

    GEMMETHOD_(void, SetMesh)(XMesh *pMesh) = 0;
};

//------------------------------------------------------------------------------------------------
struct
XCamera : public XSceneGraphNode
{
    CANVAS_INTERFACE_DECLARE(XCamera);
};

//------------------------------------------------------------------------------------------------
struct
XLight : public XSceneGraphNode
{
    CANVAS_INTERFACE_DECLARE(XLight);
};

//------------------------------------------------------------------------------------------------
// Helper classes
class CFunctionSentinel
{
    std::shared_ptr<QLog::Logger> m_Logger;
    QLog::Level m_DefaultLogLevel;
    const char *m_FunctionName;
    Gem::Result m_Result = Gem::Result::Success;

public:
    CFunctionSentinel(std::shared_ptr<QLog::Logger> logger, const char *FunctionName, QLog::Level DefaultLogLevel = QLog::Level::Info) :
        m_FunctionName(FunctionName),
        m_Logger(logger),
        m_DefaultLogLevel(DefaultLogLevel)
    {
        if (m_Logger) {
            m_Logger->Log(m_DefaultLogLevel, "Begin: %s", m_FunctionName);
        }
    }
    ~CFunctionSentinel()
    {
        if (m_Logger) {
            QLog::Level level = Gem::Failed(m_Result) ? QLog::Level::Error : m_DefaultLogLevel;
            m_Logger->Log(level, "%s: %s", GemResultString(m_Result), m_FunctionName);
        }
    }

    void SetResultCode(Gem::Result Result) { m_Result = Result; }
};

}

extern Gem::Result GEMAPI CreateCanvas(Gem::InterfaceId iid, _Outptr_result_nullonfailure_ void **ppCanvas, std::shared_ptr<QLog::Logger> pLogger = nullptr);

