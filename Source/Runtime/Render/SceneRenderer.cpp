#include "EnginePCH.h"
#include "Render/SceneRenderer.h"
#include "Component/PrimitiveComponent.h"
#include "Component/StaticMeshComponent.h"
#include "Component/BillboardComponent.h"
#include "Component/ParticleSubUVComponent.h"
#include "Engine/PrimitiveSceneProxy.h"
#include "Engine/World.h"
#include "Engine/LightSceneProxy.h"
#include "Camera/CameraProjection.h"
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
        for (uint32 Index = 0; Index < static_cast<uint32>(Slot.Data.ShadowRequestsPerLight.Num()); ++Index)
        {
            if (Slot.Data.ShadowRequestsPerLight[Index].Light->GetLightType() == ELightType::Spot)
                PrepareSpotShadow(World, Index, Slot.Data);
            else if (Slot.Data.ShadowRequestsPerLight[Index].Light->GetLightType() == ELightType::Directional)
                PrepareDirectionalShadow(World, Index, Slot.Data);
            else if (Slot.Data.ShadowRequestsPerLight[Index].Light->GetLightType() == ELightType::Point)
                PreparePointShadow(World, Index, Slot.Data);
        }
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

void FSceneRenderer::PreparePointShadow(UWorld& World, uint32 RequestIndex, FShadowFrameData& Frame)
{
    FShadowRequestPerLight& Request = Frame.ShadowRequestsPerLight[RequestIndex];
    const auto* Light = static_cast<const FPointLightSceneProxy*>(Request.Light);
    const FPointLightInfo& Point = Light->GetInfo();
    // D3D Cube 순서 +X,-X,+Y,-Y,+Z,-Z. 면의 Up도 Cube 조회 규약에 맞춘다.
    const FVector Directions[] = {FVector(1,0,0), FVector(-1,0,0), FVector(0,1,0), FVector(0,-1,0), FVector(0,0,1), FVector(0,0,-1)};
    const FVector Ups[] = {FVector(0,1,0), FVector(0,1,0), FVector(0,0,-1), FVector(0,0,1), FVector(0,1,0), FVector(0,1,0)};
    FCameraProjection Projection;
    Projection.FovDegrees = 90.0f;
    constexpr float DefaultShadowNear = 0.01f;
    Projection.NearClip = std::min(DefaultShadowNear, Point.AttenuationRadius * 0.01f);
    Projection.FarClip = Point.AttenuationRadius;
    const FMatrix ProjectionMatrix = Projection.GetMatrix(1.0f);
    for (uint32 Face = 0; Face < FTextureCubeArray::FaceCount; ++Face)
    {
        const FVector& Forward = Directions[Face];
        const FVector& Up = Ups[Face];
        const FVector Right = FVector::Cross(Up, Forward);
        // 엔진의 View 축은 +X 전방, +Y 오른쪽, +Z 위쪽이다.
        FMatrix Rotation = FMatrix::Identity;
        Rotation.SetAxis(0, Forward);
        Rotation.SetAxis(1, Right);
        Rotation.SetAxis(2, Up);
        const FMatrix View = FMatrix::MakeTranslation(Point.Position * -1.0f) * Rotation.GetTransposed();
        FShadowDepthPassInfo Pass;
        Pass.RequestIndex = RequestIndex;
        Pass.ArraySlice = Request.ShadowMapIndex * FTextureCubeArray::FaceCount + Face;
        Pass.ViewProjection = View * ProjectionMatrix;
        Pass.Frustum = ExtractFrustumPlanes(Pass.ViewProjection);
        BuildShadowCasterQueue(World, Pass);
        Frame.ShadowDepthPassInfos.Add(std::move(Pass));
    }

    // 조회 정보는 Light당 하나다. Cube 조회에는 6면 행렬 대신 위치와 Cube 번호를 쓴다.
    FShadowInfo Info;
    Info.LightPosition = Point.Position;
    Info.InvRadius = 1.0f / Point.AttenuationRadius;
    Info.ShadowMapIndex = Request.ShadowMapIndex;
    Info.LightType = static_cast<uint32>(ELightType::Point);
    Info.Bias = Light->GetShadowBias() * Info.InvRadius;
    Info.SlopeBias = Light->GetShadowSlopeBias();
    Info.MaxSlope = Light->GetShadowMaxSlope();
    // 면 중심 기준 T = 2 * d * tan(45도) / Res. 면 가장자리 텍셀은 더 작으므로 중심값이 상한이다.
    Info.HalfTexelScale = 1.0f / static_cast<float>(Frame.Resolution);
    Info.NormalOffset = Light->GetShadowNormalOffset();
    Request.ShadowIndex = static_cast<uint32>(Frame.ShadowInfos.Num());
    Frame.ShadowInfos.Add(Info);
}

