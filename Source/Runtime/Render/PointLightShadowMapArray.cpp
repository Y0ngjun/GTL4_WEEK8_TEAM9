#include "EnginePCH.h"
#include "PointLightShadowMapArray.h"
#include "RenderCommand.h"

bool FPointLightShadowMapArray::Ensure(const uint32 Count, const uint32 InResolution)
{
	//이미 같은 크기로 준비되어있으면 안만들기
	// DSV는 광원마다 면 6개이므로 Count * 6과 비교한다. 다르면 매번 다시 만들면서 slot이 초기화된다.
	if (Texture && FaceDSVs.Num() == static_cast<int32>(Count * 6) && Resolution == InResolution)
		return true;

	Reset();

	ID3D11Device* Device = RenderCommand::GetDevice();

	D3D11_TEXTURE2D_DESC Desc = {};
	Desc.Width = InResolution;
	Desc.Height = InResolution;
	Desc.MipLevels = 1;
	Desc.ArraySize = Count * 6;
	Desc.Format = DXGI_FORMAT_R24G8_TYPELESS;
	Desc.SampleDesc.Count = 1;
	Desc.Usage = D3D11_USAGE_DEFAULT;
	//깊이/스텐실로 바인딩가능하고, 리소스로 바인딩 가능
	Desc.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;
	// 6장씩 묶어 큐브로 읽을 수 있게 한다. 이 플래그가 없으면 큐브 SRV를 만들 수 없다.
	Desc.MiscFlags = D3D11_RESOURCE_MISC_TEXTURECUBE;
	if (FAILED(Device->CreateTexture2D(&Desc, nullptr, Texture.GetAddressOf())))
	{
		Reset();
		return false;
	}

	for (uint32 Slot = 0; Slot < Count; ++Slot)
	{
		for (uint32 Face = 0; Face < 6; ++Face)
		{
			D3D11_DEPTH_STENCIL_VIEW_DESC DsvDesc{};
			DsvDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
			DsvDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2DARRAY;
			DsvDesc.Texture2DArray.FirstArraySlice = Slot * 6 + Face;   // 이 광원의 이 면
			DsvDesc.Texture2DArray.ArraySize = 1;                       // 그 한 장만
			ComPtr<ID3D11DepthStencilView> Dsv;
			if (FAILED(Device->CreateDepthStencilView(Texture.Get(), &DsvDesc, Dsv.GetAddressOf())))
			{
				Reset();
				return false;
			}
			FaceDSVs.Add(Dsv);                                          // index = Slot * 6 + Face
		}
	}

	D3D11_SHADER_RESOURCE_VIEW_DESC SrvDesc{};
	//X는 8비트가 있지만 무시한다는 뜻(깊이 24비트 스텐실 8비트 인데 깊이만 필요해서)
	SrvDesc.Format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
	// 셰이더는 방향 벡터로 읽고 GPU가 면을 고른다. slot k = 큐브 k
	SrvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURECUBEARRAY;
	SrvDesc.TextureCubeArray.MostDetailedMip = 0;
	SrvDesc.TextureCubeArray.MipLevels = 1;
	SrvDesc.TextureCubeArray.First2DArrayFace = 0;
	SrvDesc.TextureCubeArray.NumCubes = Count;
	if (FAILED(Device->CreateShaderResourceView(Texture.Get(), &SrvDesc, SRV.GetAddressOf())))
	{
		Reset();
		return false;
	}

	//0번부터 꺼내지도록 역순으로 넣는다.
	for (uint32 Slot = Count; Slot > 0; --Slot)
		FreeSlots.Add(Slot - 1);
	Resolution = InResolution;
	return true;
}

ID3D11DepthStencilView* FPointLightShadowMapArray::GetFaceDSV(int32 Slot, uint32 Face) const
{
	const int32 Index = Slot * 6 + static_cast<int32>(Face);

	if (Slot >= 0 && Face < 6 && Index < FaceDSVs.Num())
		return FaceDSVs[Index].Get();

	return nullptr;
}

bool FPointLightShadowMapArray::AllocateSlot(int32& OutSlot)
{
	//Outslot은 현재 광원에 할당된 슬롯 번호이다. -1이면 아직 슬롯이 할당되지않았음
	if (OutSlot >= 0)
		return true;

	//남는 슬롯이 없으면 그림자 없이 비춘다.
	if (FreeSlots.Num() == 0)
	{
		OutSlot = -1;
		return false;
	}

	OutSlot = static_cast<int32>(FreeSlots.Last());
	FreeSlots.Pop();

	return true;
}

void FPointLightShadowMapArray::FreeSlot(int32& InOutSlot)
{
	//슬롯번호가 -1이면 할당안된거임
	if (InOutSlot < 0)
		return;

	FreeSlots.Add(static_cast<uint32>(InOutSlot));
	InOutSlot = -1;
}

void FPointLightShadowMapArray::Reset()
{
	SRV.Reset();
	FaceDSVs.Reset();
	Texture.Reset();
	FreeSlots.Reset();
	Resolution = 0;
}
