#include "EnginePCH.h"
#include "TextureArrayUtils.h"

HRESULT TextureArrayUtils::CreateResources(ID3D11Device* Device, const FTextureArrayCreateInfo& Info, uint32 ArraySize, bool bCube, FCreatedResources& OutResources)
{
	OutResources = {};
	if (!Device || Info.Width == 0 || Info.Height == 0 || ArraySize == 0 ||
		Info.Width > D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION || Info.Height > D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION ||
		ArraySize > D3D11_REQ_TEXTURE2D_ARRAY_AXIS_DIMENSION || Info.ResourceFormat == DXGI_FORMAT_UNKNOWN ||
		(Info.SRVFormat == DXGI_FORMAT_UNKNOWN && Info.DSVFormat == DXGI_FORMAT_UNKNOWN) ||
		(bCube && (Info.Width != Info.Height || ArraySize % 6 != 0)))
	{
		return E_INVALIDARG;
	}

	D3D11_TEXTURE2D_DESC Desc{};
	Desc.Width = Info.Width;
	Desc.Height = Info.Height;
	Desc.MipLevels = 1;
	Desc.ArraySize = ArraySize;
	Desc.Format = Info.ResourceFormat;
	Desc.SampleDesc.Count = 1;
	Desc.Usage = D3D11_USAGE_DEFAULT;
	Desc.BindFlags = (Info.SRVFormat != DXGI_FORMAT_UNKNOWN ? D3D11_BIND_SHADER_RESOURCE : 0) |
		(Info.DSVFormat != DXGI_FORMAT_UNKNOWN ? D3D11_BIND_DEPTH_STENCIL : 0);
	Desc.MiscFlags = bCube ? D3D11_RESOURCE_MISC_TEXTURECUBE : 0;

	FCreatedResources Created;
	ComPtr<ID3D11Texture2D> Texture;
	HRESULT Result = Device->CreateTexture2D(&Desc, nullptr, Texture.GetAddressOf());
	if (FAILED(Result)) return Result;
	Created.Texture = Texture;

	if (Info.SRVFormat != DXGI_FORMAT_UNKNOWN)
	{
		D3D11_SHADER_RESOURCE_VIEW_DESC SRVDesc{};
		SRVDesc.Format = Info.SRVFormat;
		if (bCube)
		{
			SRVDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURECUBEARRAY;
			SRVDesc.TextureCubeArray.MipLevels = 1;
			SRVDesc.TextureCubeArray.NumCubes = ArraySize / 6;
		}
		else
		{
			SRVDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
			SRVDesc.Texture2DArray.MipLevels = 1;
			SRVDesc.Texture2DArray.ArraySize = ArraySize;
		}
		Result = Device->CreateShaderResourceView(Texture.Get(), &SRVDesc, Created.SRV.GetAddressOf());
		if (FAILED(Result)) return Result;
	}

	if (Info.DSVFormat != DXGI_FORMAT_UNKNOWN)
	{
		Created.SliceDSVs.SetNum(ArraySize);
		for (uint32 Slice = 0; Slice < ArraySize; ++Slice)
		{
			D3D11_DEPTH_STENCIL_VIEW_DESC DSVDesc{};
			DSVDesc.Format = Info.DSVFormat;
			DSVDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2DARRAY;
			DSVDesc.Texture2DArray.FirstArraySlice = Slice;
			DSVDesc.Texture2DArray.ArraySize = 1;
			Result = Device->CreateDepthStencilView(Texture.Get(), &DSVDesc, Created.SliceDSVs[Slice].GetAddressOf());
			if (FAILED(Result)) return Result;
		}
	}

	// 모든 뷰가 완성된 뒤에만 소유권을 전달한다. 실패하면 임시 자원이 자동 해제된다.
	OutResources = std::move(Created);
	return S_OK;
}
