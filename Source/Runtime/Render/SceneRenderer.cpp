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

bool FSceneRenderer::AllocateShadowMaps(FWorldShadowFrame& Slot)
{
    FShadowFrameData& Frame = Slot.Data;

    // 추후 에디터에서 조정할 수 있도록 해야함.
    constexpr uint32 AtlasSize = 4096;
    constexpr uint32 MinTileSize = 256;

    Frame.ShadowAtlasSize = AtlasSize;

    // CPU 배치는 매 프레임 다시 만든다.
    if (!Slot.AtlasAllocator.Initialize(AtlasSize, MinTileSize))
        return false;

    TArray<uint32> RequestIndices;

    for (uint32 Index = 0;
        Index < Frame.ShadowMapRequests.Num();
        ++Index)
    {
        FShadowMapRequest& Request = Frame.ShadowMapRequests[Index];

        // 요청 해상도는 유지하고 이전 배치만 초기화한다.
        Request.AtlasRect = {};

        if (Request.Resolution == 0)
        {
            Request.Resolution = DefaultShadowResolution;
        }

        RequestIndices.Add(Index);
    }

    // 원본 Map 순서를 유지하고 배치 순서만 정렬한다.
    std::stable_sort(RequestIndices.begin(), RequestIndices.end(),
        [&Frame](uint32 A, uint32 B)
        {
            return Frame.ShadowMapRequests[A].Resolution > Frame.ShadowMapRequests[B].Resolution;
        });

    const float InvAtlasSize = 1.0f / static_cast<float>(AtlasSize);
    for (uint32 MapRequestIndex : RequestIndices)
    {
        FShadowMapRequest& Request =
            Frame.ShadowMapRequests[MapRequestIndex];

        if (Request.ShadowInfoIndex >= Frame.ShadowInfos.Num() ||
            !Slot.AtlasAllocator.Allocate(Request.Resolution, Request.AtlasRect))
        {
            return false;
        }

        const FShadowAtlasRect& Rect = Request.AtlasRect;
        Frame.ShadowInfos[Request.ShadowInfoIndex].AtlasUVScaleBias = FVector4(
            Rect.Size * InvAtlasSize, Rect.Size * InvAtlasSize,
            Rect.X * InvAtlasSize, Rect.Y * InvAtlasSize);
    }

    return true;
}

uint32 FSceneRenderer::RegisterShadowMap(FShadowFrameData& Frame, uint32 LightRequestIndex, const FShadowInfo& Info, uint32 Resolution)
{
    FShadowRequestPerLight& LightRequest = Frame.ShadowRequestsPerLight[LightRequestIndex];

    const uint32 ShadowInfoIndex = Frame.ShadowInfos.Add(Info);

    // Light는 첫 Map의 GPU 조회 정보 번호를 가리킨다.
    if (LightRequest.ShadowIndex == InvalidShadowIndex)
    {
        LightRequest.ShadowIndex = ShadowInfoIndex;
    }

    assert(ShadowInfoIndex == LightRequest.ShadowIndex + LightRequest.ShadowMapCount);
    ++LightRequest.ShadowMapCount;
    Frame.ShadowInfos[LightRequest.ShadowIndex].MapCount = LightRequest.ShadowMapCount;

    FShadowMapRequest MapRequest;
    MapRequest.LightRequestIndex = LightRequestIndex;
    MapRequest.ShadowInfoIndex = ShadowInfoIndex;
    MapRequest.Resolution = Resolution;

    // TODO(CSM): Cascade별 Info와 Resolution을 이 함수로 등록한다.
    return Frame.ShadowMapRequests.Add(MapRequest);
}

