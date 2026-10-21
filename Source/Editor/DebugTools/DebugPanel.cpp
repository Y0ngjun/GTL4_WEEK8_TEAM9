#include "EnginePCH.h"
#include "Editor/DebugTools/DebugPanel.h"
#include <cfloat>
#include <cstdio>

namespace
{
constexpr float LabelColumnWidth = 150.0f;
constexpr float CountColumnWidth = 56.0f;
const ImVec4 ActiveColor{0.45f, 0.82f, 0.50f, 1.0f};
const ImVec4 InactiveColor{0.95f, 0.70f, 0.30f, 1.0f};

// 바로 앞 위젯 옆에 (?)를 붙이고, 마우스를 올리면 설명을 보여 준다.
void HelpMarker(const char* Text)
{
	ImGui::SameLine();
	ImGui::TextDisabled("(?)");
	if (ImGui::BeginItemTooltip())
	{
		ImGui::PushTextWrapPos(ImGui::GetFontSize() * 28.0f);
		ImGui::TextUnformatted(Text);
		ImGui::PopTextWrapPos();
		ImGui::EndTooltip();
	}
}

// 같은 줄 오른쪽 끝에 색 글자를 붙인다. 섹션의 현재 상태 표시용이다.
void RightAlignedBadge(const char* Text, const ImVec4& Color)
{
	ImGui::SameLine();
	const float Offset = ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize(Text).x;
	if (Offset > 0.0f) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + Offset);
	ImGui::TextColored(Color, "%s", Text);
}

// 라벨 칸 폭을 고정해 여러 줄의 위젯 왼쪽 끝을 한 줄로 맞춘다.
bool BeginControlTable(const char* Id)
{
	if (!ImGui::BeginTable(Id, 2, ImGuiTableFlags_SizingFixedFit)) return false;
	ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthFixed, LabelColumnWidth);
	ImGui::TableSetupColumn("Widget", ImGuiTableColumnFlags_WidthStretch);
	return true;
}

// 설정 표의 한 줄을 시작한다. 다음 위젯은 남은 폭을 모두 쓴다.
void ControlRow(const char* Label, const char* Help)
{
	ImGui::TableNextRow();
	ImGui::TableSetColumnIndex(0);
	ImGui::AlignTextToFramePadding();
	ImGui::TextUnformatted(Label);
	if (Help) ImGui::SetItemTooltip("%s", Help);
	ImGui::TableSetColumnIndex(1);
	ImGui::SetNextItemWidth(-FLT_MIN);
}

// 0 이하를 "Off"로 보여 주는 거리·크기 입력.
void OptionalFloat(const char* Id, float& Value, float Speed, float Max, const char* Format)
{
	ImGui::DragFloat(Id, &Value, Speed, 0.0f, Max, Value > 0.0f ? Format : "Off", ImGuiSliderFlags_AlwaysClamp);
}

bool BeginStatTable(const char* Id)
{
	if (!ImGui::BeginTable(Id, 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV)) return false;
	ImGui::TableSetupColumn("Item", ImGuiTableColumnFlags_WidthStretch);
	ImGui::TableSetupColumn("Count", ImGuiTableColumnFlags_WidthFixed, CountColumnWidth);
	return true;
}

// 통계 표의 한 줄. 값은 오른쪽 정렬하고, 0은 흐리게 그려 실제로 동작한 항목이 눈에 띄게 한다.
void StatRow(const char* Label, uint32 Value, const char* Help = nullptr)
{
	ImGui::TableNextRow();
	ImGui::TableSetColumnIndex(0);
	ImGui::TextUnformatted(Label);
	if (Help) ImGui::SetItemTooltip("%s", Help);
	ImGui::TableSetColumnIndex(1);
	char Buffer[16];
	std::snprintf(Buffer, sizeof(Buffer), "%u", Value);
	const float Offset = ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize(Buffer).x;
	if (Offset > 0.0f) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + Offset);
	if (Value == 0) ImGui::TextDisabled("%s", Buffer);
	else ImGui::TextUnformatted(Buffer);
}

// Part / Total 비율 막대. 글자는 Format에 Part, Total 순으로 넣는다.
void RatioBar(uint32 Part, uint32 Total, const char* Format)
{
	char Overlay[64];
	std::snprintf(Overlay, sizeof(Overlay), Format, Part, Total);
	const float Fraction = Total > 0 ? static_cast<float>(Part) / static_cast<float>(Total) : 0.0f;
	ImGui::ProgressBar(Fraction, ImVec2(-FLT_MIN, 0.0f), Overlay);
}

void NoStatsHint()
{
	ImGui::TextDisabled("No shadows prepared for this world yet.");
	ImGui::TextDisabled("Shadows run only in Lit + Phong views.");
}
}

