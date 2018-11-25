//================================================================================================
// CanvasMath
//================================================================================================

#pragma once

#define CANVASAPI __stdcall

namespace Canvas
{
    interface ICanvas;
    interface IScene;

    interface IMeshModel;
    interface IModel;

    interface ICamera;
    interface ILight;

    interface ITransform;
    interface IMatrixTransform;
    interface IQuaternionTransform;


enum class CanvasResult
{
    Success = 0,
    GenericError = 1,
    NoInterface = 2,
    OutOfMemory = 3,
    FileNotFound = 4,
    InsufficientPrivilege = 5,
    NotImplemented = 6,
};

interface __declspec(uuid("{7ABF521F-4209-4A38-B6D7-741C95772AE0}"))
ICanvas : public IUnknown
{
public:
    virtual HRESULT STDMETHODCALLTYPE CreateScene(REFIID Riid, void **ppScene) = 0;
};

interface __declspec(uuid("{4EADEFF8-2C3C-4085-A246-C961F877C882}"))
IScene :public IUnknown
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

