#pragma pack_matrix(row_major)

// EViewMode 순서와 일치해야 한다.
#define VIEWMODE_LIT          0
#define VIEWMODE_UNLIT        1
#define VIEWMODE_WORLDNORMAL  3

// ELightingModel 순서와 일치해야 한다.
#define LIGHTING_MODEL_LAMBERT      0
#define LIGHTING_MODEL_PHONG        1
#define LIGHTING_MODEL_BLINNPHONG   2

// EShadingModel 순서와 일치해야 한다.
#define SHADING_MODEL_GOURAUD  0
#define SHADING_MODEL_PHONG    1

// 기본값: Lit, Blinn-Phong, Phong
#ifndef VIEWMODE
#define VIEWMODE VIEWMODE_LIT
#endif

#ifndef LIGHTING_MODEL
#define LIGHTING_MODEL LIGHTING_MODEL_BLINNPHONG
#endif

#ifndef SHADING_MODEL
#define SHADING_MODEL SHADING_MODEL_PHONG
#endif

#ifndef HAS_NORMAL_MAP
#define HAS_NORMAL_MAP 0
#endif
#define USE_NORMAL_MAP (SHADING_MODEL == SHADING_MODEL_PHONG && HAS_NORMAL_MAP)

#define GOURAUD (VIEWMODE == VIEWMODE_LIT && SHADING_MODEL == SHADING_MODEL_GOURAUD)

#define MAX_POINT_LIGHTS 4
#define MAX_SPOT_LIGHTS 4

struct FAmbientLightInfo
{
    float3 Color;
    float Padding;
};

struct FDirectionalLightInfo
{
    float3 Color;
    float Padding0;
    float3 Direction;
    uint ShadowIndex;
};

struct FPointLightInfo
{
    float3 Color;
    float AttenuationRadius;
    float3 Position;
    uint ShadowIndex;
};

struct FSpotLightInfo
{
    float3 Color;
    float AttenuationRadius;
    float3 Position;
    float CosInnerCone;
    float3 Direction;
    float CosOuterCone;
    uint ShadowIndex;
    uint3 Padding;
};

struct FVSInput
{
    float3 Position : POSITION;
    float3 Normal : NORMAL;
    float4 Tangent : TANGENT;
    float4 Color : COLOR; // 지금은 쓰지 않지만 나중에 쓰게 될 수 있음
    float2 UV : TEXCOORD;
};

struct FPSInput
{
    float4 Position : SV_POSITION;
    float3 Normal : NORMAL;
    float2 UV : TEXCOORD0;
    float3 WorldPosition : TEXCOORD1;
    nointerpolation uint ReceiveShadow : TEXCOORD4;
#if USE_NORMAL_MAP
    float3 Tangent : TANGENT;
    int BitangentSign : BITANGENT_SIGN;
#endif
#if GOURAUD
    float3 DiffuseLight : TEXCOORD2;
    float3 SpecularLight : TEXCOORD3;
#endif
};

cbuffer ViewConstants : register(b0)
{
    matrix ViewProjection;
    float3 CameraPosition;
    float ViewPadding;
};

cbuffer StaticMeshMaterialParams : register(b1)
{
    float4 BaseColor;
    float2 UVScrollOffset;
    uint bOpaque;
    float Shininess;
    float3 SpecularColor;
    uint bFlipNormalGreen;
};

cbuffer PerObjectConstants : register(b2)
{
    matrix World;
    matrix NormalMatrix; // World의 역전치
    uint bReceiveShadow;
    uint3 ObjectPadding;
};

cbuffer LightingConstants : register(b3)
{
    FAmbientLightInfo AmbientLight;
    FDirectionalLightInfo DirectionalLight;
    FPointLightInfo PointLights[MAX_POINT_LIGHTS];
    FSpotLightInfo SpotLights[MAX_SPOT_LIGHTS];
    uint NumPointLights;
    uint NumSpotLights;
    uint bReadyForShading;
    uint NumShadowInfos;
};

