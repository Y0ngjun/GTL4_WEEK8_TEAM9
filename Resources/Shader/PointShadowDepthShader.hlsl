#pragma pack_matrix(row_major)

cbuffer ShadowDepthConstants : register(b0)
{
    matrix ShadowViewProjection;
    float3 LightPosition;
    float InvLightRadius;
};

cbuffer PerObjectConstants : register(b2)
{
    matrix World;
    matrix NormalMatrix;
}

struct FShadowVSOutput
{
    float4 Position : SV_POSITION;
    float3 WorldPosition : TEXCOORD0;
};

FShadowVSOutput mainVS(float3 Position : POSITION)
{
    FShadowVSOutput Output;
    float4 WorldPosition = mul(float4(Position, 1.0f), World);
    Output.Position = mul(WorldPosition, ShadowViewProjection);
    Output.WorldPosition = WorldPosition.xyz;
    return Output;
}

float mainPS(FShadowVSOutput Input) : SV_Depth
{
    return saturate(length(Input.WorldPosition - LightPosition) * InvLightRadius);
}
