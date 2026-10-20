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
#include <cfloat>
#include <cstring>

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

bool IsBlack(const FVector& Color)
{
    return Color.X <= 0.0f && Color.Y <= 0.0f && Color.Z <= 0.0f;
}

bool IsSphereOutsideFrustum(const FVector& Center, float Radius, const FFrustumPlanes& Frustum)
{
    for (const FPlane& Plane : Frustum.Planes)
        if (Plane.Normal.Dot(Center) + Plane.Distance < -Radius) return true;
    return false;
}

// 점들이 평면 하나의 바깥에 모두 있으면 그 점들의 볼록 껍질도 그 평면 바깥이다.
bool ArePointsOutsideFrustum(const FVector* Points, uint32 Count, const FFrustumPlanes& Frustum)
{
    for (const FPlane& Plane : Frustum.Planes)
    {
        bool bAllOutside = true;
        for (uint32 Index = 0; Index < Count && bAllOutside; ++Index)
            bAllOutside = Plane.Normal.Dot(Points[Index]) + Plane.Distance < 0.0f;
        if (bAllOutside) return true;
    }
    return false;
}

// 값이 클수록 더 많은 단계를 통과했다. 컬링 이유는 가장 멀리 간 단계로 기록한다.
enum class ELightViewResult : uint8 { OutsideView, TooFar, TooSmall, Visible };

// 어느 View 하나라도 프러스텀 안 + 거리 안 + 크기 이상이면 Visible이다.
ELightViewResult TestLightAgainstViews(const FVector& Center, float Radius, const TArray<FShadowCullingView>& Views, const FShadowCullingSettings& Settings)
{
    if (Views.IsEmpty()) return ELightViewResult::Visible;
    ELightViewResult Result = ELightViewResult::OutsideView;
    for (const FShadowCullingView& View : Views)
    {
        if (Settings.bCullLightsByView && IsSphereOutsideFrustum(Center, Radius, View.Frustum)) continue;
        const float Distance = (Center - View.Location).Size();
        if (Settings.ShadowDistance > 0.0f && Distance - Radius > Settings.ShadowDistance)
        {
            Result = std::max(Result, ELightViewResult::TooFar);
            continue;
        }
        if (Settings.MinLightScreenRadius > 0.0f)
        {
            // 원근 카메라가 영향 범위 안에 있으면 화면 전체를 덮는다.
            const bool bCameraInside = !View.bOrthographic && Distance <= Radius;
            const float ScreenRadius = View.bOrthographic ? Radius * View.PixelsPerUnit : Radius * View.PixelsPerUnit / std::max(Distance, 1.0e-4f);
            if (!bCameraInside && ScreenRadius < Settings.MinLightScreenRadius)
            {
                Result = std::max(Result, ELightViewResult::TooSmall);
                continue;
            }
        }
        return ELightViewResult::Visible;
    }
    return Result;
}

// 큐브맵 한 면이 맡는 구 영역(|y|,|z| <= x <= R)은 라이트 위치와, 거리 R 평면 위 반폭 R 사각형으로 만든 피라미드 안에 들어간다.
bool IsPointFaceVisible(const FVector& LightPosition, float Radius, const FVector& Forward, const FVector& Right, const FVector& Up, const TArray<FShadowCullingView>& Views)
{
    if (Views.IsEmpty()) return true;
    const FVector Base = LightPosition + Forward * Radius;
    const FVector Points[5] = {
        LightPosition,
        Base + (Right + Up) * Radius, Base + (Right - Up) * Radius,
        Base - (Right - Up) * Radius, Base - (Right + Up) * Radius};
    for (const FShadowCullingView& View : Views)
        if (!ArePointsOutsideFrustum(Points, 5, View.Frustum)) return true;
    return false;
}

