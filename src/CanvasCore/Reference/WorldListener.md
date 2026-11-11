# WorldListener

The callbacks a world delivers to the app. `WorldListener` is a plain struct, not a GEM interface: the app derives from it, overrides the callbacks it handles, and registers it with [`XWorld::RegisterListener`](XWorld.md#registerlistener-unregisterlistener). Each callback does nothing by default. The world refers to the listener by pointer, without holding it, until `UnregisterListener` returns or the world is released, and the app keeps the listener alive for that time.

```cpp
struct WorldListener
{
    virtual void OnObjectOrphaned(XWorld *pWorld, XWorldObject *pObject) {}
    virtual void OnActiveCameraLost(XWorld *pWorld) {}
protected:
    ~WorldListener() = default;
};
```

The world queues its events and delivers their callbacks from inside [`XWorld::Update`](XWorld.md#update), on the world's callback thread, before it updates its world objects, in the order the events happened. The [World Callbacks](../README.md#world-callbacks) section of the spec gives the delivery rules, including what a callback may call.

## Callbacks

### OnObjectOrphaned

```cpp
void OnObjectOrphaned(XWorld *pWorld, XWorldObject *pObject);
```

`pObject` was orphaned: the world object whose node it was attached to was released, and `pObject` was attached to the node the release detached from, keeping its world transform. `pObject` is still in `pWorld`. An orphan released before its callback is due receives none.

### OnActiveCameraLost

```cpp
void OnActiveCameraLost(XWorld *pWorld);
```

`pWorld`'s active camera was not in its scene graph, and `pWorld` cleared it. `pWorld` has no active camera, and renders only its background, until the app sets one.
