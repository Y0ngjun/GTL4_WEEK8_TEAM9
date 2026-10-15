#pragma once

#include "Math/Rect.h"
#include "Camera/CameraProjection.h"
#include "Render/ViewMode.h"
#include "Math/Frustum.h"
#include "Collision/Ray.h"
#include "Asset/LOD/StaticMeshLODSelector.h"

// 준비/렌더 호출의 값 스냅샷. 카메라 원본이나 World를 소유하지 않는다.
struct FSceneView
{
    // CameraRotation의 행은 정규직교 월드 Forward/Right/Up이다.
    FSceneView(const FVector& Location, const FMatrix& CameraRotation, const FCameraProjection& Projection, const FRect& Rect, const FViewRenderMode& Mode);

    FRect ViewRect;
    FViewRenderMode RenderMode;
    FCameraProjection ProjectionSettings;
    FVector CameraLocation;
    FVector RenderLocation;
    FVector CameraForward, CameraRight, CameraUp;
    FMatrix ViewMatrix;
    FMatrix ProjectionMatrix;
    FMatrix ViewProjection;
    FFrustumPlanes Frustum;

    bool IsOrthographic() const { return ProjectionSettings.Mode == EProjectionMode::Orthographic; }
    FVector GetCameraLocation() const { return CameraLocation; }
    FVector GetCameraForward() const { return CameraForward; }
    FVector GetCameraUp() const { return CameraUp; }
    FRay Deproject(FVector2 ViewLocalPosition) const;
    FLODViewContext MakeLODContext() const;
    FMatrix BuildBillboardMatrix(const FVector& WorldPosition, float Width, float Height) const;
    // 직교 뷰의 폭/높이 상한 정책. 원본 게임 카메라는 변경하지 않는다.
    static void ConstrainOrthographicWidth(FCameraProjection& Projection, const FRect& Rect);
};
