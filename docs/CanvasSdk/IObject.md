# IObject

## IObject interface
Base interface for all Canvas interfaces.  Custom interfaces must also inherit from IObject.

```c++
interface IObject
{
    virtual CanvasResult QueryInterface(unsigned int iid, void **ppObject) = 0;
    virtual void AddRef() = 0;
    virtual CanvasResult Release() = 0;
};
```

## IObject::QueryInterface function
Queries for interface pointer corresponding to the given locally-unique iid.

```
CanvasResult IObject::QueryInterface(unsigned int iid, void *ppObject)
```

| Parameters | |
|---|---|
| unsigned int iid | Locally-unique id of interface |
| void **ppObject | Address of pointer to queried interface object |

### Return Value
Returns CanvasResult::Success if successful.  Returns CanvasResult::NoInterface if the object does not implemented the queried interface.

### Remarks

