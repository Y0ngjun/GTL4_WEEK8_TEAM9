#pragma once

#include "Core/Types.h"
#include "ShadowInfo.h"
#include "Texture2DArray.h"
#include "TextureCubeArray.h"

inline constexpr uint32 InvalidShadowResourceIndex = ~0u;
// 동시 World 수의 고정 상한. 0번 Editor + 나머지 World의 빈 슬롯 순환에 사용한다.
inline constexpr uint32 MaxShadowResourceSlots = 32;

// UI는 CPU Frame 정리 후 그려지므로 포인터 대신 Light UUID와 출력 위치만 보존한다.
struct FShadowPreviewEntry
{
    uint32 LightId;
    uint32 ShadowMapIndex;
    bool bCube;
};

// Renderer의 한 World 슬롯이 소유한다. World/Context 포인터는 보관하지 않는다.
struct FShadowResources
{
    TUniquePtr<FTexture2DArray> Depth2D;
    TUniquePtr<FTextureCubeArray> DepthCube;
    ComPtr<ID3D11Buffer> ShadowInfoBuffer;
    ComPtr<ID3D11ShaderResourceView> ShadowInfoSRV;
    uint32 ShadowInfoCapacity = 0;
    uint32 NumShadowInfos = 0;
    TArray<FShadowPreviewEntry> PreviewEntries;
    bool bReadyForShading = false;
    bool UploadShadowInfos(const TArray<FShadowInfo>& Infos);

    uint64 RenderedFrame = 0;
    bool bDepthReady = false;

    // 필요한 크기만 늘리고 충분한 기존 할당은 재사용한다. 실패하면 기존 자원은 유지한다.
    bool EnsureCapacity(uint32 Resolution, uint32 SliceCount, uint32 CubeCount);
};
