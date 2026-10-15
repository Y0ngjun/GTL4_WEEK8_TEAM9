#include "EnginePCH.h"
#include "Editor/Viewports/EditorViewportClient.h"
#include "Camera/CameraComponent.h"
#include "Editor/Rendering/GridRenderer.h"
#include "Input/InputSystem.h"

#include <algorithm>
#include <cassert>
#include <cmath>


namespace
{
constexpr float MaximumCameraPitchDegrees = 89.0f;
constexpr float MaximumRotationDegreesPerFrame = 45.0f;
constexpr float MouseWheelDeltaPerStep = 120.0f;
constexpr float OrthographicZoomFactorPerStep = 0.9f;
constexpr float MinimumOrthographicWidth = 0.01f;

float AxisValue(const EKeyCode Positive, const EKeyCode Negative)
{
	return (FInputSystem::IsKeyDown(Positive) ? 1.0f : 0.0f) - (FInputSystem::IsKeyDown(Negative) ? 1.0f : 0.0f);
}
}

void FEditorViewportClient::InitializeCamera(const UCameraComponent& MainCamera, const int32 ViewIndex, const FVector2 OutputSize)
{
	assert(ViewIndex >= 0 && ViewIndex < 4);
	const FCameraProjection Perspective{EProjectionMode::Perspective, MainCamera.GetFieldOfView(), MainCamera.GetOrthoWidth(), MainCamera.GetNearZ(), MainCamera.GetFarZ()};
	ViewCamera.Projection = Perspective;
	if (ViewIndex == 0)
	{
		ViewCamera.Location = {-11.665390f, 6.117728f, 9.921079f};
		ViewCamera.Rotation = {0.081251f, 0.256328f, -0.291035f, 0.918146f};
		ViewCamera.Rotation = FRotator(ViewCamera.Rotation.ToFRotator().Pitch, ViewCamera.Rotation.ToFRotator().Yaw, 0.0f).Quaternion();
		CameraPreset = EViewportCameraPreset::Perspective;
	}
	else
	{
		const EViewportCameraPreset Presets[] = {EViewportCameraPreset::Top, EViewportCameraPreset::Front, EViewportCameraPreset::Left};
		ApplyCameraPreset(Presets[ViewIndex - 1], OutputSize);
	}
	FSceneView::ConstrainOrthographicWidth(ViewCamera.Projection, FRect{0.0f, 0.0f, OutputSize.X, OutputSize.Y});
}

void FEditorViewportClient::OnOutputSizeChanged(const FVector2 OutputSize)
{
    FSceneView::ConstrainOrthographicWidth(ViewCamera.Projection, FRect{0.0f, 0.0f, OutputSize.X, OutputSize.Y});
}

void FEditorViewportClient::SetCamera(const FEditorCameraState& Camera, const FVector2 OutputSize)
{
	ViewCamera = Camera;
	FSceneView::ConstrainOrthographicWidth(ViewCamera.Projection, FRect{0.0f, 0.0f, OutputSize.X, OutputSize.Y});
	if (ViewCamera.Projection.Mode == EProjectionMode::Perspective)
		ViewCamera.Rotation = FRotator(ViewCamera.Rotation.ToFRotator().Pitch, ViewCamera.Rotation.ToFRotator().Yaw, 0.0f).Quaternion();
	CameraPreset = Camera.Projection.Mode == EProjectionMode::Perspective
		? EViewportCameraPreset::Perspective
		: EViewportCameraPreset::OrthographicView;
}

void FEditorViewportClient::ApplyCameraProperties(const FEditorCameraState& Camera, const bool bRotationChanged, const FVector2 OutputSize)
{
	const EViewportCameraPreset PreviousPreset = CameraPreset;
	const bool bKeepPreset = !bRotationChanged
		&& ViewCamera.Projection.Mode == Camera.Projection.Mode;
	SetCamera(Camera, OutputSize);
	if (bKeepPreset)
		CameraPreset = PreviousPreset;
}

