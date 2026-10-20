#include "EnginePCH.h"

#include "Editor/HitoriEd/EditorEngine.h"

#include "Core/EngineStatics.h"
#include "Core/EngineTimer.h"
#include "Launch/LaunchEngineLoop.h"
#include "Core/StatOverlay.h"
#include "Input/InputSystem.h"


#include "Render/GeometryGenerator.h"

#include "Engine/World.h"
#include "Component/BillboardComponent.h"
#include "Core/Stats/EditorStats.h"
#include "Engine/Level.h"

#include "Render/Renderer.h"

#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"

#include "Asset/AssetManager.h"
#include "Render/RenderResourceManager.h"

#include "Render/RenderCommand.h"
#include "Render/ColorWorkflow.h"
#include "Editor/Outliner/OutlinerPanel.h"
#include "Editor/HitoriEd/EditorFileUtils.h"
#include "Editor/HitoriEd/EditorResourcePaths.h"
#include "UObject/UObjectIterator.h"
#include "Component/SpotLightComponent.h"
#include "Component/DirectionalLightComponent.h"
#include "Core/ResourcePaths.h"

#include "Core/EngineLog.h"
#include "Core/Stats/LightweightStats.h"

namespace
{
	DECLARE_CYCLE_STAT("Viewport Update", STAT_ViewportUpdate);
	DECLARE_CYCLE_STAT("World Tick", STAT_WorldTick);
	DECLARE_CYCLE_STAT("Editor Tick", STAT_EditorTick);
	DECLARE_CYCLE_STAT("ImGui", STAT_ImGui);
}

#include "Core/SplashScreen.h"

FEngineConfig UEditorEngine::GetConfig() const
{
	FEngineConfig Desc;
	Desc.Title = L"Hitori Engine";
	Desc.Width = 1920;
	Desc.Height = 1080;
	Desc.bBorderless = false;
	Desc.SyncInterval = 0;
	// View는 각자 깊이 버퍼를 쓰고 백버퍼에는 ImGui만 그린다.
	Desc.bCreateDepthBuffer = false;
	Desc.bExitOnEscape = false;
	// 파일이 없으면 검은 배경에 상태 텍스트만 표시된다.
	Desc.SplashImage = ResourcePaths::SplashImage;
	return Desc;
}

