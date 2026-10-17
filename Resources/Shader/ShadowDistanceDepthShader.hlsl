#pragma pack_matrix(row_major)
// CPU FShadowDepthPassConstants: 64-byte matrix + float3 + float = 80 bytes.
cbuffer ShadowDepthPassConstants : register(b0)
{
    matrix ViewProjection;
    float3 LightPosition;
    float InvRadius;
};
cbuffer PerObjectConstants : register(b2)
{
    matrix World;
    matrix NormalMatrix;
};
struct VSOutput
{
    float4 Position : SV_POSITION;
    float3 WorldPosition : TEXCOORD0;
};
VSOutput mainVS(float3 Position : POSITION)
{
    VSOutput Output;
    float4 WorldPosition = mul(float4(Position, 1.0f), World);
    Output.Position = mul(WorldPosition, ViewProjection);
    Output.WorldPosition = WorldPosition.xyz;
    return Output;
}
float mainPS(VSOutput Input) : SV_Depth
{
    float Depth = length(Input.WorldPosition - LightPosition) * InvRadius;
    // Spot/Point share radial depth. Projection still determines coverage and clipping.
    clip(1.0f - Depth);
    return Depth;
}