void FSceneRenderer::PrepareSpotShadow(UWorld& World, uint32 RequestIndex, FShadowFrameData& Frame)
{
    FShadowRequestPerLight& Request = Frame.ShadowRequestsPerLight[RequestIndex];
    const auto* Light = static_cast<const FSpotLightSceneProxy*>(Request.Light);
    const FSpotLightInfo& Spot = Light->GetInfo();
    const FVector Forward = Spot.Direction.GetSafeNormal();
    // Perspective에 유효한 전방 방향과 0 < 반각 < 90도가 필요하다.
    if (Forward.Dot(Forward) == 0.0f || Spot.CosOuterCone <= 0.0f || Spot.CosOuterCone >= 1.0f) return;
    const FVector UpGuide = std::abs(Forward.Z) > 0.99f ? FVector(0, 1, 0) : FVector(0, 0, 1);
    const FVector Right = FVector::Cross(UpGuide, Forward).GetSafeNormal();
    const FVector Up = FVector::Cross(Forward, Right);
    FMatrix Rotation = FMatrix::Identity;
    Rotation.SetAxis(0, Forward);
    Rotation.SetAxis(1, Right);
    Rotation.SetAxis(2, Up);
    const FMatrix View = FMatrix::MakeTranslation(Spot.Position * -1.0f) * Rotation.GetTransposed();
    FCameraProjection Projection;
    Projection.FovDegrees = FMath::RadiansToDegrees(2.0f * std::acos(Spot.CosOuterCone));
    // 월드 거리 0.01을 기본으로 하되 작은 Light도 Far보다 작은 Near를 유지한다.
    constexpr float DefaultShadowNear = 0.01f;
    Projection.NearClip = std::min(DefaultShadowNear, Spot.AttenuationRadius * 0.01f);
    Projection.FarClip = Spot.AttenuationRadius;

    FShadowDepthPassInfo Pass;
    Pass.RequestIndex = RequestIndex;
    Pass.ArraySlice = Request.ShadowMapIndex;
    Pass.ViewProjection = View * Projection.GetMatrix(1.0f);
    Pass.Frustum = ExtractFrustumPlanes(Pass.ViewProjection);
    BuildShadowCasterQueue(World, Pass);

    FShadowInfo Info;
    Info.ViewProjection = Pass.ViewProjection;
    Info.LightPosition = Spot.Position;
    Info.InvRadius = 1.0f / Spot.AttenuationRadius;
    Info.ShadowMapIndex = Request.ShadowMapIndex;
    Info.LightType = static_cast<uint32>(ELightType::Spot);
    Info.Bias = Light->GetShadowBias() * Info.InvRadius;
    Info.SlopeBias = Light->GetShadowSlopeBias();
    Info.MaxSlope = Light->GetShadowMaxSlope();
    // 중심축 기준 T = 2 * d * tan(반각) / Res.
    const float TanHalfFov = std::sqrt(1.0f - Spot.CosOuterCone * Spot.CosOuterCone) / Spot.CosOuterCone;
    Info.HalfTexelScale = TanHalfFov / static_cast<float>(Frame.Resolution);
    Info.NormalOffset = Light->GetShadowNormalOffset();
    Request.ShadowIndex = static_cast<uint32>(Frame.ShadowInfos.Num());
    Frame.ShadowInfos.Add(Info);
    Frame.ShadowDepthPassInfos.Add(std::move(Pass));
}

