#pragma once

#include "LightInfo.h"
#include "Math/Matrix.h"

// Pass 2 StructuredBuffer의 한 항목. Point의 면별 행렬은 CPU PassInfo에만 둔다.
struct FShadowInfo
{
    FMatrix ViewProjection{};
    FVector LightPosition{};
    float InvRadius = 0.0f;
    uint32 ShadowMapIndex = 0;
    uint32 LightType = 0;
    // Light별 준비 단계에서 월드 거리 설정을 비교할 정규화 깊이 단위로 바꾼다.
    float Bias = 0.0f;
    float SlopeBias = 0.0f;
    float MaxSlope = 0.0f;
    float Padding[3] = {};
};
static_assert(sizeof(FShadowInfo) == 112);
static_assert(offsetof(FShadowInfo, LightPosition) == 64);
static_assert(offsetof(FShadowInfo, ShadowMapIndex) == 80);
static_assert(offsetof(FShadowInfo, MaxSlope) == 96);
