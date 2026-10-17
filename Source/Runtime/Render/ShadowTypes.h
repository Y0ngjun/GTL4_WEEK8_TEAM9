#pragma once

#include "Core/Types.h"
#include <Math/Matrix.h>
#include <Math/Frustum.h>
#include "RenderPacket.h"
#include "ShadowSettings.h"

enum class EShadowProjection : uint8
{
	Directional,
	Point,
	Spot
};

enum class EShadowMapType : uint8
{
	Texture2D, // Directional / Spot
	TextureCube,// Point 
};


struct FShadowMapDesc
{
	uint32 Resolution = 2048;

	// 텍스처에 깊이 이미지를 몇 장 만들지
	// Directional은 1장, Point는 6장, Spot은 1장
	uint32 SliceCount = 1;
	EShadowMapType Type = EShadowMapType::Texture2D;
};


// 깊이를 관측하는 시점 하나 (스냅샷)
struct FShadowView
{
	FMatrix ViewProjection;
	FFrustumPlanes Frustum;

	// 이번에 그릴 출력면의 DSV 인덱스
	uint32 TargetSlice = 0;

	// 광원에서 깊이를 기록할 물체들
	FRenderQueue CasterQueue;

	// directional 수신 범위를 PS에서 제한하기 위한 값.
	FVector ReceiverOrigin = FVector(0, 0, 0);
	float ReceiverMinDepth = 0.0f;
	FVector ReceiverForward = FVector(1, 0, 0);
	float ReceiverMaxDepth = 0.0f;

	FShadowView() = default;
	FShadowView(const FShadowView&) = delete;
	FShadowView& operator=(const FShadowView&) = delete;

	FShadowView(FShadowView&&) noexcept = default;
	FShadowView& operator=(FShadowView&&) noexcept = default;
};
 
// 하나의 광원에 대한 그림자들을 렌더링하기 위한 데이터묶음
struct FLightShadowData
{
	uint32 LightId = 0;
	EShadowProjection Type = EShadowProjection::Directional;
	FShadowMapDesc MapDesc;
	FShadowSettings Settings;

	// 여러 시점에서 그림자를 관측할 수 있다. (Point Light는 6면) 
	TArray<FShadowView> Views;

	FLightShadowData() = default;
	FLightShadowData(const FLightShadowData&) = delete;
	FLightShadowData& operator=(const FLightShadowData&) = delete;

	FLightShadowData(FLightShadowData&&) noexcept = default;
	FLightShadowData& operator=(FLightShadowData&&) noexcept = default;
};