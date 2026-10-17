#pragma once

#include "Render/RenderPacket.h"
#include "Render/TileLightCulling.h"
#include "Render/LightInfo.h"
#include "Asset/LOD/StaticMeshLODSelector.h"

class UStaticMeshComponent;

struct FParticleSortInput
{
    uint32 Id;
    FVector WorldPosition;
};

// Viewport가 소유하는 재사용 버퍼. World/Component/Proxy는 소유하지 않는다.
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
    }
};