// Caster 박스와 빛 방향으로 늘린 그림자 볼륨을 함께 감싸는 점 16개로 판정한다.
// 어느 View 프러스텀과도 겹치지 않으면 그 Caster의 그림자는 화면 어디에도 닿지 않는다.
bool IsCasterShadowVisible(const FAABB& Bounds, const FShadowCasterCullContext& Context, const TArray<FShadowCullingView>& Views)
{
    if (Views.IsEmpty()) return true;
    FVector Points[16];
    for (uint32 Corner = 0; Corner < 8; ++Corner)
    {
        Points[Corner] = Bounds.Center + FVector(
            (Corner & 1) ? Bounds.Extent.X : -Bounds.Extent.X,
            (Corner & 2) ? Bounds.Extent.Y : -Bounds.Extent.Y,
            (Corner & 4) ? Bounds.Extent.Z : -Bounds.Extent.Z);
    }
    if (Context.LightType == ELightType::Directional)
    {
        // 그림자는 Receiver가 있는 장면 범위 안에만 생기므로, 볼륨이 장면 바운드를 빠져나가는 곳까지만 늘린다.
        // 축마다 "출구 평면에서 가장 먼 꼭짓점이 그 평면에 닿는 거리"를 구하고 그중 최소를 쓴다.
        // 박스 안의 어느 점도 이 거리 안에 장면을 벗어나므로 보수적이다(화면에 닿는 그림자를 지우지 않는다).
        const float Direction[3] = {Context.LightDirection.X, Context.LightDirection.Y, Context.LightDirection.Z};
        const float BoxMin[3] = {Bounds.Center.X - Bounds.Extent.X, Bounds.Center.Y - Bounds.Extent.Y, Bounds.Center.Z - Bounds.Extent.Z};
        const float BoxMax[3] = {Bounds.Center.X + Bounds.Extent.X, Bounds.Center.Y + Bounds.Extent.Y, Bounds.Center.Z + Bounds.Extent.Z};
        const float SceneMin[3] = {Context.SceneMin.X, Context.SceneMin.Y, Context.SceneMin.Z};
        const float SceneMax[3] = {Context.SceneMax.X, Context.SceneMax.Y, Context.SceneMax.Z};
        float Extrude = Context.ExtrudeDistance;
        for (uint32 Axis = 0; Axis < 3; ++Axis)
        {
            if (std::abs(Direction[Axis]) < 1.0e-4f) continue;
            const float ExitPlane = Direction[Axis] > 0.0f ? SceneMax[Axis] : SceneMin[Axis];
            const float FarthestCorner = Direction[Axis] > 0.0f ? BoxMin[Axis] : BoxMax[Axis];
            Extrude = std::min(Extrude, std::max((ExitPlane - FarthestCorner) / Direction[Axis], 0.0f));
        }
        const FVector Offset = Context.LightDirection * Extrude;
        for (uint32 Corner = 0; Corner < 8; ++Corner)
            Points[8 + Corner] = Points[Corner] + Offset;
    }
    else
    {
        // 그림자 볼륨의 끝은 반경 R 구면이다. 라이트에서 박스를 보는 반각을 beta라 하면,
        // 그 구면 조각은 각 꼭짓점 방향으로 R / cos(beta)까지 늘린 점들의 껍질 안에 들어간다.
        const float CenterDistance = (Bounds.Center - Context.LightPosition).Size();
        const float BoundsRadius = Bounds.Extent.Size();
        const float SinBeta = CenterDistance > 0.0f ? BoundsRadius / CenterDistance : 1.0f;
        // 라이트가 박스 안이거나 너무 가까우면 볼륨이 사방으로 퍼지므로 컬링하지 않는다.
        if (SinBeta > 0.99f) return true;
        const float FarDistance = Context.ExtrudeDistance / std::sqrt(1.0f - SinBeta * SinBeta);
        for (uint32 Corner = 0; Corner < 8; ++Corner)
            Points[8 + Corner] = Context.LightPosition + (Points[Corner] - Context.LightPosition).GetSafeNormal() * FarDistance;
    }
    for (const FShadowCullingView& View : Views)
        if (!ArePointsOutsideFrustum(Points, 16, View.Frustum)) return true;
    return false;
}

// 점에서 박스까지 가장 가까운 거리의 제곱. 점이 박스 안이면 0이다.
float DistanceSquaredToBox(const FVector& Point, const FAABB& Bounds)
{
    const float DX = std::max(std::abs(Point.X - Bounds.Center.X) - Bounds.Extent.X, 0.0f);
    const float DY = std::max(std::abs(Point.Y - Bounds.Center.Y) - Bounds.Extent.Y, 0.0f);
    const float DZ = std::max(std::abs(Point.Z - Bounds.Center.Z) - Bounds.Extent.Z, 0.0f);
    return DX * DX + DY * DY + DZ * DZ;
}

// Caster 바운딩 구의 쉐도우 맵 상 지름(텍셀).
float ComputeCasterTexels(const FAABB& Bounds, const FShadowCasterCullContext& Context)
{
    const float BoundsRadius = Bounds.Extent.Size();
    if (Context.LightType == ELightType::Directional)
        return 2.0f * BoundsRadius * Context.TexelsPerUnit;
    // Perspective는 거리에 반비례한다. 라이트가 박스 안이면 맵 전체를 덮는다.
    const float Distance = (Bounds.Center - Context.LightPosition).Size();
    if (Distance <= BoundsRadius) return FLT_MAX;
    return 2.0f * BoundsRadius * Context.TexelsPerUnit / Distance;
}
}

void FSceneRenderer::BeginFrame()
{
    assert(!bFrameActive);
    bFrameActive = true;
    ++FrameNumber;
    CapturedWorld = nullptr;
    RegisteredShadowViews.Reset();
}

