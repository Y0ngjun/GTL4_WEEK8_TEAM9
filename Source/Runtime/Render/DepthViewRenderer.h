#pragma once

#include "Buffer.h"
#include "PipelineState.h"
#include "Texture2D.h"

struct FDepthViewConstants
{
	FMatrix InverseViewProjection;
	FVector CameraPosition;
	float VisualizeRange;
	FVector CameraForward;
	uint32 DisplayMode = 0; // 0: 카메라 거리, 1: 저장된 깊이
};
static_assert(sizeof(FDepthViewConstants) == 96, "DepthViewShader의 상수 버퍼 배치와 같아야 한다");

class FDepthViewRenderer
{
public:
	FDepthViewRenderer() = default;
	~FDepthViewRenderer() = default;

	FDepthViewRenderer(const FDepthViewRenderer&) = delete;
	FDepthViewRenderer operator=(const FDepthViewRenderer&) = delete;

	bool Init();
	bool IsReady() const;
	void OnRender(FTexture2D* depthTexture, const FMatrix& ViewProjection, const FVector& CameraPosition, const FVector& ViewCameraForward);
	// ImGui 이미지 draw 콜백에서 호출한다. VS와 draw 자체는 ImGui가 담당한다.
	void BindShadowPreview();


private:
	FPipelineState PipelineState;
	FShaderProgram* Shader = nullptr;
	TUniquePtr<FConstantBuffer> ConstantBuffer;

};
