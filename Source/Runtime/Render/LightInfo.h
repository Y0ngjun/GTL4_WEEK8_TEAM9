#pragma once

#include "Math/Vector.h"
#include "Math/Matrix.h"

// UberLit.hlsl의 MAX_POINT_LIGHTS, MAX_SPOT_LIGHTS
constexpr uint32 MaxPointLights = 4;
constexpr uint32 MaxSpotLights = 4;

// Linear
struct FAmbientLightInfo
{
	FVector Color;
	float Padding = 0.0f;
};

struct FDirectionalLightInfo
{
	FVector Color;
	float Padding0 = 0.0f;
	FVector Direction;
	float Padding1 = 0.0f;
};

struct FPointLightInfo
{
	FVector Color;
	float AttenuationRadius = 0.0f;
	FVector Position;
	float Padding = 0.0f;
	// 그림자: FPointLightShadowMapArray의 큐브 번호(-1이면 없음)와 6면 공통 원근 투영의 깊이 계수.
	// 셰이더는 방향 벡터의 가장 큰 성분 z로 depth = ZScale + ZOffset / z를 만들어 큐브 값과 비교한다.
	int32 ShadowSlot = -1;
	float ShadowZScale = 1.0f;
	float ShadowZOffset = 0.0f;
	float ShadowPadding = 0.0f;
};

struct FSpotLightInfo
{
	FVector Color;
	float AttenuationRadius = 0.0f;
	FVector Position;
	float CosInnerCone = 1.0f;
	FVector Direction;
	float CosOuterCone = 1.0f;
	// 그림자: 이 Spot 시점의 원근 VP와 FSpotShadowMapArray slice 번호. -1이면 그림자 없음.
	FMatrix ShadowViewProj;
	int32 ShadowSlot = -1;
	float ShadowPadding[3] = {};
};

struct FLightingConstants
{
	FAmbientLightInfo Ambient;
	FDirectionalLightInfo Directional;
	FPointLightInfo PointLights[MaxPointLights];
	FSpotLightInfo SpotLights[MaxSpotLights];
	uint32 NumPointLights = 0;
	uint32 NumSpotLights = 0;
	uint32 Padding[2] = {};
};

static_assert(sizeof(FAmbientLightInfo) == 16);
static_assert(sizeof(FDirectionalLightInfo) == 32);
static_assert(sizeof(FPointLightInfo) == 48);
static_assert(sizeof(FSpotLightInfo) == 128);
static_assert(sizeof(FLightingConstants) == 768);
