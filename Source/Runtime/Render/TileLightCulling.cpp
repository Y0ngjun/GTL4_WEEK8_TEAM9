#include "EnginePCH.h"
#include "TileLightCulling.h"
#include "Render/RenderCommand.h"
#include "Render/RenderResourceManager.h"
#include "TileLightCullingResources.h"

bool FTileLightCulling::Init()
{
    ComputeShader = nullptr;
    ConstantsBuffer.reset();

    FShaderProgram* LoadedShader = FRenderResourceManager::FindShaderProgram("Resources/Shader/CS_TileLightCulling.hlsl");

    if (!LoadedShader || !LoadedShader->ComputeShader || !LoadedShader->ComputeShader->IsValid())
    {
        HTR_LOG(Error, "[TileLightCulling] required compute program unavailable or invalid: Resources/Shader/CS_TileLightCulling.hlsl");
        return false;
    }

    ConstantsBuffer = RenderCommand::CreateConstantBuffer(sizeof(FTileCullingConstants));

    if (!ConstantsBuffer || !ConstantsBuffer->GetBuffer())
    {
        HTR_LOG(Error, "[TileLightCulling] constant buffer creation failed");
        ConstantsBuffer.reset();
        return false;
    }

    ComputeShader = LoadedShader;
    return true;
}

bool FTileLightCulling::Execute(FTileLightCullingResources& Resources, const uint32 ViewWidth, const uint32 ViewHeight, const FMatrix& Projection, FTexture2D* Depth, const FCullingLight* Lights, const uint32 LightCount)
{
    if (ViewWidth == 0 || ViewHeight == 0 || !ComputeShader || !ConstantsBuffer || !Depth)
        return false;

    // 화면 타일 크기는 스레드 그룹 크기와 독립적이다.
    constexpr uint32 TileSize = LIGHT_CULLING_TILE_SIZE;

    const uint32 TileCountX = ViewWidth / TileSize + (ViewWidth % TileSize != 0);
    const uint32 TileCountY = ViewHeight / TileSize + (ViewHeight % TileSize != 0);

    if (!Resources.EnsureCapacity(TileCountX, TileCountY, LightCount, MaxLightsPerTile))
        return false;

    FTileCullingConstants Constants{};
    Constants.ViewSize[0] = ViewWidth;
    Constants.ViewSize[1] = ViewHeight;
    Constants.TileCountX = TileCountX;
    Constants.LightCount = LightCount;
    Constants.Projection = Projection;
    Constants.MaxLightsPerTile = MaxLightsPerTile;

    UploadInputs(Resources, Lights, Constants);

    // CS_TileLightCulling.hlsl에 선언된 레지스터 번호
    constexpr uint32 ConstantsSlot = 0;    // b0
    constexpr uint32 LightSpheresSlot = 0; // t0
    constexpr uint32 DepthSlot = 1;        // t1
    constexpr uint32 LightCountSlot = 0;   // u0
    constexpr uint32 LightIndicesSlot = 1; // u1

    RenderCommand::BindShaderProgram(ComputeShader);
    RenderCommand::BindConstantBuffer(ConstantsSlot, ConstantsBuffer.get(), EShaderBindFlagBits::Compute);
    RenderCommand::BindShaderResource(LightSpheresSlot, Resources.LightSpheresSRV.Get(), EShaderBindFlagBits::Compute);

    RenderCommand::BindShaderResource(DepthSlot, Depth, EShaderBindFlagBits::Compute);

    RenderCommand::BindComputeUAV(LightCountSlot, Resources.TileLightCountUAV.Get());
    RenderCommand::BindComputeUAV(LightIndicesSlot, Resources.TileLightIndicesUAV.Get());

    // 타일 하나당 스레드 그룹 하나를 실행
    // 타일 배치는 2차원이므로 Z는 1
    RenderCommand::Dispatch(TileCountX, TileCountY, 1);

    // 후속 렌더링에서 결과를 SRV로 읽을 수 있도록 UAV 연결을 해제
    RenderCommand::BindComputeUAV(LightCountSlot, nullptr);
    RenderCommand::BindComputeUAV(LightIndicesSlot, nullptr);

    // 이번 실행에서 사용한 입력 슬롯도 해제
    RenderCommand::UnbindShaderResources(LightSpheresSlot, 2, EShaderBindFlagBits::Compute);
    RenderCommand::BindConstantBuffer(ConstantsSlot, nullptr, EShaderBindFlagBits::Compute);
    return true;
}


void FTileLightCulling::UploadInputs(FTileLightCullingResources& Resources, const FCullingLight* Lights, const FTileCullingConstants& Constants)
{
    // 라이트가 없으면 입력 배열 복사는 생략
    if (Constants.LightCount > 0)
    {
        const uint32 DataSize = Constants.LightCount * static_cast<uint32>(sizeof(FCullingLight));
        RenderCommand::UpdateBufferData(Resources.LightSpheres.get(), Lights, DataSize);
    }

    // 라이트가 없어도 LightCount = 0을 포함한 현재 뷰의 상수는 갱신
    RenderCommand::UpdateBufferData(ConstantsBuffer.get(), &Constants);
}
