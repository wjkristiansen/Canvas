//================================================================================================
// VSRect.hlsl - Rectangle Rendering Vertex Shader
//
// Derives a screen-aligned quad entirely from SV_VertexID and per-draw constants.
// No vertex buffer is bound.
//================================================================================================

#include "HlslTypes.h"

ConstantBuffer<HlslRectConstants> RectCB : register(b0);

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
    float2 pos = kQuadUV[vertexId] * RectCB.RectSize + RectCB.ElementOffset;

    float ndcX =  (pos.x / RectCB.ScreenSize.x) * 2.0f - 1.0f;
    float ndcY = -(pos.y / RectCB.ScreenSize.y) * 2.0f + 1.0f;

    VSOutput output;
    output.Position = float4(ndcX, ndcY, 0.0f, 1.0f);
    output.Color = RectCB.FillColor;

    return output;
}
