#pragma once

#include "Math/Vector.h"
#include "Math/Matrix.h"
#include "ShadowSettings.h"

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

	// Point 배열에서 이 광원이 사용하는 큐브 번호.
	int32 ShadowIndex = -1;

	// 복원한 View 깊이를 비교할 때 사용할 월드 단위 bias.
	float ShadowBias = 0.0f;

	// 깊이 생성에 사용한 near. far는 AttenuationRadius를 재사용한다.
	float ShadowNearClip = PointShadowNearClip;

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

	FMatrix ShadowViewProjection = FMatrix::Identity;

	// -1 이면 그림자 없음, 0 이상이면 그림자 맵 배열에서 이 광원의 그림자를 조회할 인덱스
	int32 ShadowIndex = -1; // 그림자 맵 배열에서 이 광원의 그림자를 조회할 인덱스. -1이면 그림자 없음
	float ShadowBias = 0.0f; // Spot은 광원 뷰 깊이의 월드 단위로 비교한다.
	float ShadowNearClip = SpotShadowNearClip;
	float ShadowPadding = 0.0f;
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
