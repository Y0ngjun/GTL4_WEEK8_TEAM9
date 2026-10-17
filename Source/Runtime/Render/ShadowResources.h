#pragma once

#include "Core/Types.h"
#include "Texture2DArray.h"
#include "TextureCubeArray.h"

inline constexpr uint32 InvalidShadowResourceIndex = ~0u;
// 동시 World 수의 고정 상한. 0번 Editor + 나머지 World의 빈 슬롯 순환에 사용한다.
inline constexpr uint32 MaxShadowResourceSlots = 32;
inline constexpr uint32 DefaultShadowResolution = 1024;

// Renderer의 한 World 슬롯이 소유한다. World/Context 포인터는 보관하지 않는다.
struct FShadowResources
{
    TUniquePtr<FTexture2DArray> Depth2D;
    TUniquePtr<FTextureCubeArray> DepthCube;
    uint64 RenderedFrame = 0;
    bool bDepthReady = false;

    // 필요한 크기만 늘리고 충분한 기존 할당은 재사용한다. 실패하면 기존 자원은 유지한다.
    bool EnsureCapacity(uint32 Resolution, uint32 SliceCount, uint32 CubeCount);
};
