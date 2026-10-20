#pragma pack_matrix(row_major)
// FShadowDepthPassConstants의 80-byte 배치를 공유한다.
cbuffer ShadowDepthPassConstants : register(b0)
{
    matrix ViewProjection;
    float3 LightPosition;
    float InvRadius;
};
cbuffer PerObjectConstants : register(b2)
{
    matrix World;
    matrix NormalMatrix;
};
float4 mainVS(float3 Position : POSITION) : SV_POSITION
{
    float4 WorldPosition = mul(float4(Position, 1.0f), World);
    float4 ClipPosition = mul(WorldPosition, ViewProjection);
    // D3D [0,1] 깊이. far와 가로·세로 clipping은 그대로 유지한다.
    if (ClipPosition.z < 0.0f)
        ClipPosition.z = 1.0e-6f * ClipPosition.w;
    return ClipPosition;
}
void mainPS()
{
}
