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

float GetDirectionalShadow(float3 WorldPosition)
{
    if (bHasDirectionalShadow == 0u)
    {
        return 1.0f;
    }
    
    float3 ShadowCoord = ProjectToShadowMap(WorldPosition, DirectionalShadowViewProjection);
    
    if (ShadowCoord.z > 1.0f)
    {
        return 1.0f;
    }

    float StoredDepth = DirectionalShadowMap.SampleLevel(ShadowPointSampler, float3(ShadowCoord.xy, 0.0f), 0.0f).r;
    
    // Bias -0.005f
    return ShadowCoord.z /*-0.005f*/ > StoredDepth ? 0.0f : 1.0f;
}
#endif