// 렌더 자원·월드와 편집 Viewport Panel을 초기화한다.
// Device·Window·Swapchain·AssetManager는 FEngineLoop가 먼저 만들어 둔다.
bool UEditorEngine::Init()
{
	if (!Super::Init())
		return false;

	FWorldContext& InitialWorldContext = CreateNewWorldContext(EWorldType::Editor);
	InitialWorldContext.SetCurrentWorld(UWorld::CreateWorld(EWorldType::Editor, true));
	EditorWorld = InitialWorldContext.World();

	MainWindow = GetEngineLoop().GetMainWindow();
	MainWindowSC = GetEngineLoop().GetSwapchain();
	Renderer = GetEngineLoop().GetRenderer();
	FRenderDevice* RenderDevice = GetEngineLoop().GetRenderDevice();

	EditorUI = MakeUnique<FEditorUI>();
	EditorUI->Init();

	EditorUI->SetNewSceneCallback([this]() { CreateNewScene(); });
	EditorUI->SetOpenSceneCallback([this]() { OpenScene(); });
	EditorUI->SetSaveSceneCallback([this]() { SaveCurrentScene(); });
	EditorUI->SetSaveSceneAsCallback([this]() { SaveSceneAs(); });

	OutputLogPanel = EditorUI->AddEditorPanel<FOutputLogPanel>();
	FLog::AddSink(OutputLogPanel);
	HTR_LOG(Info, "Editor Initialize...");

	HTR_LOG(Info, "Initialize ImGui...");
	ImGuiRenderer = MakeUnique<FImGuiRenderer>();
	if (!ImGuiRenderer->Init(MainWindow->GetHandle(), RenderDevice->GetDevice(), RenderDevice->GetContext()))
	{
		HTR_LOG(Error, "Failed To Initialize ImGui!");
	}
	HTR_LOG(Info, "Initialize ImGui Success!");

	GridRenderer = MakeUnique<FGridRenderer>();
	GridRenderer->Init(Renderer);

	GizmoRenderer = MakeUnique<FGizmoRenderer>();
	GizmoRenderer->Init(Renderer);

	Gizmo = MakeUnique<FGizmo>();

	FogRenderer = MakeUnique<FFogRenderer>();
	FogRenderer->Init(Renderer);

	FXAARenderer = MakeUnique<FFXAARenderer>();
	FXAARenderer->Init();

	GammaEncodeRenderer = MakeUnique<FGammaEncodeRenderer>();
	GammaEncodeRenderer->Init();

	DepthViewRenderer = MakeUnique<FDepthViewRenderer>();
	DepthViewRenderer->Init();


	// 필요한 Panel들 추가후 raw pointer 반환(소유권 = EditorUI)
	DetailsPanel = EditorUI->AddEditorPanel<FDetailsPanel>();
	EditorControlsPanel = EditorUI->AddEditorPanel<FEditorControlsPanel>();
	ViewportsPanel = EditorUI->AddEditorPanel<FViewportsPanel>();
	ViewportsPanel->SetHostWindow(MainWindow);
	ContentDrawerPanel = EditorUI->AddEditorPanel<FContentDrawerPanel>();

	// OutLine
	OutlineRenderer = MakeUnique<FOutlineRenderer>();
	OutlineRenderer->Init(Renderer);

	SettingsPanel = EditorUI->AddEditorPanel<FSettingsPanel>();
	DebugPanel = EditorUI->AddEditorPanel<FDebugPanel>();

	//ToolbarPanel->SetPlayCallback([this]() { RequestPlaySession(); });
	//ToolbarPanel->SetStopCallback([this]() { RequestEndPlayMap(); });
	//ToolbarPanel->SetIsPlayingQuery([this]() { return PlayWorld != nullptr; });

	Outline = MakeUnique<FOutline>();

	SystemFont = UAssetManager::GetAssetByPath<UFont>(ResourcePaths::DefaultFont);

	TextRenderer = MakeUnique<FTextRenderer>();
	TextRenderer->Init();

	// 투영 행렬 생성 
	UCameraComponent* MainCamera = GetActiveWorld()->GetMainCamera() ? GetActiveWorld()->GetMainCamera()->GetCameraComponent() : nullptr;
	assert(MainCamera != nullptr);
	for (int32 Index = 0; Index < ViewportsPanel->GetSlotCount(); ++Index)
    {
        auto Client = MakeUnique<FEditorViewportClient>();
        Client->SetWorld(EditorWorld);
        Client->InitializeCamera(*MainCamera, Index, ViewportsPanel->GetViewport(Index)->GetOutputSize());
        ViewportsPanel->GetViewport(Index)->SetEditorClient(std::move(Client));
    }
	// 화면 나눔 비율 설정 가져오기
	ViewportsPanel->SetSplitRatio({ SettingsPanel->GetSettings().ViewportSplitHorizontal, SettingsPanel->GetSettings().ViewportSplitVertical });
	// SingleView에 사용할 인덱스 설정
	ViewportsPanel->SetMaximizedSlotIndex(SettingsPanel->GetSettings().MaximizedViewportIndex);
	// 뷰포트 레이아웃 설정
	ViewportsPanel->SetSlotMaximized(SettingsPanel->GetSettings().bViewportMaximized);
	GetActiveWorld()->GetMainCamera()->GetCameraComponent()->SetExternalInputManaged(true);

	/// 삭제 예정
	//SceneManager = EditorUI->AddEditorPanel<FSceneManager>();
	//SceneManager->SetWorld(World);

	OutlinerPanel = EditorUI->AddEditorPanel<FOutlinerPanel>();
	OutlinerPanel->SetWorld(GetActiveWorld());
	OutlinerPanel->SetSelectionCallback(
		[this](USceneComponent* Root)
		{
			Gizmo->SetTarget(Root, true);
			DetailsPanel->SetTarget(Root ? Root->GetOwner() : nullptr);

			UPrimitiveComponent* Primitive = Cast<UPrimitiveComponent>(Root);
			if (!Primitive && Root && Root->GetOwner())
			{
				for (UActorComponent* C : Root->GetOwner()->GetComponents())
					if ((Primitive = Cast<UPrimitiveComponent>(C))) break;
			}
			Outline->SetTarget(Primitive);
		});
	DetailsPanel->SetSelectionCallback(
		[this](UActorComponent* Component)
		{
			if (USceneComponent* SceneComponent = Cast<USceneComponent>(Component))
			{
				AActor* Owner = SceneComponent->GetOwner();
				if (Owner && SceneComponent == Owner->GetRootComponent())
				{
					Gizmo->SetTarget(SceneComponent, true);
				}
				else
				{
					Gizmo->SetTarget(SceneComponent);
				}
			}
			else
			{
				Gizmo->SetTarget(nullptr);
			}
		}
	);

	OutlinerPanel->SetDeleteActorCallback(
		[this](AActor* Actor)
		{
			DeleteActor(Actor);
		}
	);
	DetailsPanel->SetDeleteComponentCallback(
		[this](UActorComponent* Component)
		{
			DeleteComponent(Component);
		}
	);

	LineBatcher = MakeUnique<FLineBatcher>();
	LineBatcher->Init(Renderer, GetActiveWorld());

	DetailsPanel->SetWorld(GetActiveWorld());

	EditorControlsPanel->SetWorld(GetActiveWorld());
	EditorControlsPanel->SetIsPlayingQuery([this]() { return IsPlaying(); });
	EditorControlsPanel->SetGizmo(Gizmo.get());
	EditorControlsPanel->SetViewportsPanel(ViewportsPanel);

	SettingsPanel->SetWorld(GetActiveWorld());
	SettingsPanel->SetViewportsPanel(ViewportsPanel);
    DetailsPanel->SetShadowPreviewQuery([this](ULightComponent* Light, FString& PreviewSource) -> const FShadowResources*
    {
        UWorld* PreviewWorld = Light->GetOwner() ? Light->GetOwner()->GetWorld() : nullptr;
        if (!PreviewWorld) return nullptr;
        if (PreviewWorld->IsPlayInEditor())
        {
            PreviewSource = "Shadow preview is available for the editor world only.";
            return nullptr;
        }
        if (Cast<UDirectionalLightComponent>(Light))
        {
            const int32 SelectedIndex = ViewportsPanel->GetActiveViewIndex();
            const FViewport* Selected = ViewportsPanel->GetViewport(SelectedIndex);
            if (!Selected)
            {
                PreviewSource = "Select a viewport to preview its directional shadow.";
                return nullptr;
            }
            PreviewSource = "Viewport " + std::to_string(SelectedIndex + 1);
            if (Selected->GetGameClient())
            {
                PreviewSource += ": select an editor viewport for shadow preview.";
                return nullptr;
            }
            PreviewSource += " (Editor)";
            if (!Selected->IsVisible())
            {
                PreviewSource += ": hidden.";
                return nullptr;
            }
            if (!Selected->GetActiveClient() || Selected->GetActiveClient()->GetWorld() != PreviewWorld)
            {
                PreviewSource += ": this light belongs to a different world.";
                return nullptr;
            }
            const FShadowResources& Resources = Selected->GetDirectionalShadow();
            if (!Resources.bDepthReady || Resources.RenderedFrame != SceneRenderer.GetFrameNumber())
            {
                PreviewSource += ": no shadow result for the current frame.";
                return nullptr;
            }
            return &Resources;
        }
        PreviewSource = "World shadow (shared by viewports).";
        const FWorldContext* Context = GetWorldContextFromWorld(PreviewWorld);
        if (!Context) return nullptr;
        const FShadowResources* Resources = Renderer->GetShadowResources(Context->ShadowResourceIndex);
        return Resources && Resources->bDepthReady && Resources->RenderedFrame == SceneRenderer.GetFrameNumber() ? Resources : nullptr;
    });
	// Debug 패널은 Settings가 소유한 그림자 설정을 직접 고치고, World별 결과를 읽는다.
	DebugPanel->SetShadowCullingSettings(&SettingsPanel->GetMutableSettings().ShadowCulling);
	DebugPanel->SetHasPlayWorldQuery([this]() { return PlayWorld != nullptr; });
	DebugPanel->SetDirectionalStatsQuery([this]() -> const FShadowCullingStats*
	{
		const FViewport* Selected = ViewportsPanel->GetViewport(ViewportsPanel->GetActiveViewIndex());
		if (!Selected || !Selected->IsVisible() || Selected->GetGameClient()) return nullptr;
		const FShadowResources& Resources = Selected->GetDirectionalShadow();
		return Resources.RenderedFrame == SceneRenderer.GetFrameNumber() && Resources.bDepthReady ? &Resources.CullingStats : nullptr;
	});
	DebugPanel->SetShadowStatsQuery([this](bool bPlayWorld) -> const FShadowCullingStats*
	{
		UWorld* StatsWorld = bPlayWorld ? PlayWorld : EditorWorld;
		const FWorldContext* Context = StatsWorld ? GetWorldContextFromWorld(StatsWorld) : nullptr;
		return Context ? SceneRenderer.GetShadowCullingStats(Context->ShadowResourceIndex) : nullptr;
	});
	SettingsPanel->SetHasLitViewQuery([this]()
	{
		for (int32 Index = 0; Index < ViewportsPanel->GetSlotCount(); ++Index)
			if (ViewportsPanel->IsViewActive(Index) && ViewportsPanel->GetActiveClient(Index)->GetViewRenderMode().ViewMode == EViewMode::Lit)
				return true;
		const FGameViewportClient* GameClient = GetPIEGameViewportClient();
		return IsPIEInWindow() && GameClient && GameClient->HasCamera() && PIEViewport->IsVisible() && GameClient->GetViewRenderMode().ViewMode == EViewMode::Lit;
	});

	SkyboxRenderer = MakeUnique<FSkyboxRenderer>();
	SkyboxRenderer->Init(ResourcePaths::DefaultSkyPanorama);

	const ImVec4 White = { 1, 1, 1, 1 };
	const ImVec4 Green = { 0.40f, 0.80f, 0.30f, 1.0f };

	Save = { "Save",   "Save Current Scene",UAssetManager::Get().LoadTexture(EditorResourcePaths::ToolbarSaveIcon),  White, { EKeyCode::S, true } };

	Play = { "Play",   "Play World",        UAssetManager::Get().LoadTexture(EditorResourcePaths::ToolbarPlayIcon),  Green, { EKeyCode::P, false, true } };

	Play.CanExecute = [this]()->bool {return !PlayWorld; };
	Play.Execute = [this]() { RequestPlaySession(); };

	Pause = { "Pause",  "Pause",			UAssetManager::Get().LoadTexture(EditorResourcePaths::ToolbarPauseIcon), White, { EKeyCode::Pause } };

	Pause.CanExecute = [this]()->bool {return !bIsPaused && PlayWorld; };
	Pause.Execute = [this]() { bIsPaused = true; };


	Resume = { "Resume", "Resume",          UAssetManager::Get().LoadTexture(EditorResourcePaths::ToolbarPlayIcon),  White, { EKeyCode::Pause } };

	Resume.CanExecute = [this]()->bool {return bIsPaused && PlayWorld; };
	Resume.Execute = [this]() { bIsPaused = false; };

	StepFrame = { "Step", "Step One Frame", UAssetManager::Get().LoadTexture(EditorResourcePaths::ToolbarStepIcon),  White, {} };

	StepFrame.CanExecute = [this]()->bool {return bIsPaused && PlayWorld; };
	StepFrame.Execute = [this]() { bStepRequested= true; };


	Stop = { "Stop",   "Stop",				UAssetManager::Get().LoadTexture(EditorResourcePaths::ToolbarStopIcon),  White, { EKeyCode::Escape } };

	Stop.CanExecute = [this]()->bool {return PlayWorld; };
	Stop.Execute = [this]() { RequestEndPlayMap(); };

	EditorUI->GetToolbar()->SetCommands({ Play, Pause, Resume, StepFrame, Stop });
	EditorUI->GetToolbar()->SetPlayModeBinding(
		[this]() { return PlayMode; },
		[this](EPlayModeType Mode) { PlayMode = Mode; },
		[this]() { return !IsPlaying(); });

	return true;
}

