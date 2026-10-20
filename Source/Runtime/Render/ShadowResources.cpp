#include "EnginePCH.h"
#include "ShadowResources.h"
#include "RenderCommand.h"

bool FShadowResources::EnsureCapacity(uint32 Resolution, uint32 SliceCount, uint32 CubeCount)
{
    if (Resolution == 0 || Resolution > D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION || SliceCount > D3D11_REQ_TEXTURE2D_ARRAY_AXIS_DIMENSION || CubeCount > D3D11_REQ_TEXTURE2D_ARRAY_AXIS_DIMENSION / FTextureCubeArray::FaceCount) return false;
    FTextureArrayCreateInfo Info{Resolution, Resolution, DXGI_FORMAT_R32_TYPELESS, DXGI_FORMAT_R32_FLOAT, DXGI_FORMAT_D32_FLOAT};
    TUniquePtr<FTexture2DArray> New2D;
    TUniquePtr<FTextureCubeArray> NewCube;
    if (SliceCount && (!Depth2D || Depth2D->GetWidth() != Resolution || Depth2D->GetArraySize() < SliceCount))
    {
        New2D = MakeUnique<FTexture2DArray>(RenderCommand::GetDevice(), Info, SliceCount);
        if (!New2D->IsValid()) return false;
    }
    if (CubeCount && (!DepthCube || DepthCube->GetWidth() != Resolution || DepthCube->GetCubeCount() < CubeCount))
    {
        NewCube = MakeUnique<FTextureCubeArray>(RenderCommand::GetDevice(), Info, CubeCount);
        if (!NewCube->IsValid()) return false;
    }
    // 같은 해상도로 배열만 늘릴 때는 기존 slice를 옮겨 쉐도우 맵 캐시를 유지한다.
    // 해상도가 바뀌면 옮길 수 없고, SceneRenderer가 해상도 변경을 보고 캐시 전체를 무효로 한다.
    ID3D11DeviceContext* Context = RenderCommand::GetContext();
    if (New2D && Depth2D && Depth2D->GetWidth() == Resolution)
        for (uint32 Slice = 0; Slice < Depth2D->GetArraySize(); ++Slice)
            Context->CopySubresourceRegion(New2D->GetRawPtr(), Slice, 0, 0, 0, Depth2D->GetRawPtr(), Slice, nullptr);
    if (NewCube && DepthCube && DepthCube->GetWidth() == Resolution)
        for (uint32 Slice = 0; Slice < DepthCube->GetCubeCount() * FTextureCubeArray::FaceCount; ++Slice)
            Context->CopySubresourceRegion(NewCube->GetRawPtr(), Slice, 0, 0, 0, DepthCube->GetRawPtr(), Slice, nullptr);
    if (New2D) Depth2D = std::move(New2D);
    if (NewCube) DepthCube = std::move(NewCube);
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
