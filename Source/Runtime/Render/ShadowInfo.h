#pragma once

#include "LightInfo.h"
#include "Math/Matrix.h"
#include "Math/Vector4.h"

// Depth 패스 해상도와 Pass 2 Bias의 텍셀 크기 계산이 같은 값을 써야 한다.
inline constexpr uint32 DefaultShadowResolution = 1024;
inline constexpr uint32 DefaultSpotShadowResolution = 512;
inline constexpr uint32 DefaultPointShadowResolution = 256;

// Shadow Map 한 장의 GPU 조회 정보. Point 면과 CSM Cascade도 각각 등록한다.
struct FShadowInfo
{
    FMatrix ViewProjection{};
    FVector LightPosition{};
    // 월드 거리 -> 비교 깊이 변환 계수.
    // Spot/Point: 1 / 감쇠 반경. Directional: 1 / (Far - Near).
    float InvRadius = 0.0f;
    uint32 MapCount = 1;
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
    // PCSS 반영 폭 계수. 결과는 모두 Map 한 장 기준 로컬 UV 반경이다.
    // Directional: 깊이 차 → UV 반경. tan(각지름/2) * 깊이 범위 / 투영 폭.
    // Spot/Point: (zR - zB) / (zB * zR) → UV 반경. 광원 반지름 / (2 * 감쇠 반경 * tan(반각)). Point 면은 반각 45도.
    float PenumbraScale = 0.0f;
    FVector4 AtlasUVScaleBias = { 1, 1, 0, 0 };
    // Spot/Point 차폐물 탐색에서 가장 먼 차폐물 위치로 쓰는 정규화 Near 깊이.
    float PCSSNearDepth = 0.0f;
    float PCSSPadding[3] = {};
};
static_assert(sizeof(FShadowInfo) == 144);
static_assert(offsetof(FShadowInfo, LightPosition) == 64);
static_assert(offsetof(FShadowInfo, MapCount) == 80);
static_assert(offsetof(FShadowInfo, MaxSlope) == 96);
static_assert(offsetof(FShadowInfo, HalfTexelScale) == 100);
static_assert(offsetof(FShadowInfo, NormalOffset) == 104);
static_assert(offsetof(FShadowInfo, PenumbraScale) == 108);
static_assert(offsetof(FShadowInfo, AtlasUVScaleBias) == 112);
static_assert(offsetof(FShadowInfo, PCSSNearDepth) == 128);
