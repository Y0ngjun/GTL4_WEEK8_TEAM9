#include "EnginePCH.h"
#include "Engine/GameViewportClient.h"
#include "Editor/Viewports/ViewportsPanel.h"
#include "Core/Window.h"
#include "Input/InputSystem.h"

#include "Core/StatOverlay.h"
#include "Core/Stats/LightweightStats.h"
#include "Math/Color.h"

#include <algorithm>
#include <cassert>
#include <format>

#include "Core/Stats/EditorStats.h"

namespace
{
constexpr float SplitterThickness = 6.0f;
// 네 View Rect의 중앙 경계에서 절반씩 줄여 Splitter가 차지할 실제 빈 공간을 만든다.
void ApplySplitterGutter(const FVector2 WindowSize, FRect ViewRects[4])
{
	const float SplitX = ViewRects[0].Width;
	const float SplitY = ViewRects[0].Height;
	const float HalfThickness = SplitterThickness * 0.5f;
	const float HalfGapX = std::min({HalfThickness, SplitX, WindowSize.X - SplitX});
	const float HalfGapY = std::min({HalfThickness, SplitY, WindowSize.Y - SplitY});
	const float RightX = SplitX + HalfGapX;
	const float BottomY = SplitY + HalfGapY;
	const float LeftWidth = SplitX - HalfGapX;
	const float TopHeight = SplitY - HalfGapY;
	const float RightWidth = WindowSize.X - RightX;
	const float BottomHeight = WindowSize.Y - BottomY;

	ViewRects[0] = {0.0f, 0.0f, LeftWidth, TopHeight};
	ViewRects[1] = {RightX, 0.0f, RightWidth, TopHeight};
	ViewRects[2] = {0.0f, BottomY, LeftWidth, BottomHeight};
	ViewRects[3] = {RightX, BottomY, RightWidth, BottomHeight};
}


constexpr ImU32 SplitterColor = IM_COL32(55, 55, 55, 230);
constexpr ImU32 SplitterHoverColor = IM_COL32(255, 192, 0, 255);
constexpr const char* CameraPresetLabels[] = {
	"Perspective", "Ortho (Current)", "Top", "Bottom", "Front", "Back", "Left", "Right"};

// Stat Overlay
constexpr float StatOverlayMargin = 8.0f;
constexpr float StatOverlayPadding = 6.0f;
constexpr ImU32 StatOverlayBackgroundColor = IM_COL32(0, 0, 0, 140);
constexpr ImU32 TitleColor = IM_COL32(255, 210, 60, 255);
constexpr ImU32 ValueColor = IM_COL32(235, 235, 235, 255);
}

FViewportsPanel::FViewportsPanel(const EViewportPanelLayout InLayout) : PanelLayout(InLayout)
{
    Slots.SetNum(PanelLayout == EViewportPanelLayout::Quad ? 4 : 1);
}

bool FViewportsPanel::Init()
{
    for (int32 Index = 0; Index < GetSlotCount(); ++Index)
        Slots[Index].Viewport = MakeUnique<FViewport>();
    return true;
}

void FViewportsPanel::Shutdown()
{
    for (int32 Index = 0; Index < GetSlotCount(); ++Index)
    {
        ReleaseInput(Index);
        Slots[Index].Viewport.reset();
    }
}

// 패널의 프레임 갱신 인터페이스이며 별도 계산은 하지 않는다.
void FViewportsPanel::Tick(float DeltaTime)
{
	(void)DeltaTime;
}

FVector2 FViewportsPanel::GetWindowMousePosition() const
{
    POINT Mouse{};
    ::GetCursorPos(&Mouse);
    ::ScreenToClient(HostWindow->GetHandle(), &Mouse);
    return {static_cast<float>(Mouse.x), static_cast<float>(Mouse.y)};
}

FRect FViewportsPanel::GetContentRect() const
{
    // 창 메시지는 이미 처리됐다. 마지막 ImGui 배치가 남아 있어도 현재 숨김/최소화를 우선한다.
    const HWND Window = HostWindow->GetHandle();
    if (!::IsWindowVisible(Window) || ::IsIconic(Window))
        return {};
    // TODO: 편집 Panel의 메인 창 밖 분리·이동은 현재 지원하지 않는다. 지원 시 실제 호스트 창을 기준으로 마우스와 Slot 좌표를 통일하고, UI 표시 단위와 출력 픽셀 간 DPI 변환 및 배치 저장·복원 정책을 함께 구현한다.
    POINT Origin{static_cast<LONG>(ContentOrigin.x), static_cast<LONG>(ContentOrigin.y)};
    ::ScreenToClient(HostWindow->GetHandle(), &Origin);
    return {static_cast<float>(Origin.x), static_cast<float>(Origin.y), ContentSize.x, ContentSize.y};
}

