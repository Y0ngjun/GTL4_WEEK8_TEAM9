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

// CPU FShadowInfo와 동일한 144-byte StructuredBuffer 항목.
struct FShadowInfo
{
    row_major matrix ViewProjection;
    float3 LightPosition;
    float InvRadius;      // 월드 거리 -> 비교 깊이. Spot/Point: 1/반경, Directional: 1/(Far-Near)
    uint MapCount;
    uint LightType;
    float Bias;
    float SlopeBias;      // 무차원 k. k = 1이면 (T/2) * tan(theta), 이론상 최소값
    float MaxSlope;
    float HalfTexelScale; // 월드 T/2. Directional: 그대로, Spot/Point: 라이트 거리를 곱한다
    float NormalOffset;   // 무차원 k. k = 1이면 (T/2) * sin(theta), 이론상 최소값
    float PenumbraScale;  // PCSS 반영 폭 계수. 결과는 Map 한 장 기준 로컬 UV (C++ FShadowInfo 주석 참고)
    float4 AtlasUVScaleBias;
    float PCSSNearDepth;  // Spot/Point 차폐물 탐색용 정규화 Near 깊이
    uint ShadowFilterMode; // Light의 ShadowFilterMode (SHADOW_FILTER_* 참고)
    float2 PCSSPadding;
};
cbuffer DirectionalShadowConstants : register(b5)
{
    uint bUseCSM;
    uint CascadeCount;
    uint2 DirectionalShadowPadding;
    float3 CascadeViewOrigin;
    float CascadeOriginPadding;
    float3 CascadeViewForward;
    float CascadeForwardPadding;
    float4 CascadeDepthRanges[8]; // xy=선택 near/far, z=혼합 폭, w=padding
};
StructuredBuffer<FShadowInfo> ShadowInfos : register(t12);
Texture2D<float> LocalShadowAtlas : register(t13);
Texture2D<float> DirectionalShadowAtlas : register(t14);
StructuredBuffer<FShadowInfo> DirectionalShadowInfos : register(t15);
SamplerState ShadowSampler : register(s1);

// PCF용 비교 샘플러
SamplerComparisonState ShadowComparisonSampler : register(s2);

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

// 텍셀 중심과 픽셀의 거리는 최대 T/2이고, 그 사이 깊이 차는 (T/2) * tan(theta)다.
// HalfTexelWorld는 월드 T/2이며, InvRadius로 비교 깊이 단위로 바꾼다.
float ComputeShadowBias(FShadowInfo Info, float CosTheta, float HalfTexelWorld)
{
    float TanTheta = sqrt(max(1.0f - CosTheta * CosTheta, 0.0f)) / max(CosTheta, 0.0001f);
    float SlopeBiasWorld = Info.SlopeBias * HalfTexelWorld * min(TanTheta, Info.MaxSlope);
    return Info.Bias + SlopeBiasWorld * Info.InvRadius;
}

// 법선 방향으로 d만큼 띄우면 L 방향 깊이 여유가 d / cos(theta) 생긴다.
// d = (T/2) * sin(theta)이면 그 여유가 (T/2) * tan(theta)와 같아지므로 k = 1이 이론상 최소값이다.
// 깊이만 빼는 Bias와 달리 조회 위치가 바뀌므로 UV와 깊이 모두 반환한 위치로 계산한다.
float3 ApplyNormalOffset(FShadowInfo Info, float3 WorldPosition, float3 N, float CosTheta, float HalfTexelWorld)
{
    float SinTheta = sqrt(max(1.0f - CosTheta * CosTheta, 0.0f));
    return WorldPosition + N * (Info.NormalOffset * HalfTexelWorld * SinTheta);
}

