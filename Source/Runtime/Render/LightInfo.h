#pragma once

#include "Math/Vector.h"

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
	int32 ShadowIndex = -1;
};

struct FSpotLightInfo
{
	FVector Color;
	float AttenuationRadius = 0.0f;
	FVector Position;
	float CosInnerCone = 1.0f;
	FVector Direction;
	float CosOuterCone = 1.0f;
	int32 ShadowIndex = -1;
	float Padding[3] = {};
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
static_assert(sizeof(FPointLightInfo) == 32);
static_assert(sizeof(FSpotLightInfo) == 64);
static_assert(sizeof(FLightingConstants) == 448);
