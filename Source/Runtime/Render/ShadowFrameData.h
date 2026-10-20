#pragma once

#include "Render/RenderPacket.h"
#include "Math/Frustum.h"
#include "ShadowInfo.h"

class FLightSceneProxy;

struct FShadowRequestPerLight
{
    // 이번 프레임에 그림자를 생성할 Light 하나
    const FLightSceneProxy* Light = nullptr;

    // Light의 shadow map이 그려질 배열의 index
    // Light 종류로 배열을 선택한다: Directional/Spot은 Texture2DArray, Point는 CubeArray.
    // 이 Index는 2D 배열에서는 slice 번호, CubeArray에서는 cube 번호다(물리 slice 번호가 아니다).
    // 배열별로 0부터 배정한다. Scene의 Light 번호나 GPU ShadowInfo 번호와는 무관하다.    
    // todo : 현재는 caching을 적용하지 않아서, 매 프레임 shadow map을 다시 그린다.
    uint32 ShadowMapIndex = 0;

    // Light별 준비가 완료된 경우에만 ShadowInfos의 번호를 설정한다. 출력 slice와 다르다.
    uint32 ShadowIndex = InvalidShadowIndex;
};

// 실제로 그릴 한 면의 CPU 자료다. 이 구조체 전체를 GPU 버퍼로 넘기지 않는다.
struct FShadowDepthPassInfo
{
    // 같은 FShadowFrameData의 ShadowRequestsPerLight 번호.
    uint32 RequestIndex = ~0u;

    // Pass 1에서 선택할 DSV의 물리 slice 번호.
    // Directional/Spot: ArraySlice = ShadowMapIndex. 
    // Point: ArraySlice = ShadowMapIndex * 6 + 면 번호.
    uint32 ArraySlice = 0;

    // Pass 1 상수 버퍼로 전달할 World -> Light Clip 변환 행렬. 
    FMatrix ViewProjection{};

    // CPU BVH 수집용 World-space Frustum. 
    FFrustumPlanes Frustum{};

    // 해당 면의 Frustum을 통과한 Caster들의 불투명 Static Mesh Section 패킷 목록 (현재는 반투명은 처리하지 않는다)
    FRenderQueue CasterPackets;
};


// 한 World의 이번 프레임 CPU 준비 자료. GPU 그림자 배열 자체를 소유하지 않는다.
// GPU 배열은 Renderer가 소유하고 WorldContext가 자원 슬롯을 가리킨다.
// 매 프레임 깊이를 다시 그리되 같은 World의 여러 View는 그 프레임 결과를 공유한다.
// GPU 배열 할당은 재사용하며, 사용할 면의 깊이는 Draw 전에 Clear한다.
struct FShadowFrameData
{
    // 이번 프레임에서 그림자 계산을 할 Light 목록
    TArray<FShadowRequestPerLight> ShadowRequestsPerLight;
    
    // 각 Light 별 그림자 계산을 할때 필요한 정보들
    // Point Light라면 FShadowRequestPerLight 1개가 추가되면 FShadowDepthPassInfo는 6개가 추가된다.
    TArray<FShadowDepthPassInfo> ShadowDepthPassInfos;

    // Light별 준비 함수가 완성된 조회 정보만 추가하고 Request.ShadowIndex를 연결한다.
    TArray<FShadowInfo> ShadowInfos;

    // Depth 패스 해상도. Bias의 텍셀 크기(T)도 이 값으로 계산한다.
    uint32 Resolution = DefaultShadowResolution;

    void ResetFrame()
    {
        ShadowInfos.Reset();
        ShadowDepthPassInfos.Reset();
        ShadowRequestsPerLight.Reset();
    }
};