cbuffer TileLightConstants : register(b4)
{
    uint TileCountX;
    uint TileCountY;
    uint TileSize;
    uint MaxLightsPerTile;
    uint StructuredPointLightCount;
    uint StructuredSpotLightCount;
    uint bVisualizeLightCount;
    uint bUseTileList;
};

StructuredBuffer<FPointLightInfo> StructuredPointLights : register(t8);
StructuredBuffer<FSpotLightInfo> StructuredSpotLights : register(t9);
Buffer<uint> TileLightCount : register(t10);
Buffer<uint> TileLightIndices : register(t11);

// CPU FShadowInfo와 동일한 112-byte StructuredBuffer 항목.
struct FShadowInfo
{
    row_major matrix ViewProjection;
    float3 LightPosition;
    float InvRadius;
    uint ShadowMapIndex;
    uint LightType;
    float Bias;
    float SlopeBias;
    float MaxSlope;
    float3 Padding;
};
StructuredBuffer<FShadowInfo> ShadowInfos : register(t12);
Texture2DArray<float> ShadowDepth2D : register(t13);
TextureCubeArray<float> ShadowDepthCube : register(t14);
SamplerState ShadowSampler : register(s1);

// 각 Light의 조회 구현에서 이 조건을 통과한 경우에만 ShadowInfos를 읽는다.
// 미구현 Light는 CPU에서 InvalidShadowIndex를 유지한다.
bool CanSampleShadow(uint ShadowIndex, uint ReceiveShadow)
{
#if VIEWMODE == VIEWMODE_LIT && SHADING_MODEL == SHADING_MODEL_PHONG
    return bReadyForShading != 0u && ReceiveShadow != 0u && ShadowIndex < NumShadowInfos;
#else
    return false;
#endif
}

float ComputeShadowBias(FShadowInfo Info, float3 GeoNormal, float3 ToLightDirection)
{
    float CosTheta = saturate(dot(normalize(GeoNormal), ToLightDirection));
    float TanTheta = sqrt(max(1.0f - CosTheta * CosTheta, 0.0f)) / max(CosTheta, 0.0001f);
    return Info.Bias + Info.SlopeBias * min(TanTheta, Info.MaxSlope);
}

// Directional은 Pass 1의 하드웨어 깊이와 같은 Orthographic NDC.z를 비교한다.
float ComputeDirectionalShadow(uint ShadowIndex, uint ReceiveShadow, float3 WorldPosition, float3 GeoNormal, float3 ToLightDirection)
{
    if (!CanSampleShadow(ShadowIndex, ReceiveShadow)) return 1.0f;
    FShadowInfo Info = ShadowInfos[ShadowIndex];
    float4 Clip = mul(float4(WorldPosition, 1.0f), Info.ViewProjection);
    float3 Ndc = Clip.xyz / Clip.w;
    if (any(abs(Ndc.xy) > 1.0f) || Ndc.z < 0.0f || Ndc.z > 1.0f) return 1.0f;
    float2 UV = float2(Ndc.x * 0.5f + 0.5f, -Ndc.y * 0.5f + 0.5f);
    float StoredDepth = ShadowDepth2D.SampleLevel(ShadowSampler, float3(UV, Info.ShadowMapIndex), 0);
    float Bias = ComputeShadowBias(Info, GeoNormal, normalize(ToLightDirection));
    return Ndc.z - Bias <= StoredDepth ? 1.0f : 0.0f;
}

