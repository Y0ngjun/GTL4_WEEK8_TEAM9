#pragma once

#include "Render/RenderPacket.h"
#include "Render/TileLightCulling.h"
#include "Render/LightInfo.h"
#include "Asset/LOD/StaticMeshLODSelector.h"
#include "ShadowTypes.h"

class UStaticMeshComponent;

struct FParticleSortInput
{
    uint32 Id;
    FVector WorldPosition;
};

// Viewport가 소유하는 재사용 버퍼. World/Component/Proxy는 소유하지 않는다.
// 카메라 뷰 하나의 렌더링에 필요한 모든 데이터를 담는다. (큐, 광원, 그림자, LOD, 파티클 등)
struct FSceneRenderData
{
    FRenderQueue RenderQueue;
    TArray<FPointLightInfo> PointLights;
    TArray<FSpotLightInfo> SpotLights;
    TArray<FCullingLight> CullingLights;
    TArray<uint32> VisibleIds;
    TArray<UStaticMeshComponent*> PendingStaticMeshes;
    TArray<FLODSelectionInput> LODInputs;
    TArray<uint8> SelectedLODs;
    TArray<FParticleSortInput> SortInputs;
    TArray<uint32> SortedParticleIds;

	// 이번 뷰에서 생성할 광원별 그림자 데이터
    TArray<FLightShadowData> LightShadows;

    void ResetFrame()
    {
        ReleaseSceneReferences();
        PointLights.Reset();
        SpotLights.Reset();
        CullingLights.Reset();
        VisibleIds.Reset();
        SelectedLODs.Reset();
        SortInputs.Reset();
        SortedParticleIds.Reset();
    }

    // Renderer가 큐를 소비한 뒤 호출한다. 그리지 않은 큐도 다음 World 변경 전에 비운다.
    // 숫자로 된 가시 ID/LOD 결과는 다음 ResetFrame까지 조회할 수 있다.
    void ReleaseSceneReferences()
    {
        RenderQueue.Reset();
        PendingStaticMeshes.Reset();
        LODInputs.Reset();
        LightShadows.Reset();   
    }
};
