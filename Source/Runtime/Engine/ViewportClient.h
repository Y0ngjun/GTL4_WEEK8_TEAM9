#pragma once

#include "Render/SceneView.h"

class UWorld;

// 기존 입력값의 묶음. 이벤트 처리나 중계 책임은 갖지 않는다.
struct FViewportInput
{
    FVector2 MousePosition{};
    float DeltaTime = 0.0f;
    float MoveSpeed = 0.0f;
    float MouseSensitivity = 0.0f;
    int32 WheelDelta = 0;
    bool bCaptured = false;
    bool bPointerOver = false;
    bool bEnabled = false;
};

// World/카메라/표시 설정을 제공한다. 배치, 렌더 작업 데이터와 타깃은 소유하지 않는다.
class FViewportClient
{
public:
    virtual ~FViewportClient() = default;
    virtual UWorld* GetWorld() const = 0;
    virtual bool HasCamera() const { return true; }
    virtual FSceneView CreateSceneView(FVector2 OutputSize) const = 0;
    virtual void UpdateInput(const FViewportInput& Input, FVector2 OutputSize) = 0;
    virtual void SetInputEnabled(bool bEnabled) {}
    virtual void OnOutputSizeChanged(FVector2 OutputSize) {}
    void SetViewRenderMode(const FViewRenderMode& Value) { ViewRenderMode = Value; }
    FViewRenderMode GetViewRenderMode() const { return ViewRenderMode; }

private:
    FViewRenderMode ViewRenderMode{};
};
