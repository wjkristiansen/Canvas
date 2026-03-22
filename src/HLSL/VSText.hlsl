//================================================================================================
// VSText.hlsl - Text Rendering Vertex Shader
//
// Reads per-vertex data from a StructuredBuffer (t0, root SRV slot).
// Converts screen-pixel coordinates to NDC using screen dimensions from b0.
//================================================================================================

cbuffer TextScreenConstants : register(b0)
{
    float2 ScreenSize;   // Viewport width, height in pixels
    float2 Padding;
};

struct TextVertex
{
    float3 Position;   // Screen-space pixel position (xy) + depth (z, ignored - always front)
    float2 TexCoord;   // Atlas UV [0,1]
    uint   Color;      // Packed RGBA: R in bits 0-7, G in 8-15, B in 16-23, A in 24-31
};

StructuredBuffer<TextVertex> Vertices : register(t0);

struct VSOutput
{
    float4 Position : SV_POSITION;
    float2 TexCoord : TEXCOORD0;
    float4 Color    : TEXCOORD1;
};

VSOutput main(uint vertexId : SV_VertexID)
{
    TextVertex v = Vertices[vertexId];

    // Convert screen-pixel (x,y) to NDC [-1,1].
    // Vertex positions are quad corner coordinates in screen pixels (integer boundaries).
    // Direct mapping: x=0 → NDC -1 (left edge), x=W → NDC +1 (right edge).
    float ndcX =  (v.Position.x / ScreenSize.x) * 2.0f - 1.0f;
    float ndcY = -(v.Position.y / ScreenSize.y) * 2.0f + 1.0f;

    VSOutput output;
    output.Position = float4(ndcX, ndcY, 0.0f, 1.0f);  // z=0 → draws in front (regular Z)
    output.TexCoord = v.TexCoord;

    // Unpack packed RGBA uint → float4
    output.Color = float4(
        ((v.Color >>  0) & 0xFF) / 255.0f,
        ((v.Color >>  8) & 0xFF) / 255.0f,
        ((v.Color >> 16) & 0xFF) / 255.0f,
        ((v.Color >> 24) & 0xFF) / 255.0f
    );

    return output;
}