FVector2 FViewportsPanel::ToViewportLocal(const int32 Index, const FVector2 WindowMousePosition) const
{
    const FRect& Rect = Slots[Index].Rect;
    return {WindowMousePosition.X - Rect.X, WindowMousePosition.Y - Rect.Y};
}

void FViewportsPanel::RouteInput(const int32 Index, FViewportInput Input)
{
    FViewport* Viewport = GetViewport(Index);
    if (!Viewport)
        return;
    Input.bCaptured = CapturedViewIndex == Index;
    Input.bPointerOver = GetPointerViewIndex(Input.MousePosition) == Index;
    Input.MousePosition = ToViewportLocal(Index, Input.MousePosition);
    Viewport->UpdateInput(Input);
}

void FViewportsPanel::ReleaseInput(const int32 Index)
{
    if (FViewport* Viewport = GetViewport(Index))
        Viewport->UpdateInput({});
    if (CapturedViewIndex == Index)
        CapturedViewIndex = InvalidSlotIndex;
}

// 엔진 입력 처리는 ImGui NewFrame보다 먼저 실행되므로 현재 OS 커서로 버튼 영역을 검사한다.
bool FViewportsPanel::IsHovered() const
{
	if (bStatResetButtonVisible && FStatOverlay::IsAnyEnabled())
	{
		POINT Cursor{};
		if (::GetCursorPos(&Cursor) &&
			Cursor.x >= StatResetButtonMin.x && Cursor.x < StatResetButtonMax.x &&
			Cursor.y >= StatResetButtonMin.y && Cursor.y < StatResetButtonMax.y)
			return false;
	}
	return bHovered;
}

// 누적 가로 Splitter 이동량을 반환하고 초기화한다.
float FViewportsPanel::ConsumeHorizontalDrag()
{
	const float Result = PendingHorizontalDrag;
	PendingHorizontalDrag = 0.0f;
	return Result;
}

// 누적 세로 Splitter 이동량을 반환하고 초기화한다.
float FViewportsPanel::ConsumeVerticalDrag()
{
	const float Result = PendingVerticalDrag;
	PendingVerticalDrag = 0.0f;
	return Result;
}

// 대기 Layout 요청을 한 번 반환하고 플래그를 지운다.
bool FViewportsPanel::ConsumeMaximizeRequest(bool& OutMaximized, int32& OutSlotIndex)
{
	if (!bHasMaximizeRequest)
		return false;
	OutMaximized = bRequestedMaximized;
	OutSlotIndex = RequestedMaximizedSlotIndex;
	bHasMaximizeRequest = false;
	return true;
}

// 대기 Preset 요청을 한 번 반환하고 지운다.
bool FViewportsPanel::ConsumeCameraPresetRequest(int32& OutViewIndex, EViewportCameraPreset& OutPreset)
{
	if (PendingCameraPresetViewIndex == InvalidSlotIndex)
		return false;
	OutViewIndex = PendingCameraPresetViewIndex;
	OutPreset = PendingCameraPreset;
	PendingCameraPresetViewIndex = InvalidSlotIndex;
	return true;
}

