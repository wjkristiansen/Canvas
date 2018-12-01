//================================================================================================
// CanvasMath
//================================================================================================

#pragma once

#define CANVASAPI __stdcall

namespace Canvas
{
enum NODE_ELEMENT_FLAGS
{
    NODE_ELEMENT_FLAGS_NONE        = 0x0,
    NODE_ELEMENT_FLAGS_CAMERA      = 0x1,
    NODE_ELEMENT_FLAGS_LIGHT       = 0x2,
    NODE_ELEMENT_FLAGS_MODEL       = 0x4,
    NODE_ELEMENT_FLAGS_TRANSFORM   = 0x8,
};

// Canvas core interface
interface ICanvas;

// Scene interface
interface IScene;

// Scene object
interface ISceneGraphNode;

// Camera interfaces
interface ICamera;

// Light interfaces
interface ILight;

// Transform interfaces
interface ITransform;

// Assets
interface ITexture;
interface IMaterial;
interface IMesh;
interface IAnimation;
interface ISkeleton;

interface __declspec(uuid("{521EBB77-A6AC-4F63-BBFD-C5289B23ABDB}"))
IIterator : public IUnknown
{
    STDMETHOD(MoveNext)() = 0;
    STDMETHOD(Remove)() = 0;
    STDMETHOD(GetCurrentObject)(REFIID riid, _COM_Outptr_ void **ppUnk) = 0;
};

interface __declspec(uuid("{7ABF521F-4209-4A38-B6D7-741C95772AE0}"))
ICanvas : public IUnknown
{
    STDMETHOD(CreateScene)(REFIID Riid, _COM_Outptr_ void **ppScene) = 0;
    STDMETHOD(CreateNode)(PCSTR pName, NODE_ELEMENT_FLAGS flags, REFIID riid, _COM_Outptr_ void **ppSceneGraphNode) = 0;
};

interface __declspec(uuid("{ABDC9885-42C0-45ED-AEA4-E549EE9C5A0F}"))
IModelInstance : public IUnknown
{
};

interface __declspec(uuid("{1707E5D1-900B-47A9-A372-5401F911A52A}"))
ICamera : public IUnknown
{
};

interface __declspec(uuid("{37A8917B-A007-4A53-A868-52D2C88E09A3}"))
ILight : public IUnknown
{
};

interface __declspec(uuid("{E4BA9961-052C-4819-9FCC-E63E75D81D22}"))
ITransform : public IUnknown
{
};

interface __declspec(uuid("{CA3A2CFD-2648-4371-9A5B-2B51AD057A9F}"))
ISceneGraphIterator : public IUnknown
{
    STDMETHOD(MoveNextSibling)() = 0;
    STDMETHOD(GetNode(REFIID riid, void **ppNode)) = 0;
    STDMETHOD(Reset)(_In_ ISceneGraphNode *pParentNode, _In_opt_ PCSTR pName) = 0;
};

interface __declspec(uuid("{92F3F7C3-3470-4D5D-A52F-86A642A7BDAB}"))
ISceneGraphNode : public IUnknown
{
    STDMETHOD(AddChild)(_In_ PCSTR pName, _In_ ISceneGraphNode *pSceneNode) = 0;
};

interface __declspec(uuid("{4EADEFF8-2C3C-4085-A246-C961F877C882}"))
IScene : public IUnknown
{
};

//interface __declspec(uuid("{656FF461-CDBD-4112-AD55-498F3D3BD4E0}"))
}


extern HRESULT CANVASAPI CreateCanvas(REFIID riid, void **ppCanvas);

