#pragma once

#include "Engine/ViewportClient.h"
#include "Render/SceneRenderData.h"
#include "Render/ForwardViewTargets.h"
#include "Render/ShadowResources.h"

// 고정된 화면의 출력 기반. 구체적인 Client 생성과 슬롯 배치는 Editor/PIE 구성 코드가 맡는다.
class FViewport
{
public:
    ~FViewport();
    void SetEditorClient(TUniquePtr<FViewportClient> Client);
    void BeginPlay(TUniquePtr<FViewportClient> Client);
    void EndPlay();
    FViewportClient* GetEditorClient() const { return EditorClient.get(); }
    FViewportClient* GetGameClient() const { return GameClient.get(); }
    FViewportClient* GetActiveClient() const { return ActiveClient; }
    void SetOutputSize(uint32 InWidth, uint32 InHeight);
    FVector2 GetOutputSize() const { return {static_cast<float>(Width), static_cast<float>(Height)}; }
    bool IsVisible() const { return Width > 0 && Height > 0; }
    void EnsureRenderTargets();
    void UpdateInput(const FViewportInput& Input);
    void ClearSceneData() { SceneRenderData.ResetFrame(); DirectionalShadow.ResetFrame(); }
    FDirectionalShadowResources& GetDirectionalShadow() { return DirectionalShadow; }
    const FDirectionalShadowResources& GetDirectionalShadow() const { return DirectionalShadow; }
    FSceneView CreateSceneView() const;
    FSceneRenderData& GetSceneRenderData() { return SceneRenderData; }
    const FRenderingInfo& GetRenderingInfo() const { return ViewTargets.RenderingInfo; }
    const FRenderingInfo& GetSceneRenderingInfo() const { return ViewTargets.SceneRenderingInfo; }
    FTileLightCullingResources& GetLightCullingResources() { return ViewTargets.LightCulling; }
    FTexture2D* GetFxaaTarget() const { return ViewTargets.FxaaTarget.get(); }
    FTexture2D* GetColorTarget() const { return ViewTargets.ColorTarget.get(); }

private:
    TUniquePtr<FViewportClient> EditorClient;
    TUniquePtr<FViewportClient> GameClient;
    FViewportClient* ActiveClient = nullptr;
    FSceneRenderData SceneRenderData;
    FForwardViewTargets ViewTargets;
    FDirectionalShadowResources DirectionalShadow;
    // 숨김 시 요청 크기는 0. ViewTargets의 실제 할당 크기와 자원은 유지한다.
    uint32 Width = 0;
    uint32 Height = 0;
};