// View Texture와 Splitter·Layout·Preset UI를 그리고 요청을 기록한다.
void FViewportsPanel::OnRender()
{
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{0.0f, 0.0f});
    const bool bVisible = ImGui::Begin("Viewports", nullptr, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoTitleBar);
    if (!bVisible)
    {
        ContentSize = {};
        bHovered = false;
        bStatResetButtonVisible = false;
        bDraggingVerticalSplitter = false;
        bDraggingHorizontalSplitter = false;
        PendingHorizontalDrag = PendingVerticalDrag = 0.0f;
        ImGui::End();
        ImGui::PopStyleVar();
        return;
    }

	ContentOrigin = ImGui::GetCursorScreenPos();
	ContentSize = ImGui::GetContentRegionAvail();
	ContentSize.x = std::max(0.0f, ContentSize.x);
	ContentSize.y = std::max(0.0f, ContentSize.y);
	bHovered = ImGui::IsWindowHovered();
	bStatResetButtonVisible = false;
    POINT WindowOrigin{};
    ::ClientToScreen(HostWindow->GetHandle(), &WindowOrigin);
    const ImVec2 ClientOrigin{static_cast<float>(WindowOrigin.x), static_cast<float>(WindowOrigin.y)};

	// 전체 캔버스를 한 번 확보한 뒤 각 렌더 타깃을 창 DrawList에 직접 그린다.
	// Image 항목 네 개를 따로 배치하면 ImGui 레이아웃과 클리핑 상태가 삽입 순서에
	// 영향을 받아, 배치 계산이 올바른 사각형을 줘도 아래쪽 행이 잘릴 수 있다.
	ImGui::Dummy(ContentSize);
	ImDrawList* DrawList = ImGui::GetWindowDrawList();
	DrawList->PushClipRect(ContentOrigin, {ContentOrigin.x + ContentSize.x, ContentOrigin.y + ContentSize.y}, true);
	for (int32 ViewIndex = 0; ViewIndex < GetSlotCount(); ++ViewIndex)
	{
		const FViewport& Viewport = *GetViewport(ViewIndex);
        const FRect& Rect = GetSlotRect(ViewIndex);
		if (!Viewport.IsVisible() || !Viewport.GetColorTarget() || !Viewport.GetActiveClient() || !Viewport.GetActiveClient()->HasCamera())
			continue;

		const ImVec2 ViewMin{ClientOrigin.x + Rect.X, ClientOrigin.y + Rect.Y};
		const ImVec2 ViewMax{ViewMin.x + Rect.Width, ViewMin.y + Rect.Height};
		DrawList->AddImage(Viewport.GetColorTarget()->GetSRV(), ViewMin, ViewMax);
	}
	DrawList->PopClipRect();

	if (PanelLayout == EViewportPanelLayout::Quad && !bSlotMaximized)
	{
		// View Rect 사이에 비워 둔 gutter의 중앙에 Splitter 버튼을 배치한다.
		const float SplitX = (GetSlotRect(0).X + GetSlotRect(0).Width + GetSlotRect(1).X) * 0.5f;
		const float SplitY = (GetSlotRect(0).Y + GetSlotRect(0).Height + GetSlotRect(2).Y) * 0.5f;

		const ImVec2 VerticalMin{ClientOrigin.x + SplitX - SplitterThickness * 0.5f, ContentOrigin.y};
		const ImVec2 VerticalMax{VerticalMin.x + SplitterThickness, ContentOrigin.y + ContentSize.y};
		const ImVec2 HorizontalMin{ContentOrigin.x, ClientOrigin.y + SplitY - SplitterThickness * 0.5f};
		const ImVec2 HorizontalMax{ContentOrigin.x + ContentSize.x, HorizontalMin.y + SplitterThickness};

		ImGui::SetCursorScreenPos(VerticalMin);
		ImGui::InvisibleButton("##ViewportPanelHorizontalSplitter", {SplitterThickness, ContentSize.y});
		ImGui::SetCursorScreenPos(HorizontalMin);
		ImGui::InvisibleButton("##ViewportPanelVerticalSplitter", {ContentSize.x, SplitterThickness});

		const bool bVerticalHovered = bHovered && ImGui::IsMouseHoveringRect(VerticalMin, VerticalMax);
		const bool bHorizontalHovered = bHovered && ImGui::IsMouseHoveringRect(HorizontalMin, HorizontalMax);
		if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
		{
			bDraggingVerticalSplitter = bVerticalHovered;
			bDraggingHorizontalSplitter = bHorizontalHovered;
		}
		if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
		{
			bDraggingVerticalSplitter = false;
			bDraggingHorizontalSplitter = false;
		}

		const bool bVerticalHighlighted = bDraggingVerticalSplitter || bVerticalHovered;
		const bool bHorizontalHighlighted = bDraggingHorizontalSplitter || bHorizontalHovered;
		DrawList->AddRectFilled(VerticalMin, VerticalMax, bVerticalHighlighted ? SplitterHoverColor : SplitterColor);
		DrawList->AddRectFilled(HorizontalMin, HorizontalMax, bHorizontalHighlighted ? SplitterHoverColor : SplitterColor);

		if (bDraggingVerticalSplitter)
			PendingHorizontalDrag += ImGui::GetIO().MouseDelta.x;
		if (bDraggingHorizontalSplitter)
			PendingVerticalDrag += ImGui::GetIO().MouseDelta.y;

		if (bVerticalHighlighted && bHorizontalHighlighted)
			ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
		else if (bVerticalHighlighted)
			ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
		else if (bHorizontalHighlighted)
			ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
	}
	else
	{
		bDraggingVerticalSplitter = false;
		bDraggingHorizontalSplitter = false;
	}

	for (int32 ViewIndex = 0; ViewIndex < GetSlotCount(); ++ViewIndex)
	{
		if (!IsViewActive(ViewIndex))
			continue;
		ImGui::SetCursorScreenPos({ ClientOrigin.x + GetSlotRect(ViewIndex).X + 8.0f, ClientOrigin.y + GetSlotRect(ViewIndex).Y + 8.0f});
		ImGui::PushID(100 + ViewIndex);
        if (const FEditorViewportClient* EditorClient = GetEditorClient(ViewIndex))
        {
            int SelectedPreset = static_cast<int>(EditorClient->GetCameraPreset());
            ImGui::SetNextItemWidth(120.0f);
            if (ImGui::Combo("##CameraPreset", &SelectedPreset, CameraPresetLabels, IM_ARRAYSIZE(CameraPresetLabels)))
            {
                PendingCameraPresetViewIndex = ViewIndex;
                PendingCameraPreset = static_cast<EViewportCameraPreset>(SelectedPreset);
            }
            ImGui::SameLine();
        }
		// 조명·셰이딩 모델은 Lit일 때만 보인다.
		{
			FViewRenderMode RenderMode = GetActiveClient(ViewIndex)->GetViewRenderMode();
			bool bChanged = false;

			int ViewMode = static_cast<int>(RenderMode.ViewMode);
			ImGui::SetNextItemWidth(100.0f);
			if (ImGui::Combo("##ViewMode", &ViewMode, ViewModeNames, IM_ARRAYSIZE(ViewModeNames)))
			{
				RenderMode.ViewMode = static_cast<EViewMode>(ViewMode);
				bChanged = true;
			}
			ImGui::SameLine();

			if (RenderMode.ViewMode == EViewMode::Lit)
			{
				const char* LightingModelLabels[] = {"Lambert", "Phong", "Blinn-Phong"};
				int LightingModel = static_cast<int>(RenderMode.LightingModel);
				ImGui::SetNextItemWidth(110.0f);
				if (ImGui::Combo("##LightingModel", &LightingModel, LightingModelLabels, IM_ARRAYSIZE(LightingModelLabels)))
				{
					RenderMode.LightingModel = static_cast<ELightingModel>(LightingModel);
					bChanged = true;
				}
				ImGui::SetItemTooltip("Lighting Model");
				ImGui::SameLine();

				int ShadingModel = static_cast<int>(RenderMode.ShadingModel);
				ImGui::SetNextItemWidth(90.0f);
				if (ImGui::Combo("##ShadingModel", &ShadingModel, ShadingModelNames, IM_ARRAYSIZE(ShadingModelNames)))
				{
					RenderMode.ShadingModel = static_cast<EShadingModel>(ShadingModel);
					bChanged = true;
				}
				ImGui::SetItemTooltip("Shading Model");
				ImGui::SameLine();
			}

			if (bChanged)
				GetActiveClient(ViewIndex)->SetViewRenderMode(RenderMode);
		}
		if (PanelLayout == EViewportPanelLayout::Quad && !bSlotMaximized)
		{
			if (ImGui::SmallButton("Maximize"))
			{
				bRequestedMaximized = true;
				RequestedMaximizedSlotIndex = ViewIndex;
				bHasMaximizeRequest = true;
			}
		}
		else if (PanelLayout == EViewportPanelLayout::Quad && ViewIndex == MaximizedSlotIndex && ImGui::SmallButton("Restore Quad"))
		{
			bRequestedMaximized = false;
			RequestedMaximizedSlotIndex = ViewIndex;
			bHasMaximizeRequest = true;
		}
		if (ViewIndex == GetEditorViewIndex() &&
			FStatOverlay::IsAnyEnabled() &&
			(FStatOverlay::IsEnabled(EStatFlags::Profile) ||
				FStatRegistry::Find(EditorStats::STAT_PickingTime)))
		{
			ImGui::SameLine();
			if (ImGui::SmallButton("Reset Stats"))
				FStatRegistry::Reset();
			StatResetButtonMin = ImGui::GetItemRectMin();
			StatResetButtonMax = ImGui::GetItemRectMax();
			bStatResetButtonVisible = true;
		}
		ImGui::PopID();
	}

	// 마지막으로 선택된 뷰포트만 오버레이
	for (int32 ViewIndex = 0; ViewIndex < GetSlotCount(); ++ViewIndex)
	{
		if (ViewIndex != GetEditorViewIndex()) continue;
		if (!IsViewActive(ViewIndex))
			continue;
		DrawStatOverlay(DrawList, { ClientOrigin.x + GetSlotRect(ViewIndex).X, ClientOrigin.y + GetSlotRect(ViewIndex).Y});
	}

	ImGui::End();
	ImGui::PopStyleVar();
}

