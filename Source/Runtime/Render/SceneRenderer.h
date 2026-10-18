#pragma once

#include "Render/SceneView.h"
#include "Render/SceneRenderData.h"
#include "Containers/Map.h"

class UWorld;
class UPrimitiveComponent;

// 뷰 결과는 빌려 쓰고, 현재 월드의 수집 작업 버퍼만 재사용한다. 렌더 패스는 실행하지 않는다.
class FSceneRenderer
{
public:
    void BeginFrame();
    // World는 이 프레임의 준비/렌더 동안 변경·파괴하지 않는다.
    // 다음 월드 수집 전에 현재 큐를 렌더하고 Data.ReleaseSceneReferences()를 호출한다.
    void PrepareView(UWorld& World, const FSceneView& View, FSceneRenderData& Data);
    // 모든 뷰의 RenderFrame 이후 World/Component의 비소유 참조를 해제한다.
    void EndFrame();

private:
    void CaptureWorld(UWorld& World);
    void BuildRenderQueue(const FSceneView& View, FSceneRenderData& Data);
    
    // 그림자를 그리는 광원을 수집한다.
	void CollectShadowLights(const FScene& Scene, FSceneRenderData& Data);

    // 그림자에 관련된 static Mesh들의 월드 AABB를 합친 SceneBounds를 계산한다.
    // 월드 좌표를 Light의 시점으로 변환하고 투영하는 행렬을 만드는 함수들
	void BuildDirectionalLightShadowView(const FScene& Scene, FSceneView SceneView, FSceneRenderData& Data);
	void BuildSpotLightShadowView(const FScene& Scene, FSceneRenderData& Data);
	void BuildPointLightShadowView(const FScene& Scene, FSceneRenderData& Data);
    void BuildShadowCasterQueue(FShadowView& ShadowView);

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
