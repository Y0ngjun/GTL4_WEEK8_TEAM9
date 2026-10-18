#pragma once
#include "EnginePCH.h"


class FSpotLightShadowMapArray final
{
public:
	
	FSpotLightShadowMapArray() = default;
	~FSpotLightShadowMapArray() = default;

	FSpotLightShadowMapArray(const FSpotLightShadowMapArray&) = delete;
	FSpotLightShadowMapArray& operator=(const FSpotLightShadowMapArray&) = delete;

	//Count장 짜리 depth 배열 텍스쳐 준비, 이미 같은 크기로 있으면 가만히
	bool Ensure(const uint32 Count, const uint32 InResolution);
	//비어있는 slice번호 하나를 빌려준다. OutSlot은 현재 광원의 Slot번호를 넣어주는데, 할당된 슬롯이 없으면 -1이 들어온다.
	bool AllocateSlot(int32& OutSlot);
	//빌려준 번호를 돌려받고,광원 쪽 값을 -1로 만든다.
	void FreeSlot(int32& InOutSlot);
	//GPU리소스와 slot 정보를 전부 버리고 처음 상태로 돌린다.
	void Reset();

	uint32 GetResolution() const { return Resolution; }
	ID3D11DepthStencilView* GetSliceDSV(int32 SlotNum) const;
	// 배열 전체를 읽는 SRV(t13). 아직 만들지 않았으면 nullptr.
	ID3D11ShaderResourceView* GetSRV() const { return SRV.Get(); }


private:

	// ArraySize = MaxSpotShadows, R24G8_TYPELESS
	ComPtr<ID3D11Texture2D>Texture;    
	// 슬라이스 i에 그릴 때 사용
	TArray<ComPtr<ID3D11DepthStencilView>>SliceDSVs;  
	//t13에 1번 바인딩
	ComPtr<ID3D11ShaderResourceView>SRV;        

	TArray<uint32>FreeSlots;

	uint32 Resolution = 1024;
};