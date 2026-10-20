#include "EnginePCH.h"
#include "Renderer.h"
#include "ShadowFrameData.h"
#include "SceneRenderer.h"
#include "Engine/LightSceneProxy.h"
#include "Engine/Viewport.h"
#include "Shader.h"
#include "Mesh.h"

#include "Core/EngineTimer.h"
#include "Core/Stats/LightweightStats.h"

#include "Engine/PrimitiveSceneProxy.h"

#include "RenderCommand.h"
#include "RenderResourceManager.h"
#include "ColorWorkflow.h"
#include "TileLightCullingResources.h"

#include "Camera/CameraComponent.h"

#include <algorithm>
#include <chrono>
#include <unordered_map>
#include <bit>

DECLARE_CYCLE_STAT("Draw Render Packets", STAT_DrawRenderPackets);
DECLARE_CYCLE_STAT("Render Queue Sorting", STAT_RenderQueueSorting);
DECLARE_CYCLE_STAT("Upload Per-Object CB", STAT_UploadPerObjectCB);

namespace
{
    // 그림자 SRV 바인딩 규약. Pass 2의 바인딩에서도 같은 상수를 사용한다.
    namespace ShadowSRVSlots
    {
        constexpr uint32 Infos = 12;
        constexpr uint32 Atlas = 13;
        constexpr uint32 DirectionalInfos = 15;
    }
    constexpr uint32 ShadowSamplerSlot = 1;
    constexpr uint32 DirectionalShadowCBSlot = 5;
    constexpr uint32 ShadowComparisonSamplerSlot = 2;

    struct FTileLightShadingConstants
    {
        uint32 TileCountX = 0;
        uint32 TileCountY = 0;
        uint32 TileSize = LIGHT_CULLING_TILE_SIZE;
        uint32 MaxLightsPerTile = 64;
        uint32 PointCount = 0;
        uint32 SpotCount = 0;
        uint32 bVisualizeLightCount = 0;
        uint32 bUseTileList = 0;
    };
    static_assert(sizeof(FTileLightShadingConstants) == 32);
    static_assert(offsetof(FTileLightShadingConstants, PointCount) == 16);

	// VSSetConstantBuffers1의 오프셋은 16개 상수(256바이트) 단위여야 하므로 오브젝트마다 256바이트 칸을 쓴다.
	constexpr uint32 PerObjectSlotConstants = 16;

	constexpr uint32 MinPerObjectSlots = 1024;

    bool UsesDepthPrepass(const UMaterial* Material)
    {
        return Material->ParamLayout == EMaterialParamLayout::StaticMesh && Material->BlendState == EBlendState::Opaque;
    }

	const FMatrix& GetPacketWorld(const FRenderPacket& Packet)
	{
		if (Packet.Proxy) return Packet.Proxy->GetLocalToWorld();
		return Packet.Model ? *Packet.Model : FMatrix::Identity;
	}

	FMatrix GetPacketNormalMatrix(const FRenderPacket& Packet)
	{
		if (Packet.Proxy) return Packet.Proxy->GetNormalMatrix();
		return MakeNormalMatrix(GetPacketWorld(Packet));
	}

	uint64 MakeSortKey(const FRenderPacket& Packet)
	{
		if (Packet.Material->BlendState != EBlendState::Opaque)
		{
			const uint32 DistanceBits = std::bit_cast<uint32>(Packet.CameraToParticleDistance);
			return (1ull << 63) | static_cast<uint64>(~DistanceBits);
		}

		return (static_cast<uint64>(Packet.Material->SortID) << 47)
			| (static_cast<uint64>(Packet.Mesh->SortID) << 31)
			| (static_cast<uint64>(Packet.LODIndex & 0x3) << 29);
	}

	// 불투명 패킷과 같은 규칙의 키 (묶음은 전부 불투명)
	uint64 MakeGroupKey(const FStaticDrawGroup& Group)
	{
		return (static_cast<uint64>(Group.Material->SortID) << 47)
			| (static_cast<uint64>(Group.Mesh->SortID) << 31)
			| (static_cast<uint64>(Group.LODIndex & 0x3) << 29);
	}
}

bool FRenderer::Init()
{
	bUsePerObjectSlots = RenderCommand::SupportsConstantBufferOffsets();
	PerObjectCB = RenderCommand::CreateConstantBuffer(sizeof(FPerObjectConstants));
	ViewCB = RenderCommand::CreateConstantBuffer(sizeof(FViewConstants));
	ShadowDepthPassCB = RenderCommand::CreateConstantBuffer(sizeof(FShadowDepthPassConstants));
	if (!ShadowDepthPassCB || !ShadowDepthPassCB->GetBuffer()) return false;
	LightingCB = RenderCommand::CreateConstantBuffer(sizeof(FLightingConstants));
	FLightingConstants LightingConstant{};
	RenderCommand::UpdateBufferData(LightingCB.get(), &LightingConstant);

    TileShadingCB = RenderCommand::CreateConstantBuffer(sizeof(FTileLightShadingConstants));
    const FTileLightShadingConstants TileDefaults{};
    RenderCommand::UpdateBufferData(TileShadingCB.get(), &TileDefaults);
    RenderCommand::BindConstantBuffer(4, TileShadingCB.get(), EShaderBindFlagBits::Vertex | EShaderBindFlagBits::Pixel);
    DepthShader = FRenderResourceManager::FindShaderProgram("Resources/Shader/StaticMeshDepthShader.hlsl");
    if (!DepthShader)
    {
        HTR_LOG(Error, "[Renderer] required depth shader not registered: Resources/Shader/StaticMeshDepthShader.hlsl");
        return false;
    }
    if (DepthShader->ComputeShader || !DepthShader->VertexShader || !DepthShader->PixelShader || !DepthShader->VertexShader->IsValid() || !DepthShader->PixelShader->IsValid())
    {
        HTR_LOG(Error, "[Renderer] invalid depth graphics program: Resources/Shader/StaticMeshDepthShader.hlsl (valid VS/PS required)");
        return false;
    }
    ShadowDistanceShader = FRenderResourceManager::FindShaderProgram("Resources/Shader/ShadowDistanceDepthShader.hlsl");
    if (!ShadowDistanceShader || ShadowDistanceShader->ComputeShader || !ShadowDistanceShader->VertexShader || !ShadowDistanceShader->PixelShader || !ShadowDistanceShader->VertexShader->IsValid() || !ShadowDistanceShader->PixelShader->IsValid())
    {
        HTR_LOG(Error, "[Renderer] required shadow distance shader is missing or invalid");
        return false;
    }
    ShadowNearClampShader = FRenderResourceManager::FindShaderProgram("Resources/Shader/ShadowNearClampDepthShader.hlsl");
    if (!ShadowNearClampShader || ShadowNearClampShader->ComputeShader || !ShadowNearClampShader->VertexShader || !ShadowNearClampShader->PixelShader || !ShadowNearClampShader->VertexShader->IsValid() || !ShadowNearClampShader->PixelShader->IsValid())
    {
        HTR_LOG(Error, "[Renderer] required shadow near clamp shader is missing or invalid");
        return false;
    }
    ShadowTileClearShader = FRenderResourceManager::FindShaderProgram("Resources/Shader/ShadowAtlasClear.hlsl");
    if (!ShadowTileClearShader || !ShadowTileClearShader->VertexShader || !ShadowTileClearShader->PixelShader ||
        !ShadowTileClearShader->VertexShader->IsValid() || !ShadowTileClearShader->PixelShader->IsValid()) return false;
    D3D11_DEPTH_STENCIL_DESC ClearDepthDesc{};
    ClearDepthDesc.DepthEnable = TRUE;
    ClearDepthDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
    ClearDepthDesc.DepthFunc = D3D11_COMPARISON_ALWAYS;
    if (FAILED(RenderCommand::GetDevice()->CreateDepthStencilState(&ClearDepthDesc, ShadowTileClearDepthState.GetAddressOf()))) return false;
    if (!TileLightCulling.Init())
    {
        HTR_LOG(Error, "[Renderer] required tile light culling initialization failed");
        return false;
    }
    HTR_LOG(Info, "[Renderer] required depth graphics program and tile culling CS initialized");
	GPUOcclusion.Init();   // 실패해도 오클루전만 못 쓸 뿐 렌더링은 된다

	return true;
}

