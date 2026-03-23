// Fullscreen triangle vertex shader for composition passes.
// Generates a single triangle covering the entire screen using SV_VertexID.
// No vertex buffer needed — invoke with DrawInstanced(3, 1, 0, 0).

struct FSOutput
{
    float2 TexCoord : TEXCOORD0;
    float4 Position : SV_Position;
};

FSOutput VSFullscreen(uint vertexId : SV_VertexID)
{
    FSOutput output;

    // Generate fullscreen triangle UVs: (0,0), (2,0), (0,2)
    output.TexCoord = float2((vertexId << 1) & 2, vertexId & 2);

    // Map UVs to NDC: (0,0) → (-1,+1), (1,1) → (+1,-1)
    output.Position = float4(output.TexCoord * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);

    return output;
}
