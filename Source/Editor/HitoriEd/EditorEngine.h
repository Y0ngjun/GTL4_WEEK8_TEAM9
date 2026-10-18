#pragma once

#include "Engine/Engine.h"
#include "Core/Window.h"
#include "Core/Types.h"

#include "Render/Renderer.h"
#include "Render/RenderDevice.h"
#include "Render/Swapchain.h"
#include "Editor/EditorUI/ImGuiRenderer.h"
#include "Editor/Rendering/GridRenderer.h"
#include "Editor/Gizmo/GizmoRenderer.h"
#include "Render/LineBatcher.h"
#include "Render/FogRenderer.h"
#include "Render/FXAARenderer.h"
#include "Render/GammaEncodeRenderer.h"
#include "Render/DepthViewRenderer.h"
#include "Engine/GameViewportClient.h"
#include "Render/SceneRenderer.h"


#include "Editor/EditorUI/EditorUI.h"
#include "Editor/OutputLog/OutputLogPanel.h"
#include "Editor/Details/DetailsPanel.h"
#include "Editor/EditorControls/EditorControlsPanel.h"
#include "Editor/Settings/SettingsPanel.h"
#include "Editor/Viewports/ViewportsPanel.h"
#include "Editor/ContentDrawer/ContentDrawerPanel.h"

#include "Editor/Rendering/Outline.h"
#include "Editor/Rendering/OutLineRenderer.h"
#include "Editor/Outliner/OutlinerPanel.h"

#include "Render/SkyboxRenderer.h"

#include "PlayInEditorDataTypes.h"
#include "Core/Misc/Optional.h"

//Temp
#include "Text/Font.h"
#include "Text/TextRenderer.h"

class UEditorEngine : public UEngine
{
	DECLARE_CLASS(UEditorEngine, UEngine)

public:
	FEngineConfig GetConfig() const override;
	bool Init() override;
	void Tick(float DeltaTime) override;
	void PreExit() override;

	// Active View의 입력과 Picking 결과만 Gizmo 및 선택 상태에 반영한다.
	void UpdateGizmoAndPicking();
	// View 하나의 Scene·Grid·Gizmo·텍스트를 해당 ViewProjection으로 렌더한다.
	// 씬은 SceneRenderingInfo(Linear float)에 그리고, 인코딩한 결과와 에디터 표시는 ViewRenderingInfo(sRGB)에 그린다.
	void RenderFrame(FViewport& Viewport, UWorld& ViewWorld, const FSceneView& View, FEditorViewportClient* OverlayClient);
	// 네 View 결과와 ImGui를 메인 Swapchain 백버퍼에 합성한다. Present는 FEngineLoop가 한다.
	void PresentFrame();
	void DeleteActor(AActor* Actor);
	void DeleteComponent(UActorComponent* Component);

	//Play 버튼을 눌렀을때 Play Session 실행을 요청한다.
	inline void RequestPlaySession() { bPlaySessionRequested = true; }
	inline void RequestEndPlayMap() { bRequestEndPlayMapQueued = true; bIsPaused = false; }

	//실제 PIE 를 실행
	void StartPlayInEditorSession();
	UWorld* CreatePIEWorldByDuplication(FWorldContext& PIEContext, UWorld* InEditorWorld);

	void OnActiveWorldChanged();

	void EndPlayMap();

	bool IsPlaying() const { return PlayWorld != nullptr; }

	////FEditorDelegates::PrePIEEnded / EndPIE.Broadcast()
	//void TeardownPlaySession(const FWorldContext& PIEContext);            // 액터 EndPlay, 월드 정리(CleanupWorld)
	//void RestoreEditorWorld(UWorld* EditorWorld);            // GWorld를 에디터 월드로 복구
	//void DestroyWorldContext(UWorld* PlayWorld);

private:
	// 이번 프레임 DeltaTime을 패널에 전달하고 에디터 단축키를 처리한다.
	void BeginFrame(float DeltaTime);
	// 패널 요청과 입력을 Panel과 클라이언트에 전달해 레이아웃·카메라 상태를 갱신한다.
	void UpdateEditorViewportState(float DeltaTime);
	// 월드를 정확히 한 번 Tick·Capture한 뒤 에디터 상호작용을 갱신한다.
	void TickWorldAndEditor(float DeltaTime);
	// 한 번 캡처한 월드 결과를 재사용해 현재 레이아웃의 각 View를 렌더한다.
	void RenderViewports();
	// 현재 표시 슬롯 또는 네이티브 창 포커스로 게임 카메라 입력을 켜고 끈다.
	void UpdatePIEInput();
	FGameViewportClient* GetPIEGameViewportClient() const;

	// 별도 PIE 창(New Editor Window)을 만들고 정리한다.
	bool CreatePIEWindow();
	void DestroyPIEWindow();
	// 현재 OS 표시 상태와 Single Panel 크기를 World Tick 전에 갱신한다. GPU 자원은 만들지 않는다.
	void UpdatePIEWindowState();
	// 렌더 단계에서 표시 가능한 창의 Swapchain 크기만 갱신한다.
	bool UpdatePIEWindowTargets();
	// PIE 창의 오프스크린 결과를 백버퍼로 복사한다. Present는 EndFrame에서 한다.
	void CopyPIEWindowToBackbuffer();
	bool IsPIEInWindow() const { return PIEWindow != nullptr; }
	// 화면 합성과 View 설정 보관으로 프레임을 마무리한다.
	void EndFrame();