void FSceneRenderer::AddShadowView(const UWorld& World, const FSceneView& View)
{
    FShadowCullingView CullingView;
    CullingView.Frustum = View.Frustum;
    CullingView.Location = View.GetCameraLocation();
    CullingView.bOrthographic = View.IsOrthographic();
    const float Height = View.ViewRect.Height;
    if (CullingView.bOrthographic)
    {
        // 직교: 화면 높이 픽셀 / 직교 높이(월드).
        const float Aspect = Height > 0.0f ? View.ViewRect.Width / Height : 1.0f;
        const float OrthoHeight = View.ProjectionSettings.OrthoWidth / Aspect;
        CullingView.PixelsPerUnit = OrthoHeight > 0.0f ? Height / OrthoHeight : 0.0f;
    }
    else
    {
        // 원근: 거리 1에서 반높이 tan(FOV/2)가 화면 반높이 픽셀에 대응한다.
        CullingView.PixelsPerUnit = 0.5f * Height / std::tan(FMath::DegreesToRadians(View.ProjectionSettings.FovDegrees) * 0.5f);
    }
    RegisteredShadowViews.Add({&World, CullingView});
}

const FShadowCullingStats* FSceneRenderer::GetShadowCullingStats(uint32 ResourceIndex) const
{
    if (ResourceIndex >= MaxShadowResourceSlots || WorldShadowFrames[ResourceIndex].PreparedFrame == 0) return nullptr;
    return &WorldShadowFrames[ResourceIndex].Data.CullingStats;
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
    OutData.CullingStats = {};
    FShadowCullingStats& Stats = OutData.CullingStats;
    const FLightSceneProxy* Directional = nullptr;
    for (const FLightSceneProxy* Light : Scene.Lights)
        if (Light->IsVisible() && Light->GetLightType() == ELightType::Directional)
            Directional = Light;

    // 기존 Directional 선택과 동일하게 마지막 visible 광원만 사용한다.
    // Directional은 장면 전체를 비추므로 View·거리·화면 크기 기준을 적용하지 않는다.
    if (Directional && Directional->CastsShadow())
    {
        ++Stats.LightsConsidered;
        OutData.ConsideredLightIds.Add(Directional->GetComponent()->GetUUID());
        if (IsBlack(static_cast<const FDirectionalLightSceneProxy*>(Directional)->GetInfo().Color))
            ++Stats.LightsCulledByIntensity;
        else
            OutData.ShadowRequestsPerLight.Add({Directional});
    }

    // BuildStructuredLight와 같은 visible 조건·Point/Spot 순서다.
    // 출력 위치는 Caster 컬링까지 끝난 뒤 Light별 준비 함수가 배정한다.
    for (ELightType Type : {ELightType::Point, ELightType::Spot})
    {
        for (const FLightSceneProxy* Light : Scene.Lights)
        {
            if (!Light->IsVisible() || Light->GetLightType() != Type) continue;
            if (!Light->CastsShadow()) continue;
            FVector Color;
            FVector CullingCenter;
            float CullingRadius = 0.0f;
            if (Type == ELightType::Point)
            {
                const FPointLightInfo& Info = static_cast<const FPointLightSceneProxy*>(Light)->GetInfo();
                if (Info.AttenuationRadius <= 0.0f) continue;
                Color = Info.Color;
                CullingCenter = Info.Position;
                CullingRadius = Info.AttenuationRadius;
            }
            else
            {
                const auto* Spot = static_cast<const FSpotLightSceneProxy*>(Light);
                if (Spot->GetInfo().AttenuationRadius <= 0.0f) continue;
                // 타일 라이트 컬링과 같은, 콘을 감싸는 구를 쓴다.
                Color = Spot->GetInfo().Color;
                CullingCenter = Spot->GetCullingSphereCenter();
                CullingRadius = Spot->GetCullingSphereRadius();
            }

            ++Stats.LightsConsidered;
            OutData.ConsideredLightIds.Add(Light->GetComponent()->GetUUID());
            if (IsBlack(Color))
            {
                ++Stats.LightsCulledByIntensity;
                continue;
            }
            const ELightViewResult ViewResult = TestLightAgainstViews(CullingCenter, CullingRadius, PreparingShadowViews, ShadowCullingSettings);
            if (ViewResult == ELightViewResult::OutsideView) { ++Stats.LightsCulledByView; continue; }
            if (ViewResult == ELightViewResult::TooFar) { ++Stats.LightsCulledByDistance; continue; }
            if (ViewResult == ELightViewResult::TooSmall) { ++Stats.LightsCulledByScreenSize; continue; }
            OutData.ShadowRequestsPerLight.Add({Light});
        }
    }
}