// 프레임 시작·View 상태·월드 갱신 후 View별 오프스크린 렌더와 UI 합성을 진행한다.
// 입력·창 메시지는 FEngineLoop가 먼저 처리하고 Present는 호출 직후에 한다.
void UEditorEngine::Tick(const float DeltaTime)
{
	// PIE 창의 닫기 버튼은 앱이 아니라 PIE 세션만 끝낸다.
	if (PIEWindow && PIEWindow->ConsumeCloseRequest())
	{
		RequestEndPlayMap();
	}
	if (bRequestEndPlayMapQueued) 
	{ 
		bRequestEndPlayMapQueued = false; 
		EndPlayMap(); 
	}
	if (bPlaySessionRequested)	
	{
		bPlaySessionRequested = false;
		if (!PlayWorld)
		{
			StartPlayInEditorSession();
		}
	}

	BeginFrame(DeltaTime);
	// 임시: 선택된 Actor를 복제해서 결과를 로그로 확인 (PIE 연결 후 삭제)
	if (!IsPlaying() && !ImGui::GetIO().WantTextInput && FInputSystem::IsKeyPressed(EKeyCode::F9))
	{
		UWorld* PIEWorld = UWorld::CreateWorld(EWorldType::PIE, false);
		FObjectDuplicationParameters Params(EditorWorld, nullptr);
		Params.DuplicationSeed.Add(EditorWorld, PIEWorld);
		Params.DuplicationSeed.Add(EditorWorld->GetPersistentLevel(), PIEWorld->GetPersistentLevel());
		Params.PortFlags = EPropertyPortFlags::PPF_DuplicateForPIE;
		StaticDuplicateObjectEx(Params);

		HTR_LOG(Info, "[DupTest] actors {} -> {}", EditorWorld->GetPersistentLevel()->GetActors().Num(), PIEWorld->GetPersistentLevel()->GetActors().Num());
		for (AActor* A : PIEWorld->GetPersistentLevel()->GetActors())
			HTR_LOG(Info, "[DupTest]   {} world={} level={} root owner ok={}", A->GetName(), A->GetWorld() == PIEWorld, A->GetLevel() == PIEWorld->GetPersistentLevel(), A->GetRootComponent() && A->GetRootComponent()->GetOwner() == A);
	}
	UpdateEditorViewportState(DeltaTime);
    UpdatePIEWindowState();
    UpdatePIEInput(); // 현재 표시/입력 대상과 포커스를 이번 World Tick 전에 반영한다.
	TickWorldAndEditor(DeltaTime);
	RenderViewports();
	EndFrame();
}

// DeltaTime을 패널에 전달하고 에디터 단축키를 처리한다.
void UEditorEngine::BeginFrame(const float DeltaTime)
{
	FStatOverlay::Tick(DeltaTime);
	EditorControlsPanel->FEditorControlsPanel::DeltaTime = DeltaTime;

	if (!ImGui::GetIO().WantTextInput && FInputSystem::IsKeyPressed(EKeyCode::Delete))
		DeleteActor(OutlinerPanel->GetSelectedActor());
}

// Panel의 선택·레이아웃을 갱신한 뒤 선택 클라이언트에 카메라 입력을 전달한다.
void UEditorEngine::UpdateEditorViewportState(const float DeltaTime)
{
	SCOPE_CYCLE_COUNTER(STAT_ViewportUpdate);

	const FVector2 ViewportSize = ViewportsPanel->GetContentSize();
	const FVector2 WindowMousePosition = ViewportsPanel->GetWindowMousePosition();

	bool bRequestedMaximized = false;
	int32 RequestedMaximizedSlotIndex = ViewportsPanel->GetMaximizedSlotIndex();
	if (ViewportsPanel->ConsumeMaximizeRequest(bRequestedMaximized, RequestedMaximizedSlotIndex))
	{
		if (bRequestedMaximized)
			ViewportsPanel->SetMaximizedSlotIndex(RequestedMaximizedSlotIndex);
		ViewportsPanel->SetSlotMaximized(bRequestedMaximized);

		FEditorSettings& Settings = SettingsPanel->GetMutableSettings();
		Settings.bViewportMaximized = bRequestedMaximized;
		Settings.MaximizedViewportIndex = RequestedMaximizedSlotIndex;
	}

	int32 PresetViewIndex = FViewportsPanel::InvalidSlotIndex;
	EViewportCameraPreset RequestedPreset = EViewportCameraPreset::Perspective;
	if (ViewportsPanel->ConsumeCameraPresetRequest(PresetViewIndex, RequestedPreset))
		ViewportsPanel->GetEditorClient(PresetViewIndex)->ApplyCameraPreset(RequestedPreset, ViewportsPanel->GetViewport(PresetViewIndex)->GetOutputSize());

	const float HorizontalDrag = ViewportsPanel->ConsumeHorizontalDrag();
	const float VerticalDrag = ViewportsPanel->ConsumeVerticalDrag();
	if (HorizontalDrag != 0.0f)
		ViewportsPanel->ApplySplitterDrag(EViewportSplitAxis::Horizontal, HorizontalDrag, ViewportSize);
	if (VerticalDrag != 0.0f)
		ViewportsPanel->ApplySplitterDrag(EViewportSplitAxis::Vertical, VerticalDrag, ViewportSize);
	if (HorizontalDrag != 0.0f || VerticalDrag != 0.0f)
	{
		const FViewportSplitRatio Ratio = ViewportsPanel->GetSplitRatio();
		SettingsPanel->GetMutableSettings().ViewportSplitHorizontal = Ratio.Horizontal;
		SettingsPanel->GetMutableSettings().ViewportSplitVertical = Ratio.Vertical;
	}

    ViewportsPanel->UpdateLayout(ViewportsPanel->GetContentRect());
    const bool bEditorInputEnabled = ::GetForegroundWindow() == MainWindow->GetHandle() && !ImGui::GetIO().WantTextInput;
    if (bEditorInputEnabled)
        ViewportsPanel->UpdateInputSelection(WindowMousePosition);
    else
        for (int32 Index = 0; Index < ViewportsPanel->GetSlotCount(); ++Index)
            ViewportsPanel->ReleaseInput(Index);
    const int32 ActiveIndex = ViewportsPanel->GetActiveViewIndex();
    if (ViewportsPanel->IsViewActive(ActiveIndex) && !ViewportsPanel->GetGameViewportClient(ActiveIndex))
    {
        FViewportInput Input;
        Input.MousePosition = WindowMousePosition;
        Input.DeltaTime = DeltaTime;
        Input.WheelDelta = bEditorInputEnabled ? FInputSystem::GetWheelDelta() : 0;
        Input.MoveSpeed = SettingsPanel->GetSettings().CameraSpeed;
        Input.MouseSensitivity = SettingsPanel->GetSettings().MouseSensitivity;
        Input.bEnabled = bEditorInputEnabled;
        ViewportsPanel->RouteInput(ActiveIndex, Input);
    }
	// 겹친 창은 Hover 선택에서 제외하고 우클릭 Capture를 우선한다.
	if (ViewportsPanel->IsHovered() || ViewportsPanel->GetCapturedViewIndex() != FViewportsPanel::InvalidSlotIndex)
		ViewportsPanel->SetEditorViewIndex(ViewportsPanel->GetActiveViewIndex());
}

// 월드를 한 번 Tick·Capture한 뒤 에디터와 피킹을 갱신한다.
void UEditorEngine::TickWorldAndEditor(const float DeltaTime)
{
	// 월드 상태는 프레임마다 정확히 한 번 갱신하고 캡처한다.

	{
		SCOPE_CYCLE_COUNTER(STAT_WorldTick);
		if (bStepRequested && bIsPaused)
		{
			GetActiveWorld()->Tick(1.0f / 60.0f);
			bStepRequested = false;
		}
		else if(!bIsPaused)
			GetActiveWorld()->Tick(DeltaTime);

	}
    // 편집 입력 또는 CameraComponent Tick이 소비하고 남은 휠은 Pause/차단 해제 후 재생하지 않는다.
    (void)FInputSystem::GetWheelDelta();
	{
		SCOPE_CYCLE_COUNTER(STAT_EditorTick);
		EditorUI->Tick(DeltaTime);
	}
	// 라이트 프록시가 아래 UpdateAllTransforms에서 바뀐 작업 흐름으로 다시 계산되도록 먼저 반영한다.
	ColorWorkflow::bLinear = SettingsPanel->GetSettings().bLinearWorkflow;
	// 월드 캡처는 View마다 그릴 월드가 다르므로 RenderViewports에서 한다.
	// PIE 중에도 PIE가 아닌 View에서는 EditorWorld 편집을 계속한다.
	UpdateGizmoAndPicking();
	//SCOPE_CYCLE_COUNTER(STAT_UpdateAllTransforms);
	EditorWorld->GetScene().UpdateAllTransforms();
	if (PlayWorld && bIsPaused)
		PlayWorld->GetScene().UpdateAllTransforms();
}