// 거리나 NDC.z가 아닌, 분할 생성과 같은 RenderLocation 기준 전방 깊이로 선택한다.
uint SelectDirectionalShadowInfo(uint ShadowIndex, float3 WorldPosition)
{
    if (ShadowIndex == 0xffffffffu || bUseCSM == 0u) return ShadowIndex;
    float Depth = dot(WorldPosition - CascadeViewOrigin, CascadeViewForward);
    for (uint Index = 0u; Index < CascadeCount; ++Index)
    {
        // 공유 경계에서는 먼저 검사하는 앞쪽 Cascade를 선택한다.
        if (Depth >= CascadeDepthRanges[Index].x && Depth <= CascadeDepthRanges[Index].y)
            return ShadowIndex + Index;
    }
    return 0xffffffffu;
}

// 호출자가 Atlas를 명시하여 Directional과 Spot/Point의 조회 대상을 분리한다.
float SampleShadowAtlasDepth(Texture2D<float> Atlas, FShadowInfo Info, float2 LocalUV)
{
    uint Width, Height;
    Atlas.GetDimensions(Width, Height);
    float2 HalfTexel = 0.5f / float2(Width, Height);
    float2 Scale = Info.AtlasUVScaleBias.xy;
    float2 Offset = Info.AtlasUVScaleBias.zw;
    // 다른 타일을 읽지 않도록 유효 Texel 중심으로 제한한다.
    float2 UV = clamp(LocalUV * Scale + Offset,
        Offset + HalfTexel, Offset + Scale - HalfTexel);
    return Atlas.SampleLevel(ShadowSampler, UV, 0);
}

// SampleShadowAtlasDepth와 같은 타일 제한으로 비교 샘플러(2x2 비교 + bilinear)를 쓴다.
// 유효 Texel 중심 안쪽으로 제한하면 2x2의 바깥 텍셀 가중치가 0이 되어 옆 타일이 섞이지 않는다.
float SampleShadowAtlasCompare(Texture2D<float> Atlas, FShadowInfo Info, float2 LocalUV, float CompareDepth)
{
    uint Width, Height;
    Atlas.GetDimensions(Width, Height);
    float2 HalfTexel = 0.5f / float2(Width, Height);
    float2 Scale = Info.AtlasUVScaleBias.xy;
    float2 Offset = Info.AtlasUVScaleBias.zw;
    float2 UV = clamp(LocalUV * Scale + Offset,
        Offset + HalfTexel, Offset + Scale - HalfTexel);
    return Atlas.SampleCmpLevelZero(ShadowComparisonSampler, UV, CompareDepth);
}

// Map 한 장(아틀라스 타일) 기준 로컬 UV에서 1텍셀의 크기.
float GetShadowMapTexelUV(Texture2D<float> Atlas, FShadowInfo Info)
{
    uint Width, Height;
    Atlas.GetDimensions(Width, Height);
    return 1.0f / max((float)Width * Info.AtlasUVScaleBias.x, 1.0f);
}

// ---------------------------------------------------------------------------------
// PCSS 공통. 1) 차폐물 탐색 2) 반영 폭 계산 3) 그 반경으로 PCF.
// 모든 오프셋은 Map 한 장 기준 로컬 UV이고, 아틀라스 조회는 위의 두 함수가 타일 안으로 제한한다.
// ---------------------------------------------------------------------------------

// Light의 ShadowFilterMode 값. C++ ULightComponent::ShadowFilterMode와 같은 순서다.
#define SHADOW_FILTER_PCF2X2_HARDWARE   0u
#define SHADOW_FILTER_PCF3X3_POINT      1u
#define SHADOW_FILTER_PCF5X5_POINT      2u
#define SHADOW_FILTER_PCF5X5_COMPARISON 3u
#define SHADOW_FILTER_PCSS              4u

// 커널 최대 반경은 탐색 반경보다 작게 유지한다.
#define PCSS_BLOCKER_SAMPLES   16
#define PCSS_PCF_SAMPLES       16
#define PCSS_MAX_SEARCH_TEXELS 12.0f
#define PCSS_MIN_KERNEL_TEXELS 1.0f
#define PCSS_MAX_KERNEL_TEXELS 8.0f

