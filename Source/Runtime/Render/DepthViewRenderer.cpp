#include "EnginePCH.h"

#include "DepthViewRenderer.h"
#include "RenderCommand.h"
#include "RenderResourceManager.h"
#include "Render/Buffer.h"


bool FDepthViewRenderer::Init()
{
	// 로드 실패 시 다른 셰이더로 대체하면 깊이 표시 인터페이스가 맞지 않는다.
	Shader = FRenderResourceManager::FindShaderProgram("Resources/Shader/DepthViewShader.hlsl");
	if (!Shader || Shader->ComputeShader ||
		!Shader->VertexShader || !Shader->VertexShader->IsValid() ||
		!Shader->PixelShader || !Shader->PixelShader->IsValid())
	{
		HTR_LOG(Error, "[DepthView] depth visualization shader unavailable");
		return false;
	}


	ConstantBuffer = RenderCommand::CreateConstantBuffer(sizeof(FDepthViewConstants));
	if (!ConstantBuffer || !ConstantBuffer->GetBuffer())
	{
		HTR_LOG(Error, "[DepthView] constant buffer creation failed");
		return false;
	}

	PipelineState.Shader = Shader;
	PipelineState.Topology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	return true;
}

bool FDepthViewRenderer::IsReady() const
{
	return Shader && !Shader->ComputeShader &&
		Shader->VertexShader && Shader->VertexShader->IsValid() &&
		Shader->PixelShader && Shader->PixelShader->IsValid() &&
		ConstantBuffer && ConstantBuffer->GetBuffer();
}

void FDepthViewRenderer::OnRender(FTexture2D* depthTexture, const FMatrix& ViewProjection, const FVector& CameraPosition, const FVector& ViewCameraForward)
{
	if (!IsReady() || !depthTexture || !depthTexture->GetSRV())
		return;

	FDepthViewConstants Constants{};
	Constants.InverseViewProjection = ViewProjection.Inverse();
	Constants.CameraPosition = CameraPosition;
	Constants.VisualizeRange = 50;
	Constants.CameraForward = ViewCameraForward;
	Constants.DisplayMode = 0;

	RenderCommand::BindPipelineState(PipelineState);
	RenderCommand::BindShaderResource(0, depthTexture, EShaderBindFlagBits::Pixel);

	RenderCommand::UpdateBufferData(ConstantBuffer.get(), &Constants);
	RenderCommand::BindConstantBuffer(0, ConstantBuffer.get(), EShaderBindFlagBits::Pixel);
	RenderCommand::Draw(3);
}

void FDepthViewRenderer::BindShadowPreview()
{
	if (!IsReady())
		return;

	// FDepthViewConstants를 초기화하고, DisplayMode = 1 설정
	FDepthViewConstants Constants{};
	Constants.DisplayMode = 1;
	// ConstantBuffer를 업로드하고 PS의 b0에 바인딩
	RenderCommand::UpdateBufferData(ConstantBuffer.get(), &Constants);
	RenderCommand::BindConstantBuffer(0, ConstantBuffer.get(), EShaderBindFlagBits::Pixel);
	// ImGui의 VS와 정점 배치는 유지하고 PS만 깊이 시각화용으로 교체한다.
	RenderCommand::GetContext()->PSSetShader(Shader->PixelShader->GetShader(), nullptr, 0);
}
