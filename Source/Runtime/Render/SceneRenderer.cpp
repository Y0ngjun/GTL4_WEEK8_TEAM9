#include "EnginePCH.h"
#include "Render/SceneRenderer.h"
#include "Component/PrimitiveComponent.h"
#include "Component/StaticMeshComponent.h"
#include "Component/BillboardComponent.h"
#include "Component/ParticleSubUVComponent.h"
#include "Engine/PrimitiveSceneProxy.h"
#include "Engine/World.h"
#include "Engine/LightSceneProxy.h"
#include "Core/Stats/LightweightStats.h"
#include <algorithm>
#include <cassert>

namespace
{
DECLARE_CYCLE_STAT("Capture World", STAT_CaptureWorld);
DECLARE_CYCLE_STAT("Build Render Queue", STAT_BuildRenderQueue);

void SortParticlesByCameraDistance(const TArray<FParticleSortInput>& Particles, const FVector& CameraLocation, TArray<uint32>& OutSortedBackToFront)
{
    // 파티클 ID와 미리 계산한 카메라 거리 제곱을 함께 담는다.
    struct FEntry { uint32 Id; float DistanceSquared; };
    TArray<FEntry> Entries;
    for (const FParticleSortInput& Particle : Particles)
    {
        const FVector Delta = Particle.WorldPosition - CameraLocation;
        Entries.Add({Particle.Id, Delta.X * Delta.X + Delta.Y * Delta.Y + Delta.Z * Delta.Z});
    }
    std::stable_sort(Entries.begin(), Entries.end(), [](const FEntry& A, const FEntry& B)
    {
        return A.DistanceSquared > B.DistanceSquared;
    });
    OutSortedBackToFront.Reset();
    for (const FEntry& Entry : Entries)
    {
        OutSortedBackToFront.Add(Entry.Id);
    }
}
}

void FSceneRenderer::BeginFrame()
{
    assert(!bFrameActive);
    bFrameActive = true;
    ++FrameNumber;
    CapturedWorld = nullptr;
}

void FSceneRenderer::PrepareView(UWorld& World, const FSceneView& View, FSceneRenderData& Data)
{
    assert(bFrameActive);
    Data.ResetFrame();
    if (CapturedWorld != &World)
    {
        SCOPE_CYCLE_COUNTER(STAT_CaptureWorld);
        CaptureWorld(World);
        CapturedWorld = &World;
    }
    SCOPE_CYCLE_COUNTER(STAT_BuildRenderQueue);
    BuildRenderQueue(View, Data);
}

void FSceneRenderer::EndFrame()
{
    for (auto& Slot : WorldShadowFrames) Slot.Data.ResetFrame();
    for (auto& Entry : PrimitiveById)
    {
        Entry.second.Primitive = nullptr;
        Entry.second.bCaptured = false;
        Entry.second.bParticlesPrepared = false;
        Entry.second.AliveParticleIndices.Reset();
    }
    RenderObjects.Reset();
    CapturedWorld = nullptr;
    bFrameActive = false;
}

void FSceneRenderer::CaptureWorld(UWorld& World)
{
    RenderObjects.Reset();
    for (auto& Entry : PrimitiveById) Entry.second.bCaptured = false;

    const FScene& Scene = World.GetScene();
    const int32 Count = Scene.Proxies.Num();
    for (int32 i = 0; i < Count;++i)
    {
        if (!Scene.PrimitiveFlags[i]) continue;
        UPrimitiveComponent* Primitive = Scene.Proxies[i]->GetComponent();
        if (!Primitive || !Primitive->IsVisible() || !Primitive->GetOwner() ||
            Primitive->GetOwner()->GetWorld() != &World) continue;
        if (World.IsPlayInEditor() && Primitive->IsHiddenInGame()) continue;
        const uint32 Id = Primitive->GetUUID();
        if (Id == 0) continue;

        FRenderableObject RenderObject{};
        RenderObject.Id = Id;
        const FPrimitiveSceneProxy* Proxy = Primitive->GetSceneProxy();
        RenderObject.WorldBounds = Proxy && Proxy->GetMesh()
            ? Proxy->GetBounds() : MakeWorldBounds(Primitive->CalcBounds());
        RenderObjects.Add(RenderObject);
        FPrimitiveSnapshot& Snapshot = PrimitiveById[Id];
        Snapshot.Primitive = Primitive;
        Snapshot.bCaptured = true;
        Snapshot.bParticlesPrepared = false;
    }

    for (auto Iterator = PrimitiveById.begin(); Iterator != PrimitiveById.end();)
    {
        const auto Current = Iterator++;
        if (!Current->second.bCaptured) PrimitiveById.Remove(Current->first);
    }
}