// 필요한 칸 수가 용량을 넘을 때만 두 배씩 키워 재할당을 드물게 한다.
void FRenderer::EnsurePerObjectSlotCapacity(uint32 SlotCount)
{
	if (SlotCount <= PerObjectSlotCapacity)
		return;

	uint32 NewCapacity = std::max(PerObjectSlotCapacity * 2, MinPerObjectSlots);
	while (NewCapacity < SlotCount)
		NewCapacity *= 2;

	PerObjectSlotCB = RenderCommand::CreateConstantBuffer(NewCapacity * ObjectSlotBytes);
	if (!PerObjectSlotCB || !PerObjectSlotCB->GetBuffer())
	{
		HTR_LOG(Warning, "Per-object constant buffer ({} slots) creation failed. Falling back to per-draw updates.", NewCapacity);
		PerObjectSlotCB.reset();
		PerObjectSlotCapacity = 0;
		return;
	}
	PerObjectSlotCapacity = NewCapacity;
}

// 원래 패킷 순서로 World·Normal 행렬을 올린다. 정렬 목록은 원래 패킷 번호의 칸을 바인딩한다.
void FRenderer::UploadPerObjectConstants()
{
	SCOPE_CYCLE_COUNTER(STAT_UploadPerObjectCB);

	const uint32 Count = static_cast<uint32>(RenderPackets.size());
	if (!bUsePerObjectSlots || Count == 0)
		return;

	EnsurePerObjectSlotCapacity(Count);
	if (!PerObjectSlotCB)
	{
		bUsePerObjectSlots = false;
		return;
	}

	uint8* Dest = static_cast<uint8*>(RenderCommand::MapWriteDiscard(PerObjectSlotCB.get()));
	if (!Dest)
	{
		bUsePerObjectSlots = false;
		return;
	}

	// 매핑된 메모리는 write-combined라 순차 쓰기만 하고 읽지 않는다.
	for (uint32 Index = 0; Index < Count; ++Index)
	{
		FRenderPacket& P = RenderPackets[Index];
		WriteObjectSlot(Dest, Index, GetPacketWorld(P), GetPacketNormalMatrix(P), P.Proxy && P.Proxy->ReceivesShadow());
		P.Slot = Index;
	}

	RenderCommand::Unmap(PerObjectSlotCB.get());
}

// 카메라의 ViewProjection과 위치를 공통 렌더 경로로 전달한다.
void FRenderer::RenderAll(FRenderQueue& InQueue, UCameraComponent* CameraComponent)
{
	RenderAll(InQueue, CameraComponent->GetViewProjectionMatrix(), CameraComponent->GetWorldLocation());
}

// 불투명 우선·반투명 거리순으로 정렬해 View 행렬과 Section 범위로 그린다.
void FRenderer::RenderAll(FRenderQueue& InQueue, const FMatrix& ViewProjection, const FVector& CameraPosition)
{
	RenderQueueSorting(InQueue, ViewProjection, CameraPosition);
	RenderOpaque(ViewProjection);
	RenderTranslucent(ViewProjection);
}

void FRenderer::RenderOpaque(const FMatrix& ViewProjection, bool bAfterPrepass)
{
    const ESceneMeshPass Pass = bAfterPrepass ? ESceneMeshPass::OpaqueAfterPrepass : ESceneMeshPass::Color;
    DrawStaticGroups(Pass);
    DrawPackets(0, FirstTranslucentIndex, ViewProjection, Pass);
}

void FRenderer::RenderDepthPrepass(const FMatrix& ViewProjection)
{
    DrawStaticGroups(ESceneMeshPass::DepthPrepass);
    DrawPackets(0, FirstTranslucentIndex, ViewProjection, ESceneMeshPass::DepthPrepass);
}

// RenderOpaque가 남긴 반투명 패킷을 먼 것부터 그린다.
void FRenderer::RenderTranslucent(const FMatrix& ViewProjection)
{
	DrawPackets(FirstTranslucentIndex, SortEntries.Num(), ViewProjection);
	RenderPackets.Reset();
	FirstTranslucentIndex = 0;
	StaticGroups.clear();   // 묶음 메모리는 World 것이므로 이번 프레임이 끝나면 놓는다
}

// 스태틱 메시 묶음을 정렬 키 순서로 그린다. 바인딩은 묶음마다 한 번, 항목마다는 칸 바인딩과 드로우만 한다.
void FRenderer::DrawStaticGroups(ESceneMeshPass Pass)
{
	if (StaticGroups.empty())
		return;

	SCOPE_CYCLE_COUNTER(STAT_DrawRenderPackets);
	// 픽셀 단위 조명이 카메라 위치를 읽으므로 PS에도 바인딩한다.
	RenderCommand::BindConstantBuffer(0, ViewCB.get(), EShaderBindFlagBits::Vertex | EShaderBindFlagBits::Pixel);
	RenderCommand::BindConstantBuffer(3, LightingCB.get(), EShaderBindFlagBits::Vertex | EShaderBindFlagBits::Pixel);

	UMaterial* BoundMaterial = nullptr;
	for (const FStaticDrawGroup* Group : StaticGroups)
	{
		if (Pass == ESceneMeshPass::DepthPrepass && !UsesDepthPrepass(Group->Material)) continue;
		RenderCommand::BindMesh(Group->Mesh, Group->LODIndex);
		if (Group->Material != BoundMaterial)
		{
			BoundMaterial = Group->Material;
			BindMaterial(BoundMaterial, Pass);
			FRenderPacket MaterialOnly;           // 머티리얼 파라미터 갱신은 패킷을 받으므로 머티리얼만 채워 넘긴다
			MaterialOnly.Material = BoundMaterial;
			if (Pass != ESceneMeshPass::DepthPrepass) UpdateMaterialParams(MaterialOnly);
		}

		for (const FStaticDrawItem& Item : Group->Items)
		{
			if (bUsePerObjectSlots && Item.Slot != InvalidObjectSlot)
			{
				RenderCommand::BindConstantBufferRange(2, PerObjectSlotCB.get(), Item.Slot * PerObjectSlotConstants, PerObjectSlotConstants, EShaderBindFlagBits::Vertex);
			}
			else
			{
				RenderCommand::BindConstantBuffer(2, PerObjectCB.get(), EShaderBindFlagBits::Vertex);
				UpdatePerObjectConstants(Item.Proxy->GetLocalToWorld(), Item.Proxy->GetNormalMatrix(), Item.Proxy->ReceivesShadow());
			}
			RenderCommand::DrawIndexed(Item.IndexCount, Item.StartIndex);
		}
	}

	// 뒤따르는 DrawPackets가 처음부터 다시 바인딩하도록 기록을 비운다.
	LastMesh = nullptr;
	LastMaterial = nullptr;
}

