#pragma once

#include "Render/SceneView.h"
#include "Render/SceneRenderData.h"
#include "Containers/Map.h"
#include "Render/ShadowFrameData.h"
#include "Render/ShadowResources.h"

class UWorld;
class FScene;
class UPrimitiveComponent;

// Light별 Caster 컬링에 필요한 값. Light별 준비 함수가 채운다.
struct FShadowCasterCullContext
{
    ELightType LightType = ELightType::Directional;
    // Spot/Point: 라이트 위치.
    FVector LightPosition{};
    // Directional: 빛이 나아가는 방향.
    FVector LightDirection{};
    // Directional: 그림자 볼륨을 늘릴 거리. Spot/Point: 감쇠 반경.
    float ExtrudeDistance = 0.0f;
    // Directional: 월드 길이 1의 텍셀 수. Spot/Point: 거리 1에서 월드 길이 1의 텍셀 수.
    float TexelsPerUnit = 0.0f;
};

// 카메라 수집 작업 버퍼와 World 슬롯별 그림자 CPU 자료를 소유한다. 렌더 패스는 실행하지 않는다.
class FSceneRenderer
{
public:
    void BeginFrame();
    uint64 GetFrameNumber() const { return FrameNumber; }
    void SetShadowCullingSettings(const FShadowCullingSettings& Settings) { ShadowCullingSettings = Settings; }
    // 이번 프레임에 그림자를 쓰는 View를 PrepareWorldShadows 전에 모두 등록한다.
    // 쉐도우 맵은 World의 모든 View가 공유하므로 View 컬링은 등록된 View의 합집합으로 판정한다.
    // 한 World에 등록된 View가 없으면 View 기반 컬링을 하지 않는다.
    void AddShadowView(const UWorld& World, const FSceneView& View);
    // 같은 자원 슬롯은 이번 프레임에 한 번만 준비한다. World 교체 전 ReleaseWorldShadows가 필요하다.
    const FShadowFrameData* PrepareWorldShadows(UWorld& World, uint32 ResourceIndex);
    void ReleaseWorldShadows(uint32 ResourceIndex);
    // 마지막으로 준비한 컬링 결과. 준비한 적 없으면 nullptr.
    const FShadowCullingStats* GetShadowCullingStats(uint32 ResourceIndex) const;
    // World는 이 프레임의 준비/렌더 동안 변경·파괴하지 않는다.
    // 다음 월드 수집 전에 현재 큐를 렌더하고 Data.ReleaseSceneReferences()를 호출한다.
    void PrepareView(UWorld& World, const FSceneView& View, FSceneRenderData& Data);
    // 모든 뷰의 RenderFrame 이후 World/Component의 비소유 참조를 해제한다.
    void EndFrame();

    // Scene의 Proxy 갱신 후 호출한다. 자료 보관과 프레임당 호출 제어는 호출자가 담당한다.
    // Light 단위 컬링(강도, View, 거리, 화면 크기)을 여기서 한다. 출력 위치는 아직 배정하지 않는다.
    void BuildShadowRequests(const FScene& Scene, FShadowFrameData& OutData);
    // Scene BVH가 최신이고 Pass.Frustum이 채워진 상태에서 호출한다.
    // Caster 단위 컬링(플래그, 라이트 프러스텀, 그림자 볼륨 ∩ View, 쉐도우 맵 상 크기)을 한다.
    // todo: 현재는 LOD 0만 사용한다.
    void BuildShadowCasterQueue(UWorld& World, FShadowDepthPassInfo& Pass, const FShadowCasterCullContext& Context, FShadowCullingStats& Stats);

private:
    struct FWorldShadowFrame
    {
        FShadowFrameData Data;
        uint64 PreparedFrame = 0;
    };
    struct FRegisteredShadowView
    {
        const UWorld* World = nullptr;
        FShadowCullingView View;
    };
    std::array<FWorldShadowFrame, MaxShadowResourceSlots> WorldShadowFrames;
    uint64 FrameNumber = 0;
    FShadowCullingSettings ShadowCullingSettings;
    TArray<FRegisteredShadowView> RegisteredShadowViews;
    // PrepareWorldShadows가 준비 중인 World의 View만 골라 둔 목록.
    TArray<FShadowCullingView> PreparingShadowViews;
    // Caster가 하나도 남지 않으면 출력 위치를 배정하지 않고 false를 반환한다.
    // NextMapIndex는 Directional/Spot이면 2D slice, Point면 Cube 번호의 다음 빈자리다.
    bool PrepareSpotShadow(UWorld& World, uint32 RequestIndex, FShadowFrameData& Frame, uint32& NextMapIndex);
    bool PrepareDirectionalShadow(UWorld& World, uint32 RequestIndex, FShadowFrameData& Frame, uint32& NextMapIndex);
    bool PreparePointShadow(UWorld& World, uint32 RequestIndex, FShadowFrameData& Frame, uint32& NextMapIndex);
    void CaptureWorld(UWorld& World);
    void BuildRenderQueue(const FSceneView& View, FSceneRenderData& Data);

    struct FPrimitiveSnapshot
    {
        UPrimitiveComponent* Primitive = nullptr;
        bool bCaptured = false;
        bool bParticlesPrepared = false;
        TArray<int32> AliveParticleIndices;
    };
    struct FRenderableObject
    {
        uint32 Id;
        FAABB WorldBounds;
    };
    TMap<uint32, FPrimitiveSnapshot> PrimitiveById;
    TArray<FRenderableObject> RenderObjects;
    // 같은 프레임의 연속된 동일 World 뷰만 수집/파티클 상수를 공유한다.
    UWorld* CapturedWorld = nullptr;
    bool bFrameActive = false;
};
