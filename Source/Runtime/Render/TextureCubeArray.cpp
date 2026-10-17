#include "EnginePCH.h"
#include "TextureCubeArray.h"
#include "TextureArrayUtils.h"

FTextureCubeArray::FTextureCubeArray(ID3D11Device* Device, const FTextureArrayCreateInfo& Info, uint32 InCubeCount)
{
	Dimension = ETextureDimension::TextureCubeArray;
	// 곱셈 전에 개수를 검사하여 물리 slice 수의 overflow도 막는다.
	if (InCubeCount == 0 || InCubeCount > D3D11_REQ_TEXTURE2D_ARRAY_AXIS_DIMENSION / FaceCount)
	{
		HTR_LOG(Error, "[TextureCubeArray] Invalid cube count: {}", InCubeCount);
		return;
	}
	TextureArrayUtils::FCreatedResources Created;
	const HRESULT Result = TextureArrayUtils::CreateResources(Device, Info, InCubeCount * FaceCount, true, Created);
	if (FAILED(Result))
	{
		HTR_LOG(Error, "[TextureCubeArray] Creation failed (hr=0x{:08X})", static_cast<uint32>(Result));
		return;
	}
	Texture = std::move(Created.Texture);
	SRV = std::move(Created.SRV);
	FaceDSVs = std::move(Created.SliceDSVs);
	Width = Info.Width;
	Height = Info.Height;
	Format = Info.ResourceFormat;
	CubeCount = InCubeCount;
}

ID3D11DepthStencilView* FTextureCubeArray::GetFaceDSV(uint32 CubeIndex, uint32 FaceIndex) const
{
	if (CubeIndex >= CubeCount || FaceIndex >= FaceCount || FaceDSVs.IsEmpty()) return nullptr;
	return FaceDSVs[CubeIndex * FaceCount + FaceIndex].Get();
}
