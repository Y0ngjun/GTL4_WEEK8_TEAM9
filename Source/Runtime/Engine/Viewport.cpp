#include "EnginePCH.h"
#include "Engine/Viewport.h"
#include <cassert>

FViewport::~FViewport()
{
    if (ActiveClient)
        ActiveClient->SetInputEnabled(false);
    ClearSceneData();
    ActiveClient = nullptr;
    GameClient.reset();
    EditorClient.reset();
    ViewTargets.Reset();
}

void FViewport::SetEditorClient(TUniquePtr<FViewportClient> Client)
{
    assert(Client && !EditorClient && !GameClient);
    EditorClient = std::move(Client);
    ActiveClient = EditorClient.get();
    if (IsVisible())
        ActiveClient->OnOutputSizeChanged(GetOutputSize());
}

void FViewport::BeginPlay(TUniquePtr<FViewportClient> Client)
{
    assert(Client && !GameClient);
    if (ActiveClient)
        ActiveClient->SetInputEnabled(false);
    ClearSceneData();
    if (EditorClient)
        Client->SetViewRenderMode(EditorClient->GetViewRenderMode());
    GameClient = std::move(Client);
    ActiveClient = GameClient.get();
    ActiveClient->SetInputEnabled(false);
    if (IsVisible())
        ActiveClient->OnOutputSizeChanged(GetOutputSize());
}

void FViewport::EndPlay()
{
    if (!GameClient)
        return;
    GameClient->SetInputEnabled(false);
    ClearSceneData();
    ViewTargets.LightCulling.Reset();
    // 선택의 영역: 현재는 PIE 종료 시 보존된 편집 표시 설정으로 복귀한다. PIE에서 변경한 표시 모드를 유지하기로 결정하면 아래 Game → Editor 복사를 활성화하고, 편집 설정 저장 정책도 함께 검토한다.
    // if (EditorClient) EditorClient->SetViewRenderMode(GameClient->GetViewRenderMode());
    ActiveClient = EditorClient.get();
    GameClient.reset();
}

void FViewport::SetOutputSize(uint32 InWidth, uint32 InHeight)
{
    Width = InWidth;
    Height = InHeight;
    if (!IsVisible())
    {
        if (ActiveClient)
            ActiveClient->SetInputEnabled(false);
        ClearSceneData();
        return;
    }
    // 보존된 편집 카메라도 기존 정사영 폭 제한을 적용한다.
    if (EditorClient)
        EditorClient->OnOutputSizeChanged(GetOutputSize());
    if (GameClient)
        GameClient->OnOutputSizeChanged(GetOutputSize());
}

void FViewport::EnsureRenderTargets()
{
    if (IsVisible())
        ViewTargets.Resize(Width, Height);
}

void FViewport::UpdateInput(const FViewportInput& Input)
{
    if (!ActiveClient)
        return;
    if (!IsVisible() || !Input.bEnabled)
    {
        ActiveClient->SetInputEnabled(false);
        return;
    }
    ActiveClient->UpdateInput(Input, GetOutputSize());
}

FSceneView FViewport::CreateSceneView() const
{
    assert(ActiveClient && IsVisible());
    return ActiveClient->CreateSceneView(GetOutputSize());
}
