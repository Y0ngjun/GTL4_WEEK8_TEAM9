#pragma once

#include "Engine/ViewportClient.h"

class UCameraComponent;
enum class EGridPlane : int32;

// editor.ini의 기존 프리셋 번호를 보존한다.
enum class EViewportCameraPreset
{
    Perspective = 0, OrthographicView = 1, Top = 2, Bottom = 3, Front = 4, Back = 5, Left = 6, Right = 7
};

// 편집/설정 UI가 한 번에 읽고 적용하는 단일 원본 상태. 게임·SceneView에서는 사용하지 않는다.
struct FEditorCameraState
{
    FVector Location{};
    FQuat Rotation = FQuat::Identity();
    FCameraProjection Projection;
};

// 편집 뷰 하나의 카메라·투영·표시 모드를 소유한다. 뷰 선택과 capture는 Panel이 맡는다.
class FEditorViewportClient : public FViewportClient
{
public:
	virtual ~FEditorViewportClient() = default;
	void SetWorld(UWorld* InWorld) { World = InWorld; }
	UWorld* GetWorld() const override { return World; }
	void InitializeCamera(const UCameraComponent& MainCamera, int32 ViewIndex, FVector2 OutputSize);
	const FEditorCameraState& GetCamera() const { return ViewCamera; }
	void SetCamera(const FEditorCameraState& Camera, FVector2 OutputSize);
	void ApplyCameraProperties(const FEditorCameraState& Camera, bool bRotationChanged, FVector2 OutputSize);
	void ApplyCameraPreset(EViewportCameraPreset Preset, FVector2 OutputSize);
	EViewportCameraPreset GetCameraPreset() const { return CameraPreset; }
	EGridPlane GetGridPlane(const FSceneView& View) const;

	void UpdateInput(const FViewportInput& Input, FVector2 OutputSize) override;

	void OnOutputSizeChanged(FVector2 OutputSize) override;
	FSceneView CreateSceneView(FVector2 OutputSize) const override;

private:
	UWorld* World = nullptr; // EditorWorld 비소유 참조.
	FEditorCameraState ViewCamera{};
	EViewportCameraPreset CameraPreset = EViewportCameraPreset::Perspective;
};