void FRenderer::RenderQueueSorting(FRenderQueue& InQueue, const FMatrix& ViewProjection, const FVector& CameraPosition)
{
	FViewConstants ViewConstants;
	ViewConstants.ViewProjection = ViewProjection;
	ViewConstants.CameraPosition = CameraPosition;
	RenderCommand::UpdateBufferData(ViewCB.get(), &ViewConstants);
	{
		SCOPE_CYCLE_COUNTER(STAT_RenderQueueSorting);

		std::swap(RenderPackets, InQueue);
		InQueue.Reset();

		// ① 패킷을 한 번 훑으며 키를 만든다. 그릴 수 없는 패킷은 목록에 넣지 않는다.
		SortEntries.Reset();
		SortEntries.Reserve(RenderPackets.Num());
		for (uint32 i = 0; i < RenderPackets.Num(); ++i)
		{
			const FRenderPacket& P = RenderPackets[i];
			if (!P.Mesh || !P.Material) continue;
			SortEntries.Add({ MakeSortKey(P), i });
		}

		// ② 16B 항목만 정렬
		std::sort(SortEntries.begin(), SortEntries.end(),
			[](const FSortEntry& A, const FSortEntry& B) { return A.Key < B.Key; });

		// ③ 반투명 시작 위치 = 최상위 비트가 처음 1인 곳
		FirstTranslucentIndex = 0;
		while (FirstTranslucentIndex < SortEntries.Num() && !(SortEntries[FirstTranslucentIndex].Key >> 63))
			++FirstTranslucentIndex;

		// ④ 스태틱 메시 묶음: 빈 묶음을 빼고 키 순으로 정렬한다. 조각마다 같은 키 묶음이 있으므로 정렬하면 서로 붙는다.
		std::erase_if(StaticGroups, [](const FStaticDrawGroup* Group) { return Group->Items.empty(); });
		std::sort(StaticGroups.begin(), StaticGroups.end(),
			[](const FStaticDrawGroup* A, const FStaticDrawGroup* B) { return MakeGroupKey(*A) < MakeGroupKey(*B); });
	}
	// Gather uploads group and packet slots together. Other queues need an upload here.
	if (!bObjectConstantsPrepared)
		UploadPerObjectConstants();
	bObjectConstantsPrepared = false;
}

// 정렬된 패킷 중 [Begin, End) 범위를 View 행렬과 Section 범위로 그린다.
void FRenderer::DrawPackets(uint32 Begin, uint32 End, const FMatrix& ViewProjection, ESceneMeshPass Pass)
{
	SCOPE_CYCLE_COUNTER(STAT_DrawRenderPackets);

	LastMesh = nullptr;
	LastMaterial = nullptr;
	uint8 LastLODIndex = 0;

	RenderCommand::BindConstantBuffer(0, ViewCB.get(), EShaderBindFlagBits::Vertex | EShaderBindFlagBits::Pixel);
	RenderCommand::BindConstantBuffer(3, LightingCB.get(), EShaderBindFlagBits::Vertex | EShaderBindFlagBits::Pixel);


	for (uint32 k = Begin; k < End; ++k)          // k = 정렬된 위치
	{
		const uint32 PacketIndex = SortEntries[k].PacketIndex;
		const FRenderPacket& RenderPacket = RenderPackets[PacketIndex];
		if (Pass == ESceneMeshPass::DepthPrepass && !UsesDepthPrepass(RenderPacket.Material)) continue;
		//if (RenderPacket.Mesh == nullptr || RenderPacket.Material == nullptr) continue;
		if (RenderPacket.Mesh != LastMesh || RenderPacket.LODIndex != LastLODIndex) {
			RenderCommand::BindMesh(RenderPacket.Mesh, RenderPacket.LODIndex);
		}
		if (RenderPacket.Material != LastMaterial) {
			BindMaterial(RenderPacket.Material, Pass);
		}
		if (Pass != ESceneMeshPass::DepthPrepass && (RenderPacket.Material != LastMaterial || RenderPacket.MaterialParamData))
			UpdateMaterialParams(RenderPacket);
		if (bUsePerObjectSlots && RenderPacket.Slot != InvalidObjectSlot)
		{
			RenderCommand::BindConstantBufferRange(2, PerObjectSlotCB.get(), RenderPacket.Slot * PerObjectSlotConstants, PerObjectSlotConstants, EShaderBindFlagBits::Vertex);
		}
		else
		{
			RenderCommand::BindConstantBuffer(2, PerObjectCB.get(), EShaderBindFlagBits::Vertex);
			UpdatePerObjectConstants(RenderPacket, ViewProjection);
		}

		RenderCommand::DrawIndexed(RenderPacket.IndexCount ? RenderPacket.IndexCount : RenderPacket.Mesh->GetIndexBuffer(RenderPacket.LODIndex)->GetIndexCount(), RenderPacket.StartIndex);
		LastMaterial = RenderPacket.Material;
		LastMesh = RenderPacket.Mesh;
		LastLODIndex = RenderPacket.LODIndex;
	}
}