// 반지름 1 원 안의 Poisson disk 16점 (점 사이 최소 거리 약 0.41).
static const float2 PCSSPoissonDisk[16] =
{
    float2( 0.3125f, -0.1937f), float2(-0.8091f, -0.5209f),
    float2(-0.3314f, -0.3645f), float2(-0.5950f,  0.1167f),
    float2( 0.6196f,  0.1214f), float2(-0.3294f,  0.7294f),
    float2( 0.2102f,  0.3908f), float2( 0.4850f, -0.8015f),
    float2( 0.7012f,  0.5625f), float2(-0.2550f, -0.9261f),
    float2( 0.0626f,  0.9386f), float2( 0.8832f, -0.3572f),
    float2(-0.8313f,  0.4664f), float2(-0.1259f,  0.0427f),
    float2( 0.0987f, -0.6498f), float2(-0.9792f, -0.0342f)
};

float2 RotatePCSSSample(float2 Sample, float2 CosSin)
{
    return float2(Sample.x * CosSin.x - Sample.y * CosSin.y, Sample.x * CosSin.y + Sample.y * CosSin.x);
}

// 픽셀마다 샘플 패턴을 회전시켜 줄무늬 대신 노이즈가 되게 한다. 월드 위치 기반이라 카메라가 움직여도 고정된다.
float2 GetPCSSRotation(float3 WorldPosition)
{
    float Angle = 6.2831853f * frac(sin(dot(WorldPosition, float3(12.9898f, 78.233f, 37.719f))) * 43758.5453f);
    return float2(cos(Angle), sin(Angle));
}

// Spot/Point는 광원이 유한한 거리에 있어 반영 폭 = 광원 반지름 * (dR - dB) / dB이다.
// 깊이가 거리 / 감쇠 반경(선형)이라 정규화 깊이 그대로 비율을 구하고, 단위 환산은 PenumbraScale에 들어 있다.
// BlockerDepth에 PCSSNearDepth를 넣으면 가장 먼 차폐물 기준의 최대 탐색 반경이 된다.
float ComputeLocalPCSSRadius(FShadowInfo Info, float ReceiverDepth, float BlockerDepth)
{
    return Info.PenumbraScale * (ReceiverDepth - BlockerDepth) / (max(BlockerDepth, Info.PCSSNearDepth) * ReceiverDepth);
}

// 한 Map(타일)에서 차폐물 탐색 → 커널 반경 → PCF. bLocal은 반경 공식을 고른다.
// Directional(false): 반경 = (zR - zB) * PenumbraScale, Spot/Point(true): ComputeLocalPCSSRadius.
float FilterShadowPCSS(Texture2D<float> Atlas, FShadowInfo Info, float2 UV, float ReceiverDepth, float CompareDepth, float2 CosSin, bool bLocal)
{
    float TexelUV = GetShadowMapTexelUV(Atlas, Info);

    // 1) 차폐물 탐색: depth 값 자체가 필요하므로 point 샘플러로 읽는다.
    // 가장 먼 차폐물 기준 최대 반경. Directional은 near(깊이 0), Spot/Point는 PCSSNearDepth.
    float SearchRaw = bLocal ? ComputeLocalPCSSRadius(Info, ReceiverDepth, Info.PCSSNearDepth) : ReceiverDepth * Info.PenumbraScale;
    float SearchUV = clamp(SearchRaw, PCSS_MIN_KERNEL_TEXELS * TexelUV, PCSS_MAX_SEARCH_TEXELS * TexelUV);
    float BlockerSum = 0.0f;
    uint BlockerCount = 0;
    [unroll]
    for (uint i = 0; i < PCSS_BLOCKER_SAMPLES; ++i)
    {
        float2 Offset = RotatePCSSSample(PCSSPoissonDisk[i], CosSin) * SearchUV;
        float SampleDepth = SampleShadowAtlasDepth(Atlas, Info, UV + Offset);
        if (SampleDepth < CompareDepth)
        {
            BlockerSum += SampleDepth;
            ++BlockerCount;
        }
    }
    if (BlockerCount == 0) return 1.0f;
    if (BlockerCount == PCSS_BLOCKER_SAMPLES) return 0.0f;
    float AvgBlockerDepth = BlockerSum / (float)BlockerCount;

    // 2) 반영 폭: 픽셀마다 다른 깊이 차 * 라이트(Map)마다 고정인 PenumbraScale. 텍셀 단위로 제한한다.
    float KernelRaw = bLocal ? ComputeLocalPCSSRadius(Info, ReceiverDepth, AvgBlockerDepth) : (ReceiverDepth - AvgBlockerDepth) * Info.PenumbraScale;
    float KernelUV = clamp(KernelRaw, PCSS_MIN_KERNEL_TEXELS * TexelUV, PCSS_MAX_KERNEL_TEXELS * TexelUV);

    // 3) 가변 반경 PCF: 각 샘플은 하드웨어 2x2 비교 + bilinear.
    float Lit = 0.0f;
    [unroll]
    for (uint j = 0; j < PCSS_PCF_SAMPLES; ++j)
    {
        float2 Offset = RotatePCSSSample(PCSSPoissonDisk[j], CosSin) * KernelUV;
        Lit += SampleShadowAtlasCompare(Atlas, Info, UV + Offset, CompareDepth);
    }
    return Lit / (float)PCSS_PCF_SAMPLES;
}