void FSceneRenderer::BuildShadowRequests(const FScene& Scene, FShadowFrameData& OutData)
{
    OutData.ResetFrame();
    const FLightSceneProxy* Directional = nullptr;
    for (const FLightSceneProxy* Light : Scene.Lights)
        if (Light->IsVisible() && Light->GetLightType() == ELightType::Directional)
            Directional = Light;

    // 기존 Directional 선택과 동일하게 마지막 visible 광원만 사용한다.
    if (Directional && Directional->CastsShadow())
        OutData.ShadowRequestsPerLight.Add({Directional});

    // BuildStructuredLight와 같은 visible 조건·Point/Spot 순서다.
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
                OutData.ShadowRequestsPerLight.Add({Light});
            }
            else
            {
                const FSpotLightInfo& Info = static_cast<const FSpotLightSceneProxy*>(Light)->GetInfo();
                if (Info.AttenuationRadius <= 0.0f) continue;
                OutData.ShadowRequestsPerLight.Add({Light});
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
            else if (Slot.Data.ShadowRequestsPerLight[Index].Light->GetLightType() == ELightType::Point)
                PreparePointShadow(World, Index, Slot.Data);
            else if (Slot.Data.ShadowRequestsPerLight[Index].Light->GetLightType() == ELightType::Directional)
            {
                // 일단은 Directinal Light 1개 
                PrepareDirectionalShadow(World, Index, Slot.Data);

                // TODO(CSM): View별 Cascade를 등록하고 모든 View 수집 후 Atlas를 배치한다.
            }

        }

        // 성공/실패와 관계없이 같은 프레임에 준비를 반복하지 않는다.
        Slot.PreparedFrame = FrameNumber;

        // 생성된 Map 요청을 Atlas에 배치한다.
        if (!AllocateShadowMaps(Slot))
        {
            // 부분적으로 배치된 자료를 렌더링에 넘기지 않는다.
            Slot.Data.ResetFrame();
            return nullptr;
        }
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
    constexpr uint32 ShadowResolution = 256;
    if (Point.AttenuationRadius <= 0.0f) return;
    // D3D Cube 순서 +X,-X,+Y,-Y,+Z,-Z. 면의 Up도 Cube 조회 규약에 맞춘다.
    const FVector Directions[] = {FVector(1,0,0), FVector(-1,0,0), FVector(0,1,0), FVector(0,-1,0), FVector(0,0,1), FVector(0,0,-1)};
    const FVector Ups[] = {FVector(0,1,0), FVector(0,1,0), FVector(0,0,-1), FVector(0,0,1), FVector(0,1,0), FVector(0,1,0)};
    FCameraProjection Projection;
    Projection.FovDegrees = 90.0f;
    constexpr float DefaultShadowNear = 0.01f;
    Projection.NearClip = std::min(DefaultShadowNear, Point.AttenuationRadius * 0.01f);
    Projection.FarClip = Point.AttenuationRadius;
    const FMatrix ProjectionMatrix = Projection.GetMatrix(1.0f);
    for (uint32 Face = 0; Face < 6; ++Face)
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
        Pass.ViewProjection = View * ProjectionMatrix;
        Pass.Frustum = ExtractFrustumPlanes(Pass.ViewProjection);
        BuildShadowCasterQueue(World, Pass);


        FShadowInfo Info;
        Info.ViewProjection = Pass.ViewProjection;
        Info.LightPosition = Point.Position;
        Info.InvRadius = 1.0f / Point.AttenuationRadius;
        Info.LightType = static_cast<uint32>(ELightType::Point);
        Info.Bias = Light->GetShadowBias() * Info.InvRadius;
        Info.SlopeBias = Light->GetShadowSlopeBias() * Info.InvRadius;
        Info.MaxSlope = Light->GetShadowMaxSlope();

        Pass.MapRequestIndex = RegisterShadowMap(Frame, RequestIndex, Info, ShadowResolution);

        Frame.ShadowDepthPassInfos.Add(std::move(Pass));
    }
}

void FSceneRenderer::PrepareSpotShadow(UWorld& World, uint32 RequestIndex, FShadowFrameData& Frame)
{
    FShadowRequestPerLight& Request = Frame.ShadowRequestsPerLight[RequestIndex];
    const auto* Light = static_cast<const FSpotLightSceneProxy*>(Request.Light);
    const FSpotLightInfo& Spot = Light->GetInfo();
    constexpr uint32 ShadowResolution = 512;
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
    Pass.ViewProjection = View * Projection.GetMatrix(1.0f);
    Pass.Frustum = ExtractFrustumPlanes(Pass.ViewProjection);
    BuildShadowCasterQueue(World, Pass);

    FShadowInfo Info;
    Info.ViewProjection = Pass.ViewProjection;
    Info.LightPosition = Spot.Position;
    Info.InvRadius = 1.0f / Spot.AttenuationRadius;
    Info.LightType = static_cast<uint32>(ELightType::Spot);
    Info.Bias = Light->GetShadowBias() * Info.InvRadius;
    Info.SlopeBias = Light->GetShadowSlopeBias() * Info.InvRadius;
    Info.MaxSlope = Light->GetShadowMaxSlope();

    // Map 요청을 만들고 이 Pass와 연결한다.
    Pass.MapRequestIndex = RegisterShadowMap(Frame, RequestIndex, Info, ShadowResolution);
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
    Pass.ViewProjection = View * Projection.GetMatrix(1.0f);
    Pass.Frustum = ExtractFrustumPlanes(Pass.ViewProjection);
    BuildShadowCasterQueue(World, Pass);

    FShadowInfo Info;
    Info.ViewProjection = Pass.ViewProjection;
    Info.LightType = static_cast<uint32>(ELightType::Directional);
    // Orthographic 깊이는 선형이므로 월드 Bias를 Far-Near로 나눈다.
    const float InvDepthRange = 1.0f / (Projection.FarClip - Projection.NearClip);
    Info.Bias = Light->GetShadowBias() * InvDepthRange;
    Info.SlopeBias = Light->GetShadowSlopeBias() * InvDepthRange;
    Info.MaxSlope = Light->GetShadowMaxSlope();
    // TODO(CSM): Cascade별 행렬로 등록하되 같은 Atlas 출력 경로를 사용한다.
    Pass.MapRequestIndex = RegisterShadowMap(Frame, RequestIndex, Info, DefaultShadowResolution);
    Frame.ShadowDepthPassInfos.Add(std::move(Pass));
}
