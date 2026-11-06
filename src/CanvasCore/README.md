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

A world object owns a branch of the scene graph and holds the state of what that branch represents: an instance of a model, with its skeleton instances and animation controller, or a branch the app builds itself. The world creates, attaches, removes, and registers its world objects, and instantiating a model into a world creates a world object.

**References.** The world holds a strong reference to each of its world objects, and a world object holds a strong reference to its **root node**. The graph holds the rest of the branch, through each node's references to its children. A world object holds a weak reference to its world, and a root node holds a weak reference back to its world object. The world object that owns any node is found by walking up to the nearest root node.

In this world, a windmill has an arrow stuck in one of its blades, and an elevator carries a car and its driver:

```
(Scene root) <============================ [Root object]
 +-- (Windmill root) <==================== [Windmill]
 |    +-- (Frame)
 |         +-- (Shaft)
 |              +-- (Blade 1)
 |              +-- (Blade 2)
 |                   +-- (Arrow root) <=== [Arrow]
 +-- (Elevator root) <==================== [Elevator]
      +-- (Platform)
           +-- (Car root) <=============== [Car]
                +-- (Seat)
                     +-- (Driver root) <== [Driver]

(node)   a scene graph node; each node holds its children strongly
[name]   a world object; the world holds every world object strongly
<===     the world object holds its root node strongly, and the root
         node refers back to its world object weakly
```

**Branches.** A world object's branch is its root node and the root's descendants, down to the root node of any other world object. Each node belongs to exactly one branch. A world object is **attached to** the world object whose branch holds the parent of its root node, which is its **parent object**. In the example, the windmill's branch runs from `(Windmill root)` through `(Blade 2)`, so the arrow is attached to the windmill. The car is attached to the elevator, the driver to the car, and the windmill and the elevator to the root object. A world object's parent object follows from where its root node is attached, and changes when it is attached elsewhere. Attachment says where a world object is, not what keeps it alive: the world holds every world object, and a world object whose orphan policy is Keep outlives its parent object.

**The root object.** Each world has one **root object**, returned by `XWorld::GetRootObject`. Its root node is the root node of the world's scene graph. Creating a world creates both, and they last as long as the world; the app uses them and never creates or removes them. The root object's branch holds every node attached directly under that root node, and every top-level world object is attached to it. Every other world object therefore has exactly one parent object. The root object stays in its world for the world's lifetime: it is the one world object that has no parent object and is not attached elsewhere, removed, or orphaned. In every other respect it is an ordinary world object.

**Attachment.** Attaching a world object to a node moves its root node there, with its whole branch and every world object attached within it, keeping either its local transform or its world transform, as the caller chooses. The world object is then attached to the world object whose branch holds that node. A world object attaches only to a node outside its own branch and outside the branches of the world objects attached to it, directly or in turn.

**Removal and orphans.** Removing a world object from its world takes its branch out of the graph. Each world object attached to it is **orphaned**, and the engine applies the orphan's **orphan policy**:

| Policy     | Effect                                                                                                                                         |
|------------|------------------------------------------------------------------------------------------------------------------------------------------------|
| **Keep**   | The orphan's root node is attached to the removed object's former parent node, keeping its world transform. The orphan stays in the world.     |
| **Remove** | The orphan is removed too, and the world objects attached to it are orphaned in turn.                                                          |

The engine applies the policies during the removal, depth first, so removing a car that stands on an elevator platform leaves its driver, whose policy is Keep, on the platform. A world object that instantiating a model creates defaults to Remove, so the doors of a removed house and the knobs of those doors go with it. Any other world object defaults to Keep. Each orphan that is kept is reported to the app in the next `XWorld::Update`, which may then change the orphan's state, such as a sleeper whose bed was removed switching from a sleeping animation to an idle one, or remove it. When the world's active camera is in a branch that leaves the world, the world has no active camera until the app sets one, and the next `XWorld::Update` reports it.

**Names.** A world object's name is a label, for logs and debugging, and any number of world objects may share one. A world object that needs to be found by name is registered with its world under a **key**, unique within the world. Registering a key that is already in use fails, and removing a world object from its world releases its key.

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

An app that registers an `XProjectListener` receives the project's world events as callbacks:

| Callback | Delivered when |
|---|---|
| `OnWorldActivated(XWorld *)` | A world becomes the active world |
| `OnWorldDeactivated(XWorld *)` | The active world is replaced |
| `OnWorldRequestFailed(XWorldRequest *)` | A request fails |

```cpp
Gem::Result XProject::RegisterListener(XProjectListener *pListener);
Gem::Result XProject::UnregisterListener(XProjectListener *pListener);
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

Package content becomes engine objects through `XModel`. A bundle that the engine instantiates loads into an `XModel`, which serves as its **template**: the bundle's local assets become the model's node hierarchy and elements, and the shared assets they reference become the GPU resources the model shares across its instances. Each [instance](../CanvasPkg/README.md#instances) of the bundle is a [world object](#worlds-and-world-objects), created by instantiating the model into a world, whose branch is a clone of the template's hierarchy.

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

The node an instance's world object is attached to is its instance transform: a placement's node for a nested instance, and the scene graph's root node for a world bundle or world region, whose world objects are attached to the world's root object. A nested instance's world object is attached within the instance that holds it and has the orphan policy Remove, so removing an instance, such as a region that unloads, removes its nested instances with it.

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
| **Region resident** | A world region's content, loaded while the region's `StreamingBounds` is near a streaming source, as a world object attached to the world's root object |

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
