#ifndef SHADOW_COMMON_HLSLI
#define SHADOW_COMMON_HLSLI

cbuffer ShadowConstants : register(b5)
{
    matrix DirectionalShadowViewProjection;
    matrix SpotShadowViewProjection;
    uint bHasDirectionalShadow;
    uint bHasSpotShadow;
    float2 ShadowPadding;
}

Texture2DArray DirectionalShadowMap : register(t12);
Texture2DArray SpotShadowMap : register(t13);
SamplerState ShadowPointSampler : register(s1);

float3 ClipToShadowCoord(float4 Clip)
{
    float3 NDC = Clip.xyz / Clip.w;
    return float3(NDC.x * 0.5f + 0.5f, -NDC.y * 0.5f + 0.5f, NDC.z);
}

float3 ProjectToShadowMap(float3 WorldPosition, matrix ShadowViewProjection)
{
    return ClipToShadowCoord(mul(float4(WorldPosition, 1.0f), ShadowViewProjection));
}

float CompareShadowDepth(Texture2DArray ShadowMap, float Slice, float3 ShadowCoord)
{
    if (ShadowCoord.z > 1.0f)
    {
        return 1.0f;
    }
    
    float StoredDepth = ShadowMap.SampleLevel(ShadowPointSampler, float3(ShadowCoord.xy, Slice), 0.0f).r;
    return ShadowCoord.z - 0.005f > StoredDepth ? 0.0f : 1.0f;
}

float GetDirectionalShadow(float3 WorldPosition)
{
    if (bHasDirectionalShadow == 0u)
    {
        return 1.0f;
    }
    
    return CompareShadowDepth(DirectionalShadowMap, 0.0f, ProjectToShadowMap(WorldPosition, DirectionalShadowViewProjection));
}

float GetSpotShadow(float3 WorldPosition)
{
    if (bHasSpotShadow == 0u)
    {
        return 1.0f;
    }
   
    float4 Clip = mul(float4(WorldPosition, 1.0f), SpotShadowViewProjection);
    if (Clip.w <= 0.0f)
    {
        return 1.0f;
    }
    return CompareShadowDepth(SpotShadowMap, 0.0f, ClipToShadowCoord(Clip));
}
#endif
