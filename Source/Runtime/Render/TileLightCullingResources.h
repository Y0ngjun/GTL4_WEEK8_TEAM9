#pragma once

#include <d3d11.h>
#include <wrl/client.h>

#include "Core/Types.h"
#include "Render/Buffer.h"


// 뷰 마다 가지고 있는 tile-based light culling에 관련된 정보
struct FTileLightCullingResources
{
    // 컬링 입력: 뷰 공간의 라이트 구 배열
    TUniquePtr<FStructuredBuffer> LightSpheres;

    // 컬링 출력: 각 타일에 영향을 주는 라이트 개수와 라이트 인덱스 목록
    TUniquePtr<FBuffer> TileLightCount;
    TUniquePtr<FBuffer> TileLightIndices;


    // culling 관련 GPU정보를 여기서 소유함
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> LightSpheresSRV;

    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> TileLightCountSRV;
    Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> TileLightCountUAV;

    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> TileLightIndicesSRV;
    Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> TileLightIndicesUAV;

    // 현재 입력 버퍼에 할당된 라이트 원소 수
    uint32 LightCapacity = 0;
    uint32 TileCapacity = 0;

    // 현재 출력 버퍼가 대응하는 타일 배치와 타일당 저장 용량
    uint32 TileCountX = 0;
    uint32 TileCountY = 0;
    uint32 MaxLightsPerTile = 0;

    bool EnsureCapacity(uint32 InTileCountX, uint32 InTileCountY, uint32 RequiredLightCount, uint32 InMaxLightsPerTile);
    bool IsValid() const;
    void Reset();
};
