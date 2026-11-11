# XModel

An immutable template that [`XCanvas::CreateWorldObject`](XCanvas.md#createworldobject) instantiates. A model holds tables, each indexed from 0: model nodes, listed parents first, and the meshes, cameras, and lights attached to them, skeletons with their sockets, animation targets, and animation clips. It also holds the GPU resources its meshes draw with, its mesh data, materials, and textures, which every instance shares.

Inherits `XCanvasElement`. Created by `XModelBuilder::Build`. The model holds its GPU resources.

## Instances

An instance of a model is a [world object](XWorldObject.md) whose root node is an instance root node. The instance has one scene node per model node, with the model node's name and local transform, under its parent's node, or under the instance root for a model node with no parent. Each mesh, camera, and light becomes an element of the same name on its model node's scene node, each skeleton a skeleton instance, and the clips are played through the instance's [animation controller](XAnimationController.md).

## GPU Resources

### GetDevice

```cpp
XGfxDevice *GetDevice();
```

Returns a weak pointer to the graphics device that created the model's GPU resources.

### GetMeshDataCount, GetMeshData, GetMaterialCount, GetMaterial, GetTextureCount, GetTexture

```cpp
uint32_t GetMeshDataCount();
XGfxMeshData *GetMeshData(uint32_t index);
uint32_t GetMaterialCount();
XGfxMaterial *GetMaterial(uint32_t index);
uint32_t GetTextureCount();
XGfxSurface *GetTexture(uint32_t index);
```

Each count returns the number of the model's mesh data, materials, or textures. Each getter returns a weak pointer to the one at `index`, or `nullptr` when `index` is out of range.

## Nodes

### GetNodeCount, GetNodeName, GetNodeParent, GetNodeTransform

```cpp
uint32_t GetNodeCount();
const char *GetNodeName(uint32_t index);
uint32_t GetNodeParent(uint32_t index);
Gem::Result GetNodeTransform(uint32_t index, Transform *pTransform);
```

`GetNodeCount` returns the number of model nodes. `GetNodeName` returns the name of the model node at `index`, or `nullptr` when `index` is out of range. `GetNodeParent` returns the index of its parent, or `InvalidIndex` when it has none or `index` is out of range. `GetNodeTransform` writes its local transform to `*pTransform`.

| Result       | When                                                  |
|--------------|-------------------------------------------------------|
| `BadPointer` | `GetNodeTransform`: `pTransform` is `nullptr`         |
| `InvalidArg` | `GetNodeTransform`: `index` is out of range           |

## Cameras

### GetCameraCount

```cpp
uint32_t GetCameraCount();
```

Returns the number of the model's cameras. Each instance has a camera of the same name on its scene node for the camera's model node, which [`XWorldObject::FindNode`](XWorldObject.md#findnode) finds by the node's name. Instantiating the model leaves the world's active camera unchanged; which camera is active is the app's choice.

## Skeletons and Animation

### GetSkeletonCount

```cpp
uint32_t GetSkeletonCount();
```

Returns the number of the model's skeletons. Each instance has one skeleton instance per skeleton.

### GetAnimationClipCount, GetAnimationClipName

```cpp
uint32_t GetAnimationClipCount();
const char *GetAnimationClipName(uint32_t index);
```

`GetAnimationClipCount` returns the number of the model's animation clips. `GetAnimationClipName` returns the name of the clip at `index`, or `nullptr` when `index` is out of range.

## Bounds

### GetBounds

```cpp
Math::AABB GetBounds();
```

Returns the axis-aligned bounding box of the model's meshes at rest, relative to the instance root. A skinned mesh is bounded in the rest pose of its skeleton.
