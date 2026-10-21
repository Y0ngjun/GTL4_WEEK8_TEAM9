#pragma once

#include "Editor/EditorUI/EditorPanel.h"
#include "Core/EngineString.h"

#include <d3d11.h>
#include <functional>
#include <utility>
#include <wrl/client.h>

struct FShadowResources;

// 선택한 광원과 무관하게 현재 프레임의 Atlas 전체를 보여주는 독립 패널.
class FShadowAtlasPanel : public IEditorPanel
{
public:
	bool Init() override { return true; }
	void Tick(float DeltaTime) override {}
	void OnRender() override;
	const char* GetPanelName() const override { return "Shadow Atlas"; }

	void SetAtlasQuery(std::function<const FShadowResources*(bool, FString&)> InQuery)
	{
		AtlasQuery = std::move(InQuery);
	}

private:
	std::function<const FShadowResources*(bool, FString&)> AtlasQuery;
	Microsoft::WRL::ComPtr<ID3D11PixelShader> PreviewShader;
	bool bDirectional = false;
};
