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
	uint32 DisplayMode = 0; // 0: 카메라 거리, 1: 저장된 깊이, 2: Spot 뷰 깊이
	float PreviewNearClip = 0.0f;
	float PreviewFarClip = 0.0f;
	float PreviewPadding[2] = {};
};
static_assert(sizeof(FDepthViewConstants) == 112, "DepthViewShader의 상수 버퍼 배치와 같아야 한다");

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
	// Details에 표시하는 선택 광원 하나의 원근 깊이 범위. (0, 0)은 기존 2D 표시.
	void SetShadowPreviewRange(float NearClip, float FarClip)
	{
		ShadowPreviewNearClip = NearClip;
		ShadowPreviewFarClip = FarClip;
	}


private:
	FPipelineState PipelineState;
	FShaderProgram* Shader = nullptr;
	TUniquePtr<FConstantBuffer> ConstantBuffer;
	float ShadowPreviewNearClip = 0.0f;
	float ShadowPreviewFarClip = 0.0f;

};