// 수동 PCF. point 샘플러로 텍셀 깊이를 그대로 읽어 셰이더가 직접 비교(0/1)하고 단순 평균한다.
// 탭은 1텍셀 간격 격자다. Radius 1 = 3x3, 2 = 5x5. 탭마다 텍셀 하나만 보므로 결과가 1/탭 수 단위 계단으로 바뀐다.
float FilterShadowPointGrid(Texture2D<float> Atlas, FShadowInfo Info, float2 UV, float CompareDepth, int Radius)
{
    float TexelUV = GetShadowMapTexelUV(Atlas, Info);
    float Lit = 0.0f;
    for (int y = -Radius; y <= Radius; ++y)
    {
        for (int x = -Radius; x <= Radius; ++x)
        {
            float SampleDepth = SampleShadowAtlasDepth(Atlas, Info, UV + float2(x, y) * TexelUV);
            Lit += CompareDepth <= SampleDepth ? 1.0f : 0.0f;
        }
    }
    float Side = (float)(2 * Radius + 1);
    return Lit / (Side * Side);
}

// 하드웨어 PCF 격자. 탭마다 비교 샘플러가 2x2 비교 + bilinear를 하고 단순 평균한다.
// 탭은 1텍셀 간격이라 서로 겹친다. Radius 2 = 5x5 탭 (텍셀 6x6 영역).
float FilterShadowComparisonGrid(Texture2D<float> Atlas, FShadowInfo Info, float2 UV, float CompareDepth, int Radius)
{
    float TexelUV = GetShadowMapTexelUV(Atlas, Info);
    float Lit = 0.0f;
    for (int y = -Radius; y <= Radius; ++y)
    {
        for (int x = -Radius; x <= Radius; ++x)
            Lit += SampleShadowAtlasCompare(Atlas, Info, UV + float2(x, y) * TexelUV, CompareDepth);
    }
    float Side = (float)(2 * Radius + 1);
    return Lit / (Side * Side);
}

