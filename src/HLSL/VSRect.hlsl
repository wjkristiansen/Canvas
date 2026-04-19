//================================================================================================
// VSRect.hlsl - Rectangle Rendering Vertex Shader
//
// Reads per-vertex data from a StructuredBuffer (t0, root SRV slot).
// Converts screen-pixel coordinates to NDC using screen dimensions from b0.
// Same vertex layout as text (TextVertex) — TexCoord field is ignored.
//================================================================================================

cbuffer TextScreenConstants : register(b0)
{
    float2 ScreenSize;   // Viewport width, height in pixels
    float2 ElementOffset; // Element screen-space position (pixels)
};

struct TextVertex
{
    float3 Position;   // Element-local pixel position (xy) + depth (z, ignored)
    float2 TexCoord;   // Unused for rects (padding in StructuredBuffer layout)
    float4 Color;      // RGBA float color
};

StructuredBuffer<TextVertex> Vertices : register(t0);

struct VSOutput
{
    float4 Position : SV_POSITION;
    float4 Color    : TEXCOORD0;
};

VSOutput main(uint vertexId : SV_VertexID)
{
    TextVertex v = Vertices[vertexId];

    float ndcX =  ((v.Position.x + ElementOffset.x) / ScreenSize.x) * 2.0f - 1.0f;
    float ndcY = -((v.Position.y + ElementOffset.y) / ScreenSize.y) * 2.0f + 1.0f;

    VSOutput output;
    output.Position = float4(ndcX, ndcY, 0.0f, 1.0f);
    output.Color = v.Color;

    return output;
}