FOcclusionMeasureResult FRenderer::MeasureOpaqueOcclusion(const FMatrix& ViewProjection)
{
	FOcclusionMeasureResult Result;

	// 불투명 드로우를 두 경로(스태틱 묶음, 일반 패킷)에서 한 목록으로 모은다. 그린 순서와 같게 묶음 먼저.
	struct FMeasureDraw
	{
		UStaticMesh* Mesh;
		UMaterial* Material;
		uint8 LODIndex;
		uint32 Slot;
		uint32 StartIndex;
		uint32 IndexCount;
		const FMatrix* World;       // 칸이 없을 때만 쓴다
		const void* ObjectKey;      // 같은 물체의 섹션을 한 물체로 센다
		bool bOccludedByGpu;
		const FRenderPacket* Packet;
	};
	std::vector<FMeasureDraw> Draws;
	for (const FStaticDrawGroup* Group : StaticGroups)
		for (const FStaticDrawItem& Item : Group->Items)
			Draws.push_back({ Group->Mesh, Group->Material, Group->LODIndex, Item.Slot, Item.StartIndex, Item.IndexCount,
				&Item.Proxy->GetLocalToWorld(), Item.Proxy, Item.bOccludedByGpu != 0, nullptr });
	for (uint32 k = 0; k < FirstTranslucentIndex; ++k)
	{
		const FRenderPacket& Packet = RenderPackets[SortEntries[k].PacketIndex];
		const uint32 IndexCount = Packet.IndexCount ? Packet.IndexCount : Packet.Mesh->GetIndexBuffer(Packet.LODIndex)->GetIndexCount();
		// 프록시가 없는 패킷(빌보드 등)은 패킷 자체를 한 물체로 센다.
		const void* Key = Packet.Proxy ? static_cast<const void*>(Packet.Proxy) : static_cast<const void*>(&Packet);
		Draws.push_back({ Packet.Mesh, Packet.Material, Packet.LODIndex, Packet.Slot, Packet.StartIndex, IndexCount,
			&GetPacketWorld(Packet), Key, Packet.bOccludedByGpu, &Packet });
	}

	const uint32 Count = static_cast<uint32>(Draws.size());
	if (Count == 0)
		return Result;

	const auto StartTime = std::chrono::high_resolution_clock::now();

	ID3D11Device* Device = RenderCommand::GetDevice();
	ID3D11DeviceContext* Context = RenderCommand::GetContext();

	// 같은 셰이더·같은 행렬로 다시 그리면 깊이가 비트 단위로 같으므로,
	// LESS_EQUAL이면 최종 깊이 버퍼에서 이 물체가 이긴 픽셀만 통과한다.
	if (!DepthLessEqualReadOnly)
	{
		D3D11_DEPTH_STENCIL_DESC Desc{};
		Desc.DepthEnable = TRUE;
		Desc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
		Desc.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
		if (FAILED(Device->CreateDepthStencilState(&Desc, DepthLessEqualReadOnly.GetAddressOf())))
			return Result;
	}

	while (OcclusionQueries.Num() < Count)
	{
		const D3D11_QUERY_DESC QueryDesc{ D3D11_QUERY_OCCLUSION, 0 };
		ComPtr<ID3D11Query> Query;
		if (FAILED(Device->CreateQuery(&QueryDesc, Query.GetAddressOf())))
			return Result;
		OcclusionQueries.Add(std::move(Query));
	}

	RenderCommand::BindConstantBuffer(0, ViewCB.get(), EShaderBindFlagBits::Vertex | EShaderBindFlagBits::Pixel);
	RenderCommand::BindConstantBuffer(3, LightingCB.get(), EShaderBindFlagBits::Vertex | EShaderBindFlagBits::Pixel);
	if (!bUsePerObjectSlots)
		RenderCommand::BindConstantBuffer(2, PerObjectCB.get(), EShaderBindFlagBits::Vertex);

	UStaticMesh* BoundMesh = nullptr;
	UMaterial* BoundMaterial = nullptr;
	uint8 BoundLOD = 0;

	// 1) 드로우마다 쿼리를 걸고 다시 그린다. 본 패스와 같은 바인딩(칸 또는 PerObjectCB)을 쓴다.
	for (uint32 Index = 0; Index < Count; ++Index)
	{
		const FMeasureDraw& Draw = Draws[Index];

		if (Draw.Mesh != BoundMesh || Draw.LODIndex != BoundLOD)
			RenderCommand::BindMesh(Draw.Mesh, Draw.LODIndex);
		if (Draw.Material != BoundMaterial)
		{
			BindMaterial(Draw.Material);
			// BindMaterial이 바꾼 상태를 측정용으로 덮어쓴다.
			RenderCommand::SetBlendState(EBlendState::NoColorWrite);
			Context->OMSetDepthStencilState(DepthLessEqualReadOnly.Get(), 0);
		}
		if (Draw.Packet && (Draw.Material != BoundMaterial || Draw.Packet->MaterialParamData))
			UpdateMaterialParams(*Draw.Packet);

		if (bUsePerObjectSlots && Draw.Slot != InvalidObjectSlot)
			RenderCommand::BindConstantBufferRange(2, PerObjectSlotCB.get(),
				Draw.Slot * PerObjectSlotConstants, PerObjectSlotConstants, EShaderBindFlagBits::Vertex);
		else
		{
			RenderCommand::BindConstantBuffer(2, PerObjectCB.get(), EShaderBindFlagBits::Vertex);
			UpdatePerObjectConstants(*Draw.World, MakeNormalMatrix(*Draw.World));   // 측정 전용 폴백이라 매번 계산한다
		}

		ID3D11Query* Query = OcclusionQueries[Index].Get();
		Context->Begin(Query);
		RenderCommand::DrawIndexed(Draw.IndexCount, Draw.StartIndex);
		Context->End(Query);

		BoundMesh = Draw.Mesh;
		BoundMaterial = Draw.Material;
		BoundLOD = Draw.LODIndex;
	}

	// 2) 결과를 기다려 모은다. 한 물체가 Section 여러 개로 나뉘면 하나라도 보이면 보이는 것으로 친다.
	std::unordered_map<const void*, bool> ObjectVisible;
	ObjectVisible.reserve(Count);
	for (uint32 Index = 0; Index < Count; ++Index)
	{
		const FMeasureDraw& Draw = Draws[Index];

		UINT64 Samples = 0;
		while (Context->GetData(OcclusionQueries[Index].Get(), &Samples, sizeof(Samples), 0) == S_FALSE) {}

		const uint64 Triangles = Draw.IndexCount / 3;
		const bool bVisible = Samples > 0;

		// Cull을 끄고 판정만 한 드로우: 가렸다고 했는데 최종 화면에 픽셀이 남았으면 잘못 가린 것이다.
		if (Draw.bOccludedByGpu)
		{
			++Result.GPUOccludedDraws;
			if (bVisible)
				++Result.FalseCulls;
		}

		++Result.TotalDraws;
		Result.TotalTriangles += Triangles;
		if (bVisible)
		{
			++Result.VisibleDraws;
			Result.VisibleTriangles += Triangles;
		}

		bool& bObjectVisible = ObjectVisible[Draw.ObjectKey];
		bObjectVisible = bObjectVisible || bVisible;
	}

	Result.TotalObjects = static_cast<uint32>(ObjectVisible.size());
	for (const auto& [Key, bVisible] : ObjectVisible)
		Result.VisibleObjects += bVisible ? 1 : 0;

	// 뒤따르는 Grid·반투명 패스를 위해 상태를 되돌린다.
	RenderCommand::SetBlendState(EBlendState::Opaque);
	RenderCommand::SetDepthStencilState(EDepthStencilState::Default);

	Result.ElapsedMs = std::chrono::duration<double, std::milli>(std::chrono::high_resolution_clock::now() - StartTime).count();
	Result.bValid = true;
	return Result;
}

