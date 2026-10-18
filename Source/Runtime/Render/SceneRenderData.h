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

// 이 View의 Directional Light 그림자 행렬. 그릴 물체가 없거나 지원하지 않는 View면 bValid가 false다.
struct FDirectionalShadowView
{
    FMatrix LightViewProj = FMatrix::Identity;
    bool bValid = false;
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
    // 절두체를 통과한 물체의 경계. 그림자 far를 보이는 물체까지로 줄인다.
    TArray<FAABB> VisibleBounds;
    // 절두체 컬링 전 월드 물체의 경계. 화면 밖에서 그림자를 드리우는 물체까지 near를 당긴다.
    TArray<FAABB> SceneBounds;
    FDirectionalShadowView DirectionalShadow;

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
        VisibleBounds.Reset();
        SceneBounds.Reset();
        DirectionalShadow = {};
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