// Spot은 UV만 투영 행렬로 구하고, 깊이는 Pass 1과 동일한 거리/반경으로 비교한다.
float ComputeSpotShadow(uint ShadowIndex, uint ReceiveShadow, float3 WorldPosition, float3 GeoNormal)
{
    if (!CanSampleShadow(ShadowIndex, ReceiveShadow)) return 1.0f;
    FShadowInfo Info = ShadowInfos[ShadowIndex];
    float4 Clip = mul(float4(WorldPosition, 1.0f), Info.ViewProjection);
    if (Clip.w <= 0.0f) return 1.0f;
    float3 Ndc = Clip.xyz / Clip.w;
    float3 ToLight = Info.LightPosition - WorldPosition;
    float Distance = length(ToLight);
    float ReceiverDepth = Distance * Info.InvRadius;
    // 범위는 Bias 적용 전에 검사한다. Near 밖/반경 밖의 점은 그림자를 조회하지 않는다.
    if (any(abs(Ndc.xy) > 1.0f) || Ndc.z < 0.0f || Ndc.z > 1.0f || ReceiverDepth >= 1.0f || Distance <= 0.0f) return 1.0f;
    float2 UV = float2(Ndc.x * 0.5f + 0.5f, -Ndc.y * 0.5f + 0.5f);
    float StoredDepth = ShadowDepth2D.SampleLevel(ShadowSampler, float3(UV, Info.ShadowMapIndex), 0);
    float Bias = ComputeShadowBias(Info, GeoNormal, ToLight / Distance);
    return ReceiverDepth - Bias <= StoredDepth ? 1.0f : 0.0f;
}

float ComputePointShadow(uint ShadowIndex, uint ReceiveShadow, float3 WorldPosition, float3 GeoNormal)
{
    if (!CanSampleShadow(ShadowIndex, ReceiveShadow)) return 1.0f;
    FShadowInfo Info = ShadowInfos[ShadowIndex];
    float3 FromLight = WorldPosition - Info.LightPosition;
    float Distance = length(FromLight);
    float ReceiverDepth = Distance * Info.InvRadius;
    if (Distance <= 0.0f || ReceiverDepth >= 1.0f) return 1.0f;
    // GPU가 방향으로 면과 UV를 선택한다. w는 물리 Slice가 아닌 Cube 번호다.
    float StoredDepth = ShadowDepthCube.SampleLevel(ShadowSampler, float4(FromLight, Info.ShadowMapIndex), 0);
    float Bias = ComputeShadowBias(Info, GeoNormal, -FromLight / Distance);
    return ReceiverDepth - Bias <= StoredDepth ? 1.0f : 0.0f;
}

Texture2D AlbedoTexture : register(t0);

#if USE_NORMAL_MAP
Texture2D NormalTexture : register(t1);
#endif

SamplerState AlbedoSampler : register(s0);

float3 DiffuseTerm(float3 Il, float3 N, float3 L)
{
    return Il * max(dot(N, L), 0.0f);
}

// Phong: A(R), B(V) Blinn-Phong: A(N), B(H)
float3 SpecularTerm(float3 Il, float3 A, float3 B, float n)
{
    return Il * pow(max(dot(A, B), 0.0f), n);
}

float ComputeDistanceAttenuation(float Distance, float Radius)
{
    return pow(saturate(1.0f - pow(Distance / Radius, 4)), 2);
}

void AccumulateLight(float3 Il, float3 N, float3 L, float3 V, float n, inout float3 DiffuseLight, inout float3 SpecularLight)
{
    float NdotL = max(dot(N, L), 0.0f);
    DiffuseLight += Il * NdotL; // Lambert
    if (NdotL > 0.0f)
    {
#if LIGHTING_MODEL == LIGHTING_MODEL_PHONG
        float3 R = 2.0f * dot(N, L) * N - L;
        // ★ NdotL을 곱해 빛이 스치는 어두운 곳에서는 스펙큘러가 0으로 부드럽게 사라지게 함
        SpecularLight += SpecularTerm(Il, R, V, n) * NdotL;
#elif LIGHTING_MODEL == LIGHTING_MODEL_BLINNPHONG
        float3 H = normalize(L + V);
        SpecularLight += SpecularTerm(Il, N, H, n) * NdotL;
#endif
    }
}