uint8* FRenderer::BeginObjectConstants(uint32 MaxSlots)
{
	bObjectConstantsPrepared = false;
	if (!bUsePerObjectSlots || MaxSlots == 0) return nullptr;
	EnsurePerObjectSlotCapacity(MaxSlots);
	if (!PerObjectSlotCB) { bUsePerObjectSlots = false; return nullptr; }
	return static_cast<uint8*>(RenderCommand::MapWriteDiscard(PerObjectSlotCB.get()));
}

void FRenderer::EndObjectConstants()
{
	if (PerObjectSlotCB)
	{
		RenderCommand::Unmap(PerObjectSlotCB.get());
		bObjectConstantsPrepared = true;
	}
}

void FRenderer::SetViewMode(const FViewRenderMode& Mode)
{
	ViewModeShader = FRenderResourceManager::GetUberLitProgram(Mode, false);
	ViewModeNormalShader = FRenderResourceManager::GetUberLitProgram(Mode, true);
}

void FRenderer::SetLighting(const FLightingConstants& GlobalLights, const TArray<FPointLightInfo>& PointLights, const TArray<FSpotLightInfo>& SpotLights)
{
	// 이전 View의 PS SRV 바인딩을 끊은 뒤 동적 LocalLight 버퍼를 다시 쓴다.
	RenderCommand::UnbindShaderResources(8, 4, EShaderBindFlagBits::Pixel);
	RenderCommand::UnbindShaderResources(8, 2, EShaderBindFlagBits::Vertex);
	RenderCommand::UpdateBufferData(LightingCB.get(), &GlobalLights);
	LocalLight.UploadLights(PointLights, SpotLights);
    // 반투명은 VS(Gouraud)와 PS 모두 전체 동적 배열을 읽는다.
    RenderCommand::BindShaderResource(8, LocalLight.GetPointLightSRV(), EShaderBindFlagBits::Vertex | EShaderBindFlagBits::Pixel);
    RenderCommand::BindShaderResource(9, LocalLight.GetSpotLightSRV(), EShaderBindFlagBits::Vertex | EShaderBindFlagBits::Pixel);

}

// Material마다 Shader/Texture/Sampler/State 꽂기
void FRenderer::BindMaterial(UMaterial* material, ESceneMeshPass Pass)
{
    if (Pass == ESceneMeshPass::DepthPrepass)
    {
        RenderCommand::BindShaderProgram(DepthShader);
        RenderCommand::SetBlendState(EBlendState::NoColorWrite);
        RenderCommand::SetDepthStencilState(EDepthStencilState::Default);
        return;
    }
	// 스태틱 메시 머티리얼만 조명 대상이다. 빌보드·파티클(ParticleSubUV)은 자기 셰이더를 그대로 쓴다.
	if (material->ParamLayout == EMaterialParamLayout::StaticMesh)
	{
		if (!ViewModeShader)
			SetViewMode(FViewRenderMode{});

		bool bUseNormalMap = material->Textures.size() > 1 && material->Textures[1] != nullptr;
		if (bUseNormalMap)
		{
			RenderCommand::BindShaderProgram(ViewModeNormalShader);
		}
		else
		{
			RenderCommand::BindShaderProgram(ViewModeShader);
		}
	}
	else
	{
		RenderCommand::BindShaderProgram(material->Shader);
	}
	RenderCommand::SetBlendState(material->BlendState);
	// 반투명은 뒤에 그려지는 Grid·다른 반투명을 가리지 않도록 깊이를 쓰지 않는다.
	const bool bTranslucent = material->BlendState != EBlendState::Opaque;
    // 패스별 GPU 상태만 선택한다. 재질에 저장된 StaticMesh 기본 깊이 상태는 변경하지 않는다.
    const EDepthStencilState DepthState = Pass == ESceneMeshPass::OpaqueAfterPrepass && UsesDepthPrepass(material)
        ? EDepthStencilState::ReadOnlyLessEqual
        : (bTranslucent && material->DepthStencilState == EDepthStencilState::Default ? EDepthStencilState::ReadOnly : material->DepthStencilState);
    RenderCommand::SetDepthStencilState(DepthState);

	for (int i = 0; i < material->Textures.size(); i++)
	{
		// 같은 파일이 먼저 sRGB로 캐시됐을 수 있어 노멀맵 슬롯은 불러온 방식과 무관하게 원본 값으로 읽는다.
		const bool bNormalMapSlot = material->ParamLayout == EMaterialParamLayout::StaticMesh && i == 1;
		RenderCommand::BindShaderResource(i, material->Textures[i], EShaderBindFlagBits::Pixel, bNormalMapSlot);
	}
	RenderCommand::BindSamplerState(0, material->SamplerState, EShaderBindFlagBits::Pixel);
}

// b1 내용 채우고 꽂기
void FRenderer::UpdateMaterialParams(const FRenderPacket& RenderPacket)
{
	switch (RenderPacket.Material->ParamLayout)
	{
	case EMaterialParamLayout::StaticMesh:
	{
		//break; // 라이팅 적용 시 제거

		const float TotalTime = EngineTimer::GetTotalTime();

		FStaticMeshMaterialParams Params{};
		Params.BaseColor = ColorWorkflow::ToShaderColor(RenderPacket.Material->BaseColor);
		Params.UVScrollOffset = RenderPacket.Material->UVScrollSpeed * TotalTime;
		Params.bOpaque = RenderPacket.Material->BlendState == EBlendState::Opaque ? 1u : 0u;
		Params.Shininess = RenderPacket.Material->Shininess;
		const FLinearColor Specular = ColorWorkflow::ToShaderColor(RenderPacket.Material->SpecularColor);
		Params.SpecularColor = FVector(Specular.R, Specular.G, Specular.B);
		Params.bFlipNormalGreen = RenderPacket.Material->bFlipNormalGreen ? 1u : 0u;

		RenderCommand::UpdateBufferData(RenderPacket.Material->ParamBuffer.get(), &Params, sizeof(FStaticMeshMaterialParams));
		// Gouraud는 VS에서 스페큘러를 계산하므로 Shininess가 VS에도 필요하다.
		RenderCommand::BindConstantBuffer(1, RenderPacket.Material->ParamBuffer.get(), EShaderBindFlagBits::Vertex | EShaderBindFlagBits::Pixel);
		break;
	}
	case EMaterialParamLayout::ParticleSubUV:
	{
		if (RenderPacket.Material->ParamBuffer && RenderPacket.MaterialParamData != nullptr)
		{
			RenderCommand::UpdateBufferData(RenderPacket.Material->ParamBuffer.get(), RenderPacket.MaterialParamData, RenderPacket.MaterialParamDataSize);
			RenderCommand::BindConstantBuffer(1, RenderPacket.Material->ParamBuffer.get(), EShaderBindFlagBits::Pixel);
		}
		break;
	}
	case EMaterialParamLayout::Billboard:
	{
		if (RenderPacket.Material->ParamBuffer && RenderPacket.MaterialParamData != nullptr)
		{
			RenderCommand::UpdateBufferData(RenderPacket.Material->ParamBuffer.get(), RenderPacket.MaterialParamData, RenderPacket.MaterialParamDataSize);
			RenderCommand::BindConstantBuffer(1, RenderPacket.Material->ParamBuffer.get(), EShaderBindFlagBits::Pixel);
		}
		break;
	}
	case EMaterialParamLayout::None:
	{
		break;
	}
	}
}