// View가 그릴 월드가 바뀔 때만 다시 캡처해 활성 View별 렌더 큐를 만들고 렌더한다.
void UEditorEngine::RenderViewports()
{
    bPIEWindowRendered = false;
    const bool bRenderPIEWindow = IsPIEInWindow() && UpdatePIEWindowTargets();
    SceneRenderer.BeginFrame();
    SceneRenderer.SetShadowCullingSettings(SettingsPanel->GetSettings().ShadowCulling);
    // 쉐도우 맵은 World의 첫 View를 그릴 때 준비되므로, 그 전에 그림자를 쓰는 View를 모두 등록한다.
    for (int32 ViewIndex = 0; ViewIndex < ViewportsPanel->GetSlotCount(); ++ViewIndex)
    {
        FViewport& Viewport = *ViewportsPanel->GetViewport(ViewIndex);
        FViewportClient* Client = Viewport.GetActiveClient();
        if (!Viewport.IsVisible() || !Client || !Client->HasCamera())
            continue;
        const FSceneView View = Viewport.CreateSceneView();
        if (ViewSupportsShadows(View))
            SceneRenderer.AddShadowView(*Client->GetWorld(), View);
    }
    if (bRenderPIEWindow && PIEViewport->GetActiveClient()->HasCamera())
    {
        const FSceneView View = PIEViewport->CreateSceneView();
        if (ViewSupportsShadows(View))
            SceneRenderer.AddShadowView(*PIEViewport->GetActiveClient()->GetWorld(), View);
    }
    for (int32 ViewIndex = 0; ViewIndex < ViewportsPanel->GetSlotCount(); ++ViewIndex)
    {
        FViewport& Viewport = *ViewportsPanel->GetViewport(ViewIndex);
        FViewportClient* Client = Viewport.GetActiveClient();
        Viewport.ClearSceneData();
        if (!Viewport.IsVisible() || !Client || !Client->HasCamera())
            continue;
        Viewport.EnsureRenderTargets();
        UWorld* ViewWorld = Client->GetWorld();
        const FSceneView View = Viewport.CreateSceneView();
        FSceneRenderData& Data = Viewport.GetSceneRenderData();
        SceneRenderer.PrepareView(*ViewWorld, View, Data);
        // 파티클/빌보드 상수가 다음 수집으로 바뀌기 전에 이 화면을 소비한다.
        RenderFrame(Viewport, *ViewWorld, View, IsPlayingView(ViewIndex) ? nullptr : ViewportsPanel->GetEditorClient(ViewIndex));
        Data.ReleaseSceneReferences();
    }
    if (bRenderPIEWindow && PIEViewport->GetActiveClient()->HasCamera())
    {
        FViewportClient* Client = PIEViewport->GetActiveClient();
        PIEViewport->EnsureRenderTargets();
        const FSceneView View = PIEViewport->CreateSceneView();
        FSceneRenderData& Data = PIEViewport->GetSceneRenderData();
        SceneRenderer.PrepareView(*Client->GetWorld(), View, Data);
        RenderFrame(*PIEViewport, *Client->GetWorld(), View, nullptr);
        Data.ReleaseSceneReferences();
        CopyPIEWindowToBackbuffer();
    }
    else if (PIEPanel)
        PIEViewport->ClearSceneData();
    SceneRenderer.EndFrame();
}

void UEditorEngine::UpdatePIEInput()
{
    if (!PIEViewport)
        return;
    FViewportInput Input;
    if (IsPIEInWindow())
    {
        Input.MousePosition = PIEPanel->GetWindowMousePosition();
        Input.bEnabled = ::GetForegroundWindow() == PIEWindow->GetHandle();
        PIEPanel->RouteInput(0, Input);
    }
    else
    {
        Input.MousePosition = ViewportsPanel->GetWindowMousePosition();
        for (int32 Index = 0; Index < ViewportsPanel->GetSlotCount(); ++Index)
        {
            if (ViewportsPanel->GetViewport(Index) != PIEViewport)
                continue;
            Input.bEnabled = Index == ViewportsPanel->GetActiveViewIndex() && ::GetForegroundWindow() == MainWindow->GetHandle() && !ImGui::GetIO().WantTextInput;
            ViewportsPanel->RouteInput(Index, Input);
        }
    }
}

FGameViewportClient* UEditorEngine::GetPIEGameViewportClient() const
{
    return PIEViewport ? static_cast<FGameViewportClient*>(PIEViewport->GetGameClient()) : nullptr;
}

// 화면을 표시하고 UI 변경 후 View 설정을 보관한다.
void UEditorEngine::EndFrame()
{
	PresentFrame();
	// 메인 Swapchain은 FEngineLoop가 VSync 설정대로 Present하므로 PIE 창은 기다리지 않고 올린다.
	if (bPIEWindowRendered && PIESwapchain)
		PIESwapchain->SwapBuffers(0);
	// UI 변경 후 설정을 복사해 종료 시 카메라 수명에 의존하지 않는다.
	SettingsPanel->CaptureViewportSettings();
	// 프로파일러 반영
	FStatRegistry::EndFrame();
}

FWorldContext& UEditorEngine::GetEditorWorldContext()
{
	for (int32 i = 0; i < WorldList.Num(); ++i)
	{
		if (WorldList[i].WorldType == EWorldType::Editor)
		{
			return WorldList[i];
		}
	}

	return CreateNewWorldContext(EWorldType::Editor);
}

FWorldContext* UEditorEngine::GetPIEWorldContext(int32 WorldPIEInstance)
{
	for (FWorldContext& WorldContext : WorldList)
	{
		if (WorldContext.WorldType == EWorldType::PIE)
		{
			return &WorldContext;
		}
	}

	return nullptr;
}

// 입력 View의 Ray와 피킹으로 Gizmo·공유 선택을 갱신한다.
void UEditorEngine::RenderOpaquePass(FViewport& Viewport, const FRenderingInfo& ViewRenderingInfo, const FSceneView& View, FRenderQueue& RenderQueue, const FDirectionalLightInfo& DirectionalLight)
{
	const FMatrix& ViewProjection = View.ViewProjection;
	const FVector ViewCameraLocation = View.GetCameraLocation();
	const FVector ViewCameraForward = View.GetCameraForward();

	// 뷰 모드가 셰이더 permutation을, Wireframe 여부가 Fill Mode를 정한다. 삼각형 연결은 그대로다.
	const FViewRenderMode ViewRenderMode = View.RenderMode;
	const bool bVisualizeLightCount = SettingsPanel->GetSettings().bVisualizeTileLightCount;
    const bool bDrawPrimitives = SettingsPanel->GetSettings().bDrawPrimitives;
    const bool bDepthPrepass = bDrawPrimitives && ViewRenderMode.ViewMode == EViewMode::Lit;
    Renderer->SetViewMode(ViewRenderMode);
    if (bDrawPrimitives)
        Renderer->RenderQueueSorting(RenderQueue, ViewProjection, ViewCameraLocation);
    if (bDepthPrepass)
    {
        FRenderingInfo DepthInfo = ViewRenderingInfo;
        DepthInfo.ColorRenderTargets.Reset();
        RenderCommand::BeginRenderPass(DepthInfo);
        RenderCommand::SetRasterizerState(ERasterizerState::SolidBack);
        RenderCommand::SetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        Renderer->RenderDepthPrepass(ViewProjection);
        RenderCommand::EndRenderPass(DepthInfo);
        Renderer->CullLights(Viewport.GetLightCullingResources(), View.ProjectionMatrix, DepthInfo.DepthStencil.Texture, Viewport.GetSceneRenderData().CullingLights, DepthInfo.ViewportSetting.Width, DepthInfo.ViewportSetting.Height, bVisualizeLightCount);
    }

	// 배경은 화면에서 sRGB 0.3 회색으로 보여야 한다. Linear 작업 흐름에서는 인코딩될 것이므로 디코딩해 지운다.
	FRenderingInfo PassInfo = ViewRenderingInfo;
	const float ClearValue = ColorWorkflow::bLinear ? FLinearColor::SRGBToLinear(0.3f) : 0.3f;
	PassInfo.ColorRenderTargets[0].ClearValue.ColorClearValue = FVector4(ClearValue, ClearValue, ClearValue, 1.0f);
    if (bDepthPrepass) PassInfo.DepthStencil.LoadOp = ERenderTargetLoadOp::Load;
	RenderCommand::BeginRenderPass(PassInfo);

	const ERasterizerState SceneRasterizerState = ViewRenderMode.ViewMode == EViewMode::Wireframe
		? ERasterizerState::Wireframe : ERasterizerState::SolidBack;

	// 렌더 루프 — 반드시 RenderAll보다 먼저
	// 직교는 시선이 모두 평행해 파노라마의 한 점만 확대되므로 하늘을 그리지 않고 지우기 색을 배경으로 쓴다.
	// 데이터 시각화 뷰도 하늘은 데이터가 없으므로 그리지 않는다.
	if (!View.IsOrthographic() && !IsDataVisualization(ViewRenderMode.ViewMode))
	{ 
		SkyboxRenderer->OnRender(ViewProjection, ViewCameraLocation, DirectionalLight);
	}

	if (bDrawPrimitives)
	{
		RenderCommand::SetRasterizerState(SceneRasterizerState);
		RenderCommand::SetBlendState(EBlendState::Opaque);
		RenderCommand::SetDepthStencilState(EDepthStencilState::Default);

		RenderCommand::SetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		// 불투명을 먼저 그리고 반투명을 먼 것부터 그린다.
		// Grid는 깊이를 쓰지 않는 반투명 위에도 겹칠 수 있지만, 언리얼의 에디터 프리미티브처럼 Overlay Pass에서 마지막에 합성한다.
		Renderer->RenderOpaque(ViewProjection, bDepthPrepass);
		Renderer->RenderTranslucent(ViewProjection);
		// 장면 Wireframe이 Grid·Gizmo·UI로 전파되지 않도록 복원한다.
		RenderCommand::SetRasterizerState(ERasterizerState::SolidBack);
	}


	RenderCommand::EndRenderPass(PassInfo);
    RenderCommand::UnbindShaderResources(8, 4, EShaderBindFlagBits::Pixel);
    RenderCommand::UnbindShaderResources(8, 2, EShaderBindFlagBits::Vertex);
}

