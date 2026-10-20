#pragma once

#include "LightInfo.h"
#include "Math/Matrix.h"
#include "Math/Vector4.h"

// Shadow Map 한 장의 GPU 조회 정보.
struct FShadowInfo
{
    FMatrix ViewProjection{};
    FVector LightPosition{};
    float InvRadius = 0.0f;

    // 첫 Map에 그룹 전체 개수를 기록한다. Point는 면 순서로 6개를 등록한다.
    uint32 MapCount = 1;

    uint32 LightType = 0;
    // Light별 준비 단계에서 월드 거리 설정을 비교할 정규화 깊이 단위로 바꾼다.
    float Bias = 0.0f;
    float SlopeBias = 0.0f;
    float MaxSlope = 0.0f;
    float Padding[3] = {};

    // xy: 타일 크기, zw: 시작 위치. 모두 Atlas UV 단위다.
    FVector4 AtlasUVScaleBias = { 1, 1, 0, 0 };
};
static_assert(sizeof(FShadowInfo) == 128);
static_assert(offsetof(FShadowInfo, LightPosition) == 64);
static_assert(offsetof(FShadowInfo, MapCount) == 80);
static_assert(offsetof(FShadowInfo, MaxSlope) == 96);
static_assert(offsetof(FShadowInfo, AtlasUVScaleBias) == 112);