// b0 MVP 채우고 꽂기
void FRenderer::UpdatePerObjectConstants(const FRenderPacket& RenderPacket, const FMatrix& ViewProjection)
{
	// rp.Transform 과 Camera VP 행렬 곱
	// 행렬곱의 결과 (MVP Matrix) Constant Buffer 업데이트 필요

	UpdatePerObjectConstants(GetPacketWorld(RenderPacket), GetPacketNormalMatrix(RenderPacket), RenderPacket.Proxy && RenderPacket.Proxy->ReceivesShadow());
}

void FRenderer::UpdatePerObjectConstants(const FMatrix& World, const FMatrix& NormalMatrix, bool bReceiveShadow)
{
	FPerObjectConstants Constants;

	Constants.World = World;
	Constants.NormalMatrix = NormalMatrix;
	Constants.bReceiveShadow = bReceiveShadow ? 1u : 0u;

	RenderCommand::UpdateBufferData(PerObjectCB.get(), &Constants);
}

void FRenderer::CullLights(FTileLightCullingResources& Resources, const FMatrix& Projection, FTexture2D* Depth, const TArray<FCullingLight>& Lights, uint32 Width, uint32 Height, bool bVisualizeLightCount)
{
    const bool bReady = TileLightCulling.Execute(Resources, Width, Height, Projection, Depth, Lights.GetData(), Lights.Num());
    FTileLightShadingConstants Constants{};
    Constants.TileCountX = Resources.TileCountX;
    Constants.TileCountY = Resources.TileCountY;
    Constants.MaxLightsPerTile = Resources.MaxLightsPerTile;
    Constants.PointCount = LocalLight.GetNumPointLights();
    Constants.SpotCount = LocalLight.GetNumSpotLights();
    Constants.bVisualizeLightCount = bReady && bVisualizeLightCount;
    Constants.bUseTileList = bReady;
    RenderCommand::UpdateBufferData(TileShadingCB.get(), &Constants);
    RenderCommand::BindConstantBuffer(4, TileShadingCB.get(), EShaderBindFlagBits::Vertex | EShaderBindFlagBits::Pixel);
    RenderCommand::BindShaderResource(10, bReady ? Resources.TileLightCountSRV.Get() : nullptr, EShaderBindFlagBits::Pixel);
    RenderCommand::BindShaderResource(11, bReady ? Resources.TileLightIndicesSRV.Get() : nullptr, EShaderBindFlagBits::Pixel);
}

bool FRenderer::DrawShadowDepth(const FShadowRequestPerLight& Request, const FShadowDepthPassInfo& Pass, FShaderProgram* Shader, ID3D11DepthStencilView* DSV, const FShadowAtlasRect& Rect)
{
    if (!DSV || !Rect.IsValid() || !ShadowTileClearShader || !ShadowTileClearDepthState) return false;
    if (!ShadowDepthPassCB || !ShadowDepthPassCB->GetBuffer() || !PerObjectCB || !PerObjectCB->GetBuffer()) return false;
    const FLightSceneProxy* Light = Request.Light;
    if (!Light) return false;

    FShadowDepthPassConstants Constants{};
    Constants.ViewProjection = Pass.ViewProjection;
    float Radius = 0.0f;
    switch (Light->GetLightType())
    {
    case ELightType::Directional:
        break;
    case ELightType::Point:
    {
        const FPointLightInfo& Info = static_cast<const FPointLightSceneProxy*>(Light)->GetInfo();
        Constants.LightPosition = Info.Position;
        Radius = Info.AttenuationRadius;
        break;
    }
    case ELightType::Spot:
    {
        const FSpotLightInfo& Info = static_cast<const FSpotLightSceneProxy*>(Light)->GetInfo();
        Constants.LightPosition = Info.Position;
        Radius = Info.AttenuationRadius;
        break;
    }
    default:
        return false;
    }
    if (!Shader) return false;
    if (Light->GetLightType() != ELightType::Directional)
    {
        if (Radius <= 0.0f) return false;
        Constants.InvRadius = 1.0f / Radius;
    }
    void* Dest = RenderCommand::MapWriteDiscard(ShadowDepthPassCB.get());
    if (!Dest) return false;
    std::memcpy(Dest, &Constants, sizeof(Constants));
    RenderCommand::Unmap(ShadowDepthPassCB.get());

    // 그림자 입력 슬롯을 비워 SRV/DSV 동시 바인딩을 피한다.
    for (uint32 Slot : {ShadowSRVSlots::Infos, ShadowSRVSlots::Atlas, ShadowSRVSlots::DirectionalInfos})
        RenderCommand::BindShaderResource(Slot, static_cast<ID3D11ShaderResourceView*>(nullptr), EShaderBindFlagBits::Vertex | EShaderBindFlagBits::Pixel);
    FRenderingInfo RenderingInfo{};
    RenderingInfo.ViewportSetting.StartX = Rect.X;
    RenderingInfo.ViewportSetting.StartY = Rect.Y;
    RenderingInfo.ViewportSetting.Width = Rect.Size;
    RenderingInfo.ViewportSetting.Height = Rect.Size;
    RenderingInfo.DepthStencil.DSV = DSV;
    RenderingInfo.DepthStencil.LoadOp = ERenderTargetLoadOp::Load;
    RenderCommand::BeginRenderPass(RenderingInfo);
    // 이 타일만 깊이 1로 초기화하여 다른 Light의 캐시를 보존한다.
    RenderCommand::BindShaderProgram(ShadowTileClearShader);
    RenderCommand::SetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    RenderCommand::SetRasterizerState(ERasterizerState::SolidNone);
    RenderCommand::SetBlendState(EBlendState::NoColorWrite);
    RenderCommand::GetContext()->OMSetDepthStencilState(ShadowTileClearDepthState.Get(), 0);
    RenderCommand::Draw(3);
    RenderCommand::BindShaderProgram(Shader);
    RenderCommand::SetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    // Bias는 Pass 2에서만 적용한다. 기존 SolidBack의 rasterizer bias는 0이다.
    RenderCommand::SetRasterizerState(ERasterizerState::SolidBack);
    RenderCommand::SetBlendState(EBlendState::NoColorWrite);
    RenderCommand::SetDepthStencilState(EDepthStencilState::Default);
    RenderCommand::BindConstantBuffer(0, ShadowDepthPassCB.get(), EShaderBindFlagBits::Vertex | EShaderBindFlagBits::Pixel);
    RenderCommand::BindConstantBuffer(2, PerObjectCB.get(), EShaderBindFlagBits::Vertex);
    for (const FRenderPacket& Packet : Pass.CasterPackets)
    {
        if (!Packet.Mesh || !Packet.Material || !UsesDepthPrepass(Packet.Material)) continue;
        RenderCommand::BindMesh(Packet.Mesh, Packet.LODIndex);
        // 카메라의 Slot 번호와 무관한 단건 갱신 경로를 사용한다.
        UpdatePerObjectConstants(Packet, Pass.ViewProjection);
        RenderCommand::DrawIndexed(Packet.IndexCount ? Packet.IndexCount : Packet.Mesh->GetIndexBuffer(Packet.LODIndex)->GetIndexCount(), Packet.StartIndex);
    }
    RenderCommand::EndRenderPass(RenderingInfo);
    RenderCommand::BindConstantBuffer(0, ViewCB.get(), EShaderBindFlagBits::Vertex | EShaderBindFlagBits::Pixel);
    LastMesh = nullptr;
    LastMaterial = nullptr;
    return true;
}

