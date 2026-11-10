# CanvasCore

CanvasCore is the Canvas engine core: [worlds and world objects](#worlds-and-world-objects), the scene graph, models, cameras, lights, UI graphs, and the **project runtime** that opens a [project](../CanvasProject/README.md) and runs its [worlds](#project-worlds). It takes no dependency on a graphics API; a graphics backend is loaded at runtime as a [plugin](#plugin-architecture). Its public interface is `src/Inc/CanvasCore.h`.

This spec is the **Canvas engine spec** that the [CanvasPkg](../CanvasPkg/README.md) and [CanvasProject](../CanvasProject/README.md) specs refer to: it defines how the engine opens a built project, activates its worlds, resolves path references, and instantiates package content.

## Layers

CanvasCore's API has two layers:

| Layer | Holds | Used by |
|---|---|---|
| **World API** | `XWorld`, `XWorldObject`, scene-graph nodes and their elements (`XMeshInstance`, `XMeshField`, `XCamera`, `XLight`, ...), `XModel`, `XUIGraph` | An app that builds and renders its own worlds, such as a model viewer, and the project runtime |
| **Project runtime** | `XProject`, `XWorldRequest`, and the [streaming](#streaming) of package content | An app that runs a project's content |

The project runtime is built on the world API: each world it runs is an `XWorld`, and each instance of package content is a world object. The world API holds no reference to the project runtime. An app may use the world API alone, the project runtime, or both.

## GEM Interface Model

Canvas objects expose functionality through **GEM interfaces**, a COM-inspired system that provides reference counting (`AddRef` / `Release`), interface discovery (`QueryInterface`), and 64-bit interface IDs. Smart pointers (`Gem::TGemPtr<T>`) manage lifetimes automatically.

Interfaces are declared with `GEM_INTERFACE_DECLARE` and mapped in concrete classes via `BEGIN_GEM_INTERFACE_MAP` / `GEM_INTERFACE_ENTRY` macros. This gives Canvas a binary-stable ABI boundary and makes the plugin architecture possible. Every call that can fail returns a `Gem::Result`.

Canvas implements its own interfaces, and an app passes to Canvas only objects that Canvas or its graphics backend created. The interfaces an app implements are named as such, such as the `XLogger` it passes to `CreateCanvas`. A **listener**, which receives the callbacks a Canvas object delivers, is a plain struct of virtual methods that the app implements, such as `WorldListener`, rather than a GEM interface. The object refers to a listener by pointer, without reference counting, from the listener's registration until its unregistration or the object's release, and the app keeps the listener alive for that time.

## Plugin Architecture

Graphics backends are loaded at runtime as shared libraries. The host calls `XCanvas::LoadPlugin(path)`, which loads the library and calls its exported `CreateCanvasPlugin` factory. This keeps CanvasCore free of any graphics-API dependency and leaves room for alternative backends.

## Threads

These rules cover every public CanvasCore interface. A graphics backend states its own rules for its device and resource interfaces.

- **Objects are used from one thread at a time.** A Canvas object may be called from any thread, and the app ensures that no two threads call it concurrently. An interface that may be called concurrently says so: `AddRef`, `Release`, and `QueryInterface` may be called from any thread, concurrently, on every object.
- **Callbacks arrive on a defined thread.** An object that delivers callbacks to the app has a **callback thread**, and delivers every callback on that thread, at a point the interface defines. The calls that decide where callbacks run, such as `XProject::Update` and listener registration, are bound to the callback thread; called from another thread, they change nothing and return `Gem::Result::WrongThread`.
- **Teardown delivers no callbacks.** Releasing the last reference to an object, from any thread, cancels its pending work and delivers no callback. Worker threads the object uses stop before it is destroyed.
- **Worker threads call no app code.** Canvas loads and streams content on its own worker threads, and their results reach the app only through the object that owns them, on that object's callback thread.
- **Log sinks are called from any thread.** The `XLogger` an app passes to `CreateCanvas` may be called from any thread, concurrently. It is the one app-supplied interface Canvas calls from its own threads.

## Worlds and World Objects

A **world** (`XWorld`) is everything that exists in one place, such as a game level, an entire open world with streaming regions, a main menu, or a loading screen. Several worlds may exist and be shown at once, such as a loading screen shown until the next level is ready, or a character preview drawn over a game in progress. Each world holds its own state, such as its active camera and background. Settings that apply to every world, such as display gamma, upscaling, the frame-rate display, and logging, belong to the engine as a whole.

A world holds a scene graph, a set of **world objects** (`XWorldObject`), and the **UI graphs** of its screen-space UI, such as its HUD and menus, drawn over the rendered world. A UI graph shown independently of every world, such as a console, is created on its own.

A world object holds the state of something in its world: an instance of a model, with its skeleton instances and animation controller, or nodes the app builds itself. A world object is anchored at a scene graph node, its **anchor node**, or has no anchor node and no nodes. The world creates, attaches, removes, and registers its world objects, and instantiating a model into a world creates a world object.

**References.** The world holds a strong reference to each of its world objects, and a world object holds a strong reference to its anchor node, if it has one. The graph holds every other node, through each node's references to its children. A world object holds a weak reference to its world, and an anchor node holds a weak reference back to its world object.

In this world, a windmill has an arrow stuck in one of its blades, and an elevator carries a car and its driver:

```
(Scene root)
 +-- (Ground)
 +-- (Windmill) <====================== [Windmill]
 |    +-- (Frame)
 |         +-- (Shaft)
 |              +-- (Blade 1)
 |              +-- (Blade 2)
 |                   +-- (Arrow) <===== [Arrow]
 +-- (Elevator) <====================== [Elevator]
      +-- (Platform)
           +-- (Car) <================= [Car]
                +-- (Seat)
                     +-- (Driver) <==== [Driver]

(node)   a scene graph node; each node holds its children strongly
[name]   a world object; the world holds every world object strongly
<===     the world object holds its anchor node strongly, and the
         anchor node refers back to its world object weakly
```

**World object nodes.** A world object's nodes, its anchor node included, are ordinary scene graph nodes. An anchor node **belongs to** the world object anchored at it, and every other node inherits the world object of its parent. The scene graph's root node belongs to no world object, so a node the app adds under it inherits none. In the example, `(Windmill)` through `(Blade 2)` belong to the windmill, and `(Scene root)` and `(Ground)` belong to no world object.

**Attachment.** A world object is **attached to** the world object that its anchor node's parent belongs to. In the example, the arrow is attached to the windmill, the car to the elevator, and the driver to the car. A world object in the scene graph whose anchor node's parent belongs to no world object, such as the windmill or the elevator, is a **top-level** world object. Which world object a world object is attached to follows from where its anchor node is, and changes when it is attached elsewhere. Attachment says where a world object is, not what keeps it alive: the world holds every world object, and a world object outlives the world object it is attached to unless it belongs to it.

Attaching a world object to a node moves its anchor node there, with the anchor node's subtree, which holds every world object attached to it, directly or in turn, keeping either its local transform or its world transform, as the caller chooses. The world object is then attached to the world object that node belongs to, or is top-level when the node belongs to none. A world object attaches only to a node outside its anchor node's subtree.

**Detached world objects.** A world object is **detached** when it has no anchor node, or when its anchor node is not in its world's scene graph, such as a world object anchored at a node the app has not yet added to the graph. Detaching a world object takes its anchor node's subtree out of the graph and leaves the world object in its world, so that it can be attached again later, as when a pooled projectile waits for its next shot. A detached world object is still in its world: the world holds it, updates it while it is active, and keeps its key. Its world does not draw its nodes, and it is attached to no world object. The world objects attached to it are detached with it, and attaching it to a node in its world's scene graph puts them back with it.

**Active world objects.** Each world object is **active** or **inactive**, and starts active. `XWorld::Update` updates the world's active world objects, in the scene graph or detached, and leaves its inactive ones as they are. `XWorld::DeactivateObject` and `XWorld::ActivateObject` change which world objects are active, such as a pool deactivating the projectiles it holds until their next shot. A change made during `XWorld::Update` takes effect after it.

**Removal and orphans.** Removing a world object from its world takes its anchor node's subtree out of the graph, and removes with it each world object that **belongs to** it. A world object that instantiating a model creates inside the model's instance belongs to the world object it is attached to, so the doors of a removed house and the knobs of those doors are removed with it. Each other world object attached to a removed world object is **orphaned**: its anchor node is attached to the node the removal detached from, the former parent of the node taken out of the graph, keeping its world transform, and it stays in the world. Removing a car that stands on an elevator platform leaves its driver on the platform. The world reports each orphan to its [listeners](#world-callbacks), so the app can change an orphan's state, such as a sleeper whose bed was removed switching from a sleeping animation to an idle one, or remove it. The world's active camera is a camera in its scene graph. `XWorld::Update` clears an active camera that is not, such as one whose node was removed or detached, or destroyed with the world object that held it, and reports the loss to the world's listeners; the world then has no active camera until the app sets one.

**Node operations.** The scene graph's node operations follow the same rules. Moving a node to a new parent moves its subtree with every world object anchored in it, and moving an anchor node attaches its world object there, as attaching the world object does. Removing a node from its parent takes its subtree out of the graph, and the world objects anchored in the subtree that are attached to no other world object in it are orphaned, as for a removed world object. Removing an anchor node from its parent detaches its world object. Removing a node and then adding it elsewhere is a removal followed by an add, so the world objects anchored in its subtree are orphaned by the removal and do not move with it. A world draws the nodes in its scene graph and updates the world objects it holds, wherever their nodes are, so a node moved into another world's scene graph is drawn by that world, and a world object stays in the world that created it.

**Names.** A world object's name is a label, for logs and debugging, and any number of world objects may share one. A world object that needs to be found by name is registered with its world under a **key**, unique within the world. Registering a key that is already in use fails, and removing a world object from its world releases its key.

### World Callbacks

An app that registers a `WorldListener` with a world receives the world's events as callbacks:

| Callback                                      | Delivered when                                                        |
|-----------------------------------------------|-----------------------------------------------------------------------|
| `OnObjectOrphaned(XWorld *, XWorldObject *)`  | A world object is orphaned                                            |
| `OnActiveCameraLost(XWorld *)`                | The world clears an active camera that is not in its scene graph      |

```cpp
Gem::Result XWorld::RegisterListener(WorldListener *pListener);
Gem::Result XWorld::UnregisterListener(WorldListener *pListener);
```

- **Callback thread.** A world that a project creates has the project's [callback thread](#threads). A world that `XCanvas::CreateWorld` creates has the thread that created it as its callback thread. `XWorld::Update`, `RegisterListener`, and `UnregisterListener` are bound to the callback thread.
- **Delivery.** Callbacks are delivered on the world's callback thread, from inside `XWorld::Update`, before it updates the world's world objects, in the order their events happen. `XWorld::Update` checks the active camera before it delivers the callbacks and after each event's callbacks, so a camera that a callback takes out of the scene graph is reported by the same `XWorld::Update`. A call that causes an event, such as `XWorld::RemoveObject`, delivers no callback itself. An orphan that is no longer in the world when its callback is due receives none.
- **Registration.** After `UnregisterListener` returns, including when it is called from inside a callback, the listener receives no further callback. Each callback does nothing by default, so a listener overrides the callbacks it handles.
- **Calls from a callback.** A callback may call any world method except `XWorld::Update`. The events its calls cause are delivered by the same `XWorld::Update`, before it updates the world's world objects.

## Projects

An app opens a **built project**, the form of a project manifest that a project build writes (see CanvasProject's [Building a Project](../CanvasProject/README.md#building-a-project)):

```cpp
Gem::Result XCanvas::OpenProject(XGfxDevice *pDevice, const char *manifestPath, XProject **ppProject);
```

`OpenProject` reads and validates the manifest, and opens every package it lists with `CCpkgReader`, reading each package's header and name directories and no payload. The packages and their [package names](../CanvasProject/README.md#package-names) are then fixed for the project's lifetime. The thread that calls `OpenProject` is the project's [callback thread](#threads).

**Failure.** `OpenProject` succeeds only when the manifest is a valid built project and every package it lists opens. A missing package file, a file that is not a package, or a manifest entry that is not a `package` fails the open. On failure, `OpenProject` returns the failing `Gem::Result`, writes `*ppProject` as null, and logs each problem it found, naming the manifest entry and file.

**Startup.** When the project declares a `defaultWorld`, `OpenProject` requests it, as [`RequestWorld`](#requesting-a-world) would, and `XProject::GetWorldRequest` returns that request, so the app can show its progress. A project without a `defaultWorld` opens with no world requested, and its first world is the one the app requests.

**Update.** The app calls `XProject::Update` once per frame on the callback thread. Every change the app can observe in the project happens inside `Update`: a request's status, the active world, instances that finished loading, and the callbacks that report them. Between two calls to `Update`, everything the app reads from the project is stable.

```cpp
Gem::Result XProject::Update(float dtime);
```

## Project Worlds

A project offers the worlds its manifest lists in `worlds`. Each entry is a **world definition**: one [world bundle](../CanvasPkg/README.md#worlds), its always-present content, and its [world regions](../CanvasPkg/README.md#world-regions). Loading a world definition creates an `XWorld`, and the project keeps it supplied while it is loaded, adding and removing the world objects of its regions as they stream.

- **One world is active.** From the first activation on, a project has exactly one **active world**: the world its latest successful request activated. Other worlds may be loaded at the same time, such as the next level loading while a loading screen is active.
- **Menus and HUDs are UI graphs.** A world's menus and HUD are its [UI graphs](#worlds-and-world-objects), drawn over the rendered world. A startup screen is a world whose content is mostly UI, and an in-game options screen can be a UI graph of the current world.
- `XProject::GetActiveWorld` returns the active world, or null before the first activation.

### Requesting a World

Every change of the active world is a request:

```cpp
Gem::Result XProject::RequestWorld(const char *worldPath, XWorldRequest **ppRequest);
```

`worldPath` is one of the project's `worlds` entries, written `package/bundle` with the package's project name. `RequestWorld` returns an error for a request it can reject at once, such as a world the project does not list, and otherwise returns a request that the engine carries out over later calls to `Update`: it loads the world, and activates it once it is loaded. The app requests worlds today; content that requests worlds, such as a menu button or a portal, uses the same operation. `RequestWorld` may be called from any thread, under the [thread rules](#threads).

- **One request is outstanding.** A new request supersedes the outstanding one, which then reports `Superseded`.
- **A failed request leaves the active world active.** A world that fails to load, such as one with a path reference that does not resolve, is reported as failed, and the active world is unchanged.

An `XWorldRequest` reports its progress. Its status changes only inside `Update`:

| Status | Meaning |
|---|---|
| `Pending` | Requested, not yet loading |
| `Loading` | Loading; `GetProgress` returns the fraction loaded |
| `Activated` | The world is the active world |
| `Failed` | The world did not load; `GetResult` returns the cause, and the log holds the details |
| `Superseded` | A later request replaced this one before it was activated |

### World Events

An app that registers a `ProjectListener` receives the project's world events as callbacks:

| Callback | Delivered when |
|---|---|
| `OnWorldActivated(XWorld *)` | A world becomes the active world |
| `OnWorldDeactivated(XWorld *)` | The active world is replaced |
| `OnWorldRequestFailed(XWorldRequest *)` | A request fails |

```cpp
Gem::Result XProject::RegisterListener(ProjectListener *pListener);
Gem::Result XProject::UnregisterListener(ProjectListener *pListener);
```

- **Delivery.** Callbacks are delivered on the project's callback thread, from inside `Update`, in the order their events happen. A call that causes an event, such as `RequestWorld`, delivers no callback itself.
- **Registration.** `RegisterListener` and `UnregisterListener` are called on the callback thread. After `UnregisterListener` returns, including when it is called from inside a callback, the listener receives no further callback.
- **Calls from a callback.** A callback may call any project method except `Update`. A world requested from inside a callback is carried out by later calls to `Update`.

A listener is optional: polling an `XWorldRequest` and calling `GetActiveWorld` observe the same changes, since both change only inside `Update`.

## Path Resolution

The engine resolves each [path reference](../CanvasPkg/README.md#paths) in a package through the project: the path's package name resolves by CanvasProject's [package-name rules](../CanvasProject/README.md#package-names), its bundle and asset names resolve through that package's name directories, and the asset it names has the `AssetType` the path's field declares. [Export names](../CanvasPkg/README.md#exports-and-imports) resolve within the placed bundle's `ExportList`.

A path that does not resolve, or resolves to an asset of another type, fails the load of the content that holds it: a world whose content holds one fails its request. A project build validates every path, so such a path means the project's package files differ from the ones the build validated together.

**Bone names** resolve when the engine loads the asset that holds them, within the skeleton that asset references (see CanvasPkg's [Package Data Types](../CanvasPkg/README.md#package-data-types)): a skin's bones and a socket's parent bone become bone indices of that skeleton, as does each bone track's bone. A skin or socket whose bone name does not resolve fails the load of the content that holds it, as a path does. A bone track whose bone name does not resolve is left out of its animation, and the load logs it, so the animation plays on the bones it does resolve. As with paths, a project build validates every bone name.

**Shared assets are loaded once per project.** A shared asset that several instances or worlds use, such as a texture, is loaded once and kept while any loaded content uses it.

## Instantiation

Package content becomes engine objects through `XModel`. A bundle that the engine instantiates loads into an `XModel`, which serves as its **template**: the bundle's local assets become the model's node hierarchy and elements, and the shared assets they reference become the GPU resources the model shares across its instances. Each [instance](../CanvasPkg/README.md#instances) of the bundle is a [world object](#worlds-and-world-objects), created by instantiating the model into a world. The world object's nodes are a clone of the template's hierarchy.

| Package asset | Engine object |
|---|---|
| `TransformNode` | `XSceneGraphNode` |
| `MeshInstance` | `XMeshInstance` on its node, drawing the `MeshData`'s `XGfxMeshData` |
| `Camera`, `Light` | `XCamera`, `XLight` on their nodes |
| `Material`, `Image` | `XGfxMaterial`, `XGfxSurface` |
| `MeshField` | `XMeshField` on its node, drawing the `MeshData`'s `XGfxMeshData` at each of its transforms |
| `Placement` | A world object, a nested instance of the placed bundle's model, attached to the placement's node |
| `SkeletonInstance`, with its `Skeleton` | A model skeleton, and in each instance an `XSkeletonInstance` on its node that holds the instance's pose |
| A socket: a `TransformNode` whose parent is a bone | A model socket: an `XSceneGraphNode` that follows its bone |
| `MeshSkin` | The skin binding of each `XMeshInstance` that draws with it |
| `AnimationClip`, with the `SkeletalAnimation` assets it binds | An animation clip, played through each instance's `XAnimationController` |
| `WorldSettings` | The world's settings: its background and skyboxes, and its initial active camera |

An instance's world object is anchored at a node whose local transform is the instance's instance transform, and the bundle's nodes and attachments that are relative to the instance transform are on that anchor node. A nested instance's anchor node is a child of its placement's node, with the placement's `Transform`, so its world object is attached to the instance that holds the placement. It belongs to that instance, so removing an instance, such as a region that unloads, removes its nested instances with it. A world bundle's or world region's anchor node is a child of the scene graph's root node, with the identity transform, so its world object is top-level.

`XModel` covers a bundle's node hierarchy, meshes, materials, textures, cameras, lights, skeletons with their sockets and skins, and animation clips. A skeleton's bones are not scene-graph nodes: each instance's `XSkeletonInstance` holds their pose. A bundle's other structure extends it:

- **Placements**: nested instances, instantiated with the instance that holds them, each as its own world object.
- **Exports and import nodes**: an import node resolves to the exported node of its placement's nested instance.
- **Mesh fields**: an `XMeshField` groups its transforms into spatial clusters, each with its own bounds, so the world culls a field cluster by cluster and the renderer draws the visible clusters with GPU instancing. Entries beyond the field's cull distance from the camera are not drawn.
- **Mobility**: each node's effective [mobility](../CanvasPkg/README.md#mobility), which the world uses to treat static content as fixed.
- **Shared assets across bundles**: an `XModel` holds the GPU resources of its own bundle, where a shared asset may be referenced from any bundle of any package in the project.

**Runtime instances.** Content requested at runtime, such as a spawned character, is an instance of a bundle with an instance transform and a mobility that the requester supplies. The interface that requests one is defined with the [streaming](#streaming) model, since its bundle may need loading first.

## Streaming

A project's content moves through these states:

| State | Holds |
|---|---|
| **Project open** | Every package's header and name directories |
| **World loaded** | A world bundle's always-present content, and the shared assets it uses |
| **World active** | The loaded world that is the project's active world |
| **Region resident** | A world region's content, loaded while the region's `StreamingBounds` is near a streaming source, as a top-level world object |

Loading runs on worker threads, and its results are applied inside `Update`. Which worlds stay loaded when they are not active, what a streaming source is, the distances at which regions load and unload, and how content requested at runtime is loaded, are open; they are the next part of this spec to define.

## Source Layout

| Path | Description |
|---|---|
| `src/CanvasCore/README.md` | This spec |
| `src/Inc/CanvasCore.h` | Public header |
| `src/CanvasCore/*.cpp`, `src/CanvasCore/*.h` | Implementation and internal headers |

## Future Directions

Directions the current design leaves room for. None are committed plans:

- **Input**: engine-side input that maps devices to named actions through switchable contexts, with each world able to select its context, and app or content code giving actions their meaning.
- **Overlay content**: UI content types in packages, so a world declares its overlays, and an interface for pushing an overlay over the active world.
- **Content-driven requests**: content that requests worlds and instances, such as a menu button bound to a world or a script on a portal, through the same operations the app uses.
