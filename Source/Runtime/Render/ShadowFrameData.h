#pragma once

#include "Render/RenderPacket.h"
#include "Math/Frustum.h"
#include "ShadowInfo.h"
#include "ShadowCulling.h"
#include "ShadowAtlasAllocator.h"

class FLightSceneProxy;

struct FShadowRequestPerLight
{
    // 이번 프레임에 그림자를 생성할 Light 하나
    const FLightSceneProxy* Light = nullptr;

    // 그룹의 첫 GPU 조회 정보. 면과 Cascade는 연속 등록한다.
    uint32 ShadowIndex = InvalidShadowIndex;
    uint32 ShadowMapCount = 0;
    // Spot/Point의 자동 해상도 선택에 쓴 최대 화면 높이 비율. Directional은 사용하지 않는다.
    float ScreenRatio = 0.0f;
};

// Map 한 장의 해상도와 Atlas 배치 결과.
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

    // 이 Pass가 사용할 Atlas Map 요청.
    uint32 MapRequestIndex = ~0u;

    // Pass 1 상수 버퍼로 전달할 World -> Light Clip 변환 행렬. 
    FMatrix ViewProjection{};

    // CPU BVH 수집용 World-space Frustum. 
    FFrustumPlanes Frustum{};

    // 해당 면의 Frustum을 통과한 Caster들의 불투명 Static Mesh Section 패킷 목록 (현재는 반투명은 처리하지 않는다)
    FRenderQueue CasterPackets;
};

struct FShadowCascadeSplit
{
    // RenderLocation 기준 전방 축의 선형 깊이. Cascade 선택 경계는 겹치지 않는다.
    float NearDepth = 0.0f;
    float FarDepth = 0.0f;
    // 앞 Cascade의 혼합 시작점까지 확장한 투영용 near. 투영 far는 FarDepth와 같다.
    float ProjectionNearDepth = 0.0f;
    // subfrustum을 감싸는 원래 구. Snapping은 투영의 가로·세로 중심에만 적용한다.
    FVector SphereCenter;
    float SphereRadius = 0.0f;
    // ViewProjection은 구 범위, Frustum은 caster 수집용 범위다.
    FShadowDepthPassInfo DepthPass;
};


// Spot/Point는 World별 Atlas, Directional은 View별 독립 Atlas에 대응한다.
struct FShadowFrameData
{
    // 이번 프레임에서 그림자 계산을 할 Light 목록
    TArray<FShadowRequestPerLight> ShadowRequestsPerLight;
    TArray<FShadowMapRequest> ShadowMapRequests;
    uint32 ShadowAtlasSize = 0;
    
    // 각 Light 별 그림자 계산을 할때 필요한 정보들
    // Point Light라면 FShadowRequestPerLight 1개가 추가되면 FShadowDepthPassInfo는 6개가 추가된다.
    TArray<FShadowDepthPassInfo> ShadowDepthPassInfos;

    // Light별 준비 함수가 조회 정보를 추가하고 Request.ShadowIndex를 연결한다. CSM은 Cascade별 연속 항목이다.
    TArray<FShadowInfo> ShadowInfos;

    // Directional 기본 해상도. Map별 실제 해상도는 ShadowMapRequests에 기록한다.
    uint32 Resolution = DefaultShadowResolution;

    // 마지막 준비의 컬링 결과. UI가 프레임 정리 후 읽으므로 ResetFrame에서 지우지 않고 준비 시작 시 지운다.
    FShadowCullingStats CullingStats;

    // 이번 프레임 Light 단위 컬링 전까지 남은 Light(visible, CastShadow, 반경 > 0)의 컴포넌트 UUID.
    // 여기 없는 Light의 캐시와 슬롯은 반납한다. View 등으로 컬링된 Light는 캐시를 유지한다.
    TArray<uint32> ConsideredLightIds;
    TArray<FShadowCascadeSplit> CascadeSplits;

    void ResetFrame()
    {
        ConsideredLightIds.Reset();
        ShadowMapRequests.Reset();
        ShadowAtlasSize = 0;
        ShadowInfos.Reset();
        ShadowDepthPassInfos.Reset();
        ShadowRequestsPerLight.Reset();
        CascadeSplits.Reset();
    }
};
