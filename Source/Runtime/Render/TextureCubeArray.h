#pragma once

#include "TextureArrayUtils.h"
#include <wrl/client.h>
#include "Containers/Array.h"
#include "Texture.h"

class FTextureCubeArray : public FTexture
{
public:
	static constexpr uint32 FaceCount = 6;
	FTextureCubeArray(ID3D11Device* Device, const FTextureArrayCreateInfo& Info, uint32 InCubeCount);
	bool IsValid() const { return Texture != nullptr; }
	uint32 GetCubeCount() const { return CubeCount; }
	// D3D 순서: +X, -X, +Y, -Y, +Z, -Z.
	ID3D11DepthStencilView* GetFaceDSV(uint32 CubeIndex, uint32 FaceIndex) const;

private:
	uint32 CubeCount = 0;
	TArray<Microsoft::WRL::ComPtr<ID3D11DepthStencilView>> FaceDSVs;
};