void FEditorViewportClient::ApplyCameraPreset(const EViewportCameraPreset Preset, const FVector2 OutputSize)
{
	FEditorCameraState& Camera = ViewCamera;
	FSceneView::ConstrainOrthographicWidth(Camera.Projection, FRect{0.0f, 0.0f, OutputSize.X, OutputSize.Y});
	CameraPreset = Preset;
	Camera.Projection.Mode = Preset == EViewportCameraPreset::Perspective
		? EProjectionMode::Perspective
		: EProjectionMode::Orthographic;

	switch (Preset)
	{
	case EViewportCameraPreset::Top:
		Camera.Location = {0.0f, 0.0f, 10.0f};
		Camera.Rotation = {0.0f, 0.70710678f, 0.0f, 0.70710678f};
		break;
	case EViewportCameraPreset::Bottom:
		Camera.Location = {0.0f, 0.0f, -10.0f};
		Camera.Rotation = {0.0f, -0.70710678f, 0.0f, 0.70710678f};
		break;
	case EViewportCameraPreset::Front:
		Camera.Location = {10.0f, 0.0f, 0.0f};
		Camera.Rotation = {0.0f, 0.0f, 1.0f, 0.0f};
		break;
	case EViewportCameraPreset::Back:
		Camera.Location = {-10.0f, 0.0f, 0.0f};
		Camera.Rotation = {0.0f, 0.0f, 0.0f, 1.0f};
		break;
	case EViewportCameraPreset::Left:
		Camera.Location = {0.0f, -10.0f, 0.0f};
		Camera.Rotation = {0.0f, 0.0f, 0.70710678f, 0.70710678f};
		break;
	case EViewportCameraPreset::Right:
		Camera.Location = {0.0f, 10.0f, 0.0f};
		Camera.Rotation = {0.0f, 0.0f, -0.70710678f, 0.70710678f};
		break;
	case EViewportCameraPreset::Perspective:
		Camera.Rotation = FRotator(Camera.Rotation.ToFRotator().Pitch, Camera.Rotation.ToFRotator().Yaw, 0.0f).Quaternion();
		break;
	case EViewportCameraPreset::OrthographicView:
		// 투영 방식만 바꾸는 프리셋은 현재 Transform을 의도적으로 유지한다.
		break;
	}
}

EGridPlane FEditorViewportClient::GetGridPlane(const FSceneView& View) const
{
	switch (CameraPreset)
	{
	case EViewportCameraPreset::Top:
	case EViewportCameraPreset::Bottom:
		return EGridPlane::XY;
	case EViewportCameraPreset::Front:
	case EViewportCameraPreset::Back:
		return EGridPlane::YZ;
	case EViewportCameraPreset::Left:
	case EViewportCameraPreset::Right:
		return EGridPlane::XZ;
	case EViewportCameraPreset::Perspective:
		return EGridPlane::XY;
	case EViewportCameraPreset::OrthographicView:
		break;
	}

	const FVector Forward = View.GetCameraForward();
	const float AbsX = std::fabs(Forward.X);
	const float AbsY = std::fabs(Forward.Y);
	const float AbsZ = std::fabs(Forward.Z);
	if (AbsZ >= AbsX && AbsZ >= AbsY) return EGridPlane::XY;
	if (AbsY >= AbsX) return EGridPlane::XZ;
	return EGridPlane::YZ;
}

