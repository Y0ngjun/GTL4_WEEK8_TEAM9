#pragma once

#include <d3d11.h>
#include <wrl/client.h>
#include "Core/Types.h"

struct FShadowDepthTarget
{
	Microsoft::WRL::ComPtr<ID3D11Texture2D> Texture;

	TArray<Microsoft::WRL::ComPtr<ID3D11DepthStencilView>> SliceDSVs;
	Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> ArraySRV;
	TArray<Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>> SliceSRVs;

	uint32 Resolution = 0;
	uint32 SliceCount = 0;
	bool bCube = false;

	bool Ensure(uint32 InResolution, uint32 InSliceCount, bool bInCube);
	bool IsValid() const;
	void Reset();

	uint64 GetMemoryBytes() const { return static_cast<uint64>(Resolution) * Resolution * 4 * SliceCount; }
};
