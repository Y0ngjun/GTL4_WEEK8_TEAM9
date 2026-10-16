#define THREADS_PER_TILE 256                // threadgroup의 크기는 이제 tile 크기와 관련이 없다
#define PLANE_COUNT      6

struct FCullingLight
{
    float3 SphereCenter;
    float Radius;
};

// point light, spotlight의 정보 (spotlight은 bounding sphere로 근사하여 넘어온다)
StructuredBuffer<FCullingLight> LightSpheres : register(t0);
Texture2D<float> SceneDepth : register(t1);

cbuffer TileCullingConstants : register(b0)
{
    uint2 ViewSize; // 렌더링할 화면의 가로·세로 크기 (픽셀)
    uint TileCountX; // 가로 타일 개수: ceil(ViewSize.x / TileSize)
    uint LightCount; // LightSpheres에서 검사할 라이트 개수

    row_major float4x4 Projection;

    uint MaxLightsPerTile; // 타일 하나에 배정한 라이트 인덱스 최대 저장 용량
    uint TileSize;
    uint2 Padding;
};

// 실제 통과 개수. 용량을 초과한 개수도 그대로 기록한다.
RWBuffer<uint> TileLightCount : register(u0);

// 타일마다 MaxLightsPerTile개의 라이트 인덱스를 저장한다.
RWBuffer<uint> TileLightIndices : register(u1);

// 스레드 그룹이 공유하는 데이터
groupshared uint SharedLightCount; // 이번 타일에 들어갈 라이트 수
groupshared uint SharedMinDepth;
groupshared uint SharedMaxDepth;
groupshared float4 SharedPlanes[PLANE_COUNT]; // 이번 타일에 대한 tile frustum planes

// 평면의 안쪽이 양수이며, xyz는 법선, w는 상수항이다.
// 정규화 후 w는 원점의 부호 있는 거리가 된다.
// 단순히 안쪽만 확인할 경우에는 정규화를 하지 않아도 되지만, radius 비교를 하기 위해서 필요하다.
float4 NormalizePlane(float4 plane)
{
    return plane / length(plane.xyz);
}

// tile frustum을 이루는 평면 하나에 대하여 구 전체가 평면 바깥인지를 나타낸다.
bool SphereOutsidePlane(float3 center, float radius, float4 plane)
{
    float signedDistance = dot(plane.xyz, center) + plane.w;
    return signedDistance < -radius;
}


[numthreads(THREADS_PER_TILE, 1, 1)]
void mainCS(uint3 groupId : SV_GroupID, uint localIndex : SV_GroupIndex)  // GroupID = dispatch 레벨에서의 block index, GroupIndex = thread group 레벨에서의 thread index
{
    uint tileIndex = groupId.y * TileCountX + groupId.x;

    // 1. opaque 깊이 범위를 구한다. 정방향 깊이에서 1은 배경이다.
    if (localIndex == 0)
    {
        SharedLightCount = 0;
        
        // float는 atomic operation이 없어서, integer로 계산한다.
        // [0, 1]의 양수 float는 uint 비트 순서도 같은 순서다.
        SharedMinDepth = asuint(1.0f);
        SharedMaxDepth = asuint(0.0f);
    }

    GroupMemoryBarrierWithGroupSync();

    for (uint pixelIndex = localIndex; pixelIndex < TileSize * TileSize; pixelIndex += THREADS_PER_TILE)
    {
        uint2 pixel = groupId.xy * TileSize + uint2(pixelIndex % TileSize, pixelIndex / TileSize);
        if (all(pixel < ViewSize))
        {
            float depth = SceneDepth.Load(int3(pixel, 0));
            if (depth < 1.0f)
            {
                InterlockedMin(SharedMinDepth, asuint(depth));
                InterlockedMax(SharedMaxDepth, asuint(depth));
            }
        }
    }

    GroupMemoryBarrierWithGroupSync();

    // 그룹 전체가 같은 조건으로 빠져나간다. 이전 프레임의 개수도 지운다.
    if (SharedMinDepth == asuint(1.0f))
    {
        if (localIndex == 0)
            TileLightCount[tileIndex] = 0;
        return;
    }

    if (localIndex == 0)
    {

        float2 pixelMin = float2(groupId.xy * TileSize);
        float2 pixelMax = min(pixelMin + float2(TileSize, TileSize), float2(ViewSize));

        // Gribb-Hartmann 방식으로 plane 계산
        float xMin = 2.0f * pixelMin.x / ViewSize.x - 1.0f;
        float xMax = 2.0f * pixelMax.x / ViewSize.x - 1.0f;
        float yTop = 1.0f - 2.0f * pixelMin.y / ViewSize.y;
        float yBottom = 1.0f - 2.0f * pixelMax.y / ViewSize.y;

        // 행 벡터 규약: 전치한 행렬의 행으로 원본의 열을 읽는다.
        float4x4 projectionT = transpose(Projection);
        float4 c1 = projectionT[0];
        float4 c2 = projectionT[1];
        float4 c3 = projectionT[2];
        float4 c4 = projectionT[3];

        SharedPlanes[0] = NormalizePlane(c1 - xMin * c4);
        SharedPlanes[1] = NormalizePlane(xMax * c4 - c1);
        SharedPlanes[2] = NormalizePlane(yTop * c4 - c2);
        SharedPlanes[3] = NormalizePlane(c2 - yBottom * c4);

        // depth pre-pass를 통해 얻은 depth buffer를 가지고 min-z plane과 max-z plane을 만든다.
        // D24 깊이 양자화로 표면 경계의 라이트가 빠지지 않도록 두 단계만큼 넓힌다.
        const float depthMargin = 2.0f / 16777215.0f;
        float minDepth = max(0.0f, asfloat(SharedMinDepth) - depthMargin);
        float maxDepth = min(1.0f, asfloat(SharedMaxDepth) + depthMargin);
        SharedPlanes[4] = NormalizePlane(c3 - minDepth * c4);
        SharedPlanes[5] = NormalizePlane(maxDepth * c4 - c3);
    }

    GroupMemoryBarrierWithGroupSync();

    // 2. 그룹 안에서 라이트를 나눠 검사하고 인덱스 기록
    for (uint lightIndex = localIndex; lightIndex < LightCount; lightIndex += THREADS_PER_TILE)
    {
        FCullingLight light = LightSpheres[lightIndex];
        bool outside = false;

        // view frustum 처리하는것과 동일하게 평면에 완전히 벗어나는 경우가 있는지 판단하여, 있으면 culling 시킨다
        for (uint planeIndex = 0; planeIndex < PLANE_COUNT; ++planeIndex)
        {
            if (SphereOutsidePlane(light.SphereCenter, light.Radius, SharedPlanes[planeIndex]))
            {
                outside = true;
                break;
            }
        }

        if (!outside)
        {
            uint slot;
            // atomic operation
            InterlockedAdd(SharedLightCount, 1, slot);  // slot에는 increment하기 이전값이 저장된다.
            
            // 아직 tile의 light list가 다 채워지지 않았다면 light index를 추가한다
            if (slot < MaxLightsPerTile)
            {
                uint outputIndex = tileIndex * MaxLightsPerTile + slot;

                TileLightIndices[outputIndex] = lightIndex;
            }
        }
    }

    GroupMemoryBarrierWithGroupSync();

    // 3. 모든 검사가 끝난 뒤 실제 통과 개수 기록
    if (localIndex == 0)
    {
        TileLightCount[tileIndex] = SharedLightCount;
    }
}
