//================================================================================================
// VSRect.hlsl - Rectangle Rendering Vertex Shader
//
// Derives a screen-aligned quad entirely from SV_VertexID and per-draw constants.
// No vertex buffer is bound.
//================================================================================================

cbuffer RectConstants : register(b0)
{
    float2 ScreenSize;       // Viewport width, height in pixels
    float2 ElementOffset;    // Element screen-space position (pixels)
    float2 RectSize;         // Rectangle width, height in pixels
    float2 _Pad0;            // Padding to align FillColor to 16-byte boundary
    float4 FillColor;        // RGBA fill color
};

static const float2 kQuadUV[6] =
{
    float2(0, 0), float2(0, 1), float2(1, 0),
    float2(0, 1), float2(1, 1), float2(1, 0)
};

struct VSOutput
{
    float4 Position : SV_POSITION;
    float4 Color    : TEXCOORD0;
};

VSOutput main(uint vertexId : SV_VertexID)
{
    float2 pos = kQuadUV[vertexId] * RectSize + ElementOffset;

    float ndcX =  (pos.x / ScreenSize.x) * 2.0f - 1.0f;
    float ndcY = -(pos.y / ScreenSize.y) * 2.0f + 1.0f;

    VSOutput output;
    output.Position = float4(ndcX, ndcY, 0.0f, 1.0f);
    output.Color = FillColor;

    return output;
}