// 항목별 줄을 모아 한 번에 배경과 텍스트를 그린다.
void FViewportsPanel::DrawStatOverlay(ImDrawList* DrawList, const ImVec2& ViewMin) const
{
	if (!DrawList || !FStatOverlay::IsAnyEnabled())
		return;

	constexpr float BytesPerMegabyte = 1024.0f * 1024.0f;

	// 제목은 UE처럼 노란색, 값은 흰색으로 구분한다.
	struct FStatLine
	{
		FString Text;
		ImU32 Color;
	};
	TArray<FStatLine> Lines;

	if (FStatOverlay::IsEnabled(EStatFlags::FPS))
	{
		Lines.Add({"FPS", TitleColor});
		Lines.Add({std::format("  {:.1f} fps", FStatOverlay::GetFPS()), ValueColor});
		Lines.Add({std::format("  {:.2f} ms", FStatOverlay::GetFrameTimeMs()), ValueColor});
	}

	if (FStatOverlay::IsEnabled(EStatFlags::Memory))
	{
		Lines.Add({"Memory", TitleColor});
		Lines.Add({std::format("  Object  {:.2f} MB ({} allocs)", static_cast<double>(FStatOverlay::GetObjectAllocationBytes()) / BytesPerMegabyte, FStatOverlay::GetObjectAllocationCount()), ValueColor});
		Lines.Add({std::format("  Process {:.2f} MB", static_cast<double>(FStatOverlay::GetProcessWorkingSetBytes()) / BytesPerMegabyte), ValueColor});
	}

	struct FProfileStatLine
	{
		FString Text;
		ImU32 Color;
		FString Values[3];
	};
	TArray<FProfileStatLine> ProfileLines;
	if (FStatOverlay::IsEnabled(EStatFlags::Profile))
	{
		ProfileLines.Add({"CPU Profile (ms)", TitleColor, {"Last", "Avg", "Max"}});

		for (const auto& [Name, Data] : FStatRegistry::GetAll())
		{
			if (TStatId{ Name } == EditorStats::STAT_PickingTime)
				continue;

			FProfileStatLine Line{"  " + FString(Name), ValueColor};
			Line.Values[0] = std::format("{:.2f}", Data.GetLastMs());
			Line.Values[1] = std::format("{:.2f}", Data.GetRecentAverageMs());
			Line.Values[2] = std::format("{:.2f}", Data.GetMaxMs());
			ProfileLines.Add(Line);
		}
	}

	if (const FCycleStatData* PickingData = FStatRegistry::Find(EditorStats::STAT_PickingTime))
	{
		ProfileLines.Add({ "Picking", TitleColor });
		FProfileStatLine Line{"  Time (ms)", ValueColor};
		Line.Values[0] = std::format("Last: {:.2f}", PickingData->GetLastMs());
		Line.Values[1] = std::format("Attempts: {}", PickingData->CallCount);
		Line.Values[2] = std::format("Acc.: {:.2f}", PickingData->GetTotalMs());
		ProfileLines.Add(Line);
	}

	if (Lines.Num() == 0 && ProfileLines.Num() == 0)
		return;

	// 제어 위젯 한 줄 아래에서 시작해 Combo와 겹치지 않게 한다.
	const float LineHeight = ImGui::GetTextLineHeight();
	const ImVec2 Origin{
		ViewMin.x + StatOverlayMargin,
		ViewMin.y + StatOverlayMargin + ImGui::GetFrameHeight() + StatOverlayMargin};

	// 최대 너비 계산
	float MaxWidth = 0.0f;
	for (int32 Index = 0; Index < Lines.Num(); ++Index)
		MaxWidth = std::max(MaxWidth, ImGui::CalcTextSize(Lines[Index].Text.c_str()).x);

	const float MinValueWidth = ImGui::CalcTextSize("00000.00").x;
	float NameWidth = 0.0f;
	float ValueWidths[3] = { MinValueWidth, MinValueWidth, MinValueWidth };
	bool bHasColumns = false;

	for (const FProfileStatLine& Line : ProfileLines)
	{
		const float TextWidth = ImGui::CalcTextSize(Line.Text.c_str()).x;
		bHasColumns = true;
		NameWidth = std::max(NameWidth, TextWidth);
		for (int32 Column = 0; Column < 3; ++Column)
			ValueWidths[Column] = std::max(MinValueWidth, ImGui::CalcTextSize(Line.Values[Column].c_str()).x);
	}

	float ColumnRight[3]{};
	if (bHasColumns)
	{
		float Width = NameWidth;
		for (int32 Column = 0; Column < 3; ++Column)
		{
			Width += ImGui::GetFontSize() + ValueWidths[Column];
			ColumnRight[Column] = Origin.x + Width;
		}
		MaxWidth = std::max(MaxWidth, Width);
	}

	const ImVec2 BackgroundMin{Origin.x - StatOverlayPadding, Origin.y - StatOverlayPadding};
	const ImVec2 BackgroundMax{
		Origin.x + MaxWidth + StatOverlayPadding,
		Origin.y + LineHeight * static_cast<float>(Lines.Num() + ProfileLines.Num()) + StatOverlayPadding};
	DrawList->AddRectFilled(BackgroundMin, BackgroundMax, StatOverlayBackgroundColor, 4.0f);

	// Lines 그리기
	for (int32 Index = 0; Index < Lines.Num(); ++Index)
	{
		DrawList->AddText( { Origin.x, Origin.y + LineHeight * static_cast<float>(Index) }, Lines[Index].Color, Lines[Index].Text.c_str());
	}

	// ProfileLines 그리기
	for (int32 Index = 0; Index < ProfileLines.Num(); ++Index)
	{
		const FProfileStatLine& Line = ProfileLines[Index];
		const float Y = Origin.y + LineHeight * static_cast<float>(Lines.Num() + Index);
		DrawList->AddText({ Origin.x, Y }, Line.Color, Line.Text.c_str());

		for (int32 Column = 0; Column < 3; ++Column)
		{
			const FString& Text = Line.Values[Column];
			const float TextWidth = ImGui::CalcTextSize(Text.c_str()).x;
			DrawList->AddText({ ColumnRight[Column] - TextWidth, Y }, Line.Color, Text.c_str());
		}
	}
}


