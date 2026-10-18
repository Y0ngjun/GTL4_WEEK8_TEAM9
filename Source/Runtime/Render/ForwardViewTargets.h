#pragma once

#include "Render/RenderingInfo.h"
#include "Render/TileLightCullingResources.h"

// 기존 Forward 경로의 linear 씬 컬러와 수동 인코딩 표시 컬러를 한 뷰가 소유한다.
struct FForwardViewTargets
{
	TUniquePtr<FTexture2D> SceneColorTarget;
	TUniquePtr<FTexture2D> ColorTarget;
	TUniquePtr<FTexture2D> DepthTarget;
	TUniquePtr<FTexture2D> FxaaTarget;
	FRenderingInfo SceneRenderingInfo{};
	FRenderingInfo RenderingInfo{};
	FTileLightCullingResources LightCulling;
	// 이 View의 절두체에 맞춘 Directional Light 깊이. 화면 크기와 무관해 Resize에서 다시 만들지 않는다.
	TUniquePtr<FTexture2D> DirectionalShadowMap;
	uint32 DirectionalShadowMapSize = 0;
	uint32 Width = 0;
	uint32 Height = 0;

	void Resize(uint32 InWidth, uint32 InHeight);
	FTexture2D* EnsureDirectionalShadowMap(uint32 Size);
	void Reset();
	static void BuildViewRenderingInfos(FRenderingInfo& OutSceneInfo, FRenderingInfo& OutViewInfo, FTexture2D* SceneColor, FTexture2D* ViewColor, FTexture2D* Depth, uint32 Width, uint32 Height);
};
