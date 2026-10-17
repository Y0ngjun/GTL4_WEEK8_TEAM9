#pragma once

#include "Render/SceneView.h"
#include "Render/SceneRenderData.h"
#include "Containers/Map.h"
#include "Render/ShadowFrameData.h"
#include "Render/ShadowResources.h"

class UWorld;
class FScene;
class UPrimitiveComponent;

// 카메라 수집 작업 버퍼와 World 슬롯별 그림자 CPU 자료를 소유한다. 렌더 패스는 실행하지 않는다.
class FSceneRenderer
{
public:
    void BeginFrame();
    uint64 GetFrameNumber() const { return FrameNumber; }
    // 같은 자원 슬롯은 이번 프레임에 한 번만 준비한다. World 교체 전 ReleaseWorldShadows가 필요하다.
    const FShadowFrameData* PrepareWorldShadows(UWorld& World, uint32 ResourceIndex);
    void ReleaseWorldShadows(uint32 ResourceIndex);
    // World는 이 프레임의 준비/렌더 동안 변경·파괴하지 않는다.
    // 다음 월드 수집 전에 현재 큐를 렌더하고 Data.ReleaseSceneReferences()를 호출한다.
    void PrepareView(UWorld& World, const FSceneView& View, FSceneRenderData& Data);
    // 모든 뷰의 RenderFrame 이후 World/Component의 비소유 참조를 해제한다.
    void EndFrame();

    // Scene의 Proxy 갱신 후 호출한다. 자료 보관과 프레임당 호출 제어는 호출자가 담당한다.
    void BuildShadowRequests(const FScene& Scene, FShadowFrameData& OutData);
    // Scene BVH가 최신이고 Pass.Frustum이 채워진 상태에서 호출한다. 
    // todo: 현재는 LOD 0만 사용한다.
    void BuildShadowCasterQueue(UWorld& World, FShadowDepthPassInfo& Pass);

private:
    struct FWorldShadowFrame
    {
        FShadowFrameData Data;
        uint64 PreparedFrame = 0;
    };
    std::array<FWorldShadowFrame, MaxShadowResourceSlots> WorldShadowFrames;
    uint64 FrameNumber = 0;
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