void UEditorEngine::RenderFogPass(UWorld& ViewWorld, const FRenderingInfo& ViewRenderingInfo, const FVector& ViewCameraLocation, const FMatrix& ViewProjection, FRenderQueue& RenderQueue)
{

	const TArray<FExponentialHeightFogSceneInfo>& SceneFogInfos = ViewWorld.GetScene().FogInfos;

	if (SceneFogInfos.Num() == 0)
		return;

	FRenderingInfo PassInfo = ViewRenderingInfo;

	//Load는 기존 내용을 그대로 두고 그 위에 그림을 그린다는뜻
	for (FRenderingDesc& Color : PassInfo.ColorRenderTargets)
		Color.LoadOp = ERenderTargetLoadOp::Load;

	PassInfo.DepthStencil.Texture = nullptr;
	RenderCommand::BeginRenderPass(PassInfo);

	FogRenderer->OnRender(ViewRenderingInfo.DepthStencil.Texture, ViewProjection, ViewCameraLocation, SceneFogInfos[0].FogInfo);

	//Fog는 여러개있더라도 1개의 Fog만 인식해서 그리도록 해야한다.

	RenderCommand::EndRenderPass(PassInfo);
}

void UEditorEngine::RenderOverlayPass(FEditorViewportClient* OverlayClient, UWorld& ViewWorld, const FRenderingInfo& ViewRenderingInfo, const FSceneView& View, FRenderQueue& RenderQueue)
{
	const FMatrix& ViewProjection = View.ViewProjection;
	const FVector ViewCameraLocation = View.GetCameraLocation();
	const FVector ViewCameraForward = View.GetCameraForward();
	// 실행 중인 PIE View에는 Grid·Gizmo·Outline 같은 에디터 표시를 그리지 않는다.
	// 별도 PIE 창은 편집 입력이 없으므로 일시정지 중에도 게임 화면만 보인다.
	const bool bShowEditorOverlay = OverlayClient != nullptr;
	FRenderingInfo PassInfo = ViewRenderingInfo;

	for (FRenderingDesc& Color : PassInfo.ColorRenderTargets)
		Color.LoadOp = ERenderTargetLoadOp::Load;

	PassInfo.DepthStencil.LoadOp = ERenderTargetLoadOp::Load;


	RenderCommand::BeginRenderPass(PassInfo);

	const FEditorSettings& Settings = SettingsPanel->GetSettings();

	// 라인 배처는 매 프레임 한 번만 비우고 한 번만 그린다.
	// 라이트 디버그 라인은 바운딩 박스 표시 여부와 무관하게 항상 그린다.
	if (bShowEditorOverlay && !Settings.bDepthView)
	{
		LineBatcher->BeginFrame();
		if (Settings.bDrawBoundingBox)
		{
			LineBatcher->BuildVertexBuffer(ViewWorld);
		}
		for (TObjectIterator<UDirectionalLightComponent> DirLight; DirLight; ++DirLight)
		{
			if (DirLight && DirLight->GetOwner() && DirLight->GetOwner()->GetWorld() == &ViewWorld)
			{
				DirLight->DrawDebug(LineBatcher.get());
			}
		}
		for (TObjectIterator<USpotLightComponent> SpotLight; SpotLight; ++SpotLight)
		{
			if (SpotLight && SpotLight->GetOwner() && SpotLight->GetOwner()->GetWorld() == &ViewWorld)
			{
				SpotLight->DrawDebug(LineBatcher.get());
			}
		}
		LineBatcher->OnRender(ViewProjection);
	}



	// Grid는 후처리(안개·FXAA)가 끝난 뒤 합성하는 에디터 표시라 안개를 적용하지 않는다.
	// 원근은 픽셀 셰이더 Grid, 직교는 선 Grid를 쓴다 (언리얼 에디터와 같은 구분).
	if (bShowEditorOverlay && Settings.bDrawGrid)
	{
		const EGridPlane GridPlane = OverlayClient->GetGridPlane(View);

		if (!View.IsOrthographic())
		{
			GridRenderer->OnRenderPSGrid(ViewProjection, ViewCameraLocation, Settings, PassInfo.ViewportSetting);
		}
		else
		{
			GridRenderer->OnRenderBatchGrid(ViewProjection, ViewCameraLocation, ViewCameraForward, GridPlane, static_cast<float>(Settings.GridSpacing), !View.IsOrthographic() || OverlayClient->GetCameraPreset() == EViewportCameraPreset::OrthographicView, PassInfo.ViewportSetting);
		}
	}

	// TextRenderComponent 렌더링
	for (TObjectIterator<UTextRenderComponent> TextComponent; TextComponent; ++TextComponent)
	{
		if (!TextComponent || !TextComponent->GetFont() || !TextComponent->IsVisible())
		{
			continue;
		}
		// 에디터·PIE 월드의 컴포넌트가 함께 순회되므로 이 View의 월드만 그린다.
		if (!TextComponent->GetOwner() || TextComponent->GetOwner()->GetWorld() != &ViewWorld)
		{
			continue;
		}

		TextRenderer->OnRender(TextComponent->GetText(), TextComponent->GetWorldMatrix(), TextComponent->GetTextSize(), *TextComponent->GetFont(), ViewProjection);
	}

	// 선택 대상은 한 월드에만 속하므로 그 월드를 그리는 View에만 표시한다.
	const auto IsInViewWorld = [&ViewWorld](const UActorComponent* Component)
	{
		return Component && Component->GetOwner() && Component->GetOwner()->GetWorld() == &ViewWorld;
	};

	// 스텐실 기반이라 선택 대상의 가시성이 꺼져 있어도 외곽선만 그린다.
	if (bShowEditorOverlay && IsInViewWorld(Outline->GetTarget()))
	{
		OutlineRenderer->OnRender(*Outline, ViewProjection, PassInfo.ViewportSetting);
	}

	if (bShowEditorOverlay && IsInViewWorld(Gizmo->GetTarget()))
	{
		//auto Target = Cast<UPrimitiveComponent>(Gizmo->GetTarget());
		//FBox box = Target->CalcBounds();

		RenderCommand::ClearDepthStencil(ViewRenderingInfo.DepthStencil.Texture);

		GizmoRenderer->OnRender(*Gizmo, ViewProjection, ViewCameraLocation, View.IsOrthographic());
	}

	RenderCommand::ClearDepthStencil(PassInfo.DepthStencil.Texture);

	if (Settings.bShowUUID)
	{
		// 월드 +Z가 아니라 화면 위쪽으로 띄워야 Top·Bottom처럼 Z축을 따라 보는 View에서도 물체를 가리지 않는다.
		const FVector ScreenUp = View.GetCameraUp();

		for (AActor* Actor : ViewWorld.GetPersistentLevel()->GetActors())
		{
			if (!Actor)
				continue;

			FBox ActorBounds;
			if (!Actor->TryGetActorBounds(ActorBounds))
			{
				const FVector ActorLocation = Actor->GetActorLocation();
				ActorBounds.Min = ActorLocation;
				ActorBounds.Max = ActorLocation;
			}

			const FVector Center = (ActorBounds.Min + ActorBounds.Max) * 0.5f;
			const FVector HalfExtent = (ActorBounds.Max - ActorBounds.Min) * 0.5f;
			// 바운드 박스가 화면 위쪽으로 뻗은 길이만큼 올린 뒤 여백을 더한다.
			const float ExtentAlongUp = std::fabs(ScreenUp.X) * HalfExtent.X
				+ std::fabs(ScreenUp.Y) * HalfExtent.Y
				+ std::fabs(ScreenUp.Z) * HalfExtent.Z;
			UUIDLocation = Center + ScreenUp * (ExtentAlongUp + 0.5f);

			FString Text =
				"UUID : " + std::to_string(Actor->GetUUID());

			TextRenderer->BuildTextMesh(Text, 0.5f, *SystemFont);

			const FMatrix BillboardWorld = View.BuildBillboardMatrix(UUIDLocation, 1.0f, 1.0f);
			TextRenderer->OnRender(Text, BillboardWorld, 0.5f, *SystemFont, ViewProjection);
		}
	}

	RenderCommand::EndRenderPass(PassInfo);
}

