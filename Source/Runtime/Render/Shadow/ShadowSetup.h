#pragma once

#include "Math/Matrix.h"
#include "Math/Frustum.h"

class FScene;
struct FSpotLightInfo;

constexpr float SpotShadowNearClip = 0.1f;

struct FShadowConstants
{
	FMatrix DirectionalShadowViewProjection;
	uint32 bHasDirectionalShadow = 0;
	float Padding[3] = {};
};
static_assert(sizeof(FShadowConstants) == 80, "HLSL b5 레이아웃(matrix + uint + float3)과 같아야 한다");

struct FProjectedShadowInfo
{
	FMatrix ViewMatrix;
	FMatrix ProjectionMatrix;
	FMatrix ViewProjection;
	FFrustumPlanes Frustum;
};

bool ComputeShadowCasterBounds(const FScene& Scene, FAABB& OutBounds);
FMatrix MakeShadowViewMatrix(const FVector& Eye, const FVector& Forward);

// Directional
FProjectedShadowInfo MakeDirectionalShadowInfo(const FVector& LightDirection, const FAABB& CasterBounds);
bool BuildDirectionalShadowInfo(const FScene& Scene, FProjectedShadowInfo& OutShadow);

// Spot
FProjectedShadowInfo MakeSpotShadowInfo(const FSpotLightInfo& Spot);
bool BuildSpotShadowInfo(const FScene& Scene, FProjectedShadowInfo& OutShadow);
