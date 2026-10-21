#include "EnginePCH.h"
#include "Editor/DebugTools/ShadowAtlasPanel.h"

#include "Render/RenderCommand.h"
#include "Render/ShadowResources.h"
#include "Render/Texture2D.h"

#include <algorithm>
#include <d3dcompiler.h>

void FShadowAtlasPanel::OnRender()
{
	ImGui::SetNextWindowSize(ImVec2(640.0f, 730.0f), ImGuiCond_FirstUseEver);
	bool bOpen = IsOpen();
	if (!ImGui::Begin(GetPanelName(), &bOpen))
	{
		SetOpen(bOpen);
		ImGui::End();
		return;
	}
	SetOpen(bOpen);

	int AtlasIndex = bDirectional ? 1 : 0;
	if (ImGui::Combo("Atlas", &AtlasIndex, "Spot / Point (World)\0CSM (Active Viewport)\0"))
		bDirectional = AtlasIndex == 1;

	FString SourceLabel;
	const FShadowResources* Resources = AtlasQuery ? AtlasQuery(bDirectional, SourceLabel) : nullptr;
	if (!SourceLabel.empty()) ImGui::TextWrapped("%s", SourceLabel.c_str());
	if (!Resources || !Resources->DepthAtlas || !Resources->DepthAtlas->GetSRV())
	{
		ImGui::TextDisabled("No shadow atlas rendered for this frame.");
		ImGui::End();
		return;
	}

	if (!PreviewShader)
	{
		ComPtr<ID3DBlob> Code, Errors;
		ID3D11Device* Device = RenderCommand::GetDevice();
		if (FAILED(D3DCompileFromFile(L"Resources/Shader/ShadowPreview.hlsl", nullptr, nullptr,
			"mainPS", "ps_5_0", 0, 0, Code.GetAddressOf(), Errors.GetAddressOf())) ||
			FAILED(Device->CreatePixelShader(Code->GetBufferPointer(), Code->GetBufferSize(), nullptr,
				PreviewShader.GetAddressOf())))
		{
			ImGui::TextDisabled("Shadow preview shader could not be created.");
			ImGui::End();
			return;
		}
	}

	const FTexture2D* Atlas = Resources->DepthAtlas.get();
	uint32 TileCount = 0;
	uint64 OccupiedPixels = 0;
	for (const FShadowPreviewEntry& Entry : Resources->PreviewEntries)
	{
		TileCount += static_cast<uint32>(Entry.MapRects.Num());
		for (const FShadowAtlasRect& Rect : Entry.MapRects)
			OccupiedPixels += static_cast<uint64>(Rect.Size) * Rect.Size;
	}
	const uint64 AtlasPixels = static_cast<uint64>(Atlas->GetWidth()) * Atlas->GetHeight();
	// R32_TYPELESS/D32_FLOAT 깊이 Atlas는 픽셀당 4바이트, mip 1개다.
	constexpr double BytesPerMiB = 1024.0 * 1024.0;
	const double AtlasMiB = static_cast<double>(AtlasPixels) * 4.0 / BytesPerMiB;
	const double OccupiedMiB = static_cast<double>(OccupiedPixels) * 4.0 / BytesPerMiB;
	const float Occupancy = AtlasPixels ? static_cast<float>(OccupiedPixels) / static_cast<float>(AtlasPixels) : 0.0f;
	ImGui::Text("Atlas: %u x %u  |  Lights: %d  |  Tiles: %u",
		Atlas->GetWidth(), Atlas->GetHeight(), Resources->PreviewEntries.Num(), TileCount);
	ImGui::Text("Atlas GPU texture: %.1f MiB (fixed)", AtlasMiB);
	ImGui::Text("Active coverage: %.1f%% (%.2f MiB equivalent)", Occupancy * 100.0f, OccupiedMiB);
	ImGui::ProgressBar(std::clamp(Occupancy, 0.0f, 1.0f), ImVec2(-1.0f, 0.0f));
	ImGui::TextDisabled("Blue: Point  |  Orange: Spot  |  Yellow: CSM");
	ImGui::TextDisabled("Depth: black = 0, white = 1");

	const ImVec2 Available = ImGui::GetContentRegionAvail();
	const float Width = std::max(1.0f, std::min(Available.x, Available.y));
	const ImVec2 ImageSize(Width, Width * Atlas->GetHeight() / Atlas->GetWidth());
	ImDrawList* DrawList = ImGui::GetWindowDrawList();
	DrawList->AddCallback([](const ImDrawList*, const ImDrawCmd* Command)
	{
		RenderCommand::GetContext()->PSSetShader(
			static_cast<ID3D11PixelShader*>(Command->UserCallbackData), nullptr, 0);
	}, PreviewShader.Get());
	ImGui::Image((ImTextureID)Atlas->GetSRV(), ImageSize);
	const ImVec2 ImageMin = ImGui::GetItemRectMin();
	const ImVec2 ImageMax = ImGui::GetItemRectMax();
	const bool bHovered = ImGui::IsItemHovered();
	DrawList->AddCallback(ImDrawCallback_ResetRenderState, nullptr);

	const float ScaleX = ImageSize.x / Atlas->GetWidth();
	const float ScaleY = ImageSize.y / Atlas->GetHeight();
	const ImVec2 Mouse = ImGui::GetMousePos();
	const float PixelX = (Mouse.x - ImageMin.x) / ScaleX;
	const float PixelY = (Mouse.y - ImageMin.y) / ScaleY;
	DrawList->PushClipRect(ImageMin, ImageMax, true);
	DrawList->AddRect(ImageMin, ImageMax, IM_COL32(140, 150, 165, 255));
	for (const FShadowPreviewEntry& Entry : Resources->PreviewEntries)
	{
		const ImU32 Color = bDirectional ? IM_COL32(255, 220, 0, 255)
			: Entry.bPoint ? IM_COL32(0, 220, 255, 255) : IM_COL32(255, 150, 40, 255);
		for (int32 MapIndex = 0; MapIndex < Entry.MapRects.Num(); ++MapIndex)
		{
			const FShadowAtlasRect& Rect = Entry.MapRects[MapIndex];
			const ImVec2 TileMin(ImageMin.x + Rect.X * ScaleX, ImageMin.y + Rect.Y * ScaleY);
			const ImVec2 TileMax(ImageMin.x + (Rect.X + Rect.Size) * ScaleX,
				ImageMin.y + (Rect.Y + Rect.Size) * ScaleY);
			DrawList->AddRect(TileMin, TileMax, Color, 0.0f, 2.0f);
			if (bHovered && PixelX >= Rect.X && PixelX < Rect.X + Rect.Size &&
				PixelY >= Rect.Y && PixelY < Rect.Y + Rect.Size)
			{
				ImGui::BeginTooltip();
				ImGui::Text("%s  |  Light %u  |  Map %d", bDirectional ? "CSM" : Entry.bPoint ? "Point" : "Spot",
					Entry.LightId, MapIndex + 1);
				ImGui::Text("Tile: %u x %u  |  (%u, %u)", Rect.Size, Rect.Size, Rect.X, Rect.Y);
				ImGui::EndTooltip();
			}
		}
	}
	DrawList->PopClipRect();
	ImGui::End();
}
