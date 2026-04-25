//================================================================================================
// VSText.hlsl - Text Rendering Vertex Shader
//
// Expands per-glyph instance data into screen-aligned quads using SV_VertexID.
// Each glyph is 6 vertices (2 triangles).  vertexId / 6 selects the glyph;
// vertexId % 6 selects the corner within the quad.
//================================================================================================

cbuffer TextConstants : register(b0)
{
    float2 ScreenSize;       // Viewport width, height in pixels
    float2 ElementOffset;    // Element screen-space position (pixels)
    float4 TextColor;        // RGBA text color (uniform per draw)
};

struct GlyphInstance
{
    float2 Offset;           // Element-local pixel position of quad top-left
    float2 Size;             // Quad width, height in pixels
    float4 AtlasUV;          // (u0, v0, u1, v1)
};

StructuredBuffer<GlyphInstance> Glyphs : register(t0);

static const float2 kCorner[6] =
{
    float2(0, 0), float2(0, 1), float2(1, 0),
    float2(0, 1), float2(1, 1), float2(1, 0)
};

struct VSOutput
{
    float4 Position : SV_POSITION;
    float2 TexCoord : TEXCOORD0;
    float4 Color    : TEXCOORD1;
};

VSOutput main(uint vertexId : SV_VertexID)
{
    uint glyphIdx = vertexId / 6;
    uint cornerIdx = vertexId % 6;

    GlyphInstance g = Glyphs[glyphIdx];
    float2 t = kCorner[cornerIdx];

    // Quad position in element-local pixels, then offset to screen space
    float2 pos = g.Offset + t * g.Size + ElementOffset;

    float ndcX =  (pos.x / ScreenSize.x) * 2.0f - 1.0f;
    float ndcY = -(pos.y / ScreenSize.y) * 2.0f + 1.0f;

    // Interpolate atlas UVs across the quad
    float2 uv = lerp(g.AtlasUV.xy, g.AtlasUV.zw, t);

    VSOutput output;
    output.Position = float4(ndcX, ndcY, 0.0f, 1.0f);
    output.TexCoord = uv;
    output.Color = TextColor;

    return output;
}
