#include "EnginePCH.h"
#include "Render/SceneRenderer.h"
#include "Component/PrimitiveComponent.h"
#include "Component/StaticMeshComponent.h"
#include "Component/BillboardComponent.h"
#include "Component/ParticleSubUVComponent.h"
#include "Engine/PrimitiveSceneProxy.h"
#include "Engine/World.h"
#include "Core/Stats/LightweightStats.h"
#include "Engine/LightSceneProxy.h"
#include "Camera/CameraProjection.h"
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

    if (View.RenderMode.ViewMode == EViewMode::Lit)
    {
        // 먼저 Data.LightShadows에 광원별 그림자 데이터를 만듬
		CollectShadowLights(World.GetScene(), Data);
		// 광원별 Shadow View를 만든 뒤 동일한 caster 수집을 적용한다.
		BuildDirectionalLightShadowView(World.GetScene(), View, Data);
		BuildSpotLightShadowView(World.GetScene(), Data);
		BuildPointLightShadowView(World.GetScene(), Data);

		for (FLightShadowData& Shadow : Data.LightShadows)
			for (FShadowView& ShadowView : Shadow.Views)
				BuildShadowCasterQueue(ShadowView);
    }
}

void FSceneRenderer::EndFrame()
{
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
// 그림자를 그리는 광원을 수집
void FSceneRenderer::CollectShadowLights(const FScene& Scene, FSceneRenderData& Data)
{
    const FLightSceneProxy* SelectedDirectional = nullptr;

    // 조명 계산과 동일한 Directional 하나를 먼저 선택
    for (const FLightSceneProxy* Proxy : Scene.Lights)
    {
        // visible한 Directional이면 SelectedDirectional을 덮어쓰기
		if (Proxy->IsVisible() && Proxy->GetLightType() == ELightType::Directional)
		{
			SelectedDirectional = Proxy;
		}
    }

    // 선택된 광원들의 그림자 요청 생성
    for (const FLightSceneProxy* Proxy : Scene.Lights)
    {
        const ELightType Type = Proxy->GetLightType();

        // Ambient는 제외해야 함.
        if (!Proxy->IsVisible() || Type == ELightType::Ambient)
            continue;

        // Directional은 위에서 선택한 하나만 사용.
        if (Type == ELightType::Directional &&
            Proxy != SelectedDirectional)
            continue;

        const FShadowSettings& Settings = Proxy->GetShadowSettings();
        if (!Settings.bCastShadows)
            continue;

        FLightShadowData Shadow;
        FShadowMapDesc& MapDesc = Shadow.MapDesc;

        // 광원 식별 정보와 설정 복사
		Shadow.LightId = Proxy->GetComponent()->GetUUID();
        Shadow.Settings = Settings;
        MapDesc.Resolution = Settings.Resolution;

        // 광원 종류에 따라 요청 정보 설정
        switch (Type)
        {
        case ELightType::Directional:
            Shadow.Type = EShadowProjection::Directional;
            MapDesc.SliceCount = 1;
            MapDesc.Type = EShadowMapType::Texture2D;
            break;

        case ELightType::Spot:
            Shadow.Type = EShadowProjection::Spot;
            MapDesc.SliceCount = 1;
            MapDesc.Type = EShadowMapType::Texture2D;
            break;

        case ELightType::Point:
            Shadow.Type = EShadowProjection::Point;
            MapDesc.SliceCount = 6;
            MapDesc.Type = EShadowMapType::TextureCube;
            break;

        default:
            continue;
        }

        Data.LightShadows.Add(std::move(Shadow));
    }
}

void FSceneRenderer::BuildDirectionalLightShadowView(const FScene& Scene, FSceneView SceneView, FSceneRenderData& Data)
{
	// 그림자에 관련된 static Mesh들의 월드 AABB를 합친 SceneBounds를 계산
    FBox SceneBounds{};
	bool bHasBounds = false;

	for (const FRenderableObject& Object : RenderObjects)
	{
		const auto* SnapShot = PrimitiveById.Find(Object.Id);
		if (!SnapShot || !SnapShot->Primitive)
			continue;
		const auto* Proxy = SnapShot->Primitive->GetSceneProxy();
        if (!Proxy || !Proxy->GetMesh())
            continue;
        if (!Proxy->IsCastingShadows() && !Proxy->IsReceivingShadows())
            continue;

		if (!bHasBounds)
		{
			SceneBounds = FBox{ Object.WorldBounds.Center - Object.WorldBounds.Extent, Object.WorldBounds.Center + Object.WorldBounds.Extent };
			bHasBounds = true;
		}
		else
		{
			SceneBounds.Expand(FBox{ Object.WorldBounds.Center - Object.WorldBounds.Extent, Object.WorldBounds.Center + Object.WorldBounds.Extent });
		}
	}

    if (!bHasBounds)
        return;

    // 중심과 반지름 계산
    const FVector SceneCenter = (SceneBounds.Min + SceneBounds.Max) * 0.5f;
    // SceneExtent는 중심에서 AABB 전체를 감싸는 구의 반지름
    const FVector SceneExtent = (SceneBounds.Max - SceneBounds.Min) * 0.5f;
    const float SceneRadius = std::max(SceneExtent.Size(), 0.01f);

    for (const FLightSceneProxy* Proxy : Scene.Lights)
    {
        if (Proxy->IsVisible() && Proxy->GetLightType() == ELightType::Directional)
        {
            // 선택된 Directional Light에 대한 그림자 데이터를 찾는다.
            for (FLightShadowData& Shadow : Data.LightShadows)
            {
                if (Shadow.LightId == Proxy->GetComponent()->GetUUID())
                {
					const auto* DirectionalProxy = static_cast<const FDirectionalLightSceneProxy*>(Proxy);

					// Light View 계산

					const FVector LightDirection = DirectionalProxy->GetInfo().Direction;
					const FVector LightForward = LightDirection.GetSafeNormal();

                    // Light 카메라의 위치
					const FVector LightEye = SceneCenter - LightForward * (2.0f * SceneRadius);

					// Light Up/Right 벡터 계산
					const FVector UpGuide = std::fabs(LightForward.Z) > 0.99f ? FVector(0, 1, 0) : FVector(0, 0, 1);
					const FVector LightRight = UpGuide.Cross(LightForward).GetSafeNormal();
					const FVector LightUp = LightForward.Cross(LightRight).GetSafeNormal();

					// Light View 행렬 계산
                    FMatrix LightRotation = FMatrix::Identity;
					LightRotation.SetAxis(0,LightForward);
					LightRotation.SetAxis(1, LightRight);
					LightRotation.SetAxis(2,LightUp);
                    const FMatrix LightView =  FMatrix::MakeTranslation(-LightEye) * LightRotation.GetTransposed();

                    // Light Projection 행렬 계산
					FCameraProjection ShadowProjection;
					ShadowProjection.Mode = EProjectionMode::Orthographic;
					ShadowProjection.OrthoWidth = SceneRadius * 2.0f;
                    ShadowProjection.NearClip = SceneRadius;
                    ShadowProjection.FarClip = SceneRadius * 3.0f;

					// 정사각형 Shadow Map을 위해 OrthoWidth와 OrthoHeight를 동일하게 설정
					const FMatrix LightProjection = ShadowProjection.GetMatrix(1.0f);

                    // 완성한 행렬과 절두체를 FShadowView에 저장
					FShadowView ShadowView;
					ShadowView.ViewProjection = LightView * LightProjection;
					ShadowView.Frustum = ExtractFrustumPlanes(ShadowView.ViewProjection);
					ShadowView.TargetSlice = 0;

					Shadow.Views.Add(std::move(ShadowView));


                }
            }
        }
    }


}

void FSceneRenderer::BuildSpotLightShadowView(const FScene& Scene, FSceneRenderData& Data)
{
    for (FLightShadowData& Shadow : Data.LightShadows)
    {
        if (Shadow.Type != EShadowProjection::Spot)
            continue;

        for (const FLightSceneProxy* Proxy : Scene.Lights)
        {
            if (!Proxy->IsVisible() ||
                Proxy->GetLightType() != ELightType::Spot ||
                Proxy->GetComponent()->GetUUID() != Shadow.LightId)
            {
                continue;
            }

            const auto* Spot = static_cast<const FSpotLightSceneProxy*>(Proxy);
            const FSpotLightInfo& Info = Spot->GetInfo();

            // 원근 투영의 near는 0보다 커야 한다.
            constexpr float NearClip = SpotShadowNearClip;
            if (Info.AttenuationRadius <= NearClip ||
                Info.Direction.SizeSquared() < 1e-8f)
            {
                continue;
            }

            // CosOuterCone에는 바깥 반각의 cos 값이 저장되어 있다.
            const float HalfAngle = std::acos(std::clamp(Info.CosOuterCone, -1.0f, 1.0f));

            if (HalfAngle <= 0.0f || HalfAngle >= PI * 0.5f)
                continue;

            const FVector Forward = Info.Direction.GetSafeNormal();

            // Forward와 평행하지 않은 보조 Up을 선택한다.
            const FVector UpGuide = std::fabs(Forward.Z) > 0.99f
                ? FVector(0, 1, 0)
                : FVector(0, 0, 1);

            const FVector Right = UpGuide.Cross(Forward).GetSafeNormal();
            const FVector Up = Forward.Cross(Right).GetSafeNormal();

            FMatrix Rotation = FMatrix::Identity;
            Rotation.SetAxis(0, Forward);
            Rotation.SetAxis(1, Right);
            Rotation.SetAxis(2, Up);

            // Directional과 달리 광원 자신의 위치에서 관측한다.
            const FMatrix LightView = FMatrix::MakeTranslation(-Info.Position) * Rotation.GetTransposed();

            FCameraProjection Projection;
            Projection.Mode = EProjectionMode::Perspective;
            Projection.FovDegrees = FMath::RadiansToDegrees(HalfAngle) * 2.0f;
            Projection.NearClip = NearClip;
            Projection.FarClip = Info.AttenuationRadius;

            FShadowView ShadowView;
            ShadowView.ViewProjection = LightView * Projection.GetMatrix(1.0f);
            ShadowView.Frustum = ExtractFrustumPlanes(ShadowView.ViewProjection);

            // 실제 배열 slice는 렌더링 준비 시 배정한다.
            ShadowView.TargetSlice = 0;

            Shadow.Views.Reset();
            Shadow.Views.Add(std::move(ShadowView));
            break;
        }
    }
}

void FSceneRenderer::BuildPointLightShadowView(const FScene& Scene, FSceneRenderData& Data)
{
	// Point Light는 6개의 큐브맵 방향을 사용하여 그림자를 생성한다.
	static const FVector FaceDirections[6] = {
		FVector(1, 0, 0),   // +X
		FVector(-1, 0, 0),  // -X
		FVector(0, 1, 0),   // +Y
		FVector(0, -1, 0),  // -Y
		FVector(0, 0, 1),   // +Z
		FVector(0, 0, -1)   // -Z
	};

    // cubemap 조회 시의 방향과 이미지 회전이 일치하도록 고정한다.
    // Spot처럼 임의의 Up을 선택하지 않는다.
	static const FVector FaceUps[6] = {
		FVector(0, 1, 0),   // +X
		FVector(0, 1, 0),   // -X
		FVector(0, 0, -1),  // +Y
		FVector(0, 0, 1),   // -Y
		FVector(0, 1, 0),   // +Z
		FVector(0, 1, 0)    // -Z
	};

    for (FLightShadowData& Shadow : Data.LightShadows)
    {
        if (Shadow.Type != EShadowProjection::Point)
            continue;

        // 기존 그림자 요청의 LightId에 해당하는 프록시를 찾는다.
        for (const FLightSceneProxy* Proxy : Scene.Lights)
        {
            if (!Proxy->IsVisible() ||
                Proxy->GetLightType() != ELightType::Point ||
                Proxy->GetComponent()->GetUUID() != Shadow.LightId)
            {
                continue;
            }

            const auto* Point = static_cast<const FPointLightSceneProxy*>(Proxy);
            const FPointLightInfo& Info = Point->GetInfo();

            if (Info.AttenuationRadius <= PointShadowNearClip)
                continue;

            // 여섯 면이 같은 원근 투영을 공유한다.
            FCameraProjection Projection;
            Projection.Mode = EProjectionMode::Perspective;
            Projection.FovDegrees = 90.0f;
            Projection.NearClip = PointShadowNearClip;
            Projection.FarClip = Info.AttenuationRadius;

            const FMatrix LightProjection =  Projection.GetMatrix(1.0f);

            Shadow.Views.Reset();

            for (uint32 Face = 0; Face < 6; ++Face)
            {
                const FVector Forward = FaceDirections[Face];
                const FVector Right = FaceUps[Face].Cross(Forward).GetSafeNormal();
                const FVector Up = Forward.Cross(Right).GetSafeNormal();

                // 현재 엔진의 View 공간:
                // X = 앞쪽 깊이, Y = 오른쪽, Z = 위쪽.
                FMatrix Rotation = FMatrix::Identity;
                Rotation.SetAxis(0, Forward);
                Rotation.SetAxis(1, Right);
                Rotation.SetAxis(2, Up);

                // 모든 면의 관측 위치는 광원 위치로 동일하다.
                const FMatrix LightView = FMatrix::MakeTranslation(-Info.Position) * Rotation.GetTransposed();

                FShadowView ShadowView;
                ShadowView.ViewProjection = LightView * LightProjection;
                ShadowView.Frustum = ExtractFrustumPlanes(ShadowView.ViewProjection);

                // 지금은 광원 내부의 면 번호만 기록한다.
                // 실제 배열 위치는 이후 CubeIndex * 6 + Face로 배정한다.
                ShadowView.TargetSlice = Face;

                Shadow.Views.Add(std::move(ShadowView));
            }

            break;
        }
    }

}

// TODO : 캡쳐한 전체 RenderObjects를 순회하지만, 나중에 BVH로 바꾸는게 좋을듯
void FSceneRenderer::BuildShadowCasterQueue(FShadowView& ShadowView)
{
	FRenderQueue& CasterQueue = ShadowView.CasterQueue;
	CasterQueue.Reset();

	for (const FRenderableObject& Object : RenderObjects)
	{
		const auto* SnapShot = PrimitiveById.Find(Object.Id);
		if (!SnapShot || !SnapShot->Primitive)
			continue;
		const auto* Proxy = SnapShot->Primitive->GetSceneProxy();
		if (!Proxy || !Proxy->GetMesh())
			continue;
		if (!Proxy->IsCastingShadows())
			continue;
		auto* StaticMesh = Cast<UStaticMeshComponent>(SnapShot->Primitive);
		if (!StaticMesh)
			continue;

		// 그림자 절두체와 AABB 검사
		if (IsAABBInFrustum(Object.WorldBounds, ShadowView.Frustum))
		{
            // 이번 메시가 추가할 패킷

            const uint32 FirstPacket = CasterQueue.Num();
            StaticMesh->SubmitToRenderQueue(CasterQueue);

            // section 별 반투명 material 에 대해 제거
            // 제거하면서 순회하므로 뒤에서 앞으로 검사
            for (uint32 Index = CasterQueue.Num(); Index > FirstPacket; --Index)
            {
                const uint32 PacketIndex = Index - 1;
                const UMaterial* Material = CasterQueue[PacketIndex].Material;

                if (!Material ||
                    Material->ParamLayout != EMaterialParamLayout::StaticMesh ||
                    Material->BlendState != EBlendState::Opaque)
                {
                    CasterQueue.RemoveAt(PacketIndex);
                }
            }
		}
	}
}
