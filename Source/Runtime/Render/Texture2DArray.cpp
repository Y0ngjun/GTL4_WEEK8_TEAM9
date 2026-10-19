#include "EnginePCH.h"
#include "Texture2DArray.h"
#include "TextureArrayUtils.h"

FTexture2DArray::FTexture2DArray(ID3D11Device* Device, const FTextureArrayCreateInfo& Info, uint32 InArraySize)
{
	Dimension = ETextureDimension::Texture2DArray;
	TextureArrayUtils::FCreatedResources Created;
	const HRESULT Result = TextureArrayUtils::CreateResources(Device, Info, InArraySize, false, Created);
	if (FAILED(Result))
	{
		HTR_LOG(Error, "[Texture2DArray] Creation failed (hr=0x{:08X})", static_cast<uint32>(Result));
		return;
	}
	Texture = std::move(Created.Texture);
	SRV = std::move(Created.SRV);
	SliceDSVs = std::move(Created.SliceDSVs);
	Width = Info.Width;
	Height = Info.Height;
	Format = Info.ResourceFormat;
	ArraySize = InArraySize;
}

ID3D11DepthStencilView* FTexture2DArray::GetSliceDSV(uint32 Slice) const
{
	return Slice < static_cast<uint32>(SliceDSVs.Num()) ? SliceDSVs[Slice].Get() : nullptr;
}
