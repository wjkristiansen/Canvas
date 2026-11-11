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

### CreateWorldObject

```cpp
Gem::Result CreateWorldObject(XSceneNode *pAnchorNode, XWorldObject **ppObject, const char *name = nullptr);
```

Creates a world object anchored at `pAnchorNode`, and holds it. A node in this world's scene graph gives a world object in the scene graph, any other node gives a [detached](../README.md#worlds-and-world-objects) world object, and `nullptr` gives a world object with no anchor node. The world object starts active. `name` is its [name](../README.md#worlds-and-world-objects), for logs and debugging. `*ppObject` receives the world object.

| Result        | When                                                                  |
|---------------|-----------------------------------------------------------------------|
| `BadPointer`  | `ppObject` is `nullptr`                                               |
| `InvalidArg`  | `pAnchorNode` is already an anchor node, or is this world's root node |
| `OutOfMemory` | The world object cannot be created                                    |

### ActivateObject, DeactivateObject

```cpp
Gem::Result ActivateObject(XWorldObject *pObject);
Gem::Result DeactivateObject(XWorldObject *pObject);
```

Make `pObject` active or inactive. `Update` updates the active world objects and leaves the inactive ones as they are. Activating an active world object, or deactivating an inactive one, changes nothing. A change made during `Update` takes effect after it.

| Result     | When                                             |
|------------|--------------------------------------------------|
| `NotFound` | `pObject` is not one of this world's world objects |

### AttachObject

```cpp
Gem::Result AttachObject(XWorldObject *pObject, XSceneNode *pParent, bool keepWorldTransform);
```

Attaches `pObject` by moving its anchor node, with the anchor node's subtree, to be the last child of `pParent`, in one step. When `keepWorldTransform` is true, the anchor node's local transform changes so that its global matrix stays the same, except for any shear, which a `Transform` cannot hold; otherwise its local transform stays the same. The world objects attached to `pObject` move with it. Activation is unchanged.

| Result       | When                                                                                         |
|--------------|----------------------------------------------------------------------------------------------|
| `NotFound`   | `pObject` is not one of this world's world objects                                           |
| `InvalidArg` | `pObject` has no anchor node, `pParent` is `nullptr`, or `pParent` is in the anchor node's subtree |

### DetachObject

```cpp
Gem::Result DetachObject(XWorldObject *pObject);
```

Detaches `pObject` by taking its anchor node, with the anchor node's subtree, out of its parent, keeping its local transform. The world objects attached to `pObject` are detached with it. `pObject` stays in this world, and its activation is unchanged. Detaching a detached world object changes nothing.

| Result     | When                                             |
|------------|--------------------------------------------------|
| `NotFound` | `pObject` is not one of this world's world objects |

### RemoveObject

```cpp
Gem::Result RemoveObject(XWorldObject *pObject);
```

Removes `pObject` from this world, taking its anchor node's subtree out of the graph. Each world object attached to `pObject` is [orphaned](../README.md#worlds-and-world-objects): its anchor node is attached to the node the removal detached from, keeping its world transform, and its listeners receive `OnObjectOrphaned` at the next `Update`. The world releases `pObject` and its unique name. A world object that the app still holds keeps its anchor node, and its `GetWorld` returns `nullptr`.

| Result     | When                                             |
|------------|--------------------------------------------------|
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
2. Updates the active world objects, in the scene graph or detached: the scene graph from the root node, then the nodes of each active detached world object.
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