void UEditorEngine::RenderFXAAPass(FTexture2D* FxaaTarget, const FRenderingInfo& ViewRenderingInfo)
{
	FTexture2D* SceneColor = ViewRenderingInfo.ColorRenderTargets[0].Texture;
	if (!SceneColor || !FxaaTarget)
		return;

	// 출력은 임시 텍스처, 깊이 없음, 어차피 전부 덮어쓰니 Clear 불필요
	FRenderingInfo PassInfo;
	PassInfo.ViewportSetting = ViewRenderingInfo.ViewportSetting;

	FRenderingDesc ColorDesc{};
	ColorDesc.Texture = FxaaTarget;
	ColorDesc.LoadOp = ERenderTargetLoadOp::DontCare;
	PassInfo.ColorRenderTargets.Add(ColorDesc);
	PassInfo.DepthStencil.Texture = nullptr;

	//장면 텍스처를 읽어서 임시 텍스처에 FXAA 결과를 그린다
	RenderCommand::BeginRenderPass(PassInfo);
	FXAARenderer->OnRender(SceneColor);
	RenderCommand::EndRenderPass(PassInfo);

	// SceneColor가 SRV로 남아 있으면 Overlay 패스에서 RTV로 쓸 때 D3D가 강제로 해제하므로 먼저 비운다
	RenderCommand::UnbindShaderResource(0, EShaderBindFlagBits::Pixel);

	RenderCommand::CopyTexture(SceneColor, FxaaTarget);
}

void UEditorEngine::RenderDepthPass(const FRenderingInfo& ViewRenderingInfo, const FMatrix& ViewProjection, const FVector& ViewCameraLocation, const FVector& ViewCameraForward)
{
	FRenderingInfo PassInfo = ViewRenderingInfo;

	for (FRenderingDesc& Color : PassInfo.ColorRenderTargets)
		Color.LoadOp = ERenderTargetLoadOp::DontCare;
	PassInfo.DepthStencil.Texture = nullptr;

	RenderCommand::BeginRenderPass(PassInfo);
	DepthViewRenderer->OnRender(ViewRenderingInfo.DepthStencil.Texture, ViewProjection, ViewCameraLocation, ViewCameraForward);
	RenderCommand::EndRenderPass(PassInfo);
}

// 입력 View의 Ray와 피킹으로 Gizmo·공유 선택을 갱신한다.
void UEditorEngine::UpdateGizmoAndPicking()
{
	// Delete는 BeginFrame에서 한 번만 처리하고 여기서는 View 입력만 다룬다.
	const int32 ViewIndex = ViewportsPanel->GetActiveViewIndex();
	if (ViewIndex == FViewportsPanel::InvalidSlotIndex || !ViewportsPanel->IsHovered())
		return;
	// 실행 중인 PIE View는 편집하지 않는다. 일시정지 중이면 PlayWorld를 피킹한다.
	if (IsPlayingView(ViewIndex))
		return;

	const FVector2 WindowMousePosition = ViewportsPanel->GetWindowMousePosition();
	FViewport* Viewport = ViewportsPanel->GetViewport(ViewIndex);
	if (!Viewport || !Viewport->IsVisible() || ViewportsPanel->GetPointerViewIndex(WindowMousePosition) != ViewIndex)
		return;
    FViewportClient* Client = Viewport->GetActiveClient();
    if (!Client || !Client->HasCamera())
        return;
	const FSceneView View = Viewport->CreateSceneView();
	const FRect& Rect = View.ViewRect;
	const FVector2 ViewLocalMouse = ViewportsPanel->ToViewportLocal(ViewIndex, WindowMousePosition);
	const FRay Ray = View.Deproject(ViewLocalMouse);
	const FMatrix& ViewProjection = View.ViewProjection;
	bool bMouseDown = FInputSystem::IsMouseDown(EMouseButton::Left);

	// 다른 월드의 선택 대상은 이 View에 Gizmo가 보이지 않으므로 조작하지 않는다.
	UWorld* ViewWorld = Client->GetWorld();
	const USceneComponent* GizmoTarget = Gizmo->GetTarget();
	const bool bGizmoInView = !GizmoTarget ||
		(GizmoTarget->GetOwner() && GizmoTarget->GetOwner()->GetWorld() == ViewWorld);

	if (bGizmoInView)
	{
		Gizmo->Update(Ray, ViewLocalMouse, ViewProjection, static_cast<int>(Rect.Width), static_cast<int>(Rect.Height), bMouseDown, View.GetCameraLocation(), View.IsOrthographic());
	}

	const bool bGizmoBlocksPick = bGizmoInView && (Gizmo->IsUsing() || Gizmo->GetHoveredAxis() >= 0);
	if (FInputSystem::IsMousePressed(EMouseButton::Left) && !bGizmoBlocksPick)
	{
		SCOPE_CYCLE_COUNTER_ALWAYS(EditorStats::STAT_PickingTime);
		const auto ResolveBillboardTransform = [](const UBillboardComponent& Billboard, const void* Context) -> FMatrix
		{
			const auto& PickView = *static_cast<const FSceneView*>(Context);
			const FVector Scale = Billboard.GetWorldScale3D();
			return PickView.BuildBillboardMatrix(Billboard.GetWorldLocation(), Scale.Y, Scale.Z);
		};
		FHitResult Hit;
		const bool bHit = ViewWorld->LineTraceSingle(Ray, Hit, ResolveBillboardTransform, &View);
		OutlinerPanel->SelectActor(bHit ? Hit.HitComponent->GetOwner() : nullptr);
	}

}

// Lit + Phong이고 그림자를 가리는 시각화가 꺼진 View만 그림자를 쓴다.
bool UEditorEngine::ViewSupportsShadows(const FSceneView& View) const
{
    const FEditorSettings& Settings = SettingsPanel->GetSettings();
    return View.RenderMode.ViewMode == EViewMode::Lit && View.RenderMode.ShadingModel == EShadingModel::Phong && Settings.bDrawPrimitives
        && !Settings.bDepthView && !Settings.bVisualizeTileLightCount;
}

// View 행렬로 Scene·Grid·Gizmo·텍스트·Outline을 렌더한다.
void UEditorEngine::RenderFrame(FViewport& Viewport, UWorld& ViewWorld, const FSceneView& View, FEditorViewportClient* OverlayClient)
{
	const FRenderingInfo& SceneRenderingInfo = Viewport.GetSceneRenderingInfo();
	const FRenderingInfo& ViewRenderingInfo = Viewport.GetRenderingInfo();
	FRenderQueue& RenderQueue = Viewport.GetSceneRenderData().RenderQueue;
	const FMatrix& ViewProjection = View.ViewProjection;
	const FVector ViewCameraLocation = View.GetCameraLocation();
	const FVector ViewCameraForward = View.GetCameraForward();
	FLightingConstants Lighting;
	FSceneRenderData& Data = Viewport.GetSceneRenderData();

	// 지원 View만 공유 그림자를 요청한다. 비지원 View는 다른 View의 결과를 지우지 않는다.
    const bool bSupportsShadows = ViewSupportsShadows(View);
    uint32 ShadowResourceIndex = InvalidShadowResourceIndex;
    const FShadowFrameData* ShadowFrame = nullptr;
    FShadowFrameData DirectionalFrame;
    if (bSupportsShadows)
    {
        if (FWorldContext* Context = GetWorldContextFromWorld(&ViewWorld))
        {
            if (Context->ShadowResourceIndex == InvalidShadowResourceIndex)
                Context->ShadowResourceIndex = Renderer->AllocateShadowResources(Context->WorldType == EWorldType::Editor);
            ShadowResourceIndex = Context->ShadowResourceIndex;
            ShadowFrame = Renderer->RenderWorldShadows(ShadowResourceIndex, ViewWorld, SceneRenderer);
        }
        Renderer->RenderDirectionalShadow(Viewport, ViewWorld, SceneRenderer, View, DirectionalFrame);
    }
    else
        Viewport.GetDirectionalShadow().ResetFrame();
    const bool bReadyForShading = Renderer->BindWorldShadows(ShadowResourceIndex, SceneRenderer.GetFrameNumber(), bSupportsShadows);
    const bool bDirectionalReady = Renderer->BindDirectionalShadow(Viewport, SceneRenderer.GetFrameNumber(), bSupportsShadows);
    // 셰이딩 준비가 성공한 경우에만 기존 요청 배열을 비소유 참조로 전달한다.
    const TArray<FShadowRequestPerLight>* ShadowRequests = bReadyForShading && ShadowFrame ? &ShadowFrame->ShadowRequestsPerLight : nullptr;
    ViewWorld.GetScene().BuildLightingConstants(Lighting, ShadowRequests, bDirectionalReady ? &DirectionalFrame.ShadowRequestsPerLight : nullptr);
    ViewWorld.GetScene().BuildStructuredLight(Data.PointLights, Data.SpotLights, Data.CullingLights, View.ViewMatrix, ShadowRequests);
    Lighting.bReadyForShading = bReadyForShading ? 1u : 0u;
    Lighting.NumShadowInfos = bReadyForShading ? Renderer->GetShadowResources(ShadowResourceIndex)->NumShadowInfos : 0u;
    Renderer->SetLighting(Lighting, Data.PointLights, Data.SpotLights);

	// 1. 불투명·반투명 Pass (Linear 씬 컬러)
	RenderOpaquePass(Viewport, SceneRenderingInfo, View, RenderQueue, Lighting.Directional);
    // Viewport 자원을 GPU 바인딩이 붙잡지 않게 조명 패스 소비 후 해제한다.
    Renderer->BindDirectionalShadow(Viewport, SceneRenderer.GetFrameNumber(), false);

	if (SettingsPanel->GetSettings().bDepthView)
	{
		// 깊이 시각화는 표시용 타깃을 통째로 덮어쓰므로 인코딩이 필요 없다
		RenderDepthPass(ViewRenderingInfo, ViewProjection, ViewCameraLocation, ViewCameraForward);
	}
	else
	{
		// 데이터 시각화 뷰는 씬 컬러 값 자체가 결과이므로 안개·인코딩·FXAA로 값을 바꾸지 않는다.
		const EViewMode ViewMode = View.RenderMode.ViewMode;
		const bool bVisualizeData = IsDataVisualization(ViewMode);

		//직교인 경우 안개 Pass를 그리지 않는다.
		if (!View.IsOrthographic() && !bVisualizeData && SettingsPanel->GetSettings().bExponentialHeightFog)
			// 2. 안개 Pass (Linear 씬 컬러)
			RenderFogPass(ViewWorld, SceneRenderingInfo, ViewCameraLocation, ViewProjection, RenderQueue);

		// 3. Gamma Encode Pass: Linear 씬 컬러 → (Lit이면 ACES) → 표시용 sRGB
		const FEditorSettings& Settings = SettingsPanel->GetSettings();
		const bool bEncode = !bVisualizeData && ColorWorkflow::bLinear;
		const bool bToneMap = bEncode && Settings.bToneMapping && ViewMode == EViewMode::Lit;
		RenderGammaEncodePass(SceneRenderingInfo, ViewRenderingInfo, bEncode, bToneMap, Settings.ExposureEV);

		// 4. FXAA Pass: 밝기 계산이 감마 공간을 전제로 하므로 인코딩 뒤에 한다
		if (!bVisualizeData && SettingsPanel->GetSettings().bEnableFXAA)
			RenderFXAAPass(Viewport.GetFxaaTarget(), ViewRenderingInfo);
	}

	// 5. 오버레이 Pass: Grid·Gizmo·UUID는 sRGB 색으로 지정되므로 인코딩 뒤 표시용 타깃에 그린다
	RenderOverlayPass(OverlayClient, ViewWorld, ViewRenderingInfo, View, RenderQueue);
}

