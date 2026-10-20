// Viewport로 지정한 Atlas 영역만 깊이 1로 초기화한다.
float4 mainVS(uint VertexId : SV_VertexID) : SV_POSITION
{
    float2 UV = float2((VertexId << 1u) & 2u, VertexId & 2u);
    return float4(UV * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f), 1.0f, 1.0f);
}

void mainPS()
{
}