uint32 FRenderer::AllocateShadowResources(bool bEditorWorld)
{
    if (bEditorWorld)
    {
        if (WorldShadowResources[0]) return InvalidShadowResourceIndex;
        WorldShadowResources[0] = MakeUnique<FShadowResources>();
        return 0;
    }
    // 0번은 Editor 전용이다. 순환하되 사용 중인 슬롯을 덮어쓰지 않는다.
    for (uint32 Attempt = 1; Attempt < MaxShadowResourceSlots; ++Attempt)
    {
        const uint32 Index = NextShadowResourceIndex;
        NextShadowResourceIndex = 1 + NextShadowResourceIndex % (MaxShadowResourceSlots - 1);
        if (WorldShadowResources[Index]) continue;
        WorldShadowResources[Index] = MakeUnique<FShadowResources>();
        return Index;
    }
    return InvalidShadowResourceIndex;
}

void FRenderer::ReleaseShadowResources(uint32 Index)
{
    if (Index >= MaxShadowResourceSlots) return;
    // Context 정리 시 아직 바인딩된 뷰가 자원의 수명을 붙잡지 않게 한다.
    for (uint32 Slot : {ShadowSRVSlots::Infos, ShadowSRVSlots::Atlas, ShadowSRVSlots::DirectionalInfos})
        RenderCommand::BindShaderResource(Slot, static_cast<ID3D11ShaderResourceView*>(nullptr), EShaderBindFlagBits::Vertex | EShaderBindFlagBits::Pixel);
    WorldShadowResources[Index].reset();
}

const FShadowResources* FRenderer::GetShadowResources(uint32 Index) const
{
    return Index < MaxShadowResourceSlots ? WorldShadowResources[Index].get() : nullptr;
}

const FShadowFrameData* FRenderer::RenderWorldShadows(uint32 Index, UWorld& World, FSceneRenderer& SceneRenderer)
{
    const uint64 FrameNumber = SceneRenderer.GetFrameNumber();
    if (Index >= MaxShadowResourceSlots || !WorldShadowResources[Index] || FrameNumber == 0) return nullptr;

    FShadowResources& Resources = *WorldShadowResources[Index];
    if (Resources.RenderedFrame == FrameNumber)
        return Resources.bReadyForShading ? &SceneRenderer.GetWorldShadowFrame(Index) : nullptr;

    // 실패한 경우에도 이번 프레임에는 다시 실행하지 않는다.
    Resources.ResetFrame();
    Resources.RenderedFrame = FrameNumber;

    // GPU 자원이 교체되면 기존 깊이를 참조하던 CPU 캐시도 무효다.
    if (Resources.DepthAtlas && (Resources.DepthAtlas->GetWidth() != DefaultShadowAtlasSize ||
        Resources.DepthAtlas->GetHeight() != DefaultShadowAtlasSize ||
        !Resources.DepthAtlas->GetSRV() || !Resources.DepthAtlas->GetDSV()))
        SceneRenderer.InvalidateShadowCache(Index);
    const FShadowFrameData* PreparedFrame = SceneRenderer.PrepareWorldShadows(World, Index);
    if (!PreparedFrame) return nullptr;

    const FShadowFrameData& Frame = *PreparedFrame;

    // 모든 패스가 캐시되어 있어도 조회 정보는 남는다.
    if (Frame.ShadowInfos.IsEmpty()) return nullptr;

    const auto Fail = [&]() -> const FShadowFrameData*
    {
        SceneRenderer.InvalidateShadowCache(Index);
        return nullptr;
    };

    for (const FShadowDepthPassInfo& Pass : Frame.ShadowDepthPassInfos)
    {
        if (Pass.RequestIndex >= Frame.ShadowRequestsPerLight.Num() ||
            Pass.MapRequestIndex >= Frame.ShadowMapRequests.Num()) return Fail();
        const FShadowRequestPerLight& Request = Frame.ShadowRequestsPerLight[Pass.RequestIndex];
        if (!Request.Light) return Fail();
        const FShadowMapRequest& Map = Frame.ShadowMapRequests[Pass.MapRequestIndex];
        const FShadowAtlasRect& Rect = Map.AtlasRect;
        if (Map.LightRequestIndex != Pass.RequestIndex || Map.ShadowInfoIndex >= Frame.ShadowInfos.Num() ||
            !Rect.IsValid() || Rect.Size > Frame.ShadowAtlasSize ||
            Rect.X > Frame.ShadowAtlasSize - Rect.Size || Rect.Y > Frame.ShadowAtlasSize - Rect.Size) return Fail();
    }

    for (uint32 Slot : {ShadowSRVSlots::Infos, ShadowSRVSlots::Atlas, ShadowSRVSlots::DirectionalInfos})
        RenderCommand::BindShaderResource(Slot, static_cast<ID3D11ShaderResourceView*>(nullptr), EShaderBindFlagBits::Vertex | EShaderBindFlagBits::Pixel);
    if (!Resources.EnsureAtlasCapacity(Frame.ShadowAtlasSize)) return Fail();

    for (const FShadowDepthPassInfo& Pass : Frame.ShadowDepthPassInfos)
    {
        const FShadowRequestPerLight& Request = Frame.ShadowRequestsPerLight[Pass.RequestIndex];
        const FShadowMapRequest& Map = Frame.ShadowMapRequests[Pass.MapRequestIndex];
        if (!DrawShadowDepth(Request, Pass, ShadowDistanceShader, Resources.DepthAtlas->GetDSV(), Map.AtlasRect)) return Fail();
    }

    if (!Resources.UploadShadowInfos(Frame.ShadowInfos)) return Fail();

    Resources.bDepthReady = true;
    Resources.bReadyForShading = true;

    for (uint32 LightIndex = 0; LightIndex < Frame.ShadowRequestsPerLight.Num(); ++LightIndex)
    {
        const FShadowRequestPerLight& Request = Frame.ShadowRequestsPerLight[LightIndex];
        if (Request.ShadowIndex == InvalidShadowIndex) continue;
        FShadowPreviewEntry Entry;
        Entry.LightId = Request.Light->GetComponent()->GetUUID();
        Entry.bPoint = Request.Light->GetLightType() == ELightType::Point;
        for (const FShadowMapRequest& Map : Frame.ShadowMapRequests)
            if (Map.LightRequestIndex == LightIndex) Entry.MapRects.Add(Map.AtlasRect);
        Resources.PreviewEntries.Add(std::move(Entry));
    }

    return &Frame;
}