void FSceneRenderer::BuildRenderQueue(const FSceneView& View, FSceneRenderData& Data)
{
    FRenderQueue& OutQueue = Data.RenderQueue;
    auto& PendingStaticMeshes = Data.PendingStaticMeshes;
    auto& LODInputs = Data.LODInputs;
    auto& SelectedLODs = Data.SelectedLODs;
    auto& SortInputs = Data.SortInputs;
    auto& SortedParticleIds = Data.SortedParticleIds;
    // 기존 순서와 AABB/절두체 검사를 그대로 사용한다.
    for (const FRenderableObject& Object : RenderObjects)
        if (IsAABBInFrustum(Object.WorldBounds, View.Frustum))
            Data.VisibleIds.Add(Object.Id);
    const FLODViewContext LODContext = View.MakeLODContext();
    for (const uint32 Id : Data.VisibleIds)
    {
        const auto Found = PrimitiveById.Find(Id);
        if (!Found || !Found->Primitive)
            continue;

        UPrimitiveComponent* Primitive = Found->Primitive;
        if (UParticleSubUVComponent* ParticleComponent = Cast<UParticleSubUVComponent>(Primitive))
        {
            const TArray<FParticle>& Particles = ParticleComponent->GetParticlesForView();
            FPrimitiveSnapshot& Snapshot = *Found;
            if (!Snapshot.bParticlesPrepared)
            {
                Snapshot.AliveParticleIndices.Reset();
                Snapshot.AliveParticleIndices.Reserve(Particles.Num());
                for (int32 Index = 0; Index < Particles.Num(); ++Index)
                    if (Particles[Index].bAlive) Snapshot.AliveParticleIndices.Add(Index);
                ParticleComponent->BeginViewSubmission();
                Snapshot.bParticlesPrepared = true;
            }
            // 반투명은 모든 emitter를 합친 최종 Renderer만 정렬한다. 불투명은 기존 거리 정렬을 유지하며 동률은 인덱스 순서다.
            const FVector CameraLocation = View.GetCameraLocation();
            const bool bOpaque = ParticleComponent->UsesOpaqueMaterial();
            if (bOpaque)
            {
                SortInputs.Reset();
                for (const int32 Index : Snapshot.AliveParticleIndices)
                    SortInputs.Add({static_cast<uint32>(Index + 1), Particles[Index].Location});
                SortParticlesByCameraDistance(SortInputs, View.GetCameraLocation(), SortedParticleIds);
            }
            for (int32 Order = 0; Order < Snapshot.AliveParticleIndices.Num(); ++Order)
            {
                const int32 ParticleIndex = bOpaque ? static_cast<int32>(SortedParticleIds[Order] - 1) : Snapshot.AliveParticleIndices[Order];
                const FParticle& Particle = Particles[ParticleIndex];
                const FMatrix ParticleWorld = View.BuildBillboardMatrix(Particle.Location, Particle.Scale, Particle.Scale);
                const FVector Delta = Particle.Location - CameraLocation;
                ParticleComponent->SubmitParticleToRenderQueue(OutQueue, ParticleIndex, ParticleWorld, Delta.Dot(Delta));
            }
        }
        else if (UBillboardComponent* Billboard = Cast<UBillboardComponent>(Primitive))
        {
            const FVector Scale = Billboard->GetWorldScale3D();
            Billboard->SubmitToRenderQueue(OutQueue, View.BuildBillboardMatrix(Billboard->GetWorldLocation(), Scale.Y, Scale.Z));
        }
        else if (auto* StaticComponent = Cast<UStaticMeshComponent>(Primitive))
        {
            UStaticMesh* Mesh = StaticComponent->GetStaticMesh();
            if (!Mesh)
                continue;

            const FPrimitiveSceneProxy* Proxy = StaticComponent->GetSceneProxy();
            if (Proxy)
            {
                PendingStaticMeshes.Add(StaticComponent);
                LODInputs.Add({Proxy->GetLODSphere(), Proxy->GetRenderState()});
            }
        }
        else
        {
            Primitive->SubmitToRenderQueue(OutQueue);
        }
    }
    SelectLODs(LODInputs, LODContext, SelectedLODs);
    for (uint32 I = 0; I < static_cast<uint32>(PendingStaticMeshes.Num()); ++I)
        PendingStaticMeshes[I]->SubmitToRenderQueue(OutQueue, SelectedLODs[I]);
}