float3 GetLightCountColor(uint LightCount)
{
    if (LightCount == 0u)
        return float3(0.0f, 0.0f, 0.0f);
    if (LightCount <= 4u)
        return float3(0.0f, 0.0f, 1.0f);
    if (LightCount <= 16u)
        return float3(0.0f, 1.0f, 0.0f);
    if (LightCount <= 32u)
        return float3(1.0f, 1.0f, 0.0f);
    if (LightCount <= 64u)
        return float3(1.0f, 0.5f, 0.0f);
    return float3(1.0f, 0.0f, 0.0f);
}

// 일단 GOURAUD 전용
void ComputeLighting(float3 N, float3 WorldPosition, float3 V, float n, out float3 DiffuseLight, out float3 SpecularLight)
{
    DiffuseLight = 0.0f;
    SpecularLight = 0.0f;

    // Directional Light
    {
        float3 Il = DirectionalLight.Color;
        float3 L = -DirectionalLight.Direction;
        float GeoFactor = 1.0f;
        AccumulateLight(Il * GeoFactor, N, L, V, n, DiffuseLight, SpecularLight);
    }

    // 반투명 Gouraud도 opaque 타일 목록이나 고정 길이 상수 배열로 제한하지 않는다.
    // Point Light
    {
        for (uint Index = 0; Index < (bOpaque ? NumPointLights : StructuredPointLightCount); ++Index)
        {
            FPointLightInfo Light;
            if (bOpaque) Light = PointLights[Index];
            else Light = StructuredPointLights[Index];
            float3 ToLight = Light.Position - WorldPosition;
            float Distance = length(ToLight);
            float Radius = Light.AttenuationRadius;
            if (Distance >= Radius)
                continue;

            float3 L = ToLight / Distance;
            float GeoFactor = 1.0f;

            float DistanceAttenuation = ComputeDistanceAttenuation(Distance, Radius);
            float3 Il = Light.Color * DistanceAttenuation * GeoFactor;
            AccumulateLight(Il, N, L, V, n, DiffuseLight, SpecularLight);
        }
    }

    // Spot Light
    {
        for (uint Index = 0; Index < (bOpaque ? NumSpotLights : StructuredSpotLightCount); ++Index)
        {
            FSpotLightInfo Light;
            if (bOpaque) Light = SpotLights[Index];
            else Light = StructuredSpotLights[Index];
            float3 ToLight = Light.Position - WorldPosition;
            float Distance = length(ToLight);
            float Radius = Light.AttenuationRadius;
            if (Distance >= Radius)
                continue;
            float3 L = ToLight / Distance;
            float GeoFactor = 1.0f;

            float DistanceAttenuation = ComputeDistanceAttenuation(Distance, Radius);
            float CosAngle = dot(-L, Light.Direction);
            float SpotAttenuation = Light.CosInnerCone == Light.CosOuterCone
            ? (CosAngle > Light.CosOuterCone ? 1.0f : 0.0f)
            : saturate((CosAngle - Light.CosOuterCone) / (Light.CosInnerCone - Light.CosOuterCone));
            
            float3 Il = Light.Color * DistanceAttenuation * SpotAttenuation * GeoFactor;
            AccumulateLight(Il, N, L, V, n, DiffuseLight, SpecularLight);
        }
    }
}