void UEditorEngine::RenderGammaEncodePass(const FRenderingInfo& SceneRenderingInfo, const FRenderingInfo& ViewRenderingInfo, bool bEncode, bool bToneMap, float ExposureEV)
{
	FTexture2D* SceneColor = SceneRenderingInfo.ColorRenderTargets[0].Texture;
	FTexture2D* ViewColor = ViewRenderingInfo.ColorRenderTargets[0].Texture;
	if (!SceneColor || !ViewColor)
		return;

	// 출력은 표시용 타깃 전체를 덮어쓰므로 Clear·깊이가 필요 없다
	FRenderingInfo PassInfo;
	PassInfo.ViewportSetting = ViewRenderingInfo.ViewportSetting;

	FRenderingDesc ColorDesc{};
	ColorDesc.Texture = ViewColor;
	ColorDesc.LoadOp = ERenderTargetLoadOp::DontCare;
	PassInfo.ColorRenderTargets.Add(ColorDesc);
	PassInfo.DepthStencil.Texture = nullptr;

	RenderCommand::BeginRenderPass(PassInfo);
	GammaEncodeRenderer->OnRender(SceneColor, bEncode, bToneMap, ExposureEV);
	RenderCommand::EndRenderPass(PassInfo);

	// 다음 프레임에 SceneColor를 RTV로 쓸 때 SRV로 남아 있으면 D3D가 강제로 해제하므로 먼저 비운다
	RenderCommand::UnbindShaderResource(0, EShaderBindFlagBits::Pixel);
}


// View Texture가 포함된 UI를 Swapchain 백버퍼에 합성한다. Present는 FEngineLoop가 한다.
void UEditorEngine::PresentFrame()
{
	// Swapchain 렌더링
	RenderCommand::BeginRenderPass(MainWindowSC->GetRenderingInfo());

	{
		SCOPE_CYCLE_COUNTER(STAT_ImGui);

		ImGuiRenderer->Begin();

		EditorUI->OnRender();

		ImGuiRenderer->End();
	}

	RenderCommand::EndRenderPass(MainWindowSC->GetRenderingInfo());
}

// ImGui를 정리한다. UObject 일괄 삭제와 공용 자원·Device 정리는 FEngineLoop가 이어서 한다.
void UEditorEngine::PreExit()
{
	// UObject 일괄 삭제와 Device 종료 전에 PIE의 모든 비소유 연결과 GPU 자원을 해제한다.
	EndPlayMap();
	DestroyPIEWindow();
	SceneRenderer.EndFrame();
	for (FWorldContext& Context : WorldList)
		ReleaseWorldShadowResources(Context.World());
	ViewportsPanel->Shutdown();
	ImGuiRenderer->Shutdown();
}

// 선택과 Gizmo 참조를 정리한 뒤 Actor를 삭제한다.
void UEditorEngine::DeleteActor(AActor* Actor)
{
	if (!Actor)
		return;
	// 실행 중 삭제는 게임 로직이 들고 있는 참조를 깨뜨릴 수 있어 Pause 상태에서만 허용한다.
	if (IsPlaying() && !bIsPaused)
		return;

	OutlinerPanel->SelectActor(nullptr);
	DetailsPanel->SelectComponent(nullptr);

	Actor->Destroy();
}

void UEditorEngine::DeleteComponent(UActorComponent* Component)
{
	if (!Component)
		return;
	if (IsPlaying() && !bIsPaused)
		return;
	DetailsPanel->SelectComponent(nullptr);
	Outline->SetTarget(nullptr);
	Component->GetOwner()->DestroyComponent(Component);
}

void UEditorEngine::StartPlayInEditorSession()
{
	if (PlayWorld)
		return;
	// 창 생성 실패 시 기존처럼 선택 슬롯에서 시작한다.
	if (PlayMode == EPlayModeType::InEditorFloating)
		CreatePIEWindow();
	FWorldContext& PIEContext = CreateNewWorldContext(EWorldType::PIE);
	PlayWorld = CreatePIEWorldByDuplication(PIEContext, EditorWorld);

	if (PlayWorld)
	{
		// 1. 씬에 배치된 ACameraActor가 있으면 해당 카메라를 메인 카메라로 지정
		ACameraActor* PlacedCamera = nullptr;
		if (PlayWorld->GetPersistentLevel())
		{
			for (AActor* Actor : PlayWorld->GetPersistentLevel()->GetActors())
			{
				if (ACameraActor* CamActor = Cast<ACameraActor>(Actor))
				{
					PlacedCamera = CamActor;
					break;
				}
			}
		}

		if (PlacedCamera)
		{
			PlayWorld->SetMainCamera(PlacedCamera);
		}
		else if (PlayWorld->GetMainCamera())
		{
			// 2. 배치된 카메라가 없으면 현재 활성 에디터 뷰포트 시점에서 시작
			const int32 SourceViewIndex = ViewportsPanel->GetEditorViewIndex();
			const FEditorCameraState& EditorCam = ViewportsPanel->GetEditorClient(SourceViewIndex)->GetCamera();
			if (UCameraComponent* GameCam = PlayWorld->GetMainCamera()->GetCameraComponent())
			{
				GameCam->SetRelativeLocation(EditorCam.Location);
				GameCam->SetRelativeRotation(EditorCam.Rotation.ToFRotator());
				GameCam->SetFieldOfView(EditorCam.Projection.FovDegrees);
				GameCam->SetNearZ(EditorCam.Projection.NearClip);
				GameCam->SetFarZ(EditorCam.Projection.FarClip);
				GameCam->SetIsOrthogonal(EditorCam.Projection.Mode == EProjectionMode::Orthographic);
				GameCam->SetOrthoWidth(EditorCam.Projection.OrthoWidth);
			}
		}
	}

	if (!PlayWorld)
	{
		DestroyWorldContext(nullptr);
		DestroyPIEWindow();
		return;
	}
    if (IsPIEInWindow())
        PIEViewport = PIEPanel->GetViewport(0);
    else
    {
        const int32 Index = ViewportsPanel->GetEditorViewIndex();
        ViewportsPanel->ReleaseInput(Index);
        PIEViewport = ViewportsPanel->GetViewport(Index);
    }
    PIEViewport->BeginPlay(MakeUnique<FGameViewportClient>(*PlayWorld));
    // BeginPlay는 입력을 차단한다. 아래 일반 프레임 갱신이 첫 World Tick 전에 한 번 허용한다.
	OnActiveWorldChanged();
}