// 모든 Light가 조회 위치(UV)와 비교 깊이를 구한 뒤 이 함수로 필터링한다. 모드는 Light마다 같으므로 분기가 픽셀 사이에서 거의 갈리지 않는다.
// bLocal: PCSS 반경 공식 선택 (Directional = false, Spot/Point = true).
float FilterShadowByMode(Texture2D<float> Atlas, FShadowInfo Info, float2 UV, float ReceiverDepth, float CompareDepth, float3 WorldPosition, bool bLocal)
{
    uint Mode = Info.ShadowFilterMode;
    float Shadow = 1.0f;
    [branch] if (Mode == SHADOW_FILTER_PCSS)
        Shadow = FilterShadowPCSS(Atlas, Info, UV, ReceiverDepth, CompareDepth, GetPCSSRotation(WorldPosition), bLocal);
    else if (Mode == SHADOW_FILTER_PCF5X5_COMPARISON)
        Shadow = FilterShadowComparisonGrid(Atlas, Info, UV, CompareDepth, 2);
    else if (Mode == SHADOW_FILTER_PCF5X5_POINT)
        Shadow = FilterShadowPointGrid(Atlas, Info, UV, CompareDepth, 2);
    else if (Mode == SHADOW_FILTER_PCF3X3_POINT)
        Shadow = FilterShadowPointGrid(Atlas, Info, UV, CompareDepth, 1);
    else
        Shadow = SampleShadowAtlasCompare(Atlas, Info, UV, CompareDepth);
    return Shadow;
}

uint SelectPointShadowFace(float3 Direction)
{
    float3 AbsDirection = abs(Direction);
    if (AbsDirection.x >= AbsDirection.y && AbsDirection.x >= AbsDirection.z)
        return Direction.x >= 0.0f ? 0u : 1u;
    if (AbsDirection.y >= AbsDirection.z)
        return Direction.y >= 0.0f ? 2u : 3u;
    return Direction.z >= 0.0f ? 4u : 5u;
}

// Directional은 Pass 1의 하드웨어 깊이와 같은 Orthographic NDC.z를 비교한다.
float SampleDirectionalShadow(uint InfoIndex, float3 WorldPosition, float3 GeoNormal, float3 ToLightDirection)
{
    FShadowInfo Info = DirectionalShadowInfos[InfoIndex];
    float3 N = normalize(GeoNormal);
    float CosTheta = saturate(dot(N, normalize(ToLightDirection)));

    // Orthographic은 텍셀 크기가 맵 전체에서 일정하다.
    float HalfTexelWorld = Info.HalfTexelScale;
    float3 ShadowPosition = ApplyNormalOffset(Info, WorldPosition, N, CosTheta, HalfTexelWorld);
    float4 Clip = mul(float4(ShadowPosition, 1.0f), Info.ViewProjection);
    float3 Ndc = Clip.xyz / Clip.w;
    if (any(abs(Ndc.xy) > 1.0f) || Ndc.z < 0.0f || Ndc.z > 1.0f) return 1.0f;

    float2 UV = float2(Ndc.x * 0.5f + 0.5f, -Ndc.y * 0.5f + 0.5f);
    float Bias = ComputeShadowBias(Info, CosTheta, HalfTexelWorld);
    return SampleShadowAtlasCompare(DirectionalShadowAtlas, Info, UV, Ndc.z - Bias);
}

// Directional 조회 + Light의 필터 모드. 직교 투영이라 NDC.z 차이가 월드 거리에 비례하므로 그대로 쓴다. Cascade마다 PenumbraScale이 다르다.
float SampleDirectionalShadowFiltered(uint InfoIndex, float3 WorldPosition, float3 GeoNormal, float3 ToLightDirection)
{
    FShadowInfo Info = DirectionalShadowInfos[InfoIndex];
    float3 N = normalize(GeoNormal);
    float CosTheta = saturate(dot(N, normalize(ToLightDirection)));
    float HalfTexelWorld = Info.HalfTexelScale;
    float3 ShadowPosition = ApplyNormalOffset(Info, WorldPosition, N, CosTheta, HalfTexelWorld);
    float4 Clip = mul(float4(ShadowPosition, 1.0f), Info.ViewProjection);
    float3 Ndc = Clip.xyz / Clip.w;
    if (any(abs(Ndc.xy) > 1.0f) || Ndc.z < 0.0f || Ndc.z > 1.0f) return 1.0f;
    float2 UV = float2(Ndc.x * 0.5f + 0.5f, -Ndc.y * 0.5f + 0.5f);
    float CompareDepth = Ndc.z - ComputeShadowBias(Info, CosTheta, HalfTexelWorld);
    return FilterShadowByMode(DirectionalShadowAtlas, Info, UV, Ndc.z, CompareDepth, WorldPosition, false);
}

