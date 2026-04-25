//================================================================================================
// VSText.hlsl - Text Rendering Vertex Shader
//
// Expands per-glyph instance data into screen-aligned quads using SV_VertexID.
// Each glyph is 6 vertices (2 triangles).  vertexId / 6 selects the glyph;
// vertexId % 6 selects the corner within the quad.
//================================================================================================

#include "HlslTypes.h"

ConstantBuffer<HlslTextConstants> TextCB : register(b0);
StructuredBuffer<HlslGlyphInstance> Glyphs : register(t0);

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

    HlslGlyphInstance g = Glyphs[glyphIdx];
    float2 t = kCorner[cornerIdx];

    // Quad position in element-local pixels, then offset to screen space
    float2 pos = g.Offset + t * g.Size + TextCB.ElementOffset;

    float ndcX =  (pos.x / TextCB.ScreenSize.x) * 2.0f - 1.0f;
    float ndcY = -(pos.y / TextCB.ScreenSize.y) * 2.0f + 1.0f;

    // Interpolate atlas UVs across the quad
    float2 uv = lerp(g.AtlasUV.xy, g.AtlasUV.zw, t);

    VSOutput output;
    output.Position = float4(ndcX, ndcY, 0.0f, 1.0f);
    output.TexCoord = uv;
    output.Color = TextCB.TextColor;

    return output;
}
