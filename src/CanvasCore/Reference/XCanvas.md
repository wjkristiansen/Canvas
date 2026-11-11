# XCanvas

The object that creates every other CanvasCore object, loads graphics plugins, and logs. Each object it creates is registered with it, and holds a weak pointer to it, which `XCanvasElement::GetCanvas` returns.

Inherits `Gem::XGeneric`. Created by [`CreateCanvas`](README.md#createcanvas).

## Plugins

### LoadPlugin

```cpp
Gem::Result LoadPlugin(const char *path, XCanvasPlugin **ppPlugin);
```

Loads the plugin module at `path`, such as `CanvasGfx12.dll`, and keeps it loaded while the canvas exists. `*ppPlugin` receives the module's plugin, which creates the objects the module implements, such as a graphics device. Returns the failure from loading the module or getting its plugin, or `OutOfMemory`.

## Creating Objects

Each method below creates an object, registers it with the canvas, and gives it the name `name`, for logs and debugging. `*ppObject`, or the method's other `X**` parameter, receives the object. Each returns `BadPointer` when that parameter is `nullptr`, and `OutOfMemory` when the object cannot be created.

### CreateWorld

```cpp
Gem::Result CreateWorld(XGfxDevice *pDevice, XWorld **ppWorld, const char *name = nullptr);
```

Creates a [world](XWorld.md) that renders with `pDevice`, with a root node and no world objects. The calling thread is the world's [callback thread](../README.md#world-callbacks).

### CreateSceneNode

```cpp
Gem::Result CreateSceneNode(XSceneNode **ppNode, const char *name = nullptr);
```

Creates a [scene node](XSceneNode.md) with the identity transform, no parent, no children, and no bound elements.

### CreateWorldObject

```cpp
Gem::Result CreateWorldObject(XModel *pModel, XWorldObject **ppObject, const char *name = nullptr);
```

Creates a [world object](XWorldObject.md) in no world, which [`XWorld::AddWorldObject`](XWorld.md#addworldobject) adds to a world. When `pModel` is not `nullptr`, the world object is an [instance](../README.md#instantiation) of it: its root node is a new instance root with no parent, under which the instance has one scene node per model node with its elements, and the instance root holds the instance's [animation controller](XAnimationController.md) when the model has animation clips. When `name` is `nullptr`, the world object takes `pModel`'s name. When `pModel` is `nullptr`, the world object is empty: it has no root node until [`XWorldObject::SetRootNode`](XWorldObject.md#getrootnode-setrootnode) gives it one.

| Result       | When                                                        |
|--------------|-------------------------------------------------------------|
| `InvalidArg` | `pModel` was created by another canvas                      |

### CreateCamera, CreateLight, CreateMeshInstance

```cpp
Gem::Result CreateCamera(XCamera **ppCamera, const char *name = nullptr);
Gem::Result CreateLight(LightType type, XLight **ppLight, const char *name = nullptr);
Gem::Result CreateMeshInstance(XMeshInstance **ppMeshInstance, const char *name = nullptr);
```

Create a camera, a light of type `type`, or a mesh instance, each a scene element bound to no node until [`XSceneNode::BindElement`](XSceneNode.md#bindelement) binds it.

### CreateModelBuilder

```cpp
Gem::Result CreateModelBuilder(XGfxDevice *pDevice, XModelBuilder **ppBuilder, const char *name = nullptr);
```

Creates a builder that produces [models](XModel.md) whose GPU resources are on `pDevice`. Returns `BadPointer` when `pDevice` is `nullptr`.

### CreateFont

```cpp
Gem::Result CreateFont(const uint8_t *pTTFData, size_t dataSize, const char *name, XFont **ppFont);
```

Creates a font from the `dataSize` bytes of TrueType data at `pTTFData`. Returns `BadPointer` when `pTTFData` is `nullptr`, and the failure from reading the font data.

### CreateUIGraph

```cpp
Gem::Result CreateUIGraph(XGfxDevice *pDevice, XUIGraph **ppGraph);
```

Creates a UI graph shown independently of every world, such as a console, that renders with `pDevice`. A world's own UI graphs are created with [`XWorld::CreateUIGraph`](XWorld.md#createuigraph-removeuigraph). Returns `BadPointer` when `pDevice` is `nullptr`.

## Registration and Logging

### RegisterElement, UnregisterElement

```cpp
Gem::Result RegisterElement(XCanvasElement *pElement);
Gem::Result UnregisterElement(XCanvasElement *pElement);
```

Add an element to the canvas's registry and take it out. `XCanvasElement::Register` and `Unregister` call them; the app calls those instead.

### GetLogger

```cpp
XLogger *GetLogger();
```

Returns a weak pointer to the logger passed to `CreateCanvas`, or `nullptr` when there is none.
