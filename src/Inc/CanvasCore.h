//================================================================================================
// CanvasMath
//================================================================================================

#pragma once

#define CANVASAPI __stdcall

namespace Canvas
{
    // Canvas core interface
    interface ICanvas;

    // Collection interfaces
    interface IIterator; // -> IUnknown
    interface INamedCollectionIterator; // -> IIterator
    interface INamedCollection; // -> IUnknown

    // Model interfaces
    interface IModel;
    interface IMeshModel;

    // Scene interface
    interface IScene;

    // Scene object
    interface ISceneObject;

    // Object Parts
    interface IObjectPart;

    // Camera interfaces
    interface ICamera; // -> IPart

    // Light interfaces
    interface ILight; // -> IPart

    // Transform interfaces
    interface ITransform; // -> IPart
    interface IMatrixTransform; // -> ITransform
    interface IEulerTransform; // -> ITransform
    interface IQuaternionTransform; // -> ITransform


interface __declspec(uuid("{8573B59B-0C8F-4456-AF0C-D06A2F40496A}"))
IEnumerable : public IUnknown
{
};

interface __declspec(uuid("{521EBB77-A6AC-4F63-BBFD-C5289B23ABDB}"))
IEnumerator : public IUnknown
{
    STDMETHOD(MoveNext)() = 0;
    STDMETHOD(Current)(REFIID iid, _COM_Outptr_ IEnumerable *ppEnumerable) = 0;
};

interface __declspec(uuid("{99BDDD40-9606-4A6A-AAC2-C75973A72CA7}"))
IStringObjectPair : public IEnumerable
{
    STDMETHOD_(PCSTR, GetString)() = 0;
    STDMETHOD(QueryObject)(REFIID riid, _COM_Outptr_opt_ IUnknown **ppUnk) = 0;
};

interface __declspec(uuid("{ED514943-2693-4E0F-ABA5-BDC92476F9B1}"))
IStringObjectPairEnumerator : public IEnumerator
{
};

interface __declspec(uuid("{61CC0772-B6D9-42DE-A2E0-0F167EF7BFEB}"))
ISortedStringObjectPairList : public IUnknown
{
    STDMETHOD(AddObject)(PCSTR pString, _In_ IUnknown *pUnknown) = 0;
    STDMETHOD(FindObject)(PCSTR pString, REFIID riid, _COM_Outptr_opt_ void **ppObj) = 0;
    STDMETHOD(EnumBegin(_COM_Outptr_ IStringObjectPairEnumerator **ppEnumerator)) = 0;
};

interface __declspec(uuid("{7ABF521F-4209-4A38-B6D7-741C95772AE0}"))
ICanvas : public IUnknown
{
    STDMETHOD(CreateScene)(REFIID Riid, _COM_Outptr_ void **ppScene) = 0;
};

interface __declspec(uuid("{4EADEFF8-2C3C-4085-A246-C961F877C882}"))
IScene :public IUnknown
{

};

interface __declspec(uuid("{2E600AEF-81A3-4CAB-B0BA-FBF607A3E741}"))
IPart : public IUnknown
{
};

interface __declspec(uuid("{AE2AC3F3-138E-4B88-B27D-C1567F38F2A1}"))
ISceneObject : public IUnknown
{
};

#if 0
class ITransform : public IGeneric
{
public:
    virtual void SetTranslation(const FloatVector3 &Translation) = 0;
    virtual void SetRotation(const FloatQuaternion &Rotation) = 0;
    virtual void GetTranslation(FloatVector3 &Translation) const = 0;
    virtual void GetRotation(FloatQuaternion &Quaternion) const = 0;
};

class INamed : public IGeneric
{
public:
    virtual const char *GetName() const = 0;
    virtual CanvasResult SetName(const char *pName) = 0;
};

class IEnumerable : public IGeneric
{
};

class IEnumerator : public IGeneric
{
public:
    virtual CanvasResult GetEnumerable(IEnumerable **ppEnumerable) = 0;
    virtual void Reset() = 0;
    virtual void MoveNext() = 0;
};

class ISortedList : public IGeneric
{
    virtual CanvasResult NewEnumerator(IEnumerator **ppEnumerator);
    virtual CanvasResult Add(const char *pKey, IEnumerable *pEnumerable, IEnumerator *pEnumerator) = 0;
    virtual CanvasResult Remove(IEnumerator *pEnumerator) = 0;
};

class IModel : pubilc IGeneric
{
public:
};

#endif


}


extern HRESULT CANVASAPI CreateCanvas(REFIID riid, void **ppCanvas);

