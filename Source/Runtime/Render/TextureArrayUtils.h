#pragma once

#include <d3d11.h>
#include <wrl/client.h>
#include "Core/Types.h"
#include "Containers/Array.h"

// UNKNOWN view format은 해당 뷰를 생성하지 않는다는 뜻이다.
struct FTextureArrayCreateInfo
{
	uint32 Width = 0;
	uint32 Height = 0;
	DXGI_FORMAT ResourceFormat = DXGI_FORMAT_UNKNOWN;
	DXGI_FORMAT SRVFormat = DXGI_FORMAT_UNKNOWN;
	DXGI_FORMAT DSVFormat = DXGI_FORMAT_UNKNOWN;
};

// 배열 구현 cpp에서만 사용하는 생성 도우미.
namespace TextureArrayUtils
{
	struct FCreatedResources
	{
		Microsoft::WRL::ComPtr<ID3D11Resource> Texture;
		Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> SRV;
		TArray<Microsoft::WRL::ComPtr<ID3D11DepthStencilView>> SliceDSVs;
	};

	HRESULT CreateResources(ID3D11Device* Device, const FTextureArrayCreateInfo& Info, uint32 ArraySize, bool bCube, FCreatedResources& OutResources);
}
