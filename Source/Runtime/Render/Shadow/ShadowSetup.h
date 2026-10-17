#pragma once

#include "Math/Matrix.h"
#include "Math/Frustum.h"

class FScene;
struct FSpotLightInfo;
struct FPointLightInfo;

constexpr float SpotShadowNearClip = 0.1f;
constexpr float PointShadowNearClip = 0.1f;
constexpr uint32 CubeFaceCount = 6;

struct FShadowConstants
{
	FMatrix DirectionalShadowViewProjection;
	FMatrix SpotShadowViewProjection;
	uint32 bHasDirectionalShadow = 0;
	uint32 bHasSpotShadow = 0;
	uint32 bHasPointShadow = 0;
	float PointShadowInvRadius = 0.0f;
	FVector PointShadowLightPosition{};
	float Padding = 0.0f;
};
static_assert(sizeof(FShadowConstants) == 160, "HLSL b5 레이아웃과 같아야 한다");

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
