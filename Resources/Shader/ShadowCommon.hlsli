#ifndef SHADOW_COMMON_HLSLI
#define SHADOW_COMMON_HLSLI

cbuffer ShadowConstants : register(b5)
{
    matrix DirectionalShadowViewProjection;
    uint bHasDirectionalShadow;
    float3 ShadowPadding;
}

Texture2DArray DirectionalShadowMap : register(t12);
SamplerState ShadowPointSampler : register(s1);

float3 ProjectToShadowMap(float3 WorldPosition, matrix ShadowViewProjection)
{
    float4 Clip = mul(float4(WorldPosition, 1.0f), ShadowViewProjection);
    float3 NDC = Clip.xyz / Clip.w;
    float2 UV = float2(NDC.x * 0.5f + 0.5f, -NDC.y * 0.5f + 0.5f);
    return float3(UV, NDC.z);
}    
#endif
