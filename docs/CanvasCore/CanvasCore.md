# CanvasCore
Core implementation of Canvas. Takes no direct dependency on D3D version, allowing for alternative graphics implementations.

## Interfaces

**IModel**  
3D model containing geometry, texture mapping, and animation data used by Canvas scene graph nodes.

**INamed**
Implemented by objects that have a name.

**IObject**  
All Canvas objects implement IObject.  Objects can be queried for other interfaces.

**IScene**  
Canvas scene graph interface.

**ISceneNode**  
Node in a Canvas scene graph.

**ITransform**  
Transform data describes the position and orientation of transformable objects, such as an ISceneNode.


