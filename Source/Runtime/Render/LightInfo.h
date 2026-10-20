#pragma once

#include "Math/Vector.h"

inline constexpr uint32 MaxShadowCascades = 8;

enum class ELightType : uint8
{
	Ambient, Directional, Point, Spot,
};

inline constexpr uint32 InvalidShadowIndex = ~0u;

// Directional의 CPU 준비 설정. GPU 조명 상수 레이아웃에는 포함하지 않는다.
struct FShadowCascadeSettings
{
    uint32 Count = 4;
    float MaxDistance = 100.0f;
    float SplitLambda = 0.5f;
};

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
	uint32 ShadowIndex = InvalidShadowIndex;
};

struct FPointLightInfo
{
	FVector Color;
	float AttenuationRadius = 0.0f;
	FVector Position;
	uint32 ShadowIndex = InvalidShadowIndex;
};

struct FSpotLightInfo
{
	FVector Color;
	float AttenuationRadius = 0.0f;
	FVector Position;
	float CosInnerCone = 1.0f;
	FVector Direction;
	float CosOuterCone = 1.0f;
	uint32 ShadowIndex = InvalidShadowIndex;
	uint32 Padding[3] = {};
};

struct FLightingConstants
{
	FAmbientLightInfo Ambient;
	FDirectionalLightInfo Directional;
	FPointLightInfo PointLights[MaxPointLights];
	FSpotLightInfo SpotLights[MaxSpotLights];
	uint32 NumPointLights = 0;
	uint32 NumSpotLights = 0;
	uint32 bReadyForShading = 0;
	uint32 NumShadowInfos = 0;
};

static_assert(sizeof(FAmbientLightInfo) == 16);
static_assert(sizeof(FDirectionalLightInfo) == 32);
static_assert(sizeof(FPointLightInfo) == 32);
static_assert(sizeof(FSpotLightInfo) == 64);
static_assert(sizeof(FLightingConstants) == 448);
static_assert(offsetof(FDirectionalLightInfo, ShadowIndex) == 28);
static_assert(offsetof(FPointLightInfo, ShadowIndex) == 28);
static_assert(offsetof(FSpotLightInfo, ShadowIndex) == 48);
static_assert(offsetof(FLightingConstants, bReadyForShading) == 440);