void FViewportsPanel::SetMaximizedSlotIndex(const int32 Index)
{
	assert(Index >= 0 && Index < GetSlotCount());
	MaximizedSlotIndex = Index;
}

void FViewportsPanel::SetSplitRatio(const FViewportSplitRatio& Value)
{
	SplitRatio = {FMath::Clamp(Value.Horizontal, MinimumSplitRatio, 1.0f - MinimumSplitRatio), FMath::Clamp(Value.Vertical, MinimumSplitRatio, 1.0f - MinimumSplitRatio)};
}

void FViewportsPanel::ApplySplitterDrag(const EViewportSplitAxis Axis, const float DeltaPixels, const FVector2 WindowSize)
{
    if (WindowSize.X <= 0.0f || WindowSize.Y <= 0.0f)
        return;
    if (Axis == EViewportSplitAxis::Horizontal)
        SplitRatio.Horizontal += DeltaPixels / WindowSize.X;
    else
        SplitRatio.Vertical += DeltaPixels / WindowSize.Y;
    SetSplitRatio(SplitRatio);
}

void FViewportsPanel::ComputeQuadLayout(const FVector2 WindowSize)
{
    const float LeftWidth = WindowSize.X * SplitRatio.Horizontal;
    const float TopHeight = WindowSize.Y * SplitRatio.Vertical;
    const float RightWidth = WindowSize.X - LeftWidth;
    const float BottomHeight = WindowSize.Y - TopHeight;
    FRect Rects[4] = {{0.0f, 0.0f, LeftWidth, TopHeight}, {LeftWidth, 0.0f, RightWidth, TopHeight}, {0.0f, TopHeight, LeftWidth, BottomHeight}, {LeftWidth, TopHeight, RightWidth, BottomHeight}};
    ApplySplitterGutter(WindowSize, Rects);
    for (int32 Index = 0; Index < 4; ++Index)
        Slots[Index].Rect = Rects[Index];
}

