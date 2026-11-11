# XWorld

A world: a scene graph, the [world objects](../README.md#worlds-and-world-objects) in it, and the world-wide state they share: the active camera, the background, and the world's UI graphs. A world holds its root node, its world objects, its UI graphs, and its active camera, and refers to its listeners by pointer.

Inherits `XCanvasElement`. Created by `XCanvas::CreateWorld`, which binds the world to a graphics device.

## Scene Graph

### GetRootNode

```cpp
XSceneNode *GetRootNode();
```

Returns a weak pointer to the world's root node. The world's scene graph is the root node and its subtree, and the root node belongs to no world object.

### GetDevice

```cpp
XGfxDevice *GetDevice();
```

Returns a weak pointer to the graphics device the world was created with.

## World Objects

### AddWorldObject

```cpp
Gem::Result AddWorldObject(XWorldObject *pObject);
```

Adds `pObject`, which [`XCanvas::CreateWorldObject`](XCanvas.md#createworldobject) created or a world released, to this world, and holds it. The world object starts active. Its place in the scene graph is unchanged: a world object that has no root node, or whose root node is not in this world's scene graph, is not [placed](../README.md#worlds-and-world-objects).

| Result          | When                                                                                                   |
|-----------------|--------------------------------------------------------------------------------------------------------|
| `BadPointer`    | `pObject` is `nullptr`                                                                                 |
| `AlreadyExists` | `pObject` is in a world, this one or another                                                           |
| `InvalidArg`    | `pObject` was created by another canvas, is an instance of a model whose GPU resources are on another device than this world's, or has this world's root node as its root node |
| `OutOfMemory`   | The world object cannot be added                                                                       |

### ActivateObject, DeactivateObject

```cpp
Gem::Result ActivateObject(XWorldObject *pObject);
Gem::Result DeactivateObject(XWorldObject *pObject);
```

Make `pObject` active or inactive. `Update` updates the active world objects and leaves the inactive ones as they are. Activating an active world object, or deactivating an inactive one, changes nothing. A change made from a [callback](WorldListener.md) applies to the `Update` that delivers the callback, which updates its world objects after it delivers its callbacks.

| Result     | When                                               |
|------------|----------------------------------------------------|
| `NotFound` | `pObject` is not one of this world's world objects |

### PlaceObject

```cpp
Gem::Result PlaceObject(XWorldObject *pObject, XSceneNode *pParent, bool keepWorldTransform);
```

Places `pObject` by [attaching](../README.md#worlds-and-world-objects) it to `pParent`: its root node, with the root node's subtree, becomes the last child of `pParent`, in one step. When `keepWorldTransform` is true, the root node's local transform changes so that its global matrix stays the same, except for any shear, which a `Transform` cannot hold; otherwise its local transform stays the same. The world objects attached to `pObject`'s nodes move with it. Activation is unchanged.

| Result       | When                                                                                             |
|--------------|--------------------------------------------------------------------------------------------------|
| `NotFound`   | `pObject` is not one of this world's world objects                                               |
| `InvalidArg` | `pObject` has no root node, `pParent` is `nullptr`, or `pParent` is in the root node's subtree   |

### DetachObject

```cpp
Gem::Result DetachObject(XWorldObject *pObject);
```

Detaches `pObject` by taking its root node, with the root node's subtree, out of its parent, keeping its local transform. The world objects attached to `pObject`'s nodes are detached with it. `pObject` stays in this world, and its activation is unchanged. Detaching a world object that is not placed changes nothing.

| Result     | When                                               |
|------------|----------------------------------------------------|
| `NotFound` | `pObject` is not one of this world's world objects |

### ReleaseWorldObject

```cpp
Gem::Result ReleaseWorldObject(XWorldObject *pObject);
```

Takes `pObject` out of this world. Its root node's subtree leaves the graph, and each world object attached to one of `pObject`'s nodes is [orphaned](../README.md#worlds-and-world-objects): it is attached to the node the release detached from, keeping its world transform, and its listeners receive `OnObjectOrphaned` at the next `Update`. The world releases `pObject`'s unique name, then `pObject` itself, which ends it unless the app holds it. A world object the app still holds keeps its root node, its `GetWorld` returns `nullptr`, and it can be added to a world again.

| Result     | When                                               |
|------------|----------------------------------------------------|
| `NotFound` | `pObject` is not one of this world's world objects |

### SetUniqueName

```cpp
Gem::Result SetUniqueName(XWorldObject *pObject, const char *uniqueName);
```

Gives `pObject` the [unique name](../README.md#worlds-and-world-objects) `uniqueName`, which no other world object in this world has, and releases its previous unique name. `nullptr` clears its unique name. Unique names are compared exactly, so they are case-sensitive. Giving `pObject` the unique name it has changes nothing.

| Result          | When                                                     |
|-----------------|----------------------------------------------------------|
| `NotFound`      | `pObject` is not one of this world's world objects       |
| `InvalidArg`    | `uniqueName` is empty                                    |
| `AlreadyExists` | Another world object in this world has `uniqueName`      |
| `OutOfMemory`   | The unique name cannot be stored; `pObject` is unchanged |

### FindObject

```cpp
XWorldObject *FindObject(const char *uniqueName);
```

Returns a weak pointer to the world object in this world whose unique name is `uniqueName`, or `nullptr` when there is none or `uniqueName` is `nullptr`.

## Listeners

### RegisterListener, UnregisterListener

```cpp
Gem::Result RegisterListener(WorldListener *pListener);
Gem::Result UnregisterListener(WorldListener *pListener);
```

Register and unregister a [`WorldListener`](WorldListener.md), which receives this world's callbacks. The world refers to `pListener` by pointer, without holding it, until `UnregisterListener` returns or the world is released. After `UnregisterListener` returns, including when it is called from a callback, `pListener` receives no further callback.

| Result          | When                                                       |
|-----------------|------------------------------------------------------------|
| `InvalidArg`    | `RegisterListener`: `pListener` is `nullptr`               |
| `AlreadyExists` | `RegisterListener`: `pListener` is already registered      |
| `OutOfMemory`   | `RegisterListener`: the listener cannot be added           |
| `NotFound`      | `UnregisterListener`: `pListener` is not registered        |

## Camera and Background

### SetActiveCamera, GetActiveCamera

```cpp
void SetActiveCamera(XCamera *pCamera);
XCamera *GetActiveCamera() const;
```

`SetActiveCamera` sets the camera this world renders from, and holds it, or clears it when `pCamera` is `nullptr`. `GetActiveCamera` returns a weak pointer to the active camera, or `nullptr` when there is none. The active camera is a camera in this world's scene graph: `Update` clears an active camera whose node is not this world's root node or a node in its subtree, and its listeners receive `OnActiveCameraLost`.

### SetBackground, GetBackground

```cpp
void SetBackground(const GfxBackgroundDesc *pDesc);
const GfxBackgroundDesc *GetBackground() const;
```

`SetBackground` copies `*pDesc` as the world's background, which fills the pixels that no geometry covers, and holds the skybox cubemaps it refers to. `nullptr` sets the default background, opaque black with no skybox. `GetBackground` returns the world's background, never `nullptr`.

## UI Graphs

### CreateUIGraph, RemoveUIGraph

```cpp
Gem::Result CreateUIGraph(XUIGraph **ppGraph);
Gem::Result RemoveUIGraph(XUIGraph *pGraph);
```

`CreateUIGraph` creates a UI graph for the world's screen-space UI, such as its HUD and menus, and holds it until `RemoveUIGraph` or the world's release. `*ppGraph` receives the UI graph. The world updates and submits its UI graphs after its scene content, in the order they were created, so each draws over the world and over the UI graphs created before it. A UI graph shown independently of every world is created with `XCanvas::CreateUIGraph`.

| Result        | When                                                     |
|---------------|----------------------------------------------------------|
| `BadPointer`  | `CreateUIGraph`: `ppGraph` is `nullptr`                  |
| `OutOfMemory` | `CreateUIGraph`: the UI graph cannot be created          |
| `NotFound`    | `RemoveUIGraph`: `pGraph` is not one of this world's UI graphs |

## Frame

### Update

```cpp
Gem::Result Update(float dtime);
```

Advances the world by `dtime` seconds:

1. Delivers the callbacks for the events since the previous `Update` to the world's [listeners](WorldListener.md), checking the active camera before the callbacks and after each event's callbacks.
2. Updates each active world object, placed or not, by updating each of its nodes: the node's [animation controller](XAnimationController.md), if it holds one, then the elements bound to the node, such as its skeleton instances, which move their sockets. The elements bound to nodes that belong to no world object are not updated.
3. Updates the world's UI graphs.

Returns the first failure, and updates nothing after it.

| Result        | When                                    |
|---------------|-----------------------------------------|
| `Unavailable` | `Update` is called from a callback      |

### SubmitRenderables

```cpp
Gem::Result SubmitRenderables(XGfxRenderQueue *pRenderQueue);
```

Submits the world to `pRenderQueue`: its active camera and background, the lights and renderable nodes of its scene graph that the active camera can see, then its UI graphs. Returns the first failure from `pRenderQueue`.

### BuildBVH

```cpp
void BuildBVH();
```

Builds the world's bounding volume hierarchies over the renderable elements and lights in its scene graph, which `SubmitRenderables` uses to find what the active camera can see, discarding the previous ones. `SubmitRenderables` builds them when they have not been built, so calling `BuildBVH` moves that cost to a time the app chooses. A call after the scene graph changes rebuilds them for its new contents.
