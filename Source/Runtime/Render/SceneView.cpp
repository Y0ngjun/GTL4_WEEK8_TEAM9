#include "EnginePCH.h"
#include "Render/SceneView.h"
#include "Camera/CameraComponent.h"
#include <algorithm>
#include <cassert>
#include <cmath>

FSceneView::FSceneView(const FVector& Location, const FMatrix& CameraRotation, const FCameraProjection& Projection, const FRect& Rect, const FViewRenderMode& Mode)
    : ViewRect(Rect), RenderMode(Mode), ProjectionSettings(Projection), CameraLocation(Location), RenderLocation(Location)
{
    assert(ViewRect.IsValid());
    CameraForward = FVector(CameraRotation.M[0][0], CameraRotation.M[0][1], CameraRotation.M[0][2]).Normalized();
    CameraRight = FVector(CameraRotation.M[1][0], CameraRotation.M[1][1], CameraRotation.M[1][2]).Normalized();
    CameraUp = FVector(CameraRotation.M[2][0], CameraRotation.M[2][1], CameraRotation.M[2][2]).Normalized();
    FCameraProjection RenderProjection = ProjectionSettings;
    const float Aspect = Rect.Width / Rect.Height;
    if (IsOrthographic())
    {
        const float HalfWidth = ProjectionSettings.OrthoWidth * 0.5f;
        const float HalfHeight = HalfWidth / Aspect;
        // 논리 위치는 LOD/조명/정렬에 남기고 렌더·피킹만 뒤로 이동한다.
        const float Radius = std::max(ProjectionSettings.FarClip, 2.0f * Location.Size() + 4.0f * std::sqrt(HalfWidth * HalfWidth + HalfHeight * HalfHeight));
        RenderLocation -= CameraForward * (Radius + ProjectionSettings.NearClip);
        RenderProjection.FarClip = ProjectionSettings.NearClip + 2.0f * Radius;
    }
    // UCameraComponent와 같은 엔진 행렬 조합을 사용한다.
    ViewMatrix = FMatrix::MakeTranslation(-RenderLocation) * CameraRotation.GetTransposed();
    ProjectionMatrix = RenderProjection.GetMatrix(Aspect);
    ViewProjection = ViewMatrix * ProjectionMatrix;
    Frustum = ExtractFrustumPlanes(ViewProjection);
}

FRay FSceneView::Deproject(const FVector2 ViewLocalPosition) const
{
    return UCameraComponent::DeprojectView(ViewMatrix, ProjectionMatrix, RenderLocation, ViewLocalPosition, {ViewRect.Width, ViewRect.Height}, IsOrthographic());
}

FLODViewContext FSceneView::MakeLODContext() const
{
    const float ScaleX = ProjectionMatrix.M[1][0];
    const float ScaleY = ProjectionMatrix.M[2][1];
    FLODViewContext Context{static_cast<uint32>(ViewRect.Width), static_cast<uint32>(ViewRect.Height)};
    Context.CameraPosition = CameraLocation;
    Context.ViewProjection = ViewProjection;
    Context.CameraForward = CameraForward;
    Context.ProjectionScaleSquared = std::max(ScaleX * ScaleX, ScaleY * ScaleY);
    Context.NearZ = ProjectionSettings.NearClip;
    Context.bOrthographic = IsOrthographic();
    Context.Prepare();
    return Context;
}

FMatrix FSceneView::BuildBillboardMatrix(const FVector& WorldPosition, const float Width, const float Height) const
{
    FVector Facing = -CameraForward;
    FVector Right = CameraRight;
    FVector Up = CameraUp;
    if (!IsOrthographic())
    {
        const FVector ToCamera = CameraLocation - WorldPosition;
        // 기존 일치 위치 임계값은 길이 1e-6 (제곱 길이 1e-12)이다.
        Facing = (ToCamera.SizeSquared() <= 1.0e-12f ? -CameraForward : ToCamera).Normalized();
        const FVector ReferenceUp = std::fabs(Facing.Dot(UpVector)) > 0.999f ? RightVector : UpVector;
        Right = ReferenceUp.Cross(Facing).Normalized();
        Up = Facing.Cross(Right).Normalized();
        // ParticleQuad의 앞면 와인딩에 맞춰 원근 빌보드의 오른쪽 축만 반전한다.
        Right = -Right;
    }
    FMatrix Matrix = FMatrix::Identity;
    Matrix.SetAxis(0, Facing);
    Matrix.SetAxis(1, Right * Width);
    Matrix.SetAxis(2, Up * Height);
    Matrix.SetOrigin(WorldPosition);
    return Matrix;
}

void FSceneView::ConstrainOrthographicWidth(FCameraProjection& Projection, const FRect& Rect)
{
    constexpr float MinimumWidth = 0.01f;
    constexpr float MaximumSpan = 80000.0f;
    const float Aspect = Rect.IsValid() ? Rect.Width / Rect.Height : 1.0f;
    const float MaximumWidth = std::max(MinimumWidth, MaximumSpan * std::min(1.0f, Aspect));
    Projection.OrthoWidth = std::isnan(Projection.OrthoWidth) ? 10.0f : FMath::Clamp(Projection.OrthoWidth, MinimumWidth, MaximumWidth);
}