float ComputeDirectionalShadow(uint ShadowIndex, uint ReceiveShadow, float3 WorldPosition, float3 GeoNormal, float3 ToLightDirection)
{
#if VIEWMODE == VIEWMODE_LIT && SHADING_MODEL == SHADING_MODEL_PHONG
    // CPU는 이 View의 Directional 바인딩이 준비된 경우에만 유효 번호를 전달한다.
    if (ReceiveShadow == 0u || ShadowIndex == 0xffffffffu) return 1.0f;
    uint InfoIndex = SelectDirectionalShadowInfo(ShadowIndex, WorldPosition);
    if (InfoIndex == 0xffffffffu) return 1.0f;
    float Shadow = SampleDirectionalShadowFiltered(InfoIndex, WorldPosition, GeoNormal, ToLightDirection);
    uint CascadeIndex = InfoIndex - ShadowIndex;
    if (bUseCSM != 0u && CascadeIndex + 1u < CascadeCount)
    {
        float FarDepth = CascadeDepthRanges[CascadeIndex].y;
        float BlendWidth = CascadeDepthRanges[CascadeIndex].z;
        float Depth = dot(WorldPosition - CascadeViewOrigin, CascadeViewForward);
        [branch]
        if (BlendWidth > 0.0f && Depth >= FarDepth - BlendWidth)
        {
            float Weight = saturate((Depth - (FarDepth - BlendWidth)) / BlendWidth);
            float NextShadow = SampleDirectionalShadowFiltered(InfoIndex + 1u, WorldPosition, GeoNormal, ToLightDirection);
            // 깊이 자체가 아니라 각 행렬·bias로 비교한 그림자 판정값을 혼합한다.
            Shadow = lerp(Shadow, NextShadow, Weight);
        }
    }
    return Shadow;
#else
    return 1.0f;
#endif
}

// Spot은 UV만 투영 행렬로 구하고, 깊이는 Pass 1과 동일한 거리/반경으로 비교한다.
float ComputeSpotShadow(uint ShadowIndex, uint ReceiveShadow, float3 WorldPosition, float3 GeoNormal)
{
    if (!CanSampleShadow(ShadowIndex, ReceiveShadow)) return 1.0f;
    FShadowInfo Info = ShadowInfos[ShadowIndex];
    float3 ToLight = Info.LightPosition - WorldPosition;
    float Distance = length(ToLight);
    if (Distance <= 0.0f) return 1.0f;
    float3 N = normalize(GeoNormal);
    float CosTheta = saturate(dot(N, ToLight / Distance));
    // Perspective는 텍셀 크기가 라이트 거리에 비례한다.
    float HalfTexelWorld = Info.HalfTexelScale * Distance;
    float3 ShadowPosition = ApplyNormalOffset(Info, WorldPosition, N, CosTheta, HalfTexelWorld);
    float4 Clip = mul(float4(ShadowPosition, 1.0f), Info.ViewProjection);
    if (Clip.w <= 0.0f) return 1.0f;
    float3 Ndc = Clip.xyz / Clip.w;
    float ReceiverDepth = length(ShadowPosition - Info.LightPosition) * Info.InvRadius;
    // 범위는 Bias 적용 전에 검사한다. Near 밖/반경 밖의 점은 그림자를 조회하지 않는다.
    if (any(abs(Ndc.xy) > 1.0f) || Ndc.z < 0.0f || Ndc.z > 1.0f || ReceiverDepth >= 1.0f) return 1.0f;
    float2 UV = float2(Ndc.x * 0.5f + 0.5f, -Ndc.y * 0.5f + 0.5f);
    float StoredDepth = SampleShadowAtlasDepth(LocalShadowAtlas, Info, UV);
    float Bias = ComputeShadowBias(Info, CosTheta, HalfTexelWorld);
    return ReceiverDepth - Bias <= StoredDepth ? 1.0f : 0.0f;
}