bool FRenderer::RenderDirectionalShadow(FViewport& Viewport, UWorld& World, FSceneRenderer& SceneRenderer, const FSceneView& View, FShadowFrameData& Frame, uint32 ResourceIndex, uint32 Resolution)
{
    FDirectionalShadowResources& Resources = Viewport.GetDirectionalShadow();
    Resources.ResetFrame();
    Frame.ResetFrame();
    if (ResourceIndex >= MaxShadowResourceSlots || !WorldShadowResources[ResourceIndex]) return false;
    const uint64 FrameNumber = SceneRenderer.GetFrameNumber();
    Frame.Resolution = Resolution;
    SceneRenderer.PrepareShadows(World, Frame, {ELightType::Directional}, &View);
    Resources.CullingStats = Frame.CullingStats;
    if (FrameNumber == 0 || Frame.ShadowRequestsPerLight.IsEmpty()) return false;
    const FShadowRequestPerLight& Request = Frame.ShadowRequestsPerLight[0];
    const bool bUseCSM = static_cast<const FDirectionalLightSceneProxy*>(Request.Light)->UsesCSM();
    const uint32 PassCount = static_cast<uint32>(bUseCSM ? Frame.CascadeSplits.Num() : Frame.ShadowDepthPassInfos.Num());
    if (PassCount == 0)
		return false;

    Resources.RenderedFrame = FrameNumber;
    RenderCommand::BindShaderResource(ShadowSRVSlots::DirectionalInfos, static_cast<ID3D11ShaderResourceView*>(nullptr), EShaderBindFlagBits::Pixel);
    if (!SceneRenderer.AllocateShadowMaps(Frame, ResourceIndex))
    {
        Resources.CullingStats = Frame.CullingStats;
        return false;
    }
    FShadowResources& WorldResources = *WorldShadowResources[ResourceIndex];
    RenderCommand::BindShaderResource(ShadowSRVSlots::Atlas, static_cast<ID3D11ShaderResourceView*>(nullptr), EShaderBindFlagBits::Vertex | EShaderBindFlagBits::Pixel);
    if (!WorldResources.EnsureAtlasCapacity(Frame.ShadowAtlasSize)) return false;
    Resources.DepthAtlas = WorldResources.DepthAtlas;

    FShaderProgram* Shader = bUseCSM ? ShadowNearClampShader : DepthShader;
    for (uint32 Index = 0; Index < PassCount; ++Index)
    {
        const FShadowDepthPassInfo& Pass = bUseCSM ? Frame.CascadeSplits[Index].DepthPass : Frame.ShadowDepthPassInfos[Index];
        if (!DrawShadowDepth(Request, Pass, Shader, Resources.DepthAtlas->GetDSV(), Frame.ShadowMapRequests[Pass.MapRequestIndex].AtlasRect))
			return false;
    }

    Resources.bDepthReady = true;
    FShadowPreviewEntry Entry;
    Entry.LightId = Request.Light->GetComponent()->GetUUID();
    for (const FShadowMapRequest& Map : Frame.ShadowMapRequests)
        Entry.MapRects.Add(Map.AtlasRect);
    Resources.PreviewEntries.Add(std::move(Entry));
    if (!Resources.SelectionCB)
		Resources.SelectionCB = RenderCommand::CreateConstantBuffer(sizeof(FDirectionalShadowConstants));
    if (!Resources.SelectionCB || !Resources.SelectionCB->GetBuffer())
		return false;

    FDirectionalShadowConstants Constants;
    Constants.bUseCSM = bUseCSM ? 1u : 0u;
    Constants.CascadeCount = static_cast<uint32>(Frame.CascadeSplits.Num());
    Constants.CascadeViewOrigin = View.RenderLocation;
    Constants.CascadeViewForward = View.CameraForward;
    for (uint32 Index = 0; Index < Constants.CascadeCount; ++Index)
    {
        const FShadowCascadeSplit& Split = Frame.CascadeSplits[Index];
        const float BlendWidth = Index + 1 < Constants.CascadeCount ? Split.FarDepth - Frame.CascadeSplits[Index + 1].ProjectionNearDepth : 0.0f;
        Constants.CascadeDepthRanges[Index] = FVector4(Split.NearDepth, Split.FarDepth, BlendWidth, 0.0f);
    }
    RenderCommand::UpdateBufferData(Resources.SelectionCB.get(), &Constants);
    Resources.bReadyForShading = Resources.UploadShadowInfos(Frame.ShadowInfos);
    return Resources.bReadyForShading;
}

bool FRenderer::BindWorldShadows(uint32 Index, uint64 FrameNumber, bool bSupported)
{
    const FShadowResources* Resources = GetShadowResources(Index);
    const bool bReady = bSupported && Resources && Resources->RenderedFrame == FrameNumber && Resources->bReadyForShading;
    RenderCommand::BindShaderResource(ShadowSRVSlots::Infos, bReady ? Resources->ShadowInfoSRV.Get() : nullptr, EShaderBindFlagBits::Pixel);
    RenderCommand::BindShaderResource(ShadowSRVSlots::Atlas, bReady && Resources->DepthAtlas ? Resources->DepthAtlas->GetSRV() : nullptr, EShaderBindFlagBits::Pixel);
    RenderCommand::BindSamplerState(ShadowSamplerSlot, ESamplerState::PointClamp, EShaderBindFlagBits::Pixel);
    RenderCommand::BindSamplerState(ShadowComparisonSamplerSlot, ESamplerState::ShadowComparison, EShaderBindFlagBits::Pixel);
    return bReady;
}

bool FRenderer::BindDirectionalShadow(const FViewport& Viewport, uint64 FrameNumber, bool bSupported)
{
    const FDirectionalShadowResources& Resources = Viewport.GetDirectionalShadow();
    const bool bReady = bSupported && Resources.RenderedFrame == FrameNumber && Resources.bReadyForShading;
    RenderCommand::BindShaderResource(ShadowSRVSlots::DirectionalInfos, bReady ? Resources.ShadowInfoSRV.Get() : nullptr, EShaderBindFlagBits::Pixel);
    if (bReady)
        RenderCommand::BindShaderResource(ShadowSRVSlots::Atlas, Resources.DepthAtlas->GetSRV(), EShaderBindFlagBits::Pixel);
    RenderCommand::BindConstantBuffer(DirectionalShadowCBSlot, bReady ? Resources.SelectionCB.get() : nullptr, EShaderBindFlagBits::Pixel);
    RenderCommand::BindSamplerState(ShadowSamplerSlot, ESamplerState::PointClamp, EShaderBindFlagBits::Pixel);
    return bReady;
}
