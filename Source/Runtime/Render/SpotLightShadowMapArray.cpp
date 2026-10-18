#include "EnginePCH.h"
#include "SpotLightShadowMapArray.h"
#include "RenderCommand.h"

bool FSpotLightShadowMapArray::Ensure(const uint32 Count, const uint32 InResolution)
{
	//이미 같은 크기로 준비되어있으면 안만들기
	if (Texture && SliceDSVs.Num() == static_cast<int32>(Count) && Resolution == InResolution)
		return true;

	Reset();

	ID3D11Device* Device = RenderCommand::GetDevice();

	D3D11_TEXTURE2D_DESC Desc = {};
	Desc.Width = InResolution;
	Desc.Height = InResolution;
	Desc.MipLevels = 1;
	Desc.ArraySize = Count;
	Desc.Format = DXGI_FORMAT_R24G8_TYPELESS;
	Desc.SampleDesc.Count = 1;
	Desc.Usage = D3D11_USAGE_DEFAULT;
	//깊이/스텐실로 바인딩가능하고, 리소스로 바인딩 가능
	Desc.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;
	if (FAILED(Device->CreateTexture2D(&Desc, nullptr, Texture.GetAddressOf())))
	{
		Reset();
		return false;
	}

	for(uint32 Slice = 0; Slice < Count; ++Slice)
	{
		D3D11_DEPTH_STENCIL_VIEW_DESC DsvDesc{};
		DsvDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;

		//DSV가 텍스처 배열의 일부(또는 전체)를 가리킨다고 지정하는 값 ArraySize가 2 이상인 텍스처에 DSV를 만들 때쓴다.
		DsvDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2DARRAY;
		//ArraySize와 짝으로 써서 배열의 몇 번째 장부터 몇 장을 가리키는지
		DsvDesc.Texture2DArray.FirstArraySlice = Slice;
		DsvDesc.Texture2DArray.ArraySize = 1;
		ComPtr<ID3D11DepthStencilView> Dsv;
		if (FAILED(Device->CreateDepthStencilView(Texture.Get(), &DsvDesc, Dsv.GetAddressOf())))
		{
			Reset();
			return false;
		}
		SliceDSVs.Add(Dsv);
	}

	D3D11_SHADER_RESOURCE_VIEW_DESC SrvDesc{};
	//X는 8비트가 있지만 무시한다는 뜻(깊이 24비트 스텐실 8비트 인데 깊이만 필요해서)
	SrvDesc.Format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
	SrvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
	SrvDesc.Texture2DArray.ArraySize = Count;
	//이 뷰가 밉맵 몇 단계를 포함하는가
	SrvDesc.Texture2DArray.MipLevels = 1;
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

ID3D11DepthStencilView* FSpotLightShadowMapArray::GetSliceDSV(int32 SlotNum) const
{
	// slot이 없는 광원(-1)이나 범위 밖 번호는 그릴 slice가 없다
	if (SlotNum < 0 || SlotNum >= SliceDSVs.Num())
		return nullptr;
	return SliceDSVs[SlotNum].Get();
}

bool FSpotLightShadowMapArray::AllocateSlot(int32& OutSlot)
{
	//Outslot은 현재 광원에 할당된 슬롯 번호이다. -1이면 아직 슬롯이 할당되지않았음
	if (OutSlot >= 0)
		return true;

	//남는 슬롯이 없으면 그림자 없이 비춘다.
	if(FreeSlots.Num() == 0)
	{
		OutSlot = -1;
		return false;
	}

	OutSlot = static_cast<int32>(FreeSlots.Last());
	FreeSlots.Pop();

	return true;
}

void FSpotLightShadowMapArray::FreeSlot(int32& InOutSlot)
{
	//슬롯번호가 -1이면 할당안된거임
	if (InOutSlot < 0)
		return;

	FreeSlots.Add(static_cast<uint32>(InOutSlot));
	InOutSlot = -1;
}

void FSpotLightShadowMapArray::Reset()
{
	SRV.Reset();
	SliceDSVs.Reset();
	Texture.Reset();
	FreeSlots.Reset();
	Resolution = 0;
}
