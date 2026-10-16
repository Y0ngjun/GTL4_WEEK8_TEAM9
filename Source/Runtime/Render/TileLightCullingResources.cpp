#include "EnginePCH.h"
#include "TileLightCullingResources.h"
#include "TileLightCulling.h"
#include "Render/RenderCommand.h"

void FTileLightCullingResources::Reset()
{
    LightSpheresSRV.Reset();

    TileLightCountSRV.Reset();
    TileLightCountUAV.Reset();

    TileLightIndicesSRV.Reset();
    TileLightIndicesUAV.Reset();

    LightSpheres.reset();
    TileLightCount.reset();
    TileLightIndices.reset();

    LightCapacity = 0;
    TileCapacity = 0;
    TileCountX = 0;
    TileCountY = 0;
    MaxLightsPerTile = 0;
}

bool FTileLightCullingResources::IsValid() const
{
    const bool bBuffersValid =
                                LightSpheres && LightSpheres->GetBuffer() &&
                                TileLightCount && TileLightCount->GetBuffer() &&
                                TileLightIndices && TileLightIndices->GetBuffer();

    const bool bViewsValid =
                                LightSpheresSRV.Get() != nullptr &&
                                TileLightCountSRV.Get() != nullptr &&
                                TileLightCountUAV.Get() != nullptr &&
                                TileLightIndicesSRV.Get() != nullptr &&
                                TileLightIndicesUAV.Get() != nullptr;

    const bool bDimensionsValid =
                                LightCapacity > 0 &&
                                TileCountX > 0 &&
                                TileCountY > 0 &&
                                MaxLightsPerTile > 0;

    return bBuffersValid && bViewsValid && bDimensionsValid;
}


/*
크기를 검증
기존 자원이 유효하고 용량이 충분하면 재사용
재생성이 필요하면 기존 자원을 정리하고 버퍼·뷰를 생성
하나라도 실패하면 전체를 정리하고 false를 반환
*/
bool FTileLightCullingResources::EnsureCapacity(uint32 InTileCountX, uint32 InTileCountY, uint32 RequiredLightCount, uint32 InMaxLightsPerTile)
{
    if (InTileCountX == 0 || InTileCountY == 0 || InMaxLightsPerTile == 0)
    {
        Reset();
        return false;
    }

    // 실제 라이트가 없어도 바인딩할 입력 버퍼는 최소 1칸 확보
    const uint32 RequiredCapacity = RequiredLightCount > 0 ? RequiredLightCount : 1;
    constexpr uint64 MaxBufferBytes = (std::numeric_limits<uint32>::max)();
    const uint64 MaxUintElements = MaxBufferBytes / sizeof(uint32);
    const uint64 NumTiles = static_cast<uint64>(InTileCountX) * InTileCountY;

    if (NumTiles > MaxUintElements || RequiredCapacity > MaxBufferBytes / sizeof(FCullingLight))
    {
        HTR_LOG(Error, "[TileLightResources] buffer size exceeds uint32 range");
        Reset();
        return false;
    }

    const uint64 NumIndices = NumTiles * InMaxLightsPerTile;

    if (NumIndices > MaxUintElements)
    {
        HTR_LOG(Error, "[TileLightResources] index buffer size exceeds uint32 range");
        Reset();
        return false;
    }

    if (IsValid() && TileCapacity >= NumTiles && MaxLightsPerTile == InMaxLightsPerTile && LightCapacity >= RequiredCapacity)
    {
        TileCountX = InTileCountX;
        TileCountY = InTileCountY;
        return true;
    }

    ID3D11Device* Device = RenderCommand::GetDevice();
    if (!LightSpheres || LightCapacity < RequiredCapacity)
    {
        LightSpheresSRV.Reset();
        // 입력은 사용자 정의 라이트 구조체 배열
        LightSpheres = RenderCommand::CreateStructuredBuffer(RequiredCapacity, sizeof(FCullingLight), D3D11_USAGE_DYNAMIC, false);
        if (!LightSpheres || !LightSpheres->GetBuffer())
        {
            Reset();
            return false;
        }

        D3D11_SHADER_RESOURCE_VIEW_DESC LightSRVDesc{};
        LightSRVDesc.Format = DXGI_FORMAT_UNKNOWN;
        LightSRVDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
        LightSRVDesc.Buffer.NumElements = RequiredCapacity;
        if (FAILED(Device->CreateShaderResourceView(LightSpheres->GetBuffer(), &LightSRVDesc, LightSpheresSRV.GetAddressOf())))
        {
            Reset();
            return false;
        }

        LightCapacity = RequiredCapacity;
    }

    // 두 출력은 R32_UINT 뷰로 해석하는 uint 배열
    const auto CreateOutputResource = [Device](uint32 ElementCount, TUniquePtr<FBuffer>& OutBuffer, Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>& OutSRV, Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView>& OutUAV) -> bool
    {
        D3D11_BUFFER_DESC Desc{};
        Desc.ByteWidth = ElementCount * sizeof(uint32);
        Desc.Usage = D3D11_USAGE_DEFAULT;
        Desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
        OutBuffer = RenderCommand::CreateBuffer(Desc);

        if (!OutBuffer || !OutBuffer->GetBuffer())
            return false;

        D3D11_SHADER_RESOURCE_VIEW_DESC SRVDesc{};
        SRVDesc.Format = DXGI_FORMAT_R32_UINT;
        SRVDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
        SRVDesc.Buffer.NumElements = ElementCount;

        if (FAILED(Device->CreateShaderResourceView(OutBuffer->GetBuffer(), &SRVDesc, OutSRV.GetAddressOf())))
            return false;

        D3D11_UNORDERED_ACCESS_VIEW_DESC UAVDesc{};
        UAVDesc.Format = DXGI_FORMAT_R32_UINT;
        UAVDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
        UAVDesc.Buffer.NumElements = ElementCount;

        if (FAILED(Device->CreateUnorderedAccessView(OutBuffer->GetBuffer(), &UAVDesc, OutUAV.GetAddressOf())))
            return false;

        return true;
    };

    if (!TileLightCount || TileCapacity < NumTiles || MaxLightsPerTile != InMaxLightsPerTile)
    {
        TileLightCountSRV.Reset();
        TileLightCountUAV.Reset();
        TileLightIndicesSRV.Reset();
        TileLightIndicesUAV.Reset();
        const bool bCreated = CreateOutputResource(static_cast<uint32>(NumTiles), TileLightCount, TileLightCountSRV, TileLightCountUAV) && CreateOutputResource(static_cast<uint32>(NumIndices), TileLightIndices, TileLightIndicesSRV, TileLightIndicesUAV);
        if (!bCreated)
        {
            HTR_LOG(Error, "[TileLightResources] buffer or view creation failed");
            Reset();
            return false;
        }
        TileCapacity = static_cast<uint32>(NumTiles);
    }
    TileCountX = InTileCountX;
    TileCountY = InTileCountY;
    MaxLightsPerTile = InMaxLightsPerTile;

    return true;
}