	//Directional Light 기준 깊이 Pass. 렌더 큐 정렬 뒤에 호출한다.
	void RenderShadowPass(FViewport& Viewport, UWorld& ViewWorld, const FRenderingInfo& ViewRenderingInfo, const FSceneView& View, const FDirectionalLightInfo& DirectionalLight);
	//불투명 물체에 대한 Pass
	void RenderOpaquePass(FViewport& Viewport, const FRenderingInfo& ViewRenderingInfo, const FSceneView& View, const FDirectionalLightInfo& DirectionalLight);
	//안개에 대한 Pass
	void RenderFogPass(UWorld& ViewWorld, const FRenderingInfo& ViewRenderingInfo, const FVector& ViewCameraLocation,const FMatrix& ViewProjection, FRenderQueue& RenderQueue);
	//Overlay Pass
	void RenderOverlayPass(FEditorViewportClient* OverlayClient, UWorld& ViewWorld, const FRenderingInfo& ViewRenderingInfo, const FSceneView& View, FRenderQueue& RenderQueue);
	//Gamma Encode Pass: Linear 씬 컬러를 톤매핑하고 화면 표시용 sRGB로 인코딩한다
	void RenderGammaEncodePass(const FRenderingInfo& SceneRenderingInfo, const FRenderingInfo& ViewRenderingInfo, bool bEncode, bool bToneMap, float ExposureEV);
	//FXAA Pass
	void RenderFXAAPass(FTexture2D* FxaaTarget, const FRenderingInfo& ViewRenderingInfo);
	//Depth Pass
	void RenderDepthPass(const FRenderingInfo& ViewRenderingInfo, const FMatrix& ViewProjection, const FVector& ViewCameraLocation, const FVector& ViewCameraForward);



	FWorldContext& GetEditorWorldContext();
	FWorldContext* GetPIEWorldContext(int32 WorldPIEInstance = 0);
	UWorld* GetActiveWorld() const { return PlayWorld ? PlayWorld : EditorWorld; }
	// PIE 중이고 일시정지가 아닌 View는 Gizmo·Picking 등 에디터 상호작용을 하지 않는다.
	bool IsPlayingView(int32 ViewIndex) const { return ViewportsPanel->GetGameViewportClient(ViewIndex) && !bIsPaused; }

	// FEngineLoop 소유. OnInit에서 받아 둔다.
	FWindow* MainWindow = nullptr;
	FSwapchain* MainWindowSC = nullptr;
	FRenderer* Renderer = nullptr;

	TUniquePtr<FEditorUI> EditorUI;

	TUniquePtr<FImGuiRenderer> ImGuiRenderer;
	TUniquePtr<FGridRenderer> GridRenderer;
	TUniquePtr<FGizmoRenderer> GizmoRenderer;
	TUniquePtr<FTextRenderer> TextRenderer;
	TUniquePtr<FFogRenderer> FogRenderer;
	TUniquePtr<FLineBatcher> LineBatcher;
	TUniquePtr<FGizmo> Gizmo;
	TUniquePtr<FOutline> Outline;
	TUniquePtr<FOutlineRenderer> OutlineRenderer;
	TUniquePtr<FSkyboxRenderer> SkyboxRenderer;
	TUniquePtr<FFXAARenderer> FXAARenderer;
	TUniquePtr<FGammaEncodeRenderer> GammaEncodeRenderer;
	TUniquePtr<FDepthViewRenderer> DepthViewRenderer;

	UFont* SystemFont;

	FOutputLogPanel* OutputLogPanel = nullptr;

	FDetailsPanel* DetailsPanel = nullptr;
	FEditorControlsPanel* EditorControlsPanel = nullptr;
	FSettingsPanel* SettingsPanel = nullptr;
	FViewportsPanel* ViewportsPanel = nullptr;
	FSceneRenderer SceneRenderer;
	FOutlinerPanel* OutlinerPanel = nullptr;
	FContentDrawerPanel* ContentDrawerPanel = nullptr;

	UWorld* EditorWorld = nullptr;
	UWorld* PlayWorld = nullptr;

	// Play 버튼이 다음 세션을 띄울 위치
	EPlayModeType PlayMode = EPlayModeType::InViewport;

	// New Editor Window(PIE) 모드에서만 존재한다. Swapchain이 창 핸들을 쓰므로 창보다 먼저 해제한다.
	TUniquePtr<FWindow> PIEWindow;
	TUniquePtr<FSwapchain> PIESwapchain;
	TUniquePtr<FViewportsPanel> PIEPanel;
	FViewport* PIEViewport = nullptr; // 현재 PIE 세션의 화면. Panel 소유이며 종료 전에 해제한다.
	bool bPIEWindowRendered = false;

	FVector UUIDLocation;

	FEditorCommand Save;

	FEditorCommand Play;
	FEditorCommand Pause;
	FEditorCommand Resume;
	FEditorCommand StepFrame;
	FEditorCommand Stop;

	bool bPlaySessionRequested = false;
	bool bRequestEndPlayMapQueued = false;
	bool bStepRequested = false;
	bool bIsPaused = false;

	void ResetSceneSelection();

	void CreateNewScene();
	void OpenScene();
	void SaveCurrentScene();
	void SaveSceneAs();
};
