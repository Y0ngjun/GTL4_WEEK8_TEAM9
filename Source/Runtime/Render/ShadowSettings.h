#pragma once
#include "Core/Types.h"

// Spot의 투영 생성, 깊이 비교, 미리보기에서 같은 near를 사용한다.
constexpr float SpotShadowNearClip = 0.01f;

// Point의 투영 생성, 깊이 비교, 미리보기에서 같은 near를 사용한다.
constexpr float PointShadowNearClip = 0.01f;

struct FShadowSettings
{
	bool bCastShadows = true;
	uint32 Resolution = 2048; // ?
	float DepthBias = 0.005f; // ?
	float SlopeBias = 0.0f; // ?
};
