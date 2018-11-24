//================================================================================================
// CanvasMath
//================================================================================================

#pragma once

#include "CanvasMath.hpp"

#define CANVASAPI __stdcall

namespace Canvas
{

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

enum CANVAS_INTERFACE_IDS
{
    CANVAS_INTERFACE_ID_NONE = 0,
    CANVAS_INTERFACE_ID_GENERIC = 1,
    CANVAS_INTERFACE_ID_CANVAS = 2,

    CANVAS_INTERFACE_ID_USER_BASE = 0x80000000
};

class IGeneric
{
public:
    static constexpr unsigned int IID = CANVAS_INTERFACE_ID_GENERIC;
    virtual void AddRef() = 0;
    virtual unsigned int Release() = 0;
    virtual CanvasResult QueryInterface(unsigned int iid, void **ppObject) = 0;
};

class ICanvas : public IGeneric
{
public:
    static constexpr unsigned int IID = CANVAS_INTERFACE_ID_CANVAS;
    virtual CanvasResult CreateScene() = 0;
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

class IScene
{

};
#endif


}


extern Canvas::CanvasResult CANVASAPI CreateCanvas(unsigned int iid, void **ppCanvas);

