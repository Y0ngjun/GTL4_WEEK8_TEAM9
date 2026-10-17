#pragma once

#include "Math/Matrix.h"
#include "Math/Frustum.h"

class FScene;
struct FSpotLightInfo;
struct FPointLightInfo;

constexpr float SpotShadowNearClip = 0.1f;
constexpr float PointShadowNearClip = 0.1f;
constexpr uint32 CubeFaceCount = 6;

constexpr uint32 MaxShadowedSpotLights = 4;
constexpr uint32 MaxShadowedPointLights = 4;
constexpr uint32 ShadowMapResolution = 1024;

struct FProjectedShadowInfo
{
	FMatrix ViewMatrix;
	FMatrix ProjectionMatrix;
	FMatrix ViewProjection;
	FFrustumPlanes Frustum;
};

struct FPointShadowSetup
{
	FProjectedShadowInfo Faces[CubeFaceCount];
	FVector LightPosition;
	float Radius = 0.0f;
};

struct FShadowSceneSetup
{
	bool bHasDirectional = false;
	FProjectedShadowInfo Directional;

	uint32 NumSpots = 0;
	FProjectedShadowInfo Spots[MaxShadowedSpotLights];

	uint32 NumPoints = 0;
	FPointShadowSetup Points[MaxShadowedPointLights];
};

struct FShadowConstants
{
	FMatrix DirectionalShadowViewProjection;
	FMatrix SpotShadowViewProjection[MaxShadowedSpotLights];
	uint32 bHasDirectionalShadow = 0;
	float Padding[3] = {};
};
static_assert(sizeof(FShadowConstants) == 336, "HLSL b5 레이아웃과 같아야 한다");

struct FShadowDepthConstants
{
	FMatrix ViewProjection;
	FVector LightPosition{};
	float InvLightRadius = 0.0f;
};
static_assert(sizeof(FShadowDepthConstants) == 80, "HLSL b0 레이아웃(matrix + float3 + float)과 같아야 한다");

bool ComputeShadowCasterBounds(const FScene& Scene, FAABB& OutBounds);
FMatrix MakeShadowViewMatrix(const FVector& Eye, const FVector& Forward);
FMatrix MakeShadowViewMatrix(const FVector& Eye, const FVector& Forward, const FVector& Right, const FVector& Up);

// Directional
FProjectedShadowInfo MakeDirectionalShadowInfo(const FVector& LightDirection, const FAABB& CasterBounds);
bool BuildDirectionalShadowInfo(const FScene& Scene, FProjectedShadowInfo& OutShadow);

// Spot
FProjectedShadowInfo MakeSpotShadowInfo(const FSpotLightInfo& Spot);
bool BuildSpotShadowInfo(const FScene& Scene, FProjectedShadowInfo& OutShadow);

// Point
void MakePointShadowInfo(const FPointLightInfo& Point, FPointShadowSetup& OutSetup);
bool BuildPointShadowInfo(const FScene& Scene, FPointShadowSetup& OutSetup);

void BuildShadowSceneSetup(const FScene& Scene, bool bEnabled, FShadowSceneSetup& OutSetup);