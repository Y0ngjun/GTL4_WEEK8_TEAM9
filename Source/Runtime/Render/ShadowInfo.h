#pragma once

#include "LightInfo.h"
#include "Math/Matrix.h"

// Depth 패스 해상도와 Pass 2 Bias의 텍셀 크기 계산이 같은 값을 써야 한다.
inline constexpr uint32 DefaultShadowResolution = 1024;

// Pass 2 StructuredBuffer의 한 항목. Point의 면별 행렬은 CPU PassInfo에만 둔다.
struct FShadowInfo
{
    FMatrix ViewProjection{};
    FVector LightPosition{};
    // 월드 거리 -> 비교 깊이 변환 계수.
    // Spot/Point: 1 / 감쇠 반경. Directional: 1 / (Far - Near).
    float InvRadius = 0.0f;
    uint32 ShadowMapIndex = 0;
    uint32 LightType = 0;
    // Bias는 월드 거리 설정을 비교할 정규화 깊이 단위로 바꾼다.
    float Bias = 0.0f;
    // 무차원 k. Slope Bias = k * (T/2) * tan(theta)이며 k = 1이 이론상 최소값이다.
    float SlopeBias = 0.0f;
    float MaxSlope = 0.0f;
    // 텍셀 절반(T/2)의 월드 크기.
    // Directional: 맵 전체에서 일정한 값 그대로 쓴다.
    // Spot/Point: T가 라이트 거리에 비례하므로 tan(반각) / 해상도를 저장하고, 셰이더에서 거리를 곱한다.
    float HalfTexelScale = 0.0f;
    // 무차원 k. Normal Offset = k * (T/2) * sin(theta)이며 k = 1이 이론상 최소값이다.
    float NormalOffset = 0.0f;
    // PCSS 반영 폭 계수. Light 종류마다 단위가 다르다.
    // Directional: 깊이 차 → UV 반경. tan(각지름/2) * 깊이 범위 / 투영 폭.
    // Spot: (zR - zB) / (zB * zR) → UV 반경. 광원 반지름 / (2 * 감쇠 반경 * tan(반각)).
    // Point: (zR - zB) / (zB * zR) → 방향 벡터 접평면 반경. 광원 반지름 / 감쇠 반경.
    float PenumbraScale = 0.0f;
    // Spot/Point 차폐물 탐색에서 가장 먼 차폐물 위치로 쓰는 정규화 Near 깊이.
    float PCSSNearDepth = 0.0f;
    float Padding[3] = {};
};
static_assert(sizeof(FShadowInfo) == 128);
static_assert(offsetof(FShadowInfo, LightPosition) == 64);
static_assert(offsetof(FShadowInfo, ShadowMapIndex) == 80);
static_assert(offsetof(FShadowInfo, MaxSlope) == 96);
static_assert(offsetof(FShadowInfo, HalfTexelScale) == 100);
static_assert(offsetof(FShadowInfo, NormalOffset) == 104);
static_assert(offsetof(FShadowInfo, PenumbraScale) == 108);
static_assert(offsetof(FShadowInfo, PCSSNearDepth) == 112);
