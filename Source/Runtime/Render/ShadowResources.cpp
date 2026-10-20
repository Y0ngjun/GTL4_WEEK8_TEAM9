#include "EnginePCH.h"
#include "ShadowResources.h"
#include "RenderCommand.h"

bool FShadowResources::EnsureAtlasCapacity(uint32 AtlasSize)
{
    if (AtlasSize == 0 || AtlasSize > D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION)
    {
        return false;
    }

    if (DepthAtlas &&
        DepthAtlas->GetWidth() == AtlasSize &&
        DepthAtlas->GetHeight() == AtlasSize &&
        DepthAtlas->GetSRV() &&
        DepthAtlas->GetDSV())
    {
        return true;
    }

    D3D11_TEXTURE2D_DESC Desc{};
    Desc.Width = AtlasSize;
    Desc.Height = AtlasSize;
    Desc.MipLevels = 1;
    Desc.ArraySize = 1;
    Desc.Format = DXGI_FORMAT_R32_TYPELESS;
    Desc.SampleDesc.Count = 1;
    Desc.Usage = D3D11_USAGE_DEFAULT;
    Desc.BindFlags =
        D3D11_BIND_DEPTH_STENCIL |
        D3D11_BIND_SHADER_RESOURCE;

    auto NewAtlas = MakeShared<FTexture2D>(RenderCommand::GetDevice(), Desc, static_cast<const void*>(nullptr));

    if (!NewAtlas->GetRawPtr() ||
        !NewAtlas->GetSRV() ||
        !NewAtlas->GetDSV())
    {
        return false;
    }

    RenderCommand::GetContext()->ClearDepthStencilView(NewAtlas->GetDSV(), D3D11_CLEAR_DEPTH, 1.0f, 0);
    DepthAtlas = std::move(NewAtlas);
    return true;
}

// LocalLight와 같은 dynamic StructuredBuffer 경로. 용량이 충분하면 재사용한다.
bool FShadowResources::UploadShadowInfos(const TArray<FShadowInfo>& Infos)
{
    NumShadowInfos = 0;
    const uint32 Count = static_cast<uint32>(Infos.Num());
    if (Count == 0) return false;
    ID3D11Device* Device = RenderCommand::GetDevice();
    if (Count > ShadowInfoCapacity)
    {
        const uint32 Capacity = std::max(Count, 16u);
        D3D11_BUFFER_DESC Desc{};
        Desc.ByteWidth = Capacity * sizeof(FShadowInfo);
        Desc.Usage = D3D11_USAGE_DYNAMIC;
        Desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        Desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        Desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
        Desc.StructureByteStride = sizeof(FShadowInfo);
        ComPtr<ID3D11Buffer> Buffer;
        ComPtr<ID3D11ShaderResourceView> SRV;
        if (FAILED(Device->CreateBuffer(&Desc, nullptr, Buffer.GetAddressOf()))) return false;
        D3D11_SHADER_RESOURCE_VIEW_DESC View{};
        View.Format = DXGI_FORMAT_UNKNOWN;
        View.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
        View.Buffer.NumElements = Capacity;
        if (FAILED(Device->CreateShaderResourceView(Buffer.Get(), &View, SRV.GetAddressOf()))) return false;
        ShadowInfoBuffer = std::move(Buffer);
        ShadowInfoSRV = std::move(SRV);
        ShadowInfoCapacity = Capacity;
    }
    ID3D11DeviceContext* Context = RenderCommand::GetContext();
    D3D11_MAPPED_SUBRESOURCE Mapped{};
    if (FAILED(Context->Map(ShadowInfoBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &Mapped))) return false;
    std::memcpy(Mapped.pData, Infos.GetData(), Count * sizeof(FShadowInfo));
    Context->Unmap(ShadowInfoBuffer.Get(), 0);
    NumShadowInfos = Count;
    return true;
}
