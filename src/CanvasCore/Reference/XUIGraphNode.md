# XUIGraphNode

A node in a UI graph, the screen-space UI drawn over a [world](../README.md#worlds-and-world-objects) or shown on its own. A node has a position, an ordered list of children, and the UI elements bound to it, such as text and rectangles. A node holds its children and its bound elements, and refers to its parent weakly.

Inherits `XCanvasElement`. Created by `XUIGraph::CreateNode`, which makes the new node the last child of the given parent, or of the graph's root node when the parent is `nullptr`.

## Positions

Positions are in pixels of the render target, with the origin at its top-left corner and y increasing downward. A node's global position is its local position plus its parent's global position, or its local position when it has no parent. An element draws at its node's global position plus its own local offset (`XUIElement::GetLocalOffset`).

### GetLocalPosition, SetLocalPosition

```cpp
const Math::FloatVector2 &GetLocalPosition() const;
void SetLocalPosition(const Math::FloatVector2 &position);
```

Get or set this node's position relative to its parent.

### GetGlobalPosition

```cpp
Math::FloatVector2 GetGlobalPosition();
```

Returns this node's position in the render target.

## Children

### AddChild

```cpp
Gem::Result AddChild(XUIGraphNode *pChild);
```

Makes `pChild` this node's last child. A child that has another parent moves, with its subtree. Adding a node that is already one of this node's children changes nothing. `pChild` must not be this node or an ancestor of it.

| Result       | When                  |
|--------------|-----------------------|
| `BadPointer` | `pChild` is `nullptr` |

### RemoveChild

```cpp
Gem::Result RemoveChild(XUIGraphNode *pChild);
```

Takes `pChild`, with its subtree, out of this node's children. `pChild` then has no parent. This node releases `pChild`, which is destroyed when nothing else holds it. Removing a node that is not one of this node's children changes nothing.

| Result       | When                  |
|--------------|-----------------------|
| `BadPointer` | `pChild` is `nullptr` |

### GetParent

```cpp
XUIGraphNode *GetParent();
```

Returns a weak pointer to this node's parent, or `nullptr` when it has none.

### GetFirstChild, GetNextSibling

```cpp
XUIGraphNode *GetFirstChild();
XUIGraphNode *GetNextSibling();
```

`GetFirstChild` returns a weak pointer to this node's first child. `GetNextSibling` returns a weak pointer to the child after this node in its parent's children. Each returns `nullptr` when there is no such node.

## Bound Elements

An element is bound to at most one node, which its `XUIElement::GetAttachedNode` returns. `XUIElement::Detach` unbinds an element from its node, as `UnbindElement` does, and changes nothing for an element bound to no node; `XUIGraph::RemoveElement` detaches an element. When a node is destroyed, the elements bound to it that something else holds are bound to no node.

### BindElement

```cpp
Gem::Result BindElement(XUIElement *pElement);
```

Binds `pElement` to this node, after the elements already bound to it, and holds it. Binding an element that is bound to another node first unbinds it from that node, which releases its reference. Binding an element that is already bound to this node changes nothing.

| Result        | When                                                     |
|---------------|----------------------------------------------------------|
| `BadPointer`  | `pElement` is `nullptr`                                  |
| `OutOfMemory` | The element list cannot grow; `pElement` is unchanged    |

### UnbindElement

```cpp
Gem::Result UnbindElement(XUIElement *pElement);
```

Unbinds `pElement` from this node. `pElement` is then bound to no node, and this node releases it, so it is destroyed when nothing else holds it.

| Result       | When                                       |
|--------------|--------------------------------------------|
| `BadPointer` | `pElement` is `nullptr`                    |
| `InvalidArg` | `pElement` is not bound to this node       |

### GetBoundElementCount, GetBoundElement

```cpp
uint32_t GetBoundElementCount();
XUIElement *GetBoundElement(uint32_t index);
```

`GetBoundElementCount` returns the number of elements bound to this node. `GetBoundElement` returns a weak pointer to the element at `index`, in binding order, where `index` is less than `GetBoundElementCount()`.
