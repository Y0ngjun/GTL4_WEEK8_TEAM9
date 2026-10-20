#pragma once

#include "Render/RenderPacket.h"
#include "Math/Frustum.h"
#include "ShadowInfo.h"
#include "ShadowCulling.h"

class FLightSceneProxy;

struct FShadowRequestPerLight
{
    // 이번 프레임에 그림자를 생성할 Light 하나
    const FLightSceneProxy* Light = nullptr;

    // Light의 shadow map이 그려질 배열의 index
    // Light 종류로 배열을 선택한다: Directional/Spot은 Texture2DArray, Point는 CubeArray.
    // 이 Index는 2D 배열에서는 slice 번호, CubeArray에서는 cube 번호다(물리 slice 번호가 아니다).
    // 배열별로 0부터 배정한다. Scene의 Light 번호나 GPU ShadowInfo 번호와는 무관하다.
    // Caster가 하나도 남지 않은 Light는 배정하지 않으므로, 배정은 Light별 준비가 끝날 때 한다.
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
    // Standard Directional/Spot: ArraySlice = ShadowMapIndex. Cascade: Cascade 번호.
    // Point: ArraySlice = ShadowMapIndex * 6 + 면 번호.
    uint32 ArraySlice = 0;

    // Pass 1 상수 버퍼로 전달할 World -> Light Clip 변환 행렬. 
    FMatrix ViewProjection{};

    // CPU BVH 수집용 World-space Frustum. 
    FFrustumPlanes Frustum{};

    // 해당 면의 Frustum을 통과한 Caster들의 불투명 Static Mesh Section 패킷 목록 (현재는 반투명은 처리하지 않는다)
    FRenderQueue CasterPackets;
};

struct FShadowCascadeSplit
{
    // RenderLocation 기준 전방 축의 선형 깊이. 현재는 겹침 없는 선택 구간과 투영 구간이 같다.
    float NearDepth = 0.0f;
    float FarDepth = 0.0f;
    // subfrustum을 감싸는 원래 구. Snapping은 투영의 가로·세로 중심에만 적용한다.
    FVector SphereCenter;
    float SphereRadius = 0.0f;
    // ViewProjection은 구 범위, Frustum은 caster 수집용 범위다.
    FShadowDepthPassInfo DepthPass;
};


// 이번 프레임 CPU 준비 자료. GPU 그림자 배열 자체를 소유하지 않는다.
// Point/Spot은 World별로 공유하고 Directional은 View 렌더 호출 안에서 소비한다.
// GPU 배열은 각각 Renderer의 World 슬롯과 Viewport가 소유한다.
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

    // 마지막 준비의 컬링 결과. UI가 프레임 정리 후 읽으므로 ResetFrame에서 지우지 않고 준비 시작 시 지운다.
    FShadowCullingStats CullingStats;

    // GPU 배열이 가져야 할 최소 크기. 캐시된 Light는 이번 프레임 패스가 없어도 자기 슬롯이 배열 안에 있어야 한다.
    uint32 Required2DSlices = 0;
    uint32 RequiredCubes = 0;

    // 이번 프레임 Light 단위 컬링 전까지 남은 Light(visible, CastShadow, 반경 > 0)의 컴포넌트 UUID.
    // 여기 없는 Light의 캐시와 슬롯은 반납한다. View 등으로 컬링된 Light는 캐시를 유지한다.
    TArray<uint32> ConsideredLightIds;
    TArray<FShadowCascadeSplit> CascadeSplits;

    void ResetFrame()
    {
        ConsideredLightIds.Reset();
        Required2DSlices = 0;
        RequiredCubes = 0;
        ShadowInfos.Reset();
        ShadowDepthPassInfos.Reset();
        ShadowRequestsPerLight.Reset();
        CascadeSplits.Reset();
    }
};