void FViewportsPanel::UpdateLayout(const FRect& Area)
{
    for (FSlot& Slot : Slots)
        Slot.Rect = {};
    if (Area.IsValid())
    {
        if (PanelLayout == EViewportPanelLayout::Single)
            Slots[0].Rect = {0.0f, 0.0f, Area.Width, Area.Height};
        else if (bSlotMaximized)
            Slots[MaximizedSlotIndex].Rect = {0.0f, 0.0f, Area.Width, Area.Height};
        else
            ComputeQuadLayout({Area.Width, Area.Height});
    }
    for (int32 Index = 0; Index < GetSlotCount(); ++Index)
    {
        FRect& Rect = Slots[Index].Rect;
        Rect.X += Area.X;
        Rect.Y += Area.Y;
        // 표시/피킹 영역과 실제 픽셀 출력 크기를 일치시킨다.
        Rect.Width = std::floor(std::max(0.0f, Rect.Width));
        Rect.Height = std::floor(std::max(0.0f, Rect.Height));
        Slots[Index].Viewport->SetOutputSize(static_cast<uint32>(Rect.Width), static_cast<uint32>(Rect.Height));
        if (!Rect.IsValid())
            ReleaseInput(Index);
    }
    if (CapturedViewIndex != InvalidSlotIndex)
        ActiveViewIndex = CapturedViewIndex;
    if (!IsViewActive(ActiveViewIndex))
        ActiveViewIndex = PanelLayout == EViewportPanelLayout::Single && IsViewActive(0) ? 0 : bSlotMaximized && IsViewActive(MaximizedSlotIndex) ? MaximizedSlotIndex : InvalidSlotIndex;
}

