#pragma pack_matrix(row_major)
cbuffer ViewConstants : register(b0)
{
    matrix ViewProjection;
    float3 CameraPosition;
    float ViewPadding;
};
cbuffer PerObjectConstants : register(b2)
{
    matrix World;
    matrix NormalMatrix;
};
float4 mainVS(float3 Position : POSITION) : SV_POSITION
{
    float4 WorldPosition = mul(float4(Position, 1.0f), World);
    return mul(WorldPosition, ViewProjection);
}
// 불투명 메시에는 alpha clip이 없다. 기존 공용 graphics 로더로 읽는 빈 PS.
void mainPS()
{
}
