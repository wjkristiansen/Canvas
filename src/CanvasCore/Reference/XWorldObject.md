# XWorldObject

The identity and state of what a set of scene graph nodes represents, such as an instance of a model. A world object is anchored at a scene graph node, its anchor node, or has none, and its nodes are the nodes that [belong to](../README.md#worlds-and-world-objects) it. Its world holds it, and it holds its anchor node; the anchor node refers back to it weakly.

Inherits `XCanvasElement`. Created by [`XWorld::CreateWorldObject`](XWorld.md#createworldobject). Its world changes it: activation, attachment, removal, and its unique name are [`XWorld`](XWorld.md) methods.

## Methods

### GetWorld

```cpp
XWorld *GetWorld();
```

Returns a weak pointer to the world the world object is in, or `nullptr` once it is removed from that world or the world is released.

### GetAnchorNode

```cpp
XSceneGraphNode *GetAnchorNode();
```

Returns a weak pointer to the world object's anchor node, or `nullptr` when it has none.

### GetUniqueName

```cpp
const char *GetUniqueName();
```

Returns the world object's [unique name](../README.md#worlds-and-world-objects), or `nullptr` when it has none. A world object has no unique name once it is removed from its world or the world is released. The string is valid until the world object's unique name changes or is released.
