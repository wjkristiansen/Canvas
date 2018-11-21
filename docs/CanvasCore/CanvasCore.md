# CanvasEngine
Main Canvas engine.  Takes no direct dependency on D3D12, allowing for alternative graphics implementations.

## Interfaces
Canvas uses an interface model similar to COM.  All Canvas objects inherit from IObject, providing access to other available interfaces using IObject::QueryInterface.

Like COM, interfaces are identified using a unique ID.  However, rather than a globally unique 256-bit guid, Canvas interfaces are identified using a locally-unique 32-bit unsigned integer. Internal interface identifiers can range from 0x00000001 to 0x7fffffff. Custom interfaces identifiers can be in the range of 0x80000000 to 0xffffffff.  The identifier 0x00000000 is reserved.

Canvas objects are reference-counted.  Objects are created with a reference count of 1.  Object references are incremented by IObject::AddRef or by a successful IObject::QueryInterface call.  IObject::Release decrements the objects reference count.  When the reference count reaches zero the object is freed.

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


