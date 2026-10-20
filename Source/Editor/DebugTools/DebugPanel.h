#pragma once

#include <functional>
#include "Editor/EditorUI/EditorPanel.h"
#include "Render/ShadowCulling.h"

// 렌더링 내부 상태를 조작하고 결과를 확인하는 개발용 패널. 지금은 쉐도우 맵 캐싱·컬링을 다룬다.
// 설정 값은 Settings 패널의 FEditorSettings가 소유하고 editor.ini에 함께 저장된다.
class FDebugPanel : public IEditorPanel
{
public:
	bool Init() override { return true; }
	void Tick(float DeltaTime) override {}
	void OnRender() override;
	const char* GetPanelName() const override { return "Debug"; }

	// 편집 대상 설정. Settings 패널이 소유한 값을 가리킨다.
	void SetShadowCullingSettings(FShadowCullingSettings* InSettings) { ShadowSettings = InSettings; }
	// bPlayWorld가 true면 PIE World, 아니면 Editor World의 마지막 컬링 결과. 준비한 적 없으면 nullptr.
	void SetShadowStatsQuery(std::function<const FShadowCullingStats*(bool bPlayWorld)> InQuery) { ShadowStatsQuery = std::move(InQuery); }
	void SetHasPlayWorldQuery(std::function<bool()> InQuery) { HasPlayWorldQuery = std::move(InQuery); }

private:
	void DrawShadowWorldSelector(bool bHasPlayWorld);
	void DrawShadowCacheSection(const FShadowCullingStats* Stats);
	void DrawLightCullingSection(const FShadowCullingStats* Stats);
	void DrawCasterCullingSection(const FShadowCullingStats* Stats);

	FShadowCullingSettings* ShadowSettings = nullptr;
	std::function<const FShadowCullingStats*(bool)> ShadowStatsQuery;
	std::function<bool()> HasPlayWorldQuery;
	// 통계를 볼 World. PIE가 없으면 Editor로 돌아간다.
	bool bShowPlayWorldStats = false;
};
