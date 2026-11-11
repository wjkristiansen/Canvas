# XWorldObject

The identity and state of something in a world, such as an instance of a model. A world object has a scene node as its root node, the top of the nodes that [belong to](../README.md#worlds-and-world-objects) it, or has no root node and no nodes. The world it is in holds it, and it holds its root node; the root node refers back to it weakly.

Inherits `XCanvasElement`. Created by [`XCanvas::CreateWorldObject`](XCanvas.md#createworldobject) in no world, and added to a world by [`XWorld::AddWorldObject`](XWorld.md#addworldobject). Its world places, detaches, activates, names, and releases it.

## Methods

### GetWorld

```cpp
XWorld *GetWorld();
```

Returns a weak pointer to the world the world object is in, or `nullptr` when it is in none.

### GetRootNode, SetRootNode

```cpp
XSceneNode *GetRootNode();
Gem::Result SetRootNode(XSceneNode *pNode);
```

`GetRootNode` returns a weak pointer to the world object's root node, or `nullptr` when it has none.

`SetRootNode` makes `pNode` the world object's root node, and holds it, or clears the root node when `pNode` is `nullptr`, and releases the previous root node. When the previous root node has a parent, `pNode` takes its place among its siblings, and the previous root node leaves the graph with its subtree. The world objects whose root nodes are in the previous root node's subtree stay in it. Replacing or clearing a root node is logged. Setting the root node the world object has changes nothing.

| Result       | When                                                                                                         |
|--------------|--------------------------------------------------------------------------------------------------------------|
| `InvalidArg` | `pNode` has a parent, is another world object's root node, is the root node of the world the world object is in, or was created by another canvas |

### FindNode

```cpp
XSceneNode *FindNode(const char *name);
```

Returns a weak pointer to the first of the world object's nodes whose name is `name`, compared exactly, or `nullptr` when there is none, `name` is `nullptr`, or the world object has no root node. The nodes are searched in pre-order from the root node: each node, then its children in order. The search leaves out the nodes of world objects attached to the world object's nodes, since those nodes belong to the attached world objects. Node names are usually unique within a world object, but some authoring tools allow repeats, and then the first match is returned.

### GetUniqueName

```cpp
const char *GetUniqueName();
```

Returns the world object's [unique name](../README.md#worlds-and-world-objects), or `nullptr` when it has none. A world object has no unique name once it is released from its world or the world is released. The string is valid until the world object's unique name changes or is released.
