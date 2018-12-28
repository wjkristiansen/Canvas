//================================================================================================
// GraphicsDevice
//================================================================================================

#pragma once

namespace Canvas
{

//------------------------------------------------------------------------------------------------
// Abstract graphics device class
class CGraphicsDevice :
    public TGeneric<CGenericBase>
{
public:
    virtual Result Present() = 0;
};

//------------------------------------------------------------------------------------------------
// Submits command streams to the GPU.
// Manages synchronization with other command contexts and
// the CPU.
class CCommandContext :
    public TGeneric<CGenericBase>
{

};


//------------------------------------------------------------------------------------------------
// Batches up work for submission to the GPU
class CCommandStream :
    public TGeneric<CGenericBase>
{

};

//------------------------------------------------------------------------------------------------
// Buffers that are CPU-writable and GPU readable.
// Typically used for uploading data.  May also be used for constant
// data, vertex buffers or index buffers depending on access frequency.
// GPU access to these is not as fast as CBuffer.
class CUploadBuffer :
    public TGeneric<CGenericBase>
{

};

//------------------------------------------------------------------------------------------------
// Buffers that are GPU accessible only.  Typically
// GPU reads from CBuffer data is faster than CUploadBuffer data.
// CBuffers are stateful.
class CBuffer :
    public TGeneric<CGenericBase>
{

};

//------------------------------------------------------------------------------------------------
class CTexture :
    public TGeneric<CGenericBase>
{

};

struct CANVAS_MATERIAL_DESC
{

    void *pVSData;
    void *pPSData;
};

//------------------------------------------------------------------------------------------------
// Describes data passed into the graphics rendering pipeline.
// This includes:
//  * Vertex layout
//  * Vertex shader state
//  * Pixel shader state
//  * Fill mode
//  * Alpha blend state
class CMaterial
{

};

//------------------------------------------------------------------------------------------------
struct CANVAS_MESH_DESC
{
    UINT NumIndices;
    UINT32 *pIndices;
    UINT NumVertices;
    UINT16 VertexStride;
    void *pVertexData;
};

//------------------------------------------------------------------------------------------------
// A mesh is essentially an indexed triangle list
// The actual layout of pixels depends on the material
class CMesh :
    public TGeneric<CGenericBase>
{
    CMesh(const CANVAS_MESH_DESC *pMeshDesc);
};





//------------------------------------------------------------------------------------------------
class CPipelineState :
    public TGeneric<CGenericBase>
{

};

//------------------------------------------------------------------------------------------------
class CDescriptorHeap :
    public TGeneric<CGenericBase>
{

};


}
