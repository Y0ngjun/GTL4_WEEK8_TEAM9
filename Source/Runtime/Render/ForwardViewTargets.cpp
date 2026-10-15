#include "EnginePCH.h"
#include "Render/ForwardViewTargets.h"
#include "Render/RenderCommand.h"

void FForwardViewTargets::Resize(const uint32 InWidth, const uint32 InHeight)
{
	if (Width == InWidth && Height == InHeight) return;
	D3D11_TEXTURE2D_DESC Desc{};
	Desc.Width = InWidth;
	Desc.Height = InHeight;
	Desc.MipLevels = 1;
	Desc.ArraySize = 1;
	Desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	Desc.SampleDesc.Count = 1;
	Desc.Usage = D3D11_USAGE_DEFAULT;
	Desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
	ColorTarget = RenderCommand::CreateTexture2D(Desc);
	// CopyResource로 되돌려 쓰므로 ColorTarget과 같은 Desc로 만든다
	FxaaTarget = RenderCommand::CreateTexture2D(Desc);

	// Linear 값을 8비트에 담으면 어두운 영역이 뭉개지고 1을 넘는 값이 잘리므로 씬 컬러는 float로 만든다
	Desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
	SceneColorTarget = RenderCommand::CreateTexture2D(Desc);

	// Fog 패스가 깊이를 셰이더에서 읽으므로 DSV와 SRV를 둘 다 만들 수 있는 TYPELESS로 만든다
	Desc.Format = DXGI_FORMAT_R24G8_TYPELESS;
	Desc.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;
	DepthTarget = RenderCommand::CreateTexture2D(Desc);

	Width = InWidth;
	Height = InHeight;
	BuildViewRenderingInfos(SceneRenderingInfo, RenderingInfo, SceneColorTarget.get(), ColorTarget.get(), DepthTarget.get(), Width, Height);
}

void FForwardViewTargets::BuildViewRenderingInfos(FRenderingInfo& OutSceneInfo, FRenderingInfo& OutViewInfo, FTexture2D* SceneColor, FTexture2D* ViewColor, FTexture2D* Depth, const uint32 Width, const uint32 Height)
{
	OutSceneInfo.ColorRenderTargets.Reset();
	OutSceneInfo.ViewportSetting.Width = Width;
	OutSceneInfo.ViewportSetting.Height = Height;
	FRenderingDesc SceneColorDesc{};
	SceneColorDesc.Texture = SceneColor;
	// 지우기 색은 작업 흐름에 따라 달라지므로 RenderOpaquePass에서 정한다.
	OutSceneInfo.ColorRenderTargets.Add(SceneColorDesc);
	OutSceneInfo.DepthStencil.Texture = Depth;

	OutViewInfo.ColorRenderTargets.Reset();
	OutViewInfo.ViewportSetting.Width = Width;
	OutViewInfo.ViewportSetting.Height = Height;
	FRenderingDesc ViewColorDesc{};
	ViewColorDesc.Texture = ViewColor;
	OutViewInfo.ColorRenderTargets.Add(ViewColorDesc);
	OutViewInfo.DepthStencil.Texture = Depth;
}

void FForwardViewTargets::Reset()
{
	SceneRenderingInfo = {};
	RenderingInfo = {};
	SceneColorTarget.reset();
	ColorTarget.reset();
	DepthTarget.reset();
	FxaaTarget.reset();
	Width = Height = 0;
}
