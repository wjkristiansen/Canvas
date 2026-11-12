# CanvasCore

CanvasCore is the Canvas engine core: [worlds and world objects](#worlds-and-world-objects), the scene graph, models, cameras, lights, UI graphs, and the **project runtime** that opens a [project](../CanvasProject/README.md) and runs its [worlds](#project-worlds). It takes no dependency on a graphics API; a graphics backend is loaded at runtime as a [plugin](#plugin-architecture). Its public interface is `src/Inc/CanvasCore.h`.

This spec is the **Canvas engine spec** that the [CanvasPkg](../CanvasPkg/README.md) and [CanvasProject](../CanvasProject/README.md) specs refer to: it defines how the engine opens a built project, activates its worlds, resolves path references, and instantiates package content.

## Layers

CanvasCore's API has two layers:

| Layer | Holds | Used by |
|---|---|---|
| **World API** | `XWorld`, `XWorldObject`, scene nodes and their elements (`XMeshInstance`, `XMeshField`, `XCamera`, `XLight`, ...), `XModel`, `XUIGraph` | An app that builds and renders its own worlds, such as a model viewer, and the project runtime |
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

A world object holds the state of something in a world: an instance of a model, with its nodes, skeleton instances, and animation controller, nodes the app builds itself, or state with no nodes at all, such as a computer opponent in a strategy game. A world object has a scene node as its **root node**, the top of the nodes it is made of, or has no root node and no nodes. `XCanvas::CreateWorldObject` creates a world object in no world: an [instance](#instantiation) of a model, whose root node is the instance root, or an empty world object, which the app gives a root node with `XWorldObject::SetRootNode`. `XWorld::AddWorldObject` adds a world object to a world, and `XWorld::ReleaseWorldObject` takes it out. A world object is in one world at most.

**References.** A world holds a strong reference to each of its world objects, and a world object holds a strong reference to its root node, if it has one. The graph holds every other node, through each node's references to its children. A world object holds a weak reference to the world it is in, and a root node holds a weak reference back to its world object.

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

(node)   a scene node; each node holds its children strongly
[name]   a world object; the world holds every world object strongly
<===     the world object holds its root node strongly, and the
         root node refers back to its world object weakly
```

**World object nodes.** A world object's nodes, its root node included, are ordinary scene nodes. A world object's root node **belongs to** it, and every other node inherits the world object of its parent. The world's root node belongs to no world object, so a node the app adds under it inherits none. In the example, `(Windmill)` through `(Blade 2)` belong to the windmill, and `(Scene root)` and `(Ground)` belong to no world object.

**Placement.** A world object in a world is **placed** when its root node is in its world's scene graph. It is placed by **attaching** it to a node, with `XWorld::PlaceObject`: its root node, with the root node's subtree, becomes the last child of that node, keeping either its local transform or its world transform, as the caller chooses. The world object is then **attached to** that node. In the example, the arrow is attached to `(Blade 2)`, the car to `(Platform)`, and the driver to `(Seat)`. A world object attached to a node that belongs to no world object, such as the windmill or the elevator, is a **top-level** world object. A world object is attached only to a node outside its root node's subtree, and the world objects attached to its nodes, directly or in turn, move with it. Detaching a world object, with `XWorld::DetachObject`, takes its root node's subtree off the node it is attached to and out of the graph, with the world objects attached to its nodes, so that it can be placed again later, as when a pooled projectile waits for its next shot. A world object is not placed when it has no root node, such as a computer opponent in a strategy game, or when its root node is not in its world's scene graph, such as an instance the app has added to the world and not yet placed, or one that is detached. Every world object in a world, placed or not, is held by the world, updated while it is active, and keeps its unique name, and the world draws no nodes of a world object that is not placed. Attachment also says how long a world object stays in its world: releasing a world object releases the world objects attached to its nodes, unless the app moves them first (see Release below).

**Root nodes.** `XWorldObject::SetRootNode` gives a world object a root node, replaces its root node, or clears it. `SetRootNode` accepts only a node that has no parent and is no other world object's root node. When the world object is placed, the new root node takes the old one's place among its siblings, and the old root node leaves the scene graph with its subtree, so an app that keeps several trees for one world object, such as a standing and a fallen statue, can swap them. An animation controller is held by the root node of the tree it was created for, so a tree keeps its animation when it is swapped out, and its animation pauses until it is swapped back in.

**Active world objects.** Each world object is **active** or **inactive**, and starts active. `XWorld::Update` updates the world's active world objects, placed or not, and leaves its inactive ones as they are. Updating a world object updates each of its nodes: the node's animation controller, if it holds one, then the elements bound to the node, such as its skeleton instances, which move their sockets. A world object attached to a node of an inactive world object is updated when it is active itself. The elements bound to nodes that belong to no world object are not updated. `XWorld::DeactivateObject` and `XWorld::ActivateObject` change which world objects are active, such as a pool deactivating the projectiles it holds until their next shot. A change made from a [callback](#world-callbacks) applies to the `XWorld::Update` that delivers the callback, which updates the world objects after it delivers its callbacks.

**Release.** Releasing a world object, with `XWorld::ReleaseWorldObject`, takes its root node's subtree out of the graph and takes the world object out of its world, which releases its reference to it. A world object the app still holds keeps its root node, and can be added to a world again, such as the world of the next level. Each world object attached to a node of a released world object is **due for release**: it is attached to the node the release detached from, the former parent of the node taken out of the graph, keeping its world transform, and it stays in the world until the next `XWorld::Update`. The world reports each world object due for release to its [listeners](#world-callbacks), and after its callbacks releases it, which makes the world objects attached to its nodes due for release in turn, so a whole tree of attached world objects is released by one `XWorld::Update`. A world object that is moved or detached before then, with `XWorld::PlaceObject`, `XWorld::DetachObject`, or a node operation that moves its root node, stays in the world. Releasing an elevator releases the car on its platform and the driver in the car. An app that moves the car out first, such as onto the floor the elevator stopped at, keeps the car, with the driver in it. Releasing the world object of a world region releases the content attached to its nodes the same way. The world's active camera is a camera in its scene graph. `XWorld::Update` clears an active camera that is not, such as one whose node was taken out of the graph, or destroyed with the world object that held it, and reports the loss to the world's listeners; the world then has no active camera until the app sets one.

**Node operations.** The scene graph's node operations move nodes with their subtrees, and the world objects whose root nodes are in a subtree move with it. Adding or inserting a node that has a parent moves it in one step, whether to a new parent or among its siblings, and a root node moved to a new parent attaches its world object to that parent, as placing the world object does. A node is never added to its own subtree. Removing a node from its parent takes its subtree out of the graph, and the world objects whose root nodes are in the subtree are detached, as when a world object is detached, so a node removed and then added elsewhere moves with them. A node with no parent has its local transform as its world transform. A world draws the nodes in its scene graph and updates the world objects it holds, wherever their nodes are, so a node moved into another world's scene graph is drawn by that world, and a world object stays in its world until it is released.

**Names.** A world object's name, like that of any Canvas element, is for logs and debugging, and any number of world objects may share one. Nodes have names too, usually unique within a world object: `XWorldObject::FindNode` finds the first of a world object's nodes with a name, and `XSceneNode::FindChild` finds a node's first child with a name. A world object that needs to be found by name has a **unique name**, which no other world object in its world has. `XWorld::SetUniqueName` gives a world object in the world a unique name, or clears it, and `XWorld::FindObject` finds the world object that has a unique name. Giving a world object a unique name that another world object has fails. A world object keeps its unique name whether or not it is placed, and releasing the world object, or releasing the world, releases it.

### World Callbacks

An app that registers a `WorldListener` with a world receives the world's events as callbacks:

| Callback                                      | Delivered when                                                        |
|-----------------------------------------------|-----------------------------------------------------------------------|
| `OnParentReleased(XWorld *, XWorldObject *)`  | A world object is due for release                                     |
| `OnActiveCameraLost(XWorld *)`                | The world clears an active camera that is not in its scene graph      |

```cpp
Gem::Result XWorld::RegisterListener(WorldListener *pListener);
Gem::Result XWorld::UnregisterListener(WorldListener *pListener);
```

- **Callback thread.** A world that a project creates has the project's [callback thread](#threads). A world that `XCanvas::CreateWorld` creates has the thread that created it as its callback thread. `XWorld::Update`, `RegisterListener`, and `UnregisterListener` are bound to the callback thread.
- **Delivery.** Callbacks are delivered on the world's callback thread, from inside `XWorld::Update`, before it updates the world's world objects, in the order their events happen. `XWorld::Update` checks the active camera before it delivers the callbacks and after each event's callbacks, so a camera that a callback takes out of the scene graph is reported by the same `XWorld::Update`. A call that causes an event, such as `XWorld::ReleaseWorldObject`, delivers no callback itself. A world object due for release that is no longer in the world when its callback is due receives none.
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
| `TransformNode` | `XSceneNode` |
| `MeshInstance` | `XMeshInstance` on its node, drawing the `MeshData`'s `XGfxMeshData` |
| `Camera`, `Light` | `XCamera`, `XLight` on their nodes |
| `Material`, `Image` | `XGfxMaterial`, `XGfxSurface` |
| `MeshField` | `XMeshField` on its node, drawing the `MeshData`'s `XGfxMeshData` at each of its transforms |
| `Placement` | A world object, a nested instance of the placed bundle's model, attached to the placement's node |
| `SkeletonInstance`, with its `Skeleton` | A model skeleton, and in each instance an `XSkeletonInstance` on its node that holds the instance's pose |
| A socket: a `TransformNode` whose parent is a bone | A model socket: an `XSceneNode` that follows its bone |
| `MeshSkin` | The skin binding of each `XMeshInstance` that draws with it |
| `AnimationClip`, with the `SkeletalAnimation` assets it binds | An animation clip, played through each instance's `XAnimationController` |
| `WorldSettings` | The world's settings: its background and skyboxes, and its initial active camera |

`XCanvas::CreateWorldObject` instantiates the model it is given, creating the instance's world object, whose root node is a new **instance root** node with no parent. The instance root holds the instance's animation controller when the model has animation clips. The app adds the world object to a world and places it there. Instantiating leaves every world's active camera unchanged.

An instance root's local transform is the instance's instance transform, and the bundle's nodes and attachments that are relative to the instance transform are on the instance root. A nested instance's world object is attached to its placement's node, with the placement's `Transform`. A world bundle's or world region's root node is a child of the world's root node, with the identity transform, so its world object is top-level.

`XModel` covers a bundle's node hierarchy, meshes, materials, textures, cameras, lights, skeletons with their sockets and skins, and animation clips. A skeleton's bones are not scene nodes: each instance's `XSkeletonInstance` holds their pose. A bundle's other structure extends it:

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

## API Reference

The [API reference](Reference/README.md) describes each public interface and function: what each method does and the results it returns.

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
