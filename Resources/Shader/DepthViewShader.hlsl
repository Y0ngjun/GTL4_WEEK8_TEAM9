#pragma pack_matrix(row_major) 
Texture2D<float> SceneDepth : register(t0);

cbuffer FDepthViewConstants : register(b0)
{
    float4x4 InverseViewProjection;
    float3 CameraPosition;
    float VisualizeRange;
    float3 CameraForward;
    uint DisplayMode;
};


struct PSInput
{
    float4 Position : SV_POSITION;
    // ImGui VS의 출력 인터페이스와 맞추고 이미지 색상·투명도를 전달한다.
    float4 Color : COLOR0;
    float2 UV : TEXCOORD0;
};

PSInput mainVS(uint VertexID : SV_VertexID)
{
    PSInput Output;
    float2 UV = float2((VertexID << 1) & 2, VertexID & 2); // (0,0) (2,0) (0,2)
    Output.Position = float4(UV * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f), 0.0f, 1.0f);
    // 전체 화면 카메라 깊이 표시에서는 색상을 그대로 유지한다.
    Output.Color = float4(1.0f, 1.0f, 1.0f, 1.0f);
    Output.UV = UV;
    return Output;
}

float4 mainPS(PSInput Input) : SV_Target
{
    // 출력 화면 크기 대신 실제 깊이 텍스처의 크기를 사용한다.
    uint Width, Height;
    SceneDepth.GetDimensions(Width, Height);
    if (Width == 0 || Height == 0)
        return float4(0.0f, 0.0f, 0.0f, 0.0f);

    // UV를 texel 좌표로 변환한다.
    // UV가 정확히 1인 경우에도 마지막 texel을 넘지 않도록 제한한다.
    uint2 Texel = min( uint2(saturate(Input.UV) * float2(Width, Height)), uint2(Width - 1, Height - 1));

    float Depth = SceneDepth.Load(int3(Texel, 0));

    // 모드 1: 저장된 깊이를 그대로 회색조로 표시한다.
    // clear 깊이 1도 흰색으로 표시된다.
    if (DisplayMode == 1)
    {
        return float4(Depth.xxx, 1.0f) * Input.Color;
    }

    // 모드 0 및 그 외 값: 기존 카메라 거리 시각화를 사용한다.
    if (Depth == 1.0f)
    {
        return float4(0.0f, 0.0f, 0.0f, 0.0f);
    }

    // UV와 저장된 깊이로 월드 위치를 복원한다.
    float2 NDC = Input.UV * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f);

    float4 WorldPos = mul( float4(NDC, Depth, 1.0f), InverseViewProjection);

    WorldPos /= WorldPos.w;

    // 카메라 진행 방향을 기준으로 깊이를 계산한다.
    float ViewDepth = dot(WorldPos.xyz - CameraPosition, CameraForward);

    float Shade = saturate(ViewDepth / VisualizeRange);
    return float4(Shade.xxx, 1.0f);
}
