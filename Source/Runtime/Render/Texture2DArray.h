#pragma once

#include "TextureArrayUtils.h"
#include <wrl/client.h>
#include "Containers/Array.h"
#include "Texture.h"

class FTexture2DArray : public FTexture
{
public:
	FTexture2DArray(ID3D11Device* Device, const FTextureArrayCreateInfo& Info, uint32 InArraySize);
	bool IsValid() const { return Texture != nullptr; }
	uint32 GetArraySize() const { return ArraySize; }
	ID3D11DepthStencilView* GetSliceDSV(uint32 Slice) const;

private:
	uint32 ArraySize = 0;
	TArray<Microsoft::WRL::ComPtr<ID3D11DepthStencilView>> SliceDSVs;
};
