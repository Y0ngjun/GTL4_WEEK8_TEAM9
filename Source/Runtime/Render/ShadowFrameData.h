#pragma once

#include "ShadowAtlasAllocator.h"
#include "Render/RenderPacket.h"
#include "Math/Frustum.h"
#include "ShadowInfo.h"

class FLightSceneProxy;

struct FShadowRequestPerLight
{
    const FLightSceneProxy* Light = nullptr;
    // 그룹의 첫 GPU 조회 정보. 면과 Cascade는 연속해서 등록한다.
    uint32 ShadowIndex = InvalidShadowIndex;

    // 해당 Light에 등록된 Map 개수.
    uint32 ShadowMapCount = 0;
};

// Map 한 장에 필요한 해상도와 배정된 Atlas 영역을 기록한다.
struct FShadowMapRequest
{
    // 해당 Map을 요청한 Light 요청의 번호.
    uint32 LightRequestIndex = ~0u;

    // 배치 결과를 반영할 GPU 조회 정보의 번호.
    uint32 ShadowInfoIndex = InvalidShadowIndex;

    // Shadow Map 한 장의 해상도.
    uint32 Resolution = 0;

    // 할당 단계에서 채우는 Atlas 영역.
    FShadowAtlasRect AtlasRect;
};

// 실제로 그릴 한 면의 CPU 자료다. 이 구조체 전체를 GPU 버퍼로 넘기지 않는다.
struct FShadowDepthPassInfo
{
    // 같은 FShadowFrameData의 ShadowRequestsPerLight 번호.
    uint32 RequestIndex = ~0u;

    // 자신이 사용할 Map 요청의 번호
    uint32 MapRequestIndex = ~0u;

    // Pass 1 상수 버퍼로 전달할 World -> Light Clip 변환 행렬. 
    FMatrix ViewProjection{};

    // CPU BVH 수집용 World-space Frustum. 
    FFrustumPlanes Frustum{};

    // 해당 면의 Frustum을 통과한 Caster들의 불투명 Static Mesh Section 패킷 목록 (현재는 반투명은 처리하지 않는다)
    FRenderQueue CasterPackets;
};


// World별 프레임 준비 자료. GPU Atlas는 FShadowResources가 소유한다.
struct FShadowFrameData
{
    // 이번 프레임에서 그림자 계산을 할 Light 목록
    TArray<FShadowRequestPerLight> ShadowRequestsPerLight;

    // 그림자 맵 단위의 할당 요청 목록. Spot은 1개, CSM은 Cascade별로 요청한다.
    // 요청 해상도 등을 기록하며, 실제 GPU 저장 위치는 할당 단계에서 결정한다.
    TArray<FShadowMapRequest> ShadowMapRequests;
    
    // 각 Light 별 그림자 계산을 할때 필요한 정보들
    // Point Light라면 FShadowRequestPerLight 1개가 추가되면 FShadowDepthPassInfo는 6개가 추가된다.
    TArray<FShadowDepthPassInfo> ShadowDepthPassInfos;

    // Map별 GPU 조회 정보. 같은 그룹은 면 또는 Cascade 순서로 연속 저장한다.
    TArray<FShadowInfo> ShadowInfos;
    // 이번 프레임의 Shadow Atlas 전체 크기.
    uint32 ShadowAtlasSize = 0;

    void ResetFrame()
    {
        ShadowInfos.Reset();
        ShadowDepthPassInfos.Reset();
        ShadowRequestsPerLight.Reset();

        ShadowMapRequests.Reset();
        ShadowAtlasSize = 0;
    }
};