int32 FViewportsPanel::GetPointerViewIndex(const FVector2 WindowMousePosition) const
{
    for (int32 Index = 0; Index < GetSlotCount(); ++Index)
    {
        const FRect& Rect = Slots[Index].Rect;
        if (Rect.IsValid() && WindowMousePosition.X >= Rect.X && WindowMousePosition.X < Rect.X + Rect.Width && WindowMousePosition.Y >= Rect.Y && WindowMousePosition.Y < Rect.Y + Rect.Height)
            return Index;
    }
    return InvalidSlotIndex;
}

void FViewportsPanel::UpdateInputSelection(const FVector2 WindowMousePosition)
{
	if (!FInputSystem::IsMouseDown(EMouseButton::Right))
		CapturedViewIndex = InvalidSlotIndex;
	const int32 PointerViewIndex = GetPointerViewIndex(WindowMousePosition);
	if (CapturedViewIndex == InvalidSlotIndex && IsViewActive(PointerViewIndex))
	{
		if (FInputSystem::IsMousePressed(EMouseButton::Right))
			CapturedViewIndex = PointerViewIndex;
		else if (FInputSystem::IsMousePressed(EMouseButton::Left))
			ActiveViewIndex = PointerViewIndex;
	}
	if (CapturedViewIndex != InvalidSlotIndex)
		ActiveViewIndex = CapturedViewIndex;
}

FEditorViewportClient* FViewportsPanel::GetEditorClient(const int32 Index) const
{
    const FViewport* Viewport = GetViewport(Index);
    return Viewport ? static_cast<FEditorViewportClient*>(Viewport->GetEditorClient()) : nullptr;
}

FGameViewportClient* FViewportsPanel::GetGameViewportClient(const int32 Index) const
{
    const FViewport* Viewport = GetViewport(Index);
    return Viewport ? static_cast<FGameViewportClient*>(Viewport->GetGameClient()) : nullptr;
}

FViewportClient* FViewportsPanel::GetActiveClient(const int32 Index) const
{
    const FViewport* Viewport = GetViewport(Index);
    return Viewport ? Viewport->GetActiveClient() : nullptr;
}
