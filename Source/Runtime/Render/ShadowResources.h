#pragma once

#include "Core/Types.h"
#include "ShadowInfo.h"
#include "ShadowCulling.h"
#include "Texture2DArray.h"
#include "TextureCubeArray.h"
#include "Buffer.h"

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

// World 슬롯 또는 Viewport가 소유한다. World/Context 포인터는 보관하지 않는다.
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
    FShadowCullingStats CullingStats;

    // GPU 할당은 유지하고 이번 프레임의 결과만 무효화한다.
    void ResetFrame()
    {
        RenderedFrame = 0;
        bDepthReady = false;
        bReadyForShading = false;
        NumShadowInfos = 0;
        PreviewEntries.Reset();
        CullingStats = {};
    }

    // 필요한 크기만 늘리고 충분한 기존 할당은 재사용한다. 실패하면 기존 자원은 유지한다.
    bool EnsureCapacity(uint32 Resolution, uint32 SliceCount, uint32 CubeCount);
};

// HLSL b5. 배열 원소는 CB의 16-byte 정렬에 맞춘다. xy=선택 near/far, z=혼합 폭, w=padding.
struct FDirectionalShadowConstants
{
    uint32 bUseCSM = 0;
    uint32 CascadeCount = 0;
    uint32 Padding[2] = {};
    FVector CascadeViewOrigin{};
    float OriginPadding = 0.0f;
    FVector CascadeViewForward{};
    float ForwardPadding = 0.0f;
    FVector4 CascadeDepthRanges[MaxShadowCascades] = {};
};
static_assert(sizeof(FDirectionalShadowConstants) == 176);
static_assert(offsetof(FDirectionalShadowConstants, CascadeViewOrigin) == 16);
static_assert(offsetof(FDirectionalShadowConstants, CascadeViewForward) == 32);
static_assert(offsetof(FDirectionalShadowConstants, CascadeDepthRanges) == 48);

// 공통 깊이 맵 자원에 Viewport Directional 전용 선택 CB만 추가한다.
struct FDirectionalShadowResources : FShadowResources
{
    TUniquePtr<FConstantBuffer> SelectionCB;
};
