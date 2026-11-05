# CanvasCore

CanvasCore is the Canvas engine core: the scene graph, models, cameras, lights, UI graph, and the **project runtime** that opens a [project](../CanvasProject/README.md) and runs its [worlds](#worlds). It takes no dependency on a graphics API; a graphics backend is loaded at runtime as a [plugin](#plugin-architecture). Its public interface is `src/Inc/CanvasCore.h`.

This spec is the **Canvas engine spec** that the [CanvasPkg](../CanvasPkg/README.md) and [CanvasProject](../CanvasProject/README.md) specs refer to: it defines how the engine opens a built project, activates its worlds, resolves path references, and instantiates package content.

## Layers

CanvasCore's API has two layers:

| Layer | Holds | Used by |
|---|---|---|
| **Scene API** | `XScene`, scene-graph nodes and their elements (`XMeshInstance`, `XCamera`, `XLight`, ...), `XModel`, `XUIGraph` | An app that builds and renders its own scenes, such as a model viewer, and the project runtime |
| **Project runtime** | `XProject`, `XWorld`, `XWorldRequest`, and the [streaming](#streaming) of package content | An app that runs a project's content |

The project runtime is built on the scene API: each world it runs is an `XScene`, and each instance of package content is a scene-graph subtree. An app may use the scene API alone, the project runtime, or both.

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

## Worlds

A **world** is the top-level unit a project runs: one [world bundle](../CanvasPkg/README.md#worlds), its always-present content, and its [world regions](../CanvasPkg/README.md#world-regions). A project offers the worlds its manifest lists in `worlds`, and each is an `XWorld` with its own `XScene`.

- **One world is active.** From the first activation on, a project has exactly one **active world**, which is the world that is updated and rendered. Several worlds may be loaded at once, such as the next level loading while a loading screen is active, and only one is active.
- **Menus and HUDs are overlays.** All menu and HUD UI is an **overlay** drawn over the active world. A startup screen is a world with little content and one overlay; an in-game options screen is an overlay over the current world, which stays active underneath.
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

**Shared assets are loaded once per project.** A shared asset that several instances or worlds use, such as a texture, is loaded once and kept while any loaded content uses it.

## Instantiation

Package content becomes engine objects through `XModel`. A bundle that the engine instantiates loads into an `XModel`, which serves as its **template**: the bundle's local assets become the model's node hierarchy and elements, and the shared assets they reference become the GPU resources the model shares across its instances. Each [instance](../CanvasPkg/README.md#instances) of the bundle is one call to `XModel::Instantiate`, which clones the template's hierarchy under the node that holds the instance.

| Package asset | Engine object |
|---|---|
| `TransformNode` | `XSceneGraphNode` |
| `MeshInstance` | `XMeshInstance` on its node, drawing the `MeshData`'s `XGfxMeshData` |
| `Camera`, `Light` | `XCamera`, `XLight` on their nodes |
| `Material`, `Image` | `XGfxMaterial`, `XGfxSurface` |
| `Placement` | A nested instance of the placed bundle's model per transform, under the placement's node |
| `AnimationClip`, with the `JointAnimation` assets it binds | An animation clip, played through each instance's `XAnimationController` |
| `WorldSettings` | The world's `XScene` settings: its background and skyboxes, and its active camera |

The node an instance is cloned under is its instance transform: a placement's node for a nested instance, and the world's scene root for a world bundle or world region.

`XModel` covers a bundle's node hierarchy, meshes, materials, textures, cameras, lights, and animation clips. A bundle's other structure extends it:

- **Placements**: nested instances, instantiated with the instance that holds them.
- **Exports and import nodes**: an import node resolves to the exported node of each nested instance its placement declares.
- **Skeletons**: a `Skeleton` is shared data and a `SkeletonInstance` poses it, where `XModel` represents bones as scene-graph nodes and binds skins to them.
- **Mobility**: each node's effective [mobility](../CanvasPkg/README.md#mobility), which the scene uses to treat static content as fixed.
- **Shared assets across bundles**: an `XModel` holds the GPU resources of its own bundle, where a shared asset may be referenced from any bundle of any package in the project.

**Runtime instances.** Content requested at runtime, such as a spawned character, is an instance of a bundle with an instance transform and a mobility that the requester supplies. The interface that requests one is defined with the [streaming](#streaming) model, since its bundle may need loading first.

## Streaming

A project's content moves through these states:

| State | Holds |
|---|---|
| **Project open** | Every package's header and name directories |
| **World loaded** | A world bundle's always-present content, and the shared assets it uses |
| **World active** | The loaded world that is updated and rendered |
| **Region resident** | A world region's content, loaded while the region's `StreamingBounds` is near a streaming source |

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
