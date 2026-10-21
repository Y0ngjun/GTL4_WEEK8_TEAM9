#include "EnginePCH.h"
#include "ShadowCascadeDebugRenderer.h"
#include "RenderCommand.h"
#include "RenderResourceManager.h"
#include "SceneView.h"
#include "ShadowFrameData.h"

namespace
{
    struct FCascadeDebugConstants
    {
        FMatrix InverseViewProjection;
        FVector ViewOrigin;
        uint32 CascadeCount;
        FVector ViewForward;
        float Padding = 0;
        FVector4 DepthRanges[MaxShadowCascades] = {};
    };
    static_assert(sizeof(FCascadeDebugConstants) == 224);
}

bool FShadowCascadeDebugRenderer::Init()
{
    PipelineState.Shader = FRenderResourceManager::GetShaderProgram("Resources/Shader/ShadowCascadeDebugShader.hlsl");
    PipelineState.BlendState = EBlendState::AlphaBlend;
    PipelineState.DepthStencilState = EDepthStencilState::Disabled;
    ConstantBuffer = RenderCommand::CreateConstantBuffer(sizeof(FCascadeDebugConstants));
    return PipelineState.Shader && ConstantBuffer && ConstantBuffer->GetBuffer();
}

void FShadowCascadeDebugRenderer::OnRender(FTexture2D* SceneDepth, const FSceneView& View, const FShadowFrameData& Frame)
{
    // Cascade 색상 시각화는 Perspective View에만 적용한다.
    if (!IsEnabled() || View.IsOrthographic() || !SceneDepth || Frame.CascadeSplits.IsEmpty()) return;
    FCascadeDebugConstants Constants{};
    Constants.InverseViewProjection = View.ViewProjection.Inverse();
    Constants.ViewOrigin = View.RenderLocation;
    Constants.ViewForward = View.CameraForward;
    Constants.CascadeCount = static_cast<uint32>(Frame.CascadeSplits.Num());
    for (uint32 Index = 0; Index < Constants.CascadeCount; ++Index)
    {
        const auto& Split = Frame.CascadeSplits[Index];
        const float Width = Index + 1 < Constants.CascadeCount ? Split.FarDepth - Frame.CascadeSplits[Index + 1].ProjectionNearDepth : 0.0f;
        Constants.DepthRanges[Index] = FVector4(Split.NearDepth, Split.FarDepth, Width, 0);
    }
    RenderCommand::BindPipelineState(PipelineState);
    RenderCommand::BindShaderResource(0, SceneDepth, EShaderBindFlagBits::Pixel);
    RenderCommand::UpdateBufferData(ConstantBuffer.get(), &Constants);
    RenderCommand::BindConstantBuffer(0, ConstantBuffer.get(), EShaderBindFlagBits::Pixel);
    RenderCommand::Draw(3);
    RenderCommand::UnbindShaderResource(0, EShaderBindFlagBits::Pixel);
}
