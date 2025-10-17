# Canvas
An experimental game-like engine.  Canvas is comprised of several subprojects.

## Important: Matrix Convention

**Canvas uses ROW VECTORS (v' = v * M) throughout the entire codebase.**

Translation is stored in the BOTTOM ROW of matrices, not the right column. This differs from most graphics libraries which use column vectors.

## Subprojects

[CanvasCore](./CanvasCore/CanvasCore.md)  
High-level implementation of core Canvas interfaces.

CanvasCore manages resources such as materials and mesh geometry.  Implements the scene graph and manages overall state.

CanvasGraphics12  
D3D12 Canvas graphics implementation.

CanvasMath  
Matrix, vector, quaternion, and other useful math.