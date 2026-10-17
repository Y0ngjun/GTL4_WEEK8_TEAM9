#include "EnginePCH.h"
#include "ShadowSetup.h"
#include "Engine/PrimitiveSceneProxy.h"
#include "Engine/Scene.h"
#include "Camera/CameraProjection.h"
#include "Engine/LightSceneProxy.h"

namespace
{
	struct FCubeFaceAxes
	{
		FVector Forward;
		FVector Right;
		FVector Up;
	};

	const FCubeFaceAxes CubeFaceAxes[CubeFaceCount] =
	{
		{ FVector(1.0f,  0.0f,  0.0f), FVector(0.0f,  0.0f, -1.0f), FVector(0.0f,  1.0f,  0.0f) },   // +X
		{ FVector(-1.0f,  0.0f,  0.0f), FVector(0.0f,  0.0f,  1.0f), FVector(0.0f,  1.0f,  0.0f) },   // -X
		{ FVector(0.0f,  1.0f,  0.0f), FVector(1.0f,  0.0f,  0.0f), FVector(0.0f,  0.0f, -1.0f) },   // +Y
		{ FVector(0.0f, -1.0f,  0.0f), FVector(1.0f,  0.0f,  0.0f), FVector(0.0f,  0.0f,  1.0f) },   // -Y
		{ FVector(0.0f,  0.0f,  1.0f), FVector(1.0f,  0.0f,  0.0f), FVector(0.0f,  1.0f,  0.0f) },   // +Z
		{ FVector(0.0f,  0.0f, -1.0f), FVector(-1.0f,  0.0f,  0.0f), FVector(0.0f,  1.0f,  0.0f) },   // -Z
	};
}

bool ComputeShadowCasterBounds(const FScene& Scene, FAABB& OutBounds)
{
	FVector Min(FLT_MAX, FLT_MAX, FLT_MAX);
	FVector Max(-FLT_MAX, -FLT_MAX, -FLT_MAX);
	bool bAny = false;

	for (int32 Index = 0; Index < Scene.Proxies.Num(); ++Index)
	{
		const FPrimitiveSceneProxy* Proxy = Scene.Proxies[Index];
		if (!Proxy->IsVisible() || !Proxy->GetMesh())
		{
			continue;
		}

		const FAABB& B = Scene.PrimitiveBounds[Index];
		const FVector BMin = B.Center - B.Extent;
		const FVector BMax = B.Center + B.Extent;
		Min = FVector(std::min(Min.X, BMin.X), std::min(Min.Y, BMin.Y), std::min(Min.Z, BMin.Z));
		Max = FVector(std::max(Max.X, BMax.X), std::max(Max.Y, BMax.Y), std::max(Max.Z, BMax.Z));
		bAny = true;
	}

	if (!bAny)
	{
		return false;
	}

	OutBounds.Center = (Min + Max) * 0.5f;
	OutBounds.Extent = (Max - Min) * 0.5f;
	return true;
}

FMatrix MakeShadowViewMatrix(const FVector& Eye, const FVector& Forward)
{
	const FVector UpHint = std::abs(Forward.Z) > 0.99f ? FVector(1.0f, 0.0f, 0.0f) : FVector(0.0f, 0.0f, 1.0f);
	const FVector Right = FVector::Cross(UpHint, Forward).GetSafeNormal();
	const FVector Up = FVector::Cross(Forward, Right);
	return MakeShadowViewMatrix(Eye, Forward, Right, Up);
}

FMatrix MakeShadowViewMatrix(const FVector& Eye, const FVector& Forward, const FVector& Right, const FVector& Up)
{
	const FMatrix Rotation(FVector4(Forward, 0.0f), FVector4(Right, 0.0f),
		FVector4(Up, 0.0f), FVector4(0.0f, 0.0f, 0.0f, 1.0f));
	return FMatrix::MakeTranslation(-Eye) * Rotation.GetTransposed();
}