UWorld* UEditorEngine::CreatePIEWorldByDuplication(FWorldContext& PIEContext, UWorld* InEditorWorld)
{
	UWorld* NewPIEWorld = UWorld::GetDuplicatedWorldForPIE(InEditorWorld);
	if (PIEContext.World()) ReleaseWorldShadowResources(PIEContext.World());
	PIEContext.SetCurrentWorld(NewPIEWorld);
	return NewPIEWorld;
}

void UEditorEngine::OnActiveWorldChanged()
{
	ResetSceneSelection();
}

void UEditorEngine::EndPlayMap()
{
	if (!PlayWorld)
		return;

	ResetSceneSelection();
    for (int32 Index = 0; Index < ViewportsPanel->GetSlotCount(); ++Index)
    {
        ViewportsPanel->ReleaseInput(Index);
        ViewportsPanel->GetViewport(Index)->ClearSceneData();
    }
    if (PIEPanel)
        PIEPanel->ReleaseInput(0);
    SceneRenderer.EndFrame();
    // 세션 조회 참조를 먼저 놓는다. 활성 편집 Client 복귀 후 게임 Client가 파괴된다.
    FViewport* EndingViewport = PIEViewport;
    PIEViewport = nullptr;
    EndingViewport->EndPlay();
    // 외부 타깃은 Device가 살아 있을 때 Swapchain/창/World보다 먼저 파괴한다.
    PIEPanel.reset();
	DestroyPIEWindow();
	PlayWorld->EndPlay();
	ReleaseWorldShadowResources(PlayWorld);
	PlayWorld->ClearWorld();
	DestroyWorldContext(PlayWorld);
	delete PlayWorld;
	PlayWorld = nullptr;
	bIsPaused = false;
	bStepRequested = false;
	OnActiveWorldChanged();
}

bool UEditorEngine::CreatePIEWindow()
{
	constexpr int32 DefaultWidth = 1280;
	constexpr int32 DefaultHeight = 720;

	TUniquePtr<FWindow> NewWindow = MakeUnique<FWindow>();
	if (!NewWindow->Create(::GetModuleHandleW(nullptr), DefaultWidth, DefaultHeight, L"Hitori Engine - PIE"))
	{
		HTR_LOG(Error, "Failed to create PIE window. Falling back to the selected viewport.");
		return false;
	}
	// 닫기 버튼은 PIE만 끝내고, ImGui는 메인 창 입력만 받도록 훅을 끊는다.
	NewWindow->SetQuitOnClose(false);
	NewWindow->SetReceivesWndProcHook(false);

	PIEWindow = std::move(NewWindow);
	PIESwapchain = MakeUnique<FSwapchain>(GetEngineLoop().GetRenderDevice(), PIEWindow.get());
	PIEPanel = MakeUnique<FViewportsPanel>(EViewportPanelLayout::Single);
	PIEPanel->SetHostWindow(PIEWindow.get());
	PIEPanel->Init();
	PIEWindow->Show();
	return true;
}

void UEditorEngine::DestroyPIEWindow()
{
	if (!PIEWindow)
		return;

	bPIEWindowRendered = false;
	PIEViewport = nullptr;
	PIEPanel.reset();

	// Swapchain이 창 핸들을 출력 대상으로 쥐고 있으므로 창보다 먼저 놓는다.
	PIESwapchain.reset();
	PIEWindow->Destroy();
	PIEWindow.reset();

	// PIE 창이 포커스를 갖고 있었다면 에디터로 돌려준다.
	::SetForegroundWindow(MainWindow->GetHandle());
}

void UEditorEngine::UpdatePIEWindowState()
{
    if (!PIEPanel)
        return;
    const HWND Window = PIEWindow->GetHandle();
    const bool bVisible = ::IsWindowVisible(Window) && !::IsIconic(Window);
    const float Width = static_cast<float>(PIEWindow->GetWidth());
    const float Height = static_cast<float>(PIEWindow->GetHeight());
    // 입력 판단에 필요한 CPU 상태만 갱신한다. 숨김은 입력/Capture와 준비 데이터를 해제한다.
    PIEPanel->UpdateLayout(bVisible ? FRect{0.0f, 0.0f, Width, Height} : FRect{});
}

bool UEditorEngine::UpdatePIEWindowTargets()
{
    if (!PIEViewport || !PIEViewport->IsVisible())
        return false;
    // 표시 상태와 슬롯 크기는 Tick 전에 확정했다. GPU 작업은 렌더 단계에서만 한다.
    if (PIEWindow->CheckResized())
        PIESwapchain->Resize(PIEWindow->GetWidth(), PIEWindow->GetHeight());
    return true;
}

void UEditorEngine::CopyPIEWindowToBackbuffer()
{
	const FViewport* Viewport = PIEViewport;
	if (!Viewport)
		return;
	const FRenderingInfo& BackbufferInfo = PIESwapchain->GetRenderingInfo();
	if (BackbufferInfo.ColorRenderTargets.Num() == 0)
		return;

	// 백버퍼는 SRV가 없어 FXAA·Fog 입력으로 쓸 수 없으므로 오프스크린 결과를 통째로 복사한다.
	// 창 크기 변경 직후 한 프레임은 크기가 어긋날 수 있어 그때는 건너뛴다.
	FTexture2D* Backbuffer = BackbufferInfo.ColorRenderTargets[0].Texture;
	if (!Backbuffer || !Viewport->GetColorTarget() ||
		BackbufferInfo.ViewportSetting.Width != Viewport->GetRenderingInfo().ViewportSetting.Width ||
		BackbufferInfo.ViewportSetting.Height != Viewport->GetRenderingInfo().ViewportSetting.Height)
		return;

	RenderCommand::CopyTexture(Backbuffer, Viewport->GetColorTarget());
	bPIEWindowRendered = true;
}




// 씬 변경으로 무효화된 에디터의 선택 참조를 모두 해제한다.
void UEditorEngine::ResetSceneSelection()
{
	Gizmo->SetTarget(nullptr, false);
	Outline->SetTarget(nullptr);
	DetailsPanel->SetTarget(nullptr);
	OutlinerPanel->SelectActor(nullptr);
}

// 새 씬 생성이 성공하면 에디터 선택 상태를 초기화한다.
void UEditorEngine::CreateNewScene()
{
	if (IsPlaying())
	{
		HTR_LOG(Warning, "Cannot create a new scene during PIE. Stop the play session first.");
		return;
	}
	for (int32 Index = 0; Index < ViewportsPanel->GetSlotCount(); ++Index)
		ViewportsPanel->GetViewport(Index)->ClearSceneData();
	ReleaseWorldShadowResources(EditorWorld);
	if (!FEditorFileUtils::NewScene(EditorWorld))
		return;

	ResetSceneSelection();
}

// 씬 불러오기가 성공하면 에디터 선택 상태를 초기화한다.
void UEditorEngine::OpenScene()
{
	if (IsPlaying())
	{
		HTR_LOG(Warning, "Cannot open a scene during PIE. Stop the play session first.");
		return;
	}
	for (int32 Index = 0; Index < ViewportsPanel->GetSlotCount(); ++Index)
		ViewportsPanel->GetViewport(Index)->ClearSceneData();
	ReleaseWorldShadowResources(EditorWorld);
	if (!FEditorFileUtils::LoadScene(EditorWorld))
		return;

	ResetSceneSelection();
}

// 공통 파일 유틸리티로 현재 씬을 저장한다.
void UEditorEngine::SaveCurrentScene()
{
	if (IsPlaying())
	{
		HTR_LOG(Warning, "Cannot save during PIE. Stop the play session first.");
		return;
	}
	FEditorFileUtils::SaveScene(EditorWorld);
}

// 공통 파일 유틸리티로 새 경로에 씬을 저장한다.
void UEditorEngine::SaveSceneAs()
{
	if (IsPlaying())
	{
		HTR_LOG(Warning, "Cannot save during PIE. Stop the play session first.");
		return;
	}
	FEditorFileUtils::SaveSceneAs(EditorWorld);
}

void UEditorEngine::ReleaseWorldShadowResources(UWorld* World)
{
    if (!World) return;
    FWorldContext* Context = GetWorldContextFromWorld(World);
    if (!Context) return;
    // CPU의 비소유 참조를 먼저 끊고 GPU 자원 슬롯을 반환한다.
    SceneRenderer.ReleaseWorldShadows(Context->ShadowResourceIndex);
    Renderer->ReleaseShadowResources(Context->ShadowResourceIndex);
    Context->ShadowResourceIndex = InvalidShadowResourceIndex;
}
