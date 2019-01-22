//================================================================================================
// GraphicsDevice
//================================================================================================

#pragma once

namespace Canvas
{

//------------------------------------------------------------------------------------------------
// Submits command streams to the GPU.
// Manages synchronization with other command contexts and
// the CPU.
// In D3D12, this wraps a command queue and a pool of command lists and command allocators.
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
public:
    GEMMETHOD_(void *, Data)() = 0;
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
// Describes data passed into the graphics rendering pipeline.
// This includes:
//  * Vertex layout
//  * Vertex shader state
//  * Pixel shader state
//  * Fill mode
//  * Alpha blend state
//------------------------------------------------------------------------------------------------
class CMaterial :
    public XMaterial,
    public CGenericBase
{

};

//------------------------------------------------------------------------------------------------
class CTexture :
    public TGeneric<CGenericBase>
{

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


//------------------------------------------------------------------------------------------------
// Abstract graphics device class
class CGraphicsDevice :
    public XGraphicsDevice,
    public CGenericBase
{
public:
    virtual Result RenderFrame() = 0;
    virtual Result AllocateUploadBuffer(UINT64 SizeInBytes, CUploadBuffer **ppUploadBuffer) = 0;
};

}
