#ifndef SHADOW_COMMON_HLSLI
#define SHADOW_COMMON_HLSLI

#define MAX_SHADOWED_SPOT_LIGHTS 4 // C++ MaxShadowedSpotLights와 같아야 함

cbuffer ShadowConstants : register(b5)
{
    matrix DirectionalShadowViewProjection;
    matrix SpotShadowViewProjection[MAX_SHADOWED_SPOT_LIGHTS];
    uint bHasDirectionalShadow;
    float3 ShadowPadding;
}

Texture2DArray DirectionalShadowMap : register(t12);
Texture2DArray SpotShadowMap : register(t13);
TextureCubeArray PointShadowMap : register(t14);
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
    return ShadowCoord.z > StoredDepth ? 0.0f : 1.0f;
}

float GetDirectionalShadow(float3 WorldPosition)
{
    if (bHasDirectionalShadow == 0u)
    {
        return 1.0f;
    }
    
    return CompareShadowDepth(DirectionalShadowMap, 0.0f, ProjectToShadowMap(WorldPosition, DirectionalShadowViewProjection));
}

float GetSpotShadow(int ShadowIndex, float3 WorldPosition)
{
    if (ShadowIndex < 0)
    {
        return 1.0f;
    }
   
    float4 Clip = mul(float4(WorldPosition, 1.0f), SpotShadowViewProjection[ShadowIndex]);
    if (Clip.w <= 0.0f)
    {
        return 1.0f;
    }
    return CompareShadowDepth(SpotShadowMap, (float)ShadowIndex, ClipToShadowCoord(Clip));
}

float GetPointShadow(int ShadowIndex, float3 WorldPosition, float3 LightPosition, float Radius)
{
    if (ShadowIndex < 0)
    {
        return 1.0f;
    }

    float3 LightToPixel = WorldPosition - LightPosition;
    float MyDistance = length(LightToPixel) / Radius;
    if (MyDistance > 1.0f)
    {
        return 1.0f;
    }
    
    float StoredDistance = PointShadowMap.SampleLevel(ShadowPointSampler, float4(LightToPixel, (float)ShadowIndex), 0.0f).r;
    return MyDistance > StoredDistance ? 0.0f : 1.0f;
}
#endif
