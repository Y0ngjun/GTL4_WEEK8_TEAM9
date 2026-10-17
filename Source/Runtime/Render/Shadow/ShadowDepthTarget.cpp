#include "EnginePCH.h"
#include "ShadowDepthTarget.h"
#include "Render/RenderCommand.h"

void FShadowDepthTarget::Reset()
{
	SliceDSVs.Reset();
	SliceSRVs.Reset();
	ArraySRV.Reset();
	Texture.Reset();
	Resolution = 0;
	SliceCount = 0;
	bCube = false;
}

bool FShadowDepthTarget::IsValid() const
{
	return Texture && ArraySRV && SliceDSVs.Num() == SliceCount && SliceCount > 0;
}

bool FShadowDepthTarget::Ensure(uint32 InResolution, uint32 InSliceCount, bool bInCube)
{
	if (IsValid() && Resolution == InResolution && SliceCount == InSliceCount && bCube == bInCube)
	{
		return true;
	}

	Reset();
	if (InResolution == 0 || InSliceCount == 0 || (bInCube && InSliceCount % 6 != 0))
	{
		return false;
	}

	ID3D11Device* Device = RenderCommand::GetDevice();

	D3D11_TEXTURE2D_DESC Desc{};
	Desc.Width = InResolution;
	Desc.Height = InResolution;
	Desc.MipLevels = 1;
	Desc.ArraySize = InSliceCount;
	Desc.Format = DXGI_FORMAT_R32_TYPELESS;
	Desc.SampleDesc.Count = 1;
	Desc.Usage = D3D11_USAGE_DEFAULT;
	Desc.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;
	Desc.MiscFlags = bInCube ? D3D11_RESOURCE_MISC_TEXTURECUBE : 0;

	if (FAILED(Device->CreateTexture2D(&Desc, nullptr, Texture.GetAddressOf())))
	{
		HTR_LOG(Error, "[Shadow] depth texture create failed ({}x{}, {} slices)", InResolution, InResolution, InSliceCount);
		Reset();
		return false;
	}

	SliceDSVs.SetNum(InSliceCount);
	SliceSRVs.SetNum(InSliceCount);
	for (uint32 Slice = 0; Slice < InSliceCount; ++Slice)
	{
		D3D11_DEPTH_STENCIL_VIEW_DESC Dsv{};
		Dsv.Format = DXGI_FORMAT_D32_FLOAT;
		Dsv.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2DARRAY;
		Dsv.Texture2DArray.FirstArraySlice = Slice;
		Dsv.Texture2DArray.ArraySize = 1;
		if (FAILED(Device->CreateDepthStencilView(Texture.Get(), &Dsv, SliceDSVs[Slice].GetAddressOf())))
		{
			HTR_LOG(Error, "[Shadow] slice DSV create failed (slice {})", Slice);
			Reset();
			return false;
		}

		D3D11_SHADER_RESOURCE_VIEW_DESC SliceSrv{};
		SliceSrv.Format = DXGI_FORMAT_R32_FLOAT;
		SliceSrv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
		SliceSrv.Texture2DArray.MipLevels = 1;
		SliceSrv.Texture2DArray.FirstArraySlice = Slice;
		SliceSrv.Texture2DArray.ArraySize = 1;
		Device->CreateShaderResourceView(Texture.Get(), &SliceSrv, SliceSRVs[Slice].GetAddressOf());
	}

	D3D11_SHADER_RESOURCE_VIEW_DESC Srv{};
	Srv.Format = DXGI_FORMAT_R32_FLOAT;
	if (bInCube)
	{
		Srv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURECUBEARRAY;
		Srv.TextureCubeArray.MipLevels = 1;
		Srv.TextureCubeArray.NumCubes = InSliceCount / 6;
	}
	else
	{
		Srv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
		Srv.Texture2DArray.MipLevels = 1;
		Srv.Texture2DArray.ArraySize = InSliceCount;
	}
	if (FAILED(Device->CreateShaderResourceView(Texture.Get(), &Srv, ArraySRV.GetAddressOf())))
	{
		HTR_LOG(Error, "[Shadow] array SRV create failed");
		Reset();
		return false;
	}

	Resolution = InResolution;
	SliceCount = InSliceCount;
	bCube = bInCube;
	HTR_LOG(Info, "[Shadow] depth target ready: {}x{}, {} slices, {} KB", Resolution, Resolution, SliceCount, GetMemoryBytes() / 1024);
	return true;
}