void ComputePixelLighting(float3 N, float3 GeoNormal, float3 WorldPosition, float3 V, float n, float2 PixelPosition, uint ReceiveShadow, out float3 DiffuseLight, out float3 SpecularLight)
{
    DiffuseLight = 0.0f;
    SpecularLight = 0.0f;

    // 화면 전체에 영향을 주는 Directional Light는 타일 목록에 넣지 않음
    {
        float3 Il = DirectionalLight.Color;
        float3 L = -DirectionalLight.Direction;
#if USE_NORMAL_MAP
        float GeoFactor = saturate(dot(GeoNormal, L) * 4.0f);
#else
        float GeoFactor = 1.0f;
#endif
        float Shadow = ComputeDirectionalShadow(DirectionalLight.ShadowIndex, ReceiveShadow, WorldPosition, GeoNormal, L);
        AccumulateLight(Il * GeoFactor * Shadow, N, L, V, n, DiffuseLight, SpecularLight);
    }

    // Opaque 깊이 기반 타일 목록은 반투명에 필요한 라이트를 누락할 수 있어
    // 반투명에서는 전체 Point/Spot 배열을 순회한다.
    // TODO: 향후 반투명 깊이에서도 유효한 clustered light list를 구성하면
    // 해당 클러스터의 라이트만 순회하도록 개선할 수 있다.
    const bool UseTileList = bOpaque != 0u && bUseTileList != 0u;
    uint TileIndex = 0;
    uint LightCount = StructuredPointLightCount + StructuredSpotLightCount;
    if (UseTileList)
    {
        uint2 TileCoord = min(uint2(PixelPosition) / TileSize, uint2(TileCountX - 1u, TileCountY - 1u));
        TileIndex = TileCoord.y * TileCountX + TileCoord.x;
        LightCount = min(TileLightCount[TileIndex], MaxLightsPerTile);
    }
    for (uint ListIndex = 0; ListIndex < LightCount; ++ListIndex)
    {
        uint LightIndex = UseTileList ? TileLightIndices[TileIndex * MaxLightsPerTile + ListIndex] : ListIndex;
        if (LightIndex < StructuredPointLightCount)
        {
            FPointLightInfo Light = StructuredPointLights[LightIndex];
            float3 ToLight = Light.Position - WorldPosition;
            float Distance = length(ToLight);
            float Radius = Light.AttenuationRadius;
            if (Distance <= 0.0f || Distance >= Radius)
                continue;

            float3 L = ToLight / Distance;
#if USE_NORMAL_MAP
            float GeoFactor = saturate(dot(GeoNormal, L) * 4.0f);
            if (GeoFactor <= 0.0f)
                continue;
#else
            float GeoFactor = 1.0f;
#endif

            float DistanceAttenuation = ComputeDistanceAttenuation(Distance, Radius);
            float Shadow = ComputePointShadow(Light.ShadowIndex, ReceiveShadow, WorldPosition, GeoNormal);
            float3 Il = Light.Color * DistanceAttenuation * GeoFactor * Shadow;
            AccumulateLight(Il, N, L, V, n, DiffuseLight, SpecularLight);

        }
        else
        {
            LightIndex -= StructuredPointLightCount;
            if (LightIndex >= StructuredSpotLightCount) continue;
            FSpotLightInfo Light = StructuredSpotLights[LightIndex];
            float3 ToLight = Light.Position - WorldPosition;
            float Distance = length(ToLight);
            float Radius = Light.AttenuationRadius;
            if (Distance <= 0.0f || Distance >= Radius)
                continue;

            float3 L = ToLight / Distance;
#if USE_NORMAL_MAP
            float GeoFactor = saturate(dot(GeoNormal, L) * 4.0f);
            if (GeoFactor <= 0.0f)
                continue;
#else
            float GeoFactor = 1.0f;
#endif

            float DistanceAttenuation = ComputeDistanceAttenuation(Distance, Radius);
            float CosAngle = dot(-L, normalize(Light.Direction));
            float SpotAttenuation = Light.CosInnerCone == Light.CosOuterCone
                ? (CosAngle > Light.CosOuterCone ? 1.0f : 0.0f)
                : saturate((CosAngle - Light.CosOuterCone) / (Light.CosInnerCone - Light.CosOuterCone));

            float Shadow = ComputeSpotShadow(Light.ShadowIndex, ReceiveShadow, WorldPosition, GeoNormal);
            float3 Il = Light.Color * DistanceAttenuation * SpotAttenuation * GeoFactor * Shadow;
            AccumulateLight(Il, N, L, V, n, DiffuseLight, SpecularLight);

        }
    }
}