float ComputePointShadow(uint ShadowIndex, uint ReceiveShadow, float3 WorldPosition, float3 GeoNormal)
{
    if (!CanSampleShadow(ShadowIndex, ReceiveShadow)) return 1.0f;
    FShadowInfo Info = ShadowInfos[ShadowIndex];
    float3 ToLight = Info.LightPosition - WorldPosition;
    float Distance = length(ToLight);
    if (Distance <= 0.0f) return 1.0f;
    float3 N = normalize(GeoNormal);
    float CosTheta = saturate(dot(N, ToLight / Distance));
    float HalfTexelWorld = Info.HalfTexelScale * Distance;
    float3 ShadowPosition = ApplyNormalOffset(Info, WorldPosition, N, CosTheta, HalfTexelWorld);
    float3 FromLight = ShadowPosition - Info.LightPosition;
    float ReceiverDepth = length(FromLight) * Info.InvRadius;
    if (ReceiverDepth <= 0.0f || ReceiverDepth >= 1.0f) return 1.0f;
    // Normal Offset이 적용된 방향으로 면과 UV를 함께 선택한다.
    uint Face = SelectPointShadowFace(FromLight);
    if (Face >= Info.MapCount || Face >= NumShadowInfos - ShadowIndex) return 1.0f;
    FShadowInfo FaceInfo = ShadowInfos[ShadowIndex + Face];
    float4 Clip = mul(float4(ShadowPosition, 1.0f), FaceInfo.ViewProjection);
    if (Clip.w <= 0.0f) return 1.0f;
    float3 Ndc = Clip.xyz / Clip.w;
    if (any(abs(Ndc.xy) > 1.0f) || Ndc.z < 0.0f || Ndc.z > 1.0f) return 1.0f;
    float2 UV = float2(Ndc.x * 0.5f + 0.5f, -Ndc.y * 0.5f + 0.5f);
    float StoredDepth = SampleShadowAtlasDepth(LocalShadowAtlas, FaceInfo, UV);
    float Bias = ComputeShadowBias(Info, CosTheta, HalfTexelWorld);
    return ReceiverDepth - Bias <= StoredDepth ? 1.0f : 0.0f;
}

// Spot 조회 + Light의 필터 모드. 조회 위치와 깊이는 ComputeSpotShadow와 같다.
float ComputeSpotShadowFiltered(uint ShadowIndex, uint ReceiveShadow, float3 WorldPosition, float3 GeoNormal)
{
    if (!CanSampleShadow(ShadowIndex, ReceiveShadow)) return 1.0f;
    FShadowInfo Info = ShadowInfos[ShadowIndex];
    float3 ToLight = Info.LightPosition - WorldPosition;
    float Distance = length(ToLight);
    if (Distance <= 0.0f) return 1.0f;
    float3 N = normalize(GeoNormal);
    float CosTheta = saturate(dot(N, ToLight / Distance));
    float HalfTexelWorld = Info.HalfTexelScale * Distance;
    float3 ShadowPosition = ApplyNormalOffset(Info, WorldPosition, N, CosTheta, HalfTexelWorld);
    float4 Clip = mul(float4(ShadowPosition, 1.0f), Info.ViewProjection);
    if (Clip.w <= 0.0f) return 1.0f;
    float3 Ndc = Clip.xyz / Clip.w;
    float ReceiverDepth = length(ShadowPosition - Info.LightPosition) * Info.InvRadius;
    if (any(abs(Ndc.xy) > 1.0f) || Ndc.z < 0.0f || Ndc.z > 1.0f || ReceiverDepth >= 1.0f) return 1.0f;
    float2 UV = float2(Ndc.x * 0.5f + 0.5f, -Ndc.y * 0.5f + 0.5f);
    float CompareDepth = ReceiverDepth - ComputeShadowBias(Info, CosTheta, HalfTexelWorld);
    return FilterShadowByMode(LocalShadowAtlas, Info, UV, ReceiverDepth, CompareDepth, WorldPosition, true);
}

