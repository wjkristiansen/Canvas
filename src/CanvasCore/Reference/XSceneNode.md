# XSceneNode

A node in a scene graph. A node has a local transform, an ordered list of children, and the scene elements bound to it, such as meshes, lights, and cameras. A node holds its children and its bound elements, and refers to its parent weakly. A node can be a [world object](XWorldObject.md)'s root node.

Inherits `XCanvasElement`. Created by `XCanvas::CreateSceneNode`.

## Transforms

A node's local transform is a `Transform`: a scale, a rotation, and a translation. Its local matrix is scale * rotation * translation. Its global matrix is its local matrix times its parent's global matrix, or its local matrix when it has no parent. A node computes its global matrix and rotation when they are read, and keeps them until its local transform or an ancestor's changes, or it moves to a new parent.

## Children

### AddChild

```cpp
Gem::Result AddChild(XSceneNode *pChild);
```

Makes `pChild` this node's last child. A child that has a parent, this node or another, moves in one step, with its subtree. Moving a world object's root node attaches the world object to this node, as [placing](../README.md#worlds-and-world-objects) it does.

| Result        | When                                                          |
|---------------|---------------------------------------------------------------|
| `InvalidArg`  | `pChild` is `nullptr`, this node, or an ancestor of this node |
| `OutOfMemory` | The child list cannot grow; `pChild` is unchanged             |

### InsertChildBefore, InsertChildAfter

```cpp
Gem::Result InsertChildBefore(XSceneNode *pChild, XSceneNode *pSibling);
Gem::Result InsertChildAfter(XSceneNode *pChild, XSceneNode *pSibling);
```

Makes `pChild` a child of this node, placed just before or just after `pSibling`, one of this node's children. A child that has a parent moves as for `AddChild`, including among this node's children.

| Result        | When                                                                                                                  |
|---------------|-----------------------------------------------------------------------------------------------------------------------|
| `InvalidArg`  | `pChild` is `nullptr`, this node, or an ancestor of this node, or `pSibling` is `nullptr`, `pChild`, or not one of this node's children; `pChild` is unchanged |
| `OutOfMemory` | The child list cannot grow; `pChild` is unchanged                                                                     |

### RemoveChild

```cpp
Gem::Result RemoveChild(XSceneNode *pChild);
```

Takes `pChild`, with its subtree, out of this node's children. `pChild` then has no parent, and its global matrix is its local matrix. This node releases `pChild`, which is destroyed when nothing else holds it. The world objects whose root nodes are in the subtree are [detached](../README.md#worlds-and-world-objects) and stay in their worlds.

| Result        | When                                      |
|---------------|-------------------------------------------|
| `InvalidArg`  | `pChild` is not one of this node's children |

### GetParent

```cpp
XSceneNode *GetParent();
```

Returns a weak pointer to this node's parent, or `nullptr` when it has none.

### GetFirstChild, GetNextChild, GetPrevChild

```cpp
XSceneNode *GetFirstChild();
XSceneNode *GetNextChild(XSceneNode *pCurrent);
XSceneNode *GetPrevChild(XSceneNode *pCurrent);
```

Each returns a weak pointer to one of this node's children, in order: the first child, or the child after or before `pCurrent`. Each returns `nullptr` when there is no such child, or when `pCurrent` is not one of this node's children.

### FindChild

```cpp
XSceneNode *FindChild(const char *name);
```

Returns a weak pointer to the first of this node's children whose [name](../README.md#worlds-and-world-objects) is `name`, compared exactly, or `nullptr` when there is none or `name` is `nullptr`. Only direct children are searched.

## Bound Elements

### BindElement

```cpp
Gem::Result BindElement(XSceneElement *pElement);
```

Binds `pElement` to this node, after the elements already bound to it, and holds it. The element's attached node is then this node. Binding an element that is already bound to this node, or `nullptr`, changes nothing.

### GetBoundElementCount, GetBoundElement

```cpp
uint32_t GetBoundElementCount();
XSceneElement *GetBoundElement(uint32_t index);
```

`GetBoundElementCount` returns the number of elements bound to this node. `GetBoundElement` returns a weak pointer to the element at `index`, in binding order, where `index` is less than `GetBoundElementCount()`.

## Animation

### GetAnimationController

```cpp
XAnimationController *GetAnimationController();
```

Returns a weak pointer to the [animation controller](XAnimationController.md) this node holds, or `nullptr` when it holds none. The root node of an instance of a model that has animation clips holds the instance's controller, which animates the nodes of the instance. [`XWorld::Update`](XWorld.md#update) updates a node's controller, before the elements bound to it, when the node belongs to an active world object.

## Transform Methods

### GetLocalTransform, SetLocalTransform

```cpp
const Transform &GetLocalTransform() const;
void SetLocalTransform(const Transform &transform);
```

Get and set this node's local transform.

### GetLocalRotation, GetLocalTranslation, GetLocalScale, SetLocalRotation, SetLocalTranslation, SetLocalScale

```cpp
const Math::FloatQuaternion &GetLocalRotation() const;
const Math::FloatVector4 &GetLocalTranslation() const;
const Math::FloatVector4 &GetLocalScale() const;
void SetLocalRotation(const Math::FloatQuaternion &rotation);
void SetLocalTranslation(const Math::FloatVector4 &translation);
void SetLocalScale(const Math::FloatVector4 &scale);
```

Get and set one part of this node's local transform.

### GetLocalMatrix, GetGlobalMatrix

```cpp
const Math::FloatMatrix4x4 GetLocalMatrix();
const Math::FloatMatrix4x4 GetGlobalMatrix();
```

Return this node's local matrix and global matrix.

### GetGlobalTranslation

```cpp
const Math::FloatVector4 GetGlobalTranslation();
```

Returns the translation row of this node's global matrix.

### GetGlobalRotation

```cpp
const Math::FloatQuaternion GetGlobalRotation();
```

Returns the rotations of this node and its ancestors composed, without their scales. It matches the rotation in the global matrix when every ancestor's scale is uniform. A non-uniform ancestor scale under a rotated child shears the global matrix, which no rotation describes.

## World Objects

### GetWorldObject

```cpp
XWorldObject *GetWorldObject();
```

Returns a weak pointer to the world object this node [belongs to](../README.md#worlds-and-world-objects), or `nullptr` when it belongs to none. A world object's root node belongs to that world object, and every other node belongs to the world object of its parent.