void FSceneRenderer::BuildShadowRequests(const FScene& Scene, FShadowFrameData& OutData)
{
    // 이전 CPU 요청/Queue만 비운다. GPU 배열을 해제하거나 재생성하는 처리가 아니다.
    OutData.ResetFrame();
    uint32 Next2DSlice = 0;
    uint32 NextCube = 0;
    const FLightSceneProxy* Directional = nullptr;
    for (const FLightSceneProxy* Light : Scene.Lights)
        if (Light->IsVisible() && Light->GetLightType() == ELightType::Directional)
            Directional = Light;

    // 기존 Directional 선택과 동일하게 마지막 visible 광원만 사용한다.
    if (Directional && Directional->CastsShadow())
        OutData.ShadowRequestsPerLight.Add({Directional, Next2DSlice++});

    // BuildStructuredLight와 같은 visible 조건·Point/Spot 순서다.
    // 요청에 포함된 Light만 출력 위치를 배정한다. 2D와 Cube 번호는 각각 0부터 시작한다.
    for (ELightType Type : {ELightType::Point, ELightType::Spot})
    {
        for (const FLightSceneProxy* Light : Scene.Lights)
        {
            if (!Light->IsVisible() || Light->GetLightType() != Type) continue;
            if (!Light->CastsShadow()) continue;
            if (Type == ELightType::Point)
            {
                const FPointLightInfo& Info = static_cast<const FPointLightSceneProxy*>(Light)->GetInfo();
                if (Info.AttenuationRadius <= 0.0f) continue;
                OutData.ShadowRequestsPerLight.Add({Light, NextCube++});
            }
            else
            {
                const FSpotLightInfo& Info = static_cast<const FSpotLightSceneProxy*>(Light)->GetInfo();
                if (Info.AttenuationRadius <= 0.0f) continue;
                OutData.ShadowRequestsPerLight.Add({Light, Next2DSlice++});
            }
        }
    }
}

void FSceneRenderer::BuildShadowCasterQueue(UWorld& World, FShadowDepthPassInfo& Pass)
{
    FRenderQueue& Queue = Pass.CasterPackets;
    Queue.Reset();
    const FFrustumPlanes& Frustum = Pass.Frustum;
    World.GetScene().BVH.QueryCull(FrustumAllPlanesMask, 
                                    [&Frustum](const FBox& Bounds, uint32& Mask)
                                    {
                                        return static_cast<EBVHCullResult>(ClassifyBoxInFrustum(Bounds, Frustum, Mask));
                                    }, 
                                    [&](FPrimitiveSceneProxy* Proxy)
                                    {
                                        if (!Proxy->IsVisible() || !Proxy->CastsShadow()) return;
                                        UStaticMeshComponent* Component = Cast<UStaticMeshComponent>(Proxy->GetComponent());
                                        if (!Component || !Component->GetOwner() || Component->GetOwner()->GetWorld() != &World) return;
                                        if (World.IsPlayInEditor() && Component->IsHiddenInGame()) return;
                                        Component->SubmitToRenderQueue(Queue, 0);
                                    }
                                    );

    // Section 패킷의 인덱스 범위는 유지하고, 불투명 Static Mesh 이외는 제외한다.
    int32 Kept = 0;
    for (int32 Index = 0; Index < Queue.Num(); ++Index)
    {
        const FRenderPacket& Packet = Queue[Index];
        if (!Packet.Mesh || !Packet.Material || Packet.Material->ParamLayout != EMaterialParamLayout::StaticMesh || Packet.Material->BlendState != EBlendState::Opaque) continue;
        if (Kept != Index) Queue[Kept] = Packet;
        ++Kept;
    }
    Queue.SetNum(Kept);
}
const FShadowFrameData* FSceneRenderer::PrepareWorldShadows(UWorld& World, uint32 ResourceIndex)
{
    if (!bFrameActive || ResourceIndex >= MaxShadowResourceSlots) return nullptr;
    FWorldShadowFrame& Slot = WorldShadowFrames[ResourceIndex];
    if (Slot.PreparedFrame != FrameNumber)
    {
        BuildShadowRequests(World.GetScene(), Slot.Data);
        // 각 Light 단계에서 유효한 행렬·Frustum을 만든 뒤 Pass를 추가하고 Caster를 수집한다.
        // 아직 준비 경로가 없으므로 빈 Pass 목록을 유지한다. 미구현을 성공으로 처리하지 않는다.
        Slot.PreparedFrame = FrameNumber;
    }
    return &Slot.Data;
}

void FSceneRenderer::ReleaseWorldShadows(uint32 ResourceIndex)
{
    if (ResourceIndex >= MaxShadowResourceSlots) return;
    WorldShadowFrames[ResourceIndex].Data.ResetFrame();
    WorldShadowFrames[ResourceIndex].PreparedFrame = 0;
}