// Point 조회 + Light의 필터 모드. 면 선택과 면별 UV는 ComputePointShadow와 같다. 각 면은 90도 원근 Map이라 Spot과 같은 식을 쓴다.
// 커널이 면 경계를 넘는 샘플은 그 면의 타일 가장자리로 제한된다(옆 면으로 이어 읽지 않음).
float ComputePointShadowFiltered(uint ShadowIndex, uint ReceiveShadow, float3 WorldPosition, float3 GeoNormal)
{
    if (!CanSampleShadow(ShadowIndex, ReceiveShadow)) return 1.0f;
    FShadowInfo Info = ShadowInfos[ShadowIndex];
    float3 ToLight = Info.LightPosition - WorldPosition;
    float Distance = length(ToLight);
    if (Distance <= 0.0f) return 1.0f;
    float3 N = normalize(GeoNormal);
    float CosTheta = saturate(dot(N, ToLight / Distance));
    float HalfTexelWorld = Info.HalfTexelScale * Distance;
    float3 ShadowPosition = ApplyNormalOffset(Info, WorldPosition, N, CosTheta, HalfTexelWorld);
    float3 FromLight = ShadowPosition - Info.LightPosition;
    float ReceiverDepth = length(FromLight) * Info.InvRadius;
    if (ReceiverDepth <= 0.0f || ReceiverDepth >= 1.0f) return 1.0f;
    uint Face = SelectPointShadowFace(FromLight);
    if (Face >= Info.MapCount || Face >= NumShadowInfos - ShadowIndex) return 1.0f;
    FShadowInfo FaceInfo = ShadowInfos[ShadowIndex + Face];
    float4 Clip = mul(float4(ShadowPosition, 1.0f), FaceInfo.ViewProjection);
    if (Clip.w <= 0.0f) return 1.0f;
    float3 Ndc = Clip.xyz / Clip.w;
    if (any(abs(Ndc.xy) > 1.0f) || Ndc.z < 0.0f || Ndc.z > 1.0f) return 1.0f;
    float2 UV = float2(Ndc.x * 0.5f + 0.5f, -Ndc.y * 0.5f + 0.5f);
    float CompareDepth = ReceiverDepth - ComputeShadowBias(Info, CosTheta, HalfTexelWorld);
    return FilterShadowByMode(LocalShadowAtlas, FaceInfo, UV, ReceiverDepth, CompareDepth, WorldPosition, true);
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
        // 빛을 등진 면은 기여가 0이므로 쉐도우 맵을 읽지 않는다.
        // HLSL의 ?:는 양쪽을 모두 계산할 수 있으므로 분기로 건너뛴다.
        float Shadow = 1.0f;
        [branch] if (dot(N, L) > 0.0f)
        {
            // Cascade 선택·혼합 안에서 Light의 ShadowFilterMode로 필터를 고른다.
            Shadow = ComputeDirectionalShadow(DirectionalLight.ShadowIndex, ReceiveShadow, WorldPosition, GeoNormal, L);
        }
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
            float Shadow = 1.0f;
            [branch] if (dot(N, L) > 0.0f)
            {
                // Light의 ShadowFilterMode로 필터를 고른다.
                Shadow = ComputePointShadowFiltered(Light.ShadowIndex, ReceiveShadow, WorldPosition, GeoNormal);
            }
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

            // 콘 밖이거나 빛을 등진 면은 기여가 0이므로 쉐도우 맵을 읽지 않는다.
            float Shadow = 1.0f;
            [branch] if (SpotAttenuation > 0.0f && dot(N, L) > 0.0f)
            {
                // Light의 ShadowFilterMode로 필터를 고른다.
                Shadow = ComputeSpotShadowFiltered(Light.ShadowIndex, ReceiveShadow, WorldPosition, GeoNormal);
            }
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
