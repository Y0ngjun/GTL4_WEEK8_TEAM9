#pragma once

#include "Math/Frustum.h"

// 그림자 컬링 기준. 에디터 Settings 패널에서 바꾸고 매 프레임 FSceneRenderer에 넘긴다.
// 쉐도우 맵은 World당 한 번 그려 모든 View가 공유하므로, View에 의존하는 기준은
// "어느 View 하나라도 필요로 하면 그린다"(모든 View의 합집합)로 판정한다.
struct FShadowCullingSettings
{
    // Light: 영향 범위가 모든 View 프러스텀 밖이면 그리지 않는다. Point는 큐브맵 면 단위로도 적용한다.
    bool bCullLightsByView = true;

    // Light: 모든 View에서 카메라와 영향 범위 사이 거리가 이 값보다 멀면 그리지 않는다. 0 이하면 끈다.
    float ShadowDistance = 0.0f;

    // Light: 모든 View에서 영향 범위의 화면 반지름(픽셀)이 이 값보다 작으면 그리지 않는다. 0 이하면 끈다.
    float MinLightScreenRadius = 2.0f;

    // Caster: Caster와 빛 방향으로 늘린 그림자 볼륨이 모든 View 프러스텀 밖이면 그리지 않는다.
    bool bCullCastersByView = true;

    // Caster: 쉐도우 맵 상 지름(텍셀)이 이 값보다 작으면 그리지 않는다. 0 이하면 끈다.
    float MinCasterTexels = 1.0f;
};

// 컬링 판정에 쓰는 View 값 스냅샷.
struct FShadowCullingView
{
    FFrustumPlanes Frustum{};
    FVector Location{};
    // 원근: 거리 1에서 월드 길이 1이 차지하는 픽셀 수. 직교: 월드 길이 1의 픽셀 수.
    float PixelsPerUnit = 0.0f;
    bool bOrthographic = false;
};

// 한 World의 마지막 준비 결과. 컬링이 실제로 동작하는지 화면에서 확인하는 용도다.
struct FShadowCullingStats
{
    // Light 단위. CastShadow가 켜진 visible Light부터 센다.
    uint32 LightsConsidered = 0;
    uint32 LightsCulledByIntensity = 0;
    uint32 LightsCulledByView = 0;
    uint32 LightsCulledByDistance = 0;
    uint32 LightsCulledByScreenSize = 0;
    uint32 LightsCulledByNoCaster = 0;
    uint32 LightsRendered = 0;

    // Point 큐브맵 면 중 View 밖이라 Caster를 모으지 않은 면 수.
    uint32 PointFacesCulled = 0;

    // Caster 단위. 패스마다 센다(Point는 걸친 면마다 따로 센다).
    uint32 CastersConsidered = 0;
    uint32 CastersCulledByView = 0;
    uint32 CastersCulledBySize = 0;
    uint32 CastersDrawn = 0;
};
