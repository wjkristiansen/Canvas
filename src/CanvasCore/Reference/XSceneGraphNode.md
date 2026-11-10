# XSceneGraphNode

A node in a scene graph. A node has a local transform, an ordered list of children, and the scene graph elements bound to it, such as meshes, lights, and cameras. A node holds its children and its bound elements, and refers to its parent weakly. A node that a [world object](../README.md#worlds-and-world-objects) is anchored at is that world object's anchor node.

Inherits `XCanvasElement`. Created by `XCanvas::CreateSceneGraphNode`.

## Transforms

A node's local transform is a `Transform`: a scale, a rotation, and a translation. Its local matrix is scale * rotation * translation. Its global matrix is its local matrix times its parent's global matrix, or its local matrix when it has no parent. A node computes its global matrix and rotation when they are read, and keeps them until its local transform or an ancestor's changes, or it moves to a new parent.

## Children

### AddChild

```cpp
Gem::Result AddChild(XSceneGraphNode *pChild);
```

Makes `pChild` this node's last child. A child that has a parent, this node or another, moves in one step, with its subtree. Moving an anchor node attaches its world object, as [attaching](../README.md#worlds-and-world-objects) the world object does.

| Result        | When                                                          |
|---------------|---------------------------------------------------------------|
| `InvalidArg`  | `pChild` is `nullptr`, this node, or an ancestor of this node |
| `OutOfMemory` | The child list cannot grow; `pChild` is unchanged             |

### InsertChildBefore, InsertChildAfter

```cpp
Gem::Result InsertChildBefore(XSceneGraphNode *pChild, XSceneGraphNode *pSibling);
Gem::Result InsertChildAfter(XSceneGraphNode *pChild, XSceneGraphNode *pSibling);
```

Makes `pChild` a child of this node, placed just before or just after `pSibling`, one of this node's children. A child that has a parent moves as for `AddChild`, including among this node's children.

| Result        | When                                                                                                                  |
|---------------|-----------------------------------------------------------------------------------------------------------------------|
| `InvalidArg`  | `pChild` is `nullptr`, this node, or an ancestor of this node, or `pSibling` is `nullptr`, `pChild`, or not one of this node's children; `pChild` is unchanged |
| `OutOfMemory` | The child list cannot grow; `pChild` is unchanged                                                                     |

### RemoveChild

```cpp
Gem::Result RemoveChild(XSceneGraphNode *pChild);
```

Takes `pChild`, with its subtree, out of this node's children. `pChild` then has no parent, and its global matrix is its local matrix. This node releases `pChild`, which is destroyed when nothing else holds it. The world objects anchored in the subtree are [detached](../README.md#worlds-and-world-objects) and stay in their worlds.

| Result        | When                                      |
|---------------|-------------------------------------------|
| `InvalidArg`  | `pChild` is not one of this node's children |

### GetParent

```cpp
XSceneGraphNode *GetParent();
```

Returns a weak pointer to this node's parent, or `nullptr` when it has none.

### GetFirstChild, GetNextChild, GetPrevChild

```cpp
XSceneGraphNode *GetFirstChild();
XSceneGraphNode *GetNextChild(XSceneGraphNode *pCurrent);
XSceneGraphNode *GetPrevChild(XSceneGraphNode *pCurrent);
```

Each returns a weak pointer to one of this node's children, in order: the first child, or the child after or before `pCurrent`. Each returns `nullptr` when there is no such child, or when `pCurrent` is not one of this node's children.

## Bound Elements

### BindElement

```cpp
Gem::Result BindElement(XSceneGraphElement *pElement);
```

Binds `pElement` to this node, after the elements already bound to it, and holds it. The element's attached node is then this node. Binding an element that is already bound to this node, or `nullptr`, changes nothing.

### GetBoundElementCount, GetBoundElement

```cpp
uint32_t GetBoundElementCount();
XSceneGraphElement *GetBoundElement(uint32_t index);
```

`GetBoundElementCount` returns the number of elements bound to this node. `GetBoundElement` returns a weak pointer to the element at `index`, in binding order, where `index` is less than `GetBoundElementCount()`.

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

## Animation and Update

### SetAnimationController, GetAnimationController

```cpp
Gem::Result SetAnimationController(XAnimationController *pController);
XAnimationController *GetAnimationController();
```

`SetAnimationController` sets the animation controller that drives this node's subtree, and holds it, or clears it when `pController` is `nullptr`. `GetAnimationController` returns a weak pointer to the controller, or `nullptr` when there is none.

| Result        | When                                                                                                    |
|---------------|---------------------------------------------------------------------------------------------------------|
| `InvalidArg`  | Something the controller drives is outside this node's subtree, as `XAnimationController::ValidateForNode` reports |

### Update

```cpp
Gem::Result Update(float dtime);
```

Advances this node's subtree by `dtime` seconds: this node's animation controller, then its bound elements in binding order, then each child's subtree in order. Returns the first failure, and updates nothing after it.

## World Objects

### GetWorldObject

```cpp
XWorldObject *GetWorldObject();
```

Returns a weak pointer to the world object this node [belongs to](../README.md#worlds-and-world-objects), or `nullptr` when it belongs to none. An anchor node belongs to the world object anchored at it, and every other node belongs to the world object of its parent.