void FDebugPanel::OnRender()
{
	ImGui::SetNextWindowSize(ImVec2(380.0f, 620.0f), ImGuiCond_FirstUseEver);
	if (!ImGui::Begin(GetPanelName()))
	{
		ImGui::End();
		return;
	}
	if (!ShadowSettings)
	{
		ImGui::TextDisabled("Shadow settings are not connected.");
		ImGui::End();
		return;
	}

	const bool bHasPlayWorld = HasPlayWorldQuery && HasPlayWorldQuery();
	if (!bHasPlayWorld) bShowPlayWorldStats = false;
	const FShadowCullingStats* Stats = ShadowStatsQuery ? ShadowStatsQuery(bShowPlayWorldStats) : nullptr;

	DrawShadowWorldSelector(bHasPlayWorld);
	ImGui::Spacing();
	DrawShadowCacheSection(Stats);
	DrawLightCullingSection(Stats);
	DrawCasterCullingSection(Stats);
	if (ImGui::CollapsingHeader("Directional / CSM (Selected Viewport)", ImGuiTreeNodeFlags_DefaultOpen))
	{
		const FShadowCullingStats* Directional = DirectionalStatsQuery ? DirectionalStatsQuery() : nullptr;
		if (Directional && BeginStatTable("##DirectionalShadowStats"))
		{
			StatRow("Lights prepared", Directional->LightsRendered);
			StatRow("Depth passes", Directional->ShadowPassesDrawn, "One pass per cascade in CSM. No cached passes.");
			StatRow("Casters considered", Directional->CastersConsidered, "Counted separately for each cascade.");
			StatRow("Shadow outside view", Directional->CastersCulledByView);
			StatRow("Smaller than min size", Directional->CastersCulledBySize);
			StatRow("Casters drawn to shadow map", Directional->CastersDrawn, "Objects with depth draw packets after material filtering. Multiple sections count once per pass.");
			ImGui::EndTable();
		}
		else if (!Directional)
			ImGui::TextDisabled("Select a visible editor viewport with directional shadows.");
		if (Directional && !Directional->CascadeCasters.IsEmpty())
		{
			ImGui::TextWrapped("Totals above include repeated casters across cascades.");
			if (ImGui::BeginTable("##CascadeCasterStats", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg))
			{
				ImGui::TableSetupColumn("Cascade");
				ImGui::TableSetupColumn("Considered");
				ImGui::TableSetupColumn("Below min");
				ImGui::TableSetupColumn("Drawn");
				ImGui::TableHeadersRow();
				for (int32 Index = 0; Index < Directional->CascadeCasters.Num(); ++Index)
				{
					const auto& Cascade = Directional->CascadeCasters[Index];
					ImGui::TableNextRow();
					ImGui::TableSetColumnIndex(0); ImGui::Text("%d", Index + 1);
					ImGui::TableSetColumnIndex(1); ImGui::Text("%u", Cascade.CastersConsidered);
					ImGui::TableSetColumnIndex(2); ImGui::Text("%u", Cascade.CastersCulledBySize);
					ImGui::TableSetColumnIndex(3); ImGui::Text("%u", Cascade.CastersDrawn);
				}
				ImGui::EndTable();
			}
		}
	}

	ImGui::Spacing();
	ImGui::Separator();
	ImGui::Spacing();
	if (ImGui::Button("Reset to Defaults"))
		*ShadowSettings = FShadowCullingSettings{};
	ImGui::SameLine();
	ImGui::AlignTextToFramePadding();
	ImGui::TextDisabled("Saved with Settings > Save");

	ImGui::End();
}

void FDebugPanel::DrawShadowWorldSelector(bool bHasPlayWorld)
{
	// 통계는 World마다 따로 쌓인다. PIE 중이면 PIE World를 골라 볼 수 있다.
	ImGui::AlignTextToFramePadding();
	ImGui::TextUnformatted("Stats of");
	ImGui::SameLine();
	ImGui::BeginDisabled(!bHasPlayWorld);
	if (ImGui::RadioButton("Editor World", !bShowPlayWorldStats)) bShowPlayWorldStats = false;
	ImGui::SameLine();
	if (ImGui::RadioButton("PIE World", bShowPlayWorldStats)) bShowPlayWorldStats = true;
	ImGui::EndDisabled();
	if (!bHasPlayWorld) ImGui::SetItemTooltip("Start Play In Editor to inspect the PIE world.");
}

