#pragma once

#include "Editor/EditorUI/EditorPanel.h"
#include "Editor/Viewports/EditorViewportClient.h"
#include "Engine/Viewport.h"

class FGameViewportClient;
class FWindow;

// 생성 시 고정하는 슬롯 구성. Quad 한 슬롯 최대화는 별도 표시 상태다.
enum class EViewportPanelLayout { Quad, Single };

struct FViewportSplitRatio
{
    float Horizontal = 0.5f;
    float Vertical = 0.5f;
};
enum class EViewportSplitAxis { Horizontal, Vertical };

class FViewportsPanel : public IEditorPanel
{
public:
    explicit FViewportsPanel(EViewportPanelLayout InLayout = EViewportPanelLayout::Quad);
    static constexpr int32 InvalidSlotIndex = -1;
    int32 GetSlotCount() const { return Slots.Num(); }
    void SetHostWindow(FWindow* Window) { HostWindow = Window; }
    FViewport* GetViewport(int32 Index) const { return Index >= 0 && Index < GetSlotCount() ? Slots[Index].Viewport.get() : nullptr; }
    const FRect& GetSlotRect(int32 Index) const { return Slots[Index].Rect; }
    FRect GetContentRect() const;
    FVector2 ToViewportLocal(int32 Index, FVector2 WindowMousePosition) const;
    void RouteInput(int32 Index, FViewportInput Input);
    void ReleaseInput(int32 Index);
    void Shutdown();
	// 구성에 맞는 고정 Viewport만 만든다. Client 종류는 구성 호출자가 선택한다.
	bool Init() override;
	// IEditorPanel 프레임 인터페이스. 입력 전달과 레이아웃 갱신은 Engine이 조정한다.
	void Tick(float DeltaTime) override;
	// 각 View Texture와 Splitter·Layout·Preset 조작 UI를 ImGui 패널에 그린다.
	void OnRender() override;
	const char* GetPanelName() const override { return "Viewports"; }

    FEditorViewportClient* GetEditorClient(int32 Index) const;
    FGameViewportClient* GetGameViewportClient(int32 Index) const;
    FViewportClient* GetActiveClient(int32 Index) const;
	void SetSlotMaximized(bool bMaximized) { bSlotMaximized = PanelLayout == EViewportPanelLayout::Quad && bMaximized; }
	bool IsSlotMaximized() const { return bSlotMaximized; }
	void SetMaximizedSlotIndex(int32 Index);
	int32 GetMaximizedSlotIndex() const { return MaximizedSlotIndex; }
	void SetSplitRatio(const FViewportSplitRatio& Value);
	const FViewportSplitRatio& GetSplitRatio() const { return SplitRatio; }
	void UpdateLayout(const FRect& Area);
	void ApplySplitterDrag(EViewportSplitAxis Axis, float DeltaPixels, FVector2 WindowSize);
	void UpdateInputSelection(FVector2 WindowMousePosition);
	int32 GetPointerViewIndex(FVector2 WindowMousePosition) const;
	int32 GetActiveViewIndex() const { return ActiveViewIndex; }
	int32 GetEditorViewIndex() const { return bSlotMaximized ? MaximizedSlotIndex : EditorViewIndex; }
	void SetEditorViewIndex(int32 Index) { if (Index >= 0 && Index < GetSlotCount()) EditorViewIndex = Index; }
	int32 GetCapturedViewIndex() const { return CapturedViewIndex; }
	bool IsViewActive(int32 Index) const { const auto* Viewport = GetViewport(Index); return Viewport && Viewport->IsVisible(); }

	FVector2 GetContentSize() const { return {ContentSize.x, ContentSize.y}; }
	FVector2 GetWindowMousePosition() const;
	bool IsHovered() const;

	float ConsumeHorizontalDrag();
	float ConsumeVerticalDrag();
	// UI에서 발생한 Quad 최대화/복귀 요청을 한 번 소비한다.
	bool ConsumeMaximizeRequest(bool& OutMaximized, int32& OutSlotIndex);
	// UI에서 발생한 View별 Camera Preset 요청을 한 번 소비하도록 반환한다.
	bool ConsumeCameraPresetRequest(int32& OutViewIndex, EViewportCameraPreset& OutPreset);

private:
    struct FSlot
    {
        FRect Rect{}; // 해당 창의 client 영역 기준. 배치 좌표의 단일 원본.
        TUniquePtr<FViewport> Viewport;
    };
    TArray<FSlot> Slots; // 생성 시 한 번 크기를 정하며 Slot-Viewport 연결은 바꾸지 않는다.
    const EViewportPanelLayout PanelLayout;
    FWindow* HostWindow = nullptr; // Engine 소유.
	FViewportSplitRatio SplitRatio{};
	int32 CapturedViewIndex = InvalidSlotIndex;
	int32 ActiveViewIndex = InvalidSlotIndex;
	int32 EditorViewIndex = 0;
	static constexpr float MinimumSplitRatio = 0.1f;
    void ComputeQuadLayout(FVector2 WindowSize);
	// 콘솔 stat 명령으로 켜진 항목을 해당 View의 좌상단에 겹쳐 그린다.
	void DrawStatOverlay(ImDrawList* DrawList, const ImVec2& ViewMin) const;

	ImVec2 ContentOrigin{};
	ImVec2 ContentSize{1.0f, 1.0f};
	bool bHovered = false;
	bool bStatResetButtonVisible = false;
	ImVec2 StatResetButtonMin{};
	ImVec2 StatResetButtonMax{};
	float PendingHorizontalDrag = 0.0f;
	float PendingVerticalDrag = 0.0f;
	// 누른 뒤 커서가 Splitter 영역을 벗어나도 버튼을 놓을 때까지 드래그를 유지한다.
	bool bDraggingVerticalSplitter = false;
	bool bDraggingHorizontalSplitter = false;
	bool bSlotMaximized = false;
	int32 MaximizedSlotIndex = 0;
	bool bHasMaximizeRequest = false;
	bool bRequestedMaximized = false;
	int32 RequestedMaximizedSlotIndex = 0;
	int32 PendingCameraPresetViewIndex = InvalidSlotIndex;
	EViewportCameraPreset PendingCameraPreset = EViewportCameraPreset::Perspective;
};
