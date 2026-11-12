# WorldListener

The callbacks a world delivers to the app. `WorldListener` is a plain struct, not a GEM interface: the app derives from it, overrides the callbacks it handles, and registers it with [`XWorld::RegisterListener`](XWorld.md#registerlistener-unregisterlistener). Each callback does nothing by default. The world refers to the listener by pointer, without holding it, until `UnregisterListener` returns or the world is released, and the app keeps the listener alive for that time.

```cpp
struct WorldListener
{
    virtual void OnParentReleased(XWorld *pWorld, XWorldObject *pObject) {}
    virtual void OnActiveCameraLost(XWorld *pWorld) {}
protected:
    ~WorldListener() = default;
};
```

The world queues its events and delivers their callbacks from inside [`XWorld::Update`](XWorld.md#update), on the world's callback thread, before it updates its world objects, in the order the events happened. The [World Callbacks](../README.md#world-callbacks) section of the spec gives the delivery rules, including what a callback may call.

## Callbacks

### OnParentReleased

```cpp
void OnParentReleased(XWorld *pWorld, XWorldObject *pObject);
```

The world object whose node `pObject` was attached to was released, and `pObject` is [due for release](../README.md#worlds-and-world-objects): it is attached to the node the release detached from, keeping its world transform, and is still in `pWorld`. After the callbacks for this event, `pWorld` releases `pObject` unless it was moved or detached, with [`XWorld::PlaceObject`](XWorld.md#placeobject), [`XWorld::DetachObject`](XWorld.md#detachobject), or a node operation that moves its root node. A callback that places `pObject` elsewhere keeps it, and the world objects attached to its nodes stay with it and receive no callback. A world object released before its callback is due receives none.

### OnActiveCameraLost

```cpp
void OnActiveCameraLost(XWorld *pWorld);
```

`pWorld`'s active camera was not in its scene graph, and `pWorld` cleared it. `pWorld` has no active camera, and renders only its background, until the app sets one.
