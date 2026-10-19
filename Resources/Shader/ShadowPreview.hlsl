Texture2D<float> DepthTexture : register(t0);

// ImGui의 기존 Vertex Shader 출력과 연결한다.
float4 mainPS(float4 Position : SV_POSITION, float4 Color : COLOR0, float2 UV : TEXCOORD0) : SV_Target
{
    uint Width, Height;
    DepthTexture.GetDimensions(Width, Height);
    int2 Pixel = min(int2(UV * float2(Width, Height)), int2(Width - 1, Height - 1));
    float Depth = DepthTexture.Load(int3(Pixel, 0));
    return float4(Depth, Depth, Depth, 1.0f) * Color;
}