FProjectedShadowInfo MakeDirectionalShadowInfo(const FVector& LightDirection, const FAABB& CasterBounds)
{
	const FVector Forward = LightDirection.GetSafeNormal();

	const float Radius = std::max(CasterBounds.Extent.Size(), 1.0f);
	const FVector Eye = CasterBounds.Center - Forward * (Radius * 2.0f);

	FCameraProjection Projection;
	Projection.Mode = EProjectionMode::Orthographic;
	Projection.OrthoWidth = Radius * 2.0f;
	Projection.NearClip = Radius;
	Projection.FarClip = Radius * 3.0f;

	FProjectedShadowInfo Info;
	Info.ViewMatrix = MakeShadowViewMatrix(Eye, Forward);
	Info.ProjectionMatrix = Projection.GetMatrix(1.0f);
	Info.ViewProjection = Info.ViewMatrix * Info.ProjectionMatrix;
	Info.Frustum = ExtractFrustumPlanes(Info.ViewProjection);
	return Info;
}

bool BuildDirectionalShadowInfo(const FScene& Scene, FProjectedShadowInfo& OutShadow)
{
	FLightingConstants Lighting;
	Scene.BuildLightingConstants(Lighting);
	if (Lighting.Directional.Direction.Size() < 0.5f)
	{
		return false;
	}

	FAABB Bounds;
	if (!ComputeShadowCasterBounds(Scene, Bounds))
	{
		return false;
	}

	OutShadow = MakeDirectionalShadowInfo(Lighting.Directional.Direction, Bounds);
	return true;
}

FProjectedShadowInfo MakeSpotShadowInfo(const FSpotLightInfo& Spot)
{
	const FVector Forward = Spot.Direction.GetSafeNormal();

	const float OuterHalfAngleRadians = std::acos(std::clamp(Spot.CosOuterCone, -1.0f, 1.0f));

	FCameraProjection Projection;
	Projection.Mode = EProjectionMode::Perspective;
	Projection.FovDegrees = OuterHalfAngleRadians * 2.0f * 180.0f / PI;
	Projection.NearClip = SpotShadowNearClip;
	Projection.FarClip = std::max(Spot.AttenuationRadius, SpotShadowNearClip * 2.0f);

	FProjectedShadowInfo Info;
	Info.ViewMatrix = MakeShadowViewMatrix(Spot.Position, Forward);
	Info.ProjectionMatrix = Projection.GetMatrix(1.0f);
	Info.ViewProjection = Info.ViewMatrix * Info.ProjectionMatrix;
	Info.Frustum = ExtractFrustumPlanes(Info.ViewProjection);
	return Info;
}

bool BuildSpotShadowInfo(const FScene& Scene, FProjectedShadowInfo& OutShadow)
{
	for (const FLightSceneProxy* Proxy : Scene.Lights)
	{
		if (!Proxy->IsVisible() || Proxy->GetLightType() != ELightType::Spot)
		{
			continue;
		}

		OutShadow = MakeSpotShadowInfo(static_cast<const FSpotLightSceneProxy*>(Proxy)->GetInfo());
		return true;
	}
	return false;
}

void MakePointShadowInfo(const FPointLightInfo& Point, FPointShadowSetup& OutSetup)
{
	FCameraProjection Projection;
	Projection.Mode = EProjectionMode::Perspective;
	Projection.FovDegrees = 90.0f;
	Projection.NearClip = PointShadowNearClip;
	Projection.FarClip = std::max(Point.AttenuationRadius, PointShadowNearClip * 2.0f);
	const FMatrix ProjectionMatrix = Projection.GetMatrix(1.0f);

	for (uint32 Face = 0; Face < CubeFaceCount; ++Face)
	{
		const FCubeFaceAxes& Axes = CubeFaceAxes[Face];
		FProjectedShadowInfo& Info = OutSetup.Faces[Face];
		Info.ViewMatrix = MakeShadowViewMatrix(Point.Position, Axes.Forward, Axes.Right, Axes.Up);
		Info.ProjectionMatrix = ProjectionMatrix;
		Info.ViewProjection = Info.ViewMatrix * Info.ProjectionMatrix;
		Info.Frustum = ExtractFrustumPlanes(Info.ViewProjection);
	}

	OutSetup.LightPosition = Point.Position;
	OutSetup.Radius = Point.AttenuationRadius;
}

bool BuildPointShadowInfo(const FScene& Scene, FPointShadowSetup& OutSetup)
{
	for (const FLightSceneProxy* Proxy : Scene.Lights)
	{
		if (!Proxy->IsVisible() || Proxy->GetLightType() != ELightType::Point)
		{
			continue;
		}

		MakePointShadowInfo(static_cast<const FPointLightSceneProxy*>(Proxy)->GetInfo(), OutSetup);
		return true;
	}
	return false;
}