void FEditorViewportClient::UpdateInput(const FViewportInput& Input, const FVector2 OutputSize)
{
    const float DeltaTime = Input.DeltaTime;
    const bool bCaptured = Input.bCaptured;
    const bool bPointerOver = Input.bPointerOver;
    const int32 WheelDelta = Input.WheelDelta;
    const float MoveSpeed = Input.MoveSpeed;
    const float MouseSensitivity = Input.MouseSensitivity;
	constexpr float PerspectiveWheelSpeed = 0.1f;
	FVector MoveAxis{};
	float ZoomDelta = 0.0f;
	const bool bCameraCaptured = FInputSystem::IsMouseDown(EMouseButton::Right) && bCaptured;
	const float ForwardAxis = bCameraCaptured ? AxisValue(EKeyCode::W, EKeyCode::S) : 0.0f;
	const float RightAxis = bCameraCaptured ? AxisValue(EKeyCode::D, EKeyCode::A) : 0.0f;
	const float VerticalAxis = bCameraCaptured ? AxisValue(EKeyCode::E, EKeyCode::Q) : 0.0f;
	const bool bOrthographic = ViewCamera.Projection.Mode == EProjectionMode::Orthographic;
	// 직교 카메라 이동은 화면 평면으로 제한한다.
	// A/D는 수평, W/S는 수직으로 이동한다. 선택한 축 정렬 View가 뒤집히거나
	// 깊이축을 따라 이동하지 않도록 Q/E와 마우스 회전은 의도적으로 무시한다.
	const FVector Axis = bOrthographic
		? FVector(0.0f, RightAxis, ForwardAxis).Normalized()
		: FVector(ForwardAxis, RightAxis, VerticalAxis).Normalized();
	MoveAxis = {Axis.X * MoveSpeed, Axis.Y * MoveSpeed, Axis.Z * MoveSpeed};

	bool bUpdateEulerRotation = false;
	float UpdatedYawDegrees = 0.0f;
	float UpdatedPitchDegrees = 0.0f;
	if (!bOrthographic && bCameraCaptured)
	{
		const FQuat& CurrentRotation = ViewCamera.Rotation;
		const float CurrentYawDegrees = CurrentRotation.ToFRotator().Yaw;
		const float CurrentPitchDegrees = -CurrentRotation.ToFRotator().Pitch;
		const float YawDeltaDegrees = FMath::Clamp(FInputSystem::GetMouseDeltaX() * MouseSensitivity, -MaximumRotationDegreesPerFrame, MaximumRotationDegreesPerFrame);
		const float PitchDeltaDegrees = FMath::Clamp(-FInputSystem::GetMouseDeltaY() * MouseSensitivity, -MaximumRotationDegreesPerFrame, MaximumRotationDegreesPerFrame);
		UpdatedYawDegrees = CurrentYawDegrees + YawDeltaDegrees;
		UpdatedPitchDegrees = FMath::Clamp(CurrentPitchDegrees + PitchDeltaDegrees, -MaximumCameraPitchDegrees, MaximumCameraPitchDegrees);
		bUpdateEulerRotation = true;
	}
	if (bOrthographic)
	{
		// 패널 밖이나 다른 View 위의 휠 입력은 마지막 선택 카메라에 전달하지 않는다.
		const int RoutedWheelDelta = bCameraCaptured || bPointerOver ? WheelDelta : 0;
		const float WheelSteps = static_cast<float>(RoutedWheelDelta) / MouseWheelDeltaPerStep;
		ViewCamera.Projection.OrthoWidth = std::max(MinimumOrthographicWidth, ViewCamera.Projection.OrthoWidth * std::pow(OrthographicZoomFactorPerStep, WheelSteps));
		// 큰 휠 입력과 overflow도 동일한 안전 범위의 최댓값으로 제한한다.
		FSceneView::ConstrainOrthographicWidth(ViewCamera.Projection, FRect{0.0f, 0.0f, OutputSize.X, OutputSize.Y});
	}
	else
	{
		ZoomDelta = bCameraCaptured || bPointerOver
			? static_cast<float>(WheelDelta) * PerspectiveWheelSpeed : 0.0f;
	}
	assert(DeltaTime >= 0.0f);
	const FQuat& Q = ViewCamera.Rotation;
	assert(Q.X * Q.X + Q.Y * Q.Y + Q.Z * Q.Z + Q.W * Q.W > 1.0e-12f);
	FEditorCameraState UpdatedCamera = ViewCamera;
	const FVector LocalTranslation{MoveAxis.X + ZoomDelta, MoveAxis.Y, MoveAxis.Z};
	UpdatedCamera.Location += ViewCamera.Rotation.Normalized().RotateVector(LocalTranslation) * DeltaTime;
	if (bOrthographic && bCameraCaptured)
	{
		const float WorldUnitsPerPixel = ViewCamera.Projection.OrthoWidth / OutputSize.X;
		const FVector Right = ViewCamera.Rotation.GetRightVector().Normalized();
		const FVector Up = ViewCamera.Rotation.GetUpVector().Normalized();
		// 화면을 잡아 끄는 방향으로 보이도록 카메라는 수평 드래그의 반대, 수직 드래그의 Up 방향으로 이동한다.
		const FVector PanOffset = Right * (-static_cast<float>(FInputSystem::GetMouseDeltaX()) * WorldUnitsPerPixel)
			+ Up * (static_cast<float>(FInputSystem::GetMouseDeltaY()) * WorldUnitsPerPixel);
		UpdatedCamera.Location += PanOffset;
	}
	if (bUpdateEulerRotation)
		UpdatedCamera.Rotation = FRotator(-UpdatedPitchDegrees, UpdatedYawDegrees, 0.0f).Quaternion();
	ViewCamera = UpdatedCamera;
}

FSceneView FEditorViewportClient::CreateSceneView(const FVector2 OutputSize) const
{
    // 기존 뷰 행렬과 같이 사용 시에만 정규화한다. 저장된 quaternion 성분은 덮어쓰지 않는다.
    const FQuat& Q = ViewCamera.Rotation;
    assert(Q.X * Q.X + Q.Y * Q.Y + Q.Z * Q.Z + Q.W * Q.W > 1.0e-12f);
    return FSceneView(ViewCamera.Location, Q.Normalized().ToFMatrix(), ViewCamera.Projection, FRect{0.0f, 0.0f, OutputSize.X, OutputSize.Y}, GetViewRenderMode());
}