void FDebugPanel::DrawShadowCacheSection(const FShadowCullingStats* Stats)
{
	if (!ImGui::CollapsingHeader("Shadow Map Cache", ImGuiTreeNodeFlags_DefaultOpen)) return;

	ImGui::Checkbox("Cache Shadow Maps", &ShadowSettings->bCacheShadowMaps);
	HelpMarker(
		"Reuse last frame's shadow maps. A face is redrawn only when its light's view-projection changes "
		"or a caster is added, moved, changed or removed inside it.\n\n"
		"Off: redraw everything every frame. Use it to compare against cache bugs.\n\n"
		"On: view-based caster culling and Point face culling are disabled, "
		"because a cached map must not depend on the camera.");
	// 설정이 아니라 마지막 준비가 실제로 어떤 모드였는지 보여 준다.
	if (Stats) RightAlignedBadge(Stats->bCaching ? "CACHING" : "REDRAW ALL", Stats->bCaching ? ActiveColor : InactiveColor);

	if (!Stats)
	{
		NoStatsHint();
		return;
	}
	ImGui::Spacing();
	RatioBar(Stats->ShadowPassesCached, Stats->ShadowPassesDrawn + Stats->ShadowPassesCached, "%u / %u passes reused");
	if (BeginStatTable("##ShadowCacheStats"))
	{
		StatRow("Passes drawn", Stats->ShadowPassesDrawn,
			"Shadow map passes rendered this frame. Directional and Spot use one pass, Point uses one per cube face.");
		StatRow("Passes reused", Stats->ShadowPassesCached, "Passes kept from an earlier frame.");
		StatRow("Lights invalidated", Stats->CacheLightsInvalidated,
			"Lights whose view-projection changed (moved, rotated, radius or cone changed). All their passes are redrawn.");
		StatRow("Faces hit by casters", Stats->CacheFacesInvalidatedByCaster,
			"Cached passes whose frustum overlapped the old or new bounds of a changed caster.");
		ImGui::EndTable();
	}
	ImGui::Spacing();
}

void FDebugPanel::DrawLightCullingSection(const FShadowCullingStats* Stats)
{
	if (!ImGui::CollapsingHeader("Light Culling", ImGuiTreeNodeFlags_DefaultOpen)) return;

	ImGui::TextDisabled("Point/Spot only. Judged against every shadowed view.");
	if (BeginControlTable("##LightCullingControls"))
	{
		ControlRow("Cull By View", "Skip lights whose influence sphere is outside every view frustum.");
		ImGui::Checkbox("##CullLightsByView", &ShadowSettings->bCullLightsByView);
		ControlRow("Shadow Distance", "Skip lights farther than this from every camera. 0 = off.");
		OptionalFloat("##ShadowDistance", ShadowSettings->ShadowDistance, 1.0f, 100000.0f, "%.1f");
		ControlRow("Min Screen Radius", "Skip lights whose influence radius is smaller than this on every view. 0 = off.");
		OptionalFloat("##MinLightScreenRadius", ShadowSettings->MinLightScreenRadius, 0.1f, 1000.0f, "%.1f px");
		ImGui::EndTable();
	}

	if (!Stats) return;
	ImGui::Spacing();
	RatioBar(Stats->LightsRendered, Stats->LightsConsidered, "%u / %u lights shadowed");
	if (BeginStatTable("##LightCullingStats"))
	{
		StatRow("Zero intensity", Stats->LightsCulledByIntensity, "Color x Intensity is black.");
		StatRow("Outside views", Stats->LightsCulledByView);
		StatRow("Beyond shadow distance", Stats->LightsCulledByDistance);
		StatRow("Too small on screen", Stats->LightsCulledByScreenSize);
		StatRow("No casters", Stats->LightsCulledByNoCaster, "Nothing left to draw after caster culling. The light keeps no shadow map slot.");
		StatRow("Point faces culled", Stats->PointFacesCulled,
			Stats->bCaching ? "Disabled while caching." : "Cube faces outside every view. They are cleared instead of drawn.");
		ImGui::EndTable();
	}
	ImGui::Spacing();
}

void FDebugPanel::DrawCasterCullingSection(const FShadowCullingStats* Stats)
{
	if (!ImGui::CollapsingHeader("Caster Culling", ImGuiTreeNodeFlags_DefaultOpen)) return;

	const bool bCaching = ShadowSettings->bCacheShadowMaps;
	if (BeginControlTable("##CasterCullingControls"))
	{
		ControlRow("Cull By View", "Skip casters whose shadow volume (bounds extruded away from the light) misses every view.");
		ImGui::Checkbox("##CullCastersByView", &ShadowSettings->bCullCastersByView);
		if (bCaching)
		{
			ImGui::SameLine();
			ImGui::TextDisabled("World cache: off; Directional: active");
		}
		ControlRow("Min Size", "Skip casters whose bounding diameter in the shadow map is smaller than this. 0 = off.");
		OptionalFloat("##MinCasterTexels", ShadowSettings->MinCasterTexels, 0.05f, 64.0f, "%.2f texels");
		ImGui::EndTable();
	}

	if (!Stats) return;
	ImGui::Spacing();
	RatioBar(Stats->CastersDrawn, Stats->CastersConsidered, "%u / %u casters drawn");
	if (BeginStatTable("##CasterCullingStats"))
	{
		StatRow("Shadow outside views", Stats->CastersCulledByView);
		StatRow("Smaller than min size", Stats->CastersCulledBySize);
		ImGui::EndTable();
	}
	// 캐싱 중에는 다시 그린 패스의 Caster만 센다.
	if (Stats->bCaching) ImGui::TextDisabled("Counted only for passes redrawn this frame.");
	ImGui::Spacing();
}