void FSceneRenderer::PrepareDirectionalShadow(UWorld& World, uint32 RequestIndex, FShadowFrameData& Frame)
{
    FShadowRequestPerLight& Request = Frame.ShadowRequestsPerLight[RequestIndex];
    const auto* Light = static_cast<const FDirectionalLightSceneProxy*>(Request.Light);
    const FVector Forward = Light->GetInfo().Direction.GetSafeNormal();
    if (Forward.Dot(Forward) == 0.0f) return;

    // 기존 BVH가 갱신한 장면 경계를 재사용한다. 실제 Caster 선별은 Queue 수집에서 한다.
    const FBox* Bounds = World.GetScene().BVH.GetRootBounds();
    if (!Bounds) return;
    const FVector Center = (Bounds->Min + Bounds->Max) * 0.5f;
    const float Radius = (Bounds->Max - Center).Size();
    if (Radius <= 0.0f) return;

    // 장면 AABB를 덮는 구: Eye=C-d*2r, 폭/높이=2r, Near=r, Far=3r.
    const FVector Eye = Center - Forward * (2.0f * Radius);
    const FVector UpGuide = std::abs(Forward.Z) > 0.99f ? FVector(0, 1, 0) : FVector(0, 0, 1);
    const FVector Right = FVector::Cross(UpGuide, Forward).GetSafeNormal();
    const FVector Up = FVector::Cross(Forward, Right);
    FMatrix Rotation = FMatrix::Identity;
    Rotation.SetAxis(0, Forward);
    Rotation.SetAxis(1, Right);
    Rotation.SetAxis(2, Up);
    const FMatrix View = FMatrix::MakeTranslation(Eye * -1.0f) * Rotation.GetTransposed();
    FCameraProjection Projection;
    Projection.Mode = EProjectionMode::Orthographic;
    Projection.OrthoWidth = 2.0f * Radius;
    Projection.NearClip = Radius;
    Projection.FarClip = 3.0f * Radius;

    FShadowDepthPassInfo Pass;
    Pass.RequestIndex = RequestIndex;
    Pass.ArraySlice = Request.ShadowMapIndex;
    Pass.ViewProjection = View * Projection.GetMatrix(1.0f);
    Pass.Frustum = ExtractFrustumPlanes(Pass.ViewProjection);
    BuildShadowCasterQueue(World, Pass);

    FShadowInfo Info;
    Info.ViewProjection = Pass.ViewProjection;
    Info.ShadowMapIndex = Request.ShadowMapIndex;
    Info.LightType = static_cast<uint32>(ELightType::Directional);
    // Orthographic 깊이는 선형이므로 월드 거리를 Far-Near로 나누면 비교 깊이가 된다.
    const float InvDepthRange = 1.0f / (Projection.FarClip - Projection.NearClip);
    Info.InvRadius = InvDepthRange;
    Info.Bias = Light->GetShadowBias() * InvDepthRange;
    Info.SlopeBias = Light->GetShadowSlopeBias();
    Info.MaxSlope = Light->GetShadowMaxSlope();
    // Orthographic은 텍셀 크기가 맵 전체에서 일정하다: T = OrthoWidth / Res.
    const float TexelSize = Projection.OrthoWidth / static_cast<float>(Frame.Resolution);
    Info.HalfTexelScale = 0.5f * TexelSize;
    Info.NormalOffset = Light->GetShadowNormalOffset();
    Request.ShadowIndex = static_cast<uint32>(Frame.ShadowInfos.Num());
    Frame.ShadowInfos.Add(Info);
    Frame.ShadowDepthPassInfos.Add(std::move(Pass));
}