void FSceneRenderer::BuildShadowCasterQueue(UWorld& World, FShadowDepthPassInfo& Pass, const FShadowCasterCullContext& Context, FShadowCullingStats& Stats)
{
    FRenderQueue& Queue = Pass.CasterPackets;
    Queue.Reset();
    const FFrustumPlanes& Frustum = Pass.Frustum;
    // 라이트 프러스텀 밖은 BVH가 걸러낸다.
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
                                        const FAABB& Bounds = Proxy->GetBounds();
                                        // 프러스텀 판정은 평면마다 따로 하므로, 바닥처럼 넓은 박스는 실제로 겹치지 않아도 통과할 수 있다.
                                        // Point/Spot은 영향 구와 박스 거리로 한 번 더 걸러 라이트 프러스텀 컬링의 일부로 처리한다.
                                        if (Context.LightType != ELightType::Directional &&
                                            DistanceSquaredToBox(Context.LightPosition, Bounds) > Context.ExtrudeDistance * Context.ExtrudeDistance)
                                            return;
                                        ++Stats.CastersConsidered;
                                        if (ShadowCullingSettings.MinCasterTexels > 0.0f && ComputeCasterTexels(Bounds, Context) < ShadowCullingSettings.MinCasterTexels)
                                        {
                                            ++Stats.CastersCulledBySize;
                                            return;
                                        }
                                        if (ShadowCullingSettings.bCullCastersByView && !IsCasterShadowVisible(Bounds, Context, PreparingShadowViews))
                                        {
                                            ++Stats.CastersCulledByView;
                                            return;
                                        }
                                        // 아래 Material 필터 전 수다. 반투명만 있는 Caster도 여기서는 센다.
                                        ++Stats.CastersDrawn;
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

namespace
{
// 빈자리가 있으면 재사용하고, 없으면 끝에 하나 늘린다.
uint32 AllocateMapIndex(TArray<uint8>& Used)
{
    for (int32 Index = 0; Index < Used.Num(); ++Index)
    {
        if (!Used[Index])
        {
            Used[Index] = 1;
            return static_cast<uint32>(Index);
        }
    }
    Used.Add(1);
    return static_cast<uint32>(Used.Num() - 1);
}

void FreeMapIndex(TArray<uint8>& Used, uint32 Index)
{
    if (Index < static_cast<uint32>(Used.Num())) Used[Index] = 0;
}

// 사용 중인 가장 큰 번호 + 1. GPU 배열은 이 크기 이상이어야 한다.
uint32 GetRequiredMapCount(const TArray<uint8>& Used)
{
    for (int32 Index = Used.Num() - 1; Index >= 0; --Index)
        if (Used[Index]) return static_cast<uint32>(Index + 1);
    return 0;
}

// 엔진의 View 축은 +X 전방, +Y 오른쪽, +Z 위쪽이다.
FMatrix MakeLightView(const FVector& Eye, const FVector& Forward, const FVector& Right, const FVector& Up)
{
    FMatrix Rotation = FMatrix::Identity;
    Rotation.SetAxis(0, Forward);
    Rotation.SetAxis(1, Right);
    Rotation.SetAxis(2, Up);
    return FMatrix::MakeTranslation(Eye * -1.0f) * Rotation.GetTransposed();
}

bool BuildPointSetup(const FPointLightSceneProxy& Light, uint32 Resolution, FLightShadowSetup& Out)
{
    const FPointLightInfo& Point = Light.GetInfo();
    // D3D Cube 순서 +X,-X,+Y,-Y,+Z,-Z. 면의 Up도 Cube 조회 규약에 맞춘다.
    const FVector Directions[] = {FVector(1,0,0), FVector(-1,0,0), FVector(0,1,0), FVector(0,-1,0), FVector(0,0,1), FVector(0,0,-1)};
    const FVector Ups[] = {FVector(0,1,0), FVector(0,1,0), FVector(0,0,-1), FVector(0,0,1), FVector(0,1,0), FVector(0,1,0)};
    FCameraProjection Projection;
    Projection.FovDegrees = 90.0f;
    constexpr float DefaultShadowNear = 0.01f;
    Projection.NearClip = std::min(DefaultShadowNear, Point.AttenuationRadius * 0.01f);
    Projection.FarClip = Point.AttenuationRadius;
    const FMatrix ProjectionMatrix = Projection.GetMatrix(1.0f);

    Out.PassCount = FTextureCubeArray::FaceCount;
    for (uint32 Face = 0; Face < FTextureCubeArray::FaceCount; ++Face)
    {
        Out.FaceForward[Face] = Directions[Face];
        Out.FaceUp[Face] = Ups[Face];
        Out.FaceRight[Face] = FVector::Cross(Ups[Face], Directions[Face]);
        Out.ViewProjections[Face] = MakeLightView(Point.Position, Out.FaceForward[Face], Out.FaceRight[Face], Out.FaceUp[Face]) * ProjectionMatrix;
        Out.Frusta[Face] = ExtractFrustumPlanes(Out.ViewProjections[Face]);
    }

    Out.Context.LightType = ELightType::Point;
    Out.Context.LightPosition = Point.Position;
    Out.Context.ExtrudeDistance = Point.AttenuationRadius;
    // 거리 d에서 텍셀 크기는 2 * d * tan(45도) / Res다.
    Out.Context.TexelsPerUnit = 0.5f * static_cast<float>(Resolution);

    // 조회 정보는 Light당 하나다. Cube 조회에는 6면 행렬 대신 위치와 Cube 번호를 쓴다.
    FShadowInfo& Info = Out.Info;
    Info.LightPosition = Point.Position;
    Info.InvRadius = 1.0f / Point.AttenuationRadius;
    Info.LightType = static_cast<uint32>(ELightType::Point);
    Info.Bias = Light.GetShadowBias() * Info.InvRadius;
    Info.SlopeBias = Light.GetShadowSlopeBias();
    Info.MaxSlope = Light.GetShadowMaxSlope();
    // 면 중심 기준 T = 2 * d * tan(45도) / Res. 면 가장자리 텍셀은 더 작으므로 중심값이 상한이다.
    Info.HalfTexelScale = 1.0f / static_cast<float>(Resolution);
    Info.NormalOffset = Light.GetShadowNormalOffset();
    // PCSS 커널 각반경 = 광원 반지름 * (dR - dB) / (dB * dR). 정규화 깊이(거리 / 감쇠 반경)로 쓰면 1 / 감쇠 반경이 남는다.
    Info.PenumbraScale = std::max(Light.GetSourceRadius(), 0.0f) * Info.InvRadius;
    Info.PCSSNearDepth = Projection.NearClip * Info.InvRadius;
    return true;
}

bool BuildSpotSetup(const FSpotLightSceneProxy& Light, uint32 Resolution, FLightShadowSetup& Out)
{
    const FSpotLightInfo& Spot = Light.GetInfo();
    const FVector Forward = Spot.Direction.GetSafeNormal();
    // Perspective에 유효한 전방 방향과 0 < 반각 < 90도가 필요하다.
    if (Forward.Dot(Forward) == 0.0f || Spot.CosOuterCone <= 0.0f || Spot.CosOuterCone >= 1.0f) return false;
    const FVector UpGuide = std::abs(Forward.Z) > 0.99f ? FVector(0, 1, 0) : FVector(0, 0, 1);
    const FVector Right = FVector::Cross(UpGuide, Forward).GetSafeNormal();
    const FVector Up = FVector::Cross(Forward, Right);
    FCameraProjection Projection;
    Projection.FovDegrees = FMath::RadiansToDegrees(2.0f * std::acos(Spot.CosOuterCone));
    // 월드 거리 0.01을 기본으로 하되 작은 Light도 Far보다 작은 Near를 유지한다.
    constexpr float DefaultShadowNear = 0.01f;
    Projection.NearClip = std::min(DefaultShadowNear, Spot.AttenuationRadius * 0.01f);
    Projection.FarClip = Spot.AttenuationRadius;
    // 중심축 기준 T = 2 * d * tan(반각) / Res.
    const float TanHalfFov = std::sqrt(1.0f - Spot.CosOuterCone * Spot.CosOuterCone) / Spot.CosOuterCone;

    Out.PassCount = 1;
    Out.ViewProjections[0] = MakeLightView(Spot.Position, Forward, Right, Up) * Projection.GetMatrix(1.0f);
    Out.Frusta[0] = ExtractFrustumPlanes(Out.ViewProjections[0]);

    Out.Context.LightType = ELightType::Spot;
    Out.Context.LightPosition = Spot.Position;
    Out.Context.ExtrudeDistance = Spot.AttenuationRadius;
    Out.Context.TexelsPerUnit = static_cast<float>(Resolution) / (2.0f * TanHalfFov);

    FShadowInfo& Info = Out.Info;
    Info.ViewProjection = Out.ViewProjections[0];
    Info.LightPosition = Spot.Position;
    Info.InvRadius = 1.0f / Spot.AttenuationRadius;
    Info.LightType = static_cast<uint32>(ELightType::Spot);
    Info.Bias = Light.GetShadowBias() * Info.InvRadius;
    Info.SlopeBias = Light.GetShadowSlopeBias();
    Info.MaxSlope = Light.GetShadowMaxSlope();
    Info.HalfTexelScale = TanHalfFov / static_cast<float>(Resolution);
    Info.NormalOffset = Light.GetShadowNormalOffset();
    // 거리 d의 섀도맵 UV 1은 월드 2 * d * tan(반각)이다. PCSS 커널 UV = 광원 반지름 * (zR - zB) / (zB * zR) / (2 * 감쇠 반경 * tan(반각)).
    Info.PenumbraScale = std::max(Light.GetSourceRadius(), 0.0f) / (2.0f * Spot.AttenuationRadius * TanHalfFov);
    Info.PCSSNearDepth = Projection.NearClip * Info.InvRadius;
    return true;
}

bool BuildDirectionalSetup(UWorld& World, const FDirectionalLightSceneProxy& Light, uint32 Resolution, FLightShadowSetup& Out)
{
    const FVector Forward = Light.GetInfo().Direction.GetSafeNormal();
    if (Forward.Dot(Forward) == 0.0f) return false;

    // 기존 BVH가 갱신한 장면 경계를 재사용한다. 실제 Caster 선별은 Queue 수집에서 한다.
    const FBox* Bounds = World.GetScene().BVH.GetRootBounds();
    if (!Bounds) return false;
    const FVector Center = (Bounds->Min + Bounds->Max) * 0.5f;
    const float Radius = (Bounds->Max - Center).Size();
    if (Radius <= 0.0f) return false;

    // 장면 AABB를 덮는 구: Eye=C-d*2r, 폭/높이=2r, Near=r, Far=3r.
    const FVector Eye = Center - Forward * (2.0f * Radius);
    const FVector UpGuide = std::abs(Forward.Z) > 0.99f ? FVector(0, 1, 0) : FVector(0, 0, 1);
    const FVector Right = FVector::Cross(UpGuide, Forward).GetSafeNormal();
    const FVector Up = FVector::Cross(Forward, Right);
    FCameraProjection Projection;
    Projection.Mode = EProjectionMode::Orthographic;
    Projection.OrthoWidth = 2.0f * Radius;
    Projection.NearClip = Radius;
    Projection.FarClip = 3.0f * Radius;

    Out.PassCount = 1;
    Out.ViewProjections[0] = MakeLightView(Eye, Forward, Right, Up) * Projection.GetMatrix(1.0f);
    Out.Frusta[0] = ExtractFrustumPlanes(Out.ViewProjections[0]);

    Out.Context.LightType = ELightType::Directional;
    Out.Context.LightDirection = Forward;
    // 장면 구 안의 Caster가 드리우는 그림자는 깊이 범위(Far - Near = 2r)를 넘지 않는다.
    Out.Context.ExtrudeDistance = Projection.FarClip - Projection.NearClip;
    Out.Context.SceneMin = Bounds->Min;
    Out.Context.SceneMax = Bounds->Max;
    Out.Context.TexelsPerUnit = static_cast<float>(Resolution) / Projection.OrthoWidth;

    FShadowInfo& Info = Out.Info;
    Info.ViewProjection = Out.ViewProjections[0];
    Info.LightType = static_cast<uint32>(ELightType::Directional);
    // Orthographic 깊이는 선형이므로 월드 거리를 Far-Near로 나누면 비교 깊이가 된다.
    const float InvDepthRange = 1.0f / (Projection.FarClip - Projection.NearClip);
    Info.InvRadius = InvDepthRange;
    Info.Bias = Light.GetShadowBias() * InvDepthRange;
    Info.SlopeBias = Light.GetShadowSlopeBias();
    Info.MaxSlope = Light.GetShadowMaxSlope();
    // Orthographic은 텍셀 크기가 맵 전체에서 일정하다: T = OrthoWidth / Res.
    const float TexelSize = Projection.OrthoWidth / static_cast<float>(Resolution);
    Info.HalfTexelScale = 0.5f * TexelSize;
    Info.NormalOffset = Light.GetShadowNormalOffset();
    // PCSS 커널 반경(월드) = 깊이 차 * tan(각지름/2). 깊이는 Far-Near, 가로는 OrthoWidth로 정규화한다.
    const float HalfAngle = FMath::DegreesToRadians(std::max(Light.GetLightSourceAngle(), 0.0f)) * 0.5f;
    Info.PenumbraScale = std::tan(HalfAngle) * (Projection.FarClip - Projection.NearClip) / Projection.OrthoWidth;
    return true;
}
}

const FShadowFrameData* FSceneRenderer::PrepareWorldShadows(UWorld& World, uint32 ResourceIndex)
{
    if (!bFrameActive || ResourceIndex >= MaxShadowResourceSlots) return nullptr;
    FWorldShadowFrame& Slot = WorldShadowFrames[ResourceIndex];
    if (Slot.PreparedFrame == FrameNumber) return &Slot.Data;

    PreparingShadowViews.Reset();
    for (const FRegisteredShadowView& Registered : RegisteredShadowViews)
        if (Registered.World == &World) PreparingShadowViews.Add(Registered.View);

    BuildShadowRequests(World.GetScene(), Slot.Data);
    FShadowCullingStats& Stats = Slot.Data.CullingStats;
    const bool bCaching = ShadowCullingSettings.bCacheShadowMaps;
    Stats.bCaching = bCaching;

    // 1. 캐시 무효화. 기준이 바뀌었으면 전부, 아니면 바뀐 Caster의 바운드가 걸친 면만 무효로 한다.
    //    이번 프레임에 그리지 않는(View 등으로 컬링된) Light의 캐시에도 적용해야 다음에 쓸 때 낡은 내용이 남지 않는다.
    TArray<FAABB> Invalidations;
    bool bInvalidateAll = false;
    World.GetScene().ConsumeShadowInvalidations(Invalidations, bInvalidateAll);
    bInvalidateAll = bInvalidateAll || !bCaching || !Slot.bCacheActive
        || Slot.CachedResolution != Slot.Data.Resolution
        || Slot.CachedMinCasterTexels != ShadowCullingSettings.MinCasterTexels;
    Slot.bCacheActive = bCaching;
    Slot.CachedResolution = Slot.Data.Resolution;
    Slot.CachedMinCasterTexels = ShadowCullingSettings.MinCasterTexels;
    for (auto& Pair : Slot.CacheEntries)
    {
        FShadowCacheEntry& Entry = Pair.second;
        Entry.bSeen = false;
        for (uint32 Face = 0; Face < Entry.PassCount; ++Face)
        {
            if (!Entry.bFaceValid[Face]) continue;
            bool bHit = bInvalidateAll;
            for (int32 Index = 0; Index < Invalidations.Num() && !bHit; ++Index)
                bHit = IsAABBInFrustum(Invalidations[Index], Entry.Frusta[Face]);
            if (!bHit) continue;
            Entry.bFaceValid[Face] = false;
            if (!bInvalidateAll) ++Stats.CacheFacesInvalidatedByCaster;
        }
    }
    for (uint32 LightId : Slot.Data.ConsideredLightIds)
        if (FShadowCacheEntry* Entry = Slot.CacheEntries.Find(LightId)) Entry->bSeen = true;

    // 2. Light별 준비.
    for (uint32 Index = 0; Index < static_cast<uint32>(Slot.Data.ShadowRequestsPerLight.Num()); ++Index)
    {
        const FLightSceneProxy* Light = Slot.Data.ShadowRequestsPerLight[Index].Light;
        FLightShadowSetup Setup;
        bool bValidSetup = false;
        switch (Light->GetLightType())
        {
        case ELightType::Directional:
            bValidSetup = BuildDirectionalSetup(World, *static_cast<const FDirectionalLightSceneProxy*>(Light), Slot.Data.Resolution, Setup);
            break;
        case ELightType::Spot:
            bValidSetup = BuildSpotSetup(*static_cast<const FSpotLightSceneProxy*>(Light), Slot.Data.Resolution, Setup);
            break;
        case ELightType::Point:
            bValidSetup = BuildPointSetup(*static_cast<const FPointLightSceneProxy*>(Light), Slot.Data.Resolution, Setup);
            break;
        default:
            break;
        }
        if (bValidSetup && PrepareLightShadow(World, Index, Slot, Setup)) ++Stats.LightsRendered;
    }

    // 3. 이번 프레임에 고려되지 않은 Light(삭제, CastShadow 꺼짐 등)는 캐시와 슬롯을 반납한다.
    for (auto Iterator = Slot.CacheEntries.begin(); Iterator != Slot.CacheEntries.end();)
    {
        const auto Current = Iterator++;
        if (Current->second.bSeen) continue;
        if (Current->second.MapIndex != InvalidShadowMapIndex)
            FreeMapIndex(Current->second.bCube ? Slot.UsedCubes : Slot.Used2DSlices, Current->second.MapIndex);
        Slot.CacheEntries.Remove(Current->first);
    }

    Slot.Data.Required2DSlices = GetRequiredMapCount(Slot.Used2DSlices);
    Slot.Data.RequiredCubes = GetRequiredMapCount(Slot.UsedCubes);
    Slot.PreparedFrame = FrameNumber;
    return &Slot.Data;
}

bool FSceneRenderer::PrepareLightShadow(UWorld& World, uint32 RequestIndex, FWorldShadowFrame& Slot, const FLightShadowSetup& Setup)
{
    FShadowFrameData& Frame = Slot.Data;
    FShadowCullingStats& Stats = Frame.CullingStats;
    FShadowRequestPerLight& Request = Frame.ShadowRequestsPerLight[RequestIndex];
    const bool bCaching = ShadowCullingSettings.bCacheShadowMaps;
    const bool bCube = Setup.PassCount == FTextureCubeArray::FaceCount;
    TArray<uint8>& UsedMaps = bCube ? Slot.UsedCubes : Slot.Used2DSlices;

    FShadowCacheEntry& Entry = Slot.CacheEntries[Request.Light->GetComponent()->GetUUID()];
    Entry.bSeen = true;
    Entry.bCube = bCube;

    // 캐시 키 비교. 행렬이 하나라도 다르면 Light가 바뀐 것이므로 모든 면을 다시 그린다.
    bool bKeyChanged = Entry.PassCount != Setup.PassCount;
    for (uint32 Face = 0; Face < Setup.PassCount && !bKeyChanged; ++Face)
        bKeyChanged = std::memcmp(&Entry.ViewProjections[Face], &Setup.ViewProjections[Face], sizeof(FMatrix)) != 0;
    if (bKeyChanged)
    {
        if (bCaching && Entry.PassCount != 0) ++Stats.CacheLightsInvalidated;
        Entry.PassCount = Setup.PassCount;
        for (uint32 Face = 0; Face < Setup.PassCount; ++Face)
        {
            Entry.ViewProjections[Face] = Setup.ViewProjections[Face];
            Entry.Frusta[Face] = Setup.Frusta[Face];
            Entry.bFaceValid[Face] = false;
        }
    }

    FShadowDepthPassInfo Passes[MaxShadowPassesPerLight];
    bool bRebuilt[MaxShadowPassesPerLight] = {};
    const auto RebuildFace = [&](uint32 Face)
    {
        FShadowDepthPassInfo& Pass = Passes[Face];
        Pass.RequestIndex = RequestIndex;
        Pass.ViewProjection = Setup.ViewProjections[Face];
        Pass.Frustum = Setup.Frusta[Face];
        // 캐싱 중에는 면 내용이 카메라에 의존하면 안 되므로 면 View 컬링을 하지 않는다.
        if (!bCaching && bCube && ShadowCullingSettings.bCullLightsByView &&
            !IsPointFaceVisible(Setup.Context.LightPosition, Setup.Context.ExtrudeDistance, Setup.FaceForward[Face], Setup.FaceRight[Face], Setup.FaceUp[Face], PreparingShadowViews))
            ++Stats.PointFacesCulled;
        else
            BuildShadowCasterQueue(World, Pass, Setup.Context, Stats);
        Entry.bFaceHasCaster[Face] = Pass.CasterPackets.Num() > 0;
        bRebuilt[Face] = true;
    };
    // 캐싱을 끄면 매 프레임 모든 면을 다시 그린다.
    for (uint32 Face = 0; Face < Setup.PassCount; ++Face)
        if (!bCaching || !Entry.bFaceValid[Face]) RebuildFace(Face);

    bool bHasCaster = false;
    for (uint32 Face = 0; Face < Setup.PassCount; ++Face)
        bHasCaster = bHasCaster || Entry.bFaceHasCaster[Face];
    if (!bHasCaster)
    {
        // 그릴 Caster가 없으면 슬롯도 필요 없다. 다시 모은 면은 "비어 있음"으로 유효 처리해 무효화될 때까지 다시 모으지 않는다.
        if (Entry.MapIndex != InvalidShadowMapIndex)
        {
            FreeMapIndex(UsedMaps, Entry.MapIndex);
            Entry.MapIndex = InvalidShadowMapIndex;
        }
        for (uint32 Face = 0; Face < Setup.PassCount; ++Face)
            if (bRebuilt[Face]) Entry.bFaceValid[Face] = bCaching;
        ++Stats.LightsCulledByNoCaster;
        return false;
    }
    if (Entry.MapIndex == InvalidShadowMapIndex)
    {
        Entry.MapIndex = AllocateMapIndex(UsedMaps);
        // 새 슬롯의 이전 내용은 다른 Light의 것일 수 있으므로 비어 있던 면까지 모두 다시 그린다.
        for (uint32 Face = 0; Face < Setup.PassCount; ++Face)
            if (!bRebuilt[Face]) RebuildFace(Face);
    }

    // 다시 모은 면만 그린다. 빈 면도 패스를 내서 Clear한다.
    for (uint32 Face = 0; Face < Setup.PassCount; ++Face)
    {
        if (!bRebuilt[Face])
        {
            ++Stats.ShadowPassesCached;
            continue;
        }
        Passes[Face].ArraySlice = bCube ? Entry.MapIndex * FTextureCubeArray::FaceCount + Face : Entry.MapIndex;
        Frame.ShadowDepthPassInfos.Add(std::move(Passes[Face]));
        Entry.bFaceValid[Face] = bCaching;
        ++Stats.ShadowPassesDrawn;
    }

    Request.ShadowMapIndex = Entry.MapIndex;
    FShadowInfo Info = Setup.Info;
    Info.ShadowMapIndex = Entry.MapIndex;
    Request.ShadowIndex = static_cast<uint32>(Frame.ShadowInfos.Num());
    Frame.ShadowInfos.Add(Info);
    return true;
}

void FSceneRenderer::ReleaseWorldShadows(uint32 ResourceIndex)
{
    if (ResourceIndex >= MaxShadowResourceSlots) return;
    FWorldShadowFrame& Slot = WorldShadowFrames[ResourceIndex];
    Slot.Data.ResetFrame();
    Slot.PreparedFrame = 0;
    // GPU 배열과 함께 버려지므로 캐시와 슬롯도 처음 상태로 돌린다.
    Slot.CacheEntries.Reset();
    Slot.Used2DSlices.Reset();
    Slot.UsedCubes.Reset();
    Slot.bCacheActive = false;
    Slot.CachedResolution = 0;
    Slot.CachedMinCasterTexels = -1.0f;
}

void FSceneRenderer::InvalidateShadowCache(uint32 ResourceIndex)
{
    if (ResourceIndex >= MaxShadowResourceSlots) return;
    for (auto& Pair : WorldShadowFrames[ResourceIndex].CacheEntries)
        for (bool& bValid : Pair.second.bFaceValid) bValid = false;
}
