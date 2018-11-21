# Canvas
An experimental D3D12 engine.  Canvas is comprised of several subprojects.

[CanvasCore](./CanvasCore/CanvasCore.md)  
Implementation of core Canvas interfaces.  Takes no direct dependency on D3D12, allowing for alternative graphics implementations.

CanvasCore loads models and textures and manages the scene graph.  At some point it may implement or incorporate a physics engine.

CanvasGraphics12  
D3D12 Canvas graphics implementation.

CanvasMath  
Matrix, vector, quaternion, and other useful math.