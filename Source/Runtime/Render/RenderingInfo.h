#pragma once

#include "Core/Types.h"
#include "Math/Vector4.h"
#include "Texture2D.h"
#include "Containers/Array.h"

enum class ERenderTargetLoadOp
{
	Load,
	Clear,
	DontCare,
};

enum class ERenderTargetStoreOp
{
	Store,
	DontCare,
};

struct FViewportSettings
{
	int32 StartX = 0;
	int32 StartY = 0;
	uint32 Width = 0;
	uint32 Height = 0;
	float MinDepth = 0.0f;
	float MaxDepth = 1.0f;
};

struct FClearValue
{
	FVector4 ColorClearValue = { 0.3f, 0.3f, 0.3f, 1.0f };
	float DepthClearValue = 1.0f;
	uint8 StencilClearValue = 0;
};

struct FRenderingDesc
{
	FTexture2D* Texture = nullptr;
	ERenderTargetLoadOp  LoadOp = ERenderTargetLoadOp::Clear;
	ERenderTargetStoreOp StoreOp = ERenderTargetStoreOp::Store;
	FClearValue ClearValue = FClearValue();

	// Texture를 지정하지 않아도 Depth Pass를 수행할 수 있도록 DSV를 직접 받는다.
	// 비소유 참조이며 Texture의 DSV보다 우선한다.
	ID3D11DepthStencilView* DSV = nullptr;
	uint32 ClearFlags = D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL;
};

struct FRenderingInfo
{
	FViewportSettings ViewportSetting;
	TArray<FRenderingDesc> ColorRenderTargets;
	FRenderingDesc DepthStencil;
};