FPSInput mainVS(FVSInput Input)
{
    FPSInput Output;

    float4 WorldPosition = mul(float4(Input.Position, 1.0f), World);

    Output.Position = mul(WorldPosition, ViewProjection);
    Output.WorldPosition = WorldPosition.xyz;
    Output.Normal = mul(Input.Normal, (float3x3) NormalMatrix);
#if USE_NORMAL_MAP
    Output.Tangent = mul(Input.Tangent.xyz, (float3x3) World);
    Output.BitangentSign = Input.Tangent.w;
#endif
    Output.UV = Input.UV;
    Output.ReceiveShadow = bReceiveShadow;
#if GOURAUD
    float3 N = normalize(Output.Normal);
    float3 V = normalize(CameraPosition - Output.WorldPosition);
    // TODO
    ComputeLighting(N, Output.WorldPosition, V, Shininess, Output.DiffuseLight, Output.SpecularLight);
    
#endif
    return Output;
}

float4 mainPS(FPSInput Input) : SV_TARGET
{
    float4 FinalColor = float4(0.0f, 0.0f, 0.0f, 1.0f);

    float4 TextureColor = AlbedoTexture.Sample(AlbedoSampler, Input.UV + UVScrollOffset);
    float4 SurfaceColor = TextureColor * BaseColor;

    float3 Albedo = SurfaceColor.rgb;
    float Alpha = bOpaque ? 1.0f : SurfaceColor.a;

    float3 Kd = Albedo;
    float3 Ka = Kd; // 나중에 Ka 를 개별로 설정할 수 있음
    float3 Ks = SpecularColor;
    
    float3 Normal = normalize(Input.Normal);
    
#if USE_NORMAL_MAP
    // 그람 슈미트 직교화
    float3 Tangent = normalize(Input.Tangent - dot(Input.Tangent, Normal) * Normal);
    float3 BiTangent = cross(Normal, Tangent) * Input.BitangentSign;
    float3x3 TBN = float3x3(Tangent, BiTangent, Normal);
    // Texture Normal 매핑
    float3 NormalSample = NormalTexture.Sample(AlbedoSampler, Input.UV + UVScrollOffset).xyz;
    float3 TextureNormal = normalize(NormalSample * 2.0f - 1.0f);
    TextureNormal.y = bFlipNormalGreen ? -TextureNormal.y : TextureNormal.y;
    // TBN 행렬을 이용하여 월드 공간으로 변환
    float3 WorldNormal = mul(TextureNormal, TBN);
#else
    float3 WorldNormal = Normal;
#endif
   
#if VIEWMODE == VIEWMODE_UNLIT
    FinalColor = float4(Albedo, Alpha);
#elif VIEWMODE == VIEWMODE_WORLDNORMAL
    // 색이 아니라 데이터이므로 Gamma Encode Pass에서 인코딩하지 않는다.
    FinalColor = float4(normalize(WorldNormal) * 0.5f + 0.5f, 1.0f);
#else // Lit
    if (bVisualizeLightCount != 0u)
    {
        uint2 PixelCoord = uint2(Input.Position.xy);
        uint2 TileCoord = min(PixelCoord / TileSize, uint2(TileCountX - 1u, TileCountY - 1u));
        uint TileIndex = TileCoord.y * TileCountX + TileCoord.x;
        
        float3 Color = GetLightCountColor(TileLightCount[TileIndex]);
        uint2 LocalCoord = PixelCoord % TileSize;

        if (LocalCoord.x == 0u || LocalCoord.y == 0u)
        {
            Color *= 0.2f;
        }

        return float4(Color, 1.0f);
    }
    
    float3 DiffuseLight;
    float3 SpecularLight;
#if SHADING_MODEL == SHADING_MODEL_GOURAUD
    DiffuseLight = Input.DiffuseLight;
    SpecularLight = Input.SpecularLight;
#else // Phong
    float3 N = normalize(WorldNormal);
    float3 V = normalize(CameraPosition - Input.WorldPosition);
    
    ComputePixelLighting(N, Normal, Input.WorldPosition, V, Shininess, Input.Position.xy, Input.ReceiveShadow, DiffuseLight, SpecularLight);
    
#endif
    FinalColor = float4(Ka * AmbientLight.Color + Kd * DiffuseLight + Ks * SpecularLight, Alpha);
#endif
    return FinalColor;
}
