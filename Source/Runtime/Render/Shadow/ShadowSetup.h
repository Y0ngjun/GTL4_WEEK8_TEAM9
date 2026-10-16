#pragma once

#include "Math/Matrix.h"
#include "Math/Frustum.h"

class FScene;

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

// Caster 전체를 감싸는 AABB
bool ComputeShadowCasterBounds(const FScene& Scene, FAABB& OutBounds);

FMatrix MakeShadowViewMatrix(const FVector& Eye, const FVector& Forward);
FProjectedShadowInfo MakeDirectionalShadowInfo(const FVector& LightDirection, const FAABB& CasterBounds);
bool BuildDirectionalShadowInfo(const FScene& Scene, FProjectedShadowInfo& OutShadow);
