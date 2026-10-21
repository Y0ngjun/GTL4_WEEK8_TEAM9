#pragma pack_matrix(row_major)
Texture2D<float> SceneDepth : register(t0);
cbuffer CascadeDebugConstants : register(b0)
{
    float4x4 InverseViewProjection;
    float3 ViewOrigin;
    uint CascadeCount;
    float3 ViewForward;
    float Padding;
    float4 DepthRanges[8];
};
struct PSInput
{
    float4 Position : SV_POSITION;
    float2 UV : TEXCOORD0;
};
PSInput mainVS(uint VertexID : SV_VertexID)
{
    PSInput Output;
    Output.UV = float2((VertexID << 1) & 2, VertexID & 2);
    Output.Position = float4(Output.UV * float2(2, -2) + float2(-1, 1), 0, 1);
    return Output;
}
float4 mainPS(PSInput Input) : SV_TARGET
{
    float Depth = SceneDepth.Load(int3(Input.Position.xy, 0));
    if (Depth >= 1.0f) discard;
    float2 NDC = Input.UV * float2(2, -2) + float2(-1, 1);
    float4 World = mul(float4(NDC, Depth, 1), InverseViewProjection);
    float ViewDepth = dot(World.xyz / World.w - ViewOrigin, ViewForward);
    static const float3 Colors[8] = {
        float3(1, .15, .15), float3(.15, 1, .15), float3(.15, .35, 1), float3(1, 1, .15),
        float3(1, .15, 1), float3(.15, 1, 1), float3(1, .5, .15), float3(.6, .3, 1)
    };
    for (uint Index = 0; Index < CascadeCount; ++Index)
    {
        float4 Range = DepthRanges[Index];
        if (ViewDepth < Range.x || ViewDepth > Range.y) continue;
        float3 Color = Colors[Index];
        if (Index + 1 < CascadeCount && Range.z > 0)
            Color = lerp(Color, Colors[Index + 1], saturate((ViewDepth - (Range.y - Range.z)) / Range.z));
        return float4(Color, .45);
    }
    discard;
    return 0;
}
