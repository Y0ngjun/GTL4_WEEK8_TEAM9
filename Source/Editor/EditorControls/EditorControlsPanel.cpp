#include "EnginePCH.h"

#include "Editor/EditorControls/EditorControlsPanel.h"
#include "Editor/Viewports/ViewportsPanel.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"

#include "Engine/World.h"

#include "Input/InputSystem.h"

#include <cmath>

// 패널 초기화 성공을 반환한다.
bool FEditorControlsPanel::Init()
{
	return true;
}

// Space 입력으로 기즈모 편집 모드를 순환한다.
void FEditorControlsPanel::Tick(float DeltaTime)
{
	if (ImGui::GetIO().WantTextInput)
		return;

	if (Gizmo && FInputSystem::IsKeyPressed(EKeyCode::Space))
	{
		const int NextMode = (static_cast<int>(Gizmo->GetMode()) + 1) % 3;
		Gizmo->SetMode(static_cast<EGizmoMode>(NextMode));
	}
}

// 선택한 클래스의 액터를 월드에 생성한다.
void FEditorControlsPanel::AddActor(uint32 Index)
{
	if (IsPlayingQuery && IsPlayingQuery())
		return;

	World->SpawnActor(Classes[Index]);
}

// 액터 생성·카메라 속성·기즈모·경로 추적 UI를 그린다.
void FEditorControlsPanel::OnRender()
{
	ImGui::SetNextWindowSize(ImVec2(400, 200), ImGuiCond_FirstUseEver);
	ImGui::Begin("Editor Controls");
	ImGui::Spacing();

	char FpsText[64];
	std::snprintf(FpsText, sizeof(FpsText), "FPS %.1f   %.1f ms", 1.0f / DeltaTime, DeltaTime * 1000.0f);
	float TextWidth = ImGui::CalcTextSize(FpsText).x;
	float CursorX = ImGui::GetCursorPosX();
	float AvailableWidth = ImGui::GetContentRegionAvail().x;

	ImGui::SetCursorPosX(CursorX + AvailableWidth - TextWidth);
	ImGui::TextDisabled("%s", FpsText);

	//////////////////////////////////////////////////////

	ImGui::Dummy(ImVec2(0.0f, SectionGap));
	ImGui::SeparatorText("Actor Spawn");

	const float SpawnButtonWidth = 70.0f;

	char CountText[32];
	std::snprintf(
		CountText,
		sizeof(CountText),
		"%d Actors",
		World->GetActorNum()
	);

	const float CountWidth = ImGui::CalcTextSize(CountText).x;
	const float Available = ImGui::GetContentRegionAvail().x;
	const float Spacing = ImGui::GetStyle().ItemSpacing.x;

	const float ComboWidth =
		Available - SpawnButtonWidth - CountWidth - Spacing * 2.0f;

	ImGui::SetNextItemWidth(ComboWidth);
	ImGui::Combo("##ActorType", &SelectedIndex, Items, IM_ARRAYSIZE(Items));

	ImGui::SameLine();

	// PIE 중에는 보이지 않는 에디터 월드에 스폰되므로 막는다.
	const bool bPlaying = IsPlayingQuery && IsPlayingQuery();
	ImGui::BeginDisabled(bPlaying);
	if (ImGui::Button("Spawn", ImVec2(SpawnButtonWidth, 0)))
	{
		AddActor(SelectedIndex);
	}
	ImGui::EndDisabled();

	ImGui::SameLine();
	ImGui::TextDisabled("%s", CountText);

	//////////////////////////////////////////////////////

	DrawCameraProperties();

	//////////////////////////////////////////////////////

	// Gizmo가 없는 앱(Benchmark 등)에서는 이 섹션을 통째로 건너뛴다.
	if (Gizmo)
	{
		ImGui::Dummy(ImVec2(0.0f, SectionGap));
		ImGui::SeparatorText("Gizmo");
	}

	if (Gizmo && ImGui::BeginTable("GizmoControls", 2))
	{
		ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthFixed, 70.0f);
		ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);

		ImGui::TableNextRow();
		ImGui::TableSetColumnIndex(0);
		ImGui::Text("Mode");

		ImGui::TableSetColumnIndex(1);
		ImGui::SetNextItemWidth(-1.0f);

		GizmoSelectedIndex = static_cast<int32>(Gizmo->GetMode());
		if (ImGui::Combo("##GizmoMode", &GizmoSelectedIndex, GizmoItems, IM_ARRAYSIZE(GizmoItems)))
		{
			Gizmo->SetMode(static_cast<EGizmoMode>(GizmoSelectedIndex));
		}

		ImGui::TableNextRow();
		ImGui::TableSetColumnIndex(0);
		ImGui::Text("Space");

		ImGui::TableSetColumnIndex(1);
		ImGui::SetNextItemWidth(-1.0f);

		if (ImGui::Combo("##GizmoSpace", &SpaceSelectedIndex, SpaceItems, IM_ARRAYSIZE(SpaceItems)))
		{
			Gizmo->SetSpace(static_cast<EGizmoSpace>(SpaceSelectedIndex));
		}

		ImGui::EndTable();
	}

	//////////////////////////////////////////////////////

	ImGui::End();
}

// 실제 View 속성을 편집하며 패널 이동 후에도 마지막 View를 유지하고 변경된 값만 반영한다.
void FEditorControlsPanel::DrawCameraProperties()
{
	ImGui::Dummy(ImVec2(0.0f, SectionGap));
	ImGui::SeparatorText("Viewport");

	UCameraComponent* CamCom = World && World->GetMainCamera() ? World->GetMainCamera()->GetCameraComponent() : nullptr;
	if (!ViewportsPanel && !CamCom) return;

	int32 CameraViewIndex = 0;
	FEditorCameraState Camera{};

	if (ViewportsPanel)
	{
		CameraViewIndex = ViewportsPanel->GetEditorViewIndex();

		const bool bMaximized = ViewportsPanel->IsSlotMaximized();
		if (bMaximized) CameraViewIndex = ViewportsPanel->GetMaximizedSlotIndex();

		ImGui::SetNextItemWidth(-1.0f);
		ImGui::BeginDisabled(bMaximized);

        const FString Preview = std::format("View {}", CameraViewIndex);
        if (ImGui::BeginCombo("##CameraView", Preview.c_str()))
        {
            for (int32 Index = 0; Index < ViewportsPanel->GetSlotCount(); ++Index)
            {
                const FString Label = std::format("View {}", Index);
                if (ImGui::Selectable(Label.c_str(), CameraViewIndex == Index))
                {
                    CameraViewIndex = Index;
                    ViewportsPanel->SetEditorViewIndex(Index);
                }
                if (CameraViewIndex == Index)
                    ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
		ImGui::EndDisabled();

		Camera = ViewportsPanel->GetEditorClient(CameraViewIndex)->GetCamera();
	}

	bool bChanged = false;
	bool bOrthogonal = ViewportsPanel ? Camera.Projection.Mode == EProjectionMode::Orthographic : CamCom->GetIsOrthogonal();

	ImGui::Dummy(ImVec2(0.0f, SubsectionGap));
	ImGui::TextDisabled("Projection");

	// Camera Projection Table
	if (ImGui::BeginTable("Camera Projection", 2))
	{
		ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthFixed, 100.0f);
		ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);

		// FOV 또는 Ortho Width
		ImGui::TableNextRow();
		ImGui::TableSetColumnIndex(0);
		ImGui::Text(bOrthogonal ? "Ortho Width" : "FOV");

		ImGui::TableSetColumnIndex(1);
		ImGui::SetNextItemWidth(-1.0f);

		if (bOrthogonal)
		{
			float OrthoWidth = ViewportsPanel ? Camera.Projection.OrthoWidth : CamCom->GetOrthoWidth();
			if (ImGui::DragFloat("##OrthoWidth", &OrthoWidth, 0.1f) && std::isfinite(OrthoWidth))
			{
				OrthoWidth = FMath::Clamp(OrthoWidth, 0.01f, 1000000.0f);

				if (ViewportsPanel)
				{
					Camera.Projection.OrthoWidth = OrthoWidth;
					bChanged = true;
				}
				else
				{
					CamCom->SetOrthoWidth(OrthoWidth);
				}
			}
		}
		else
		{
			float FOV = ViewportsPanel ? Camera.Projection.FovDegrees : CamCom->GetFieldOfView();
			if (ImGui::DragFloat("##FOV", &FOV, 0.1f) && std::isfinite(FOV))
			{
				FOV = FMath::Clamp(FOV, 1.0f, 179.0f);

				if (ViewportsPanel)
				{
					Camera.Projection.FovDegrees = FOV;
					bChanged = true;
				}
				else
				{
					CamCom->SetFieldOfView(FOV);
				}
			}
		}

		// Near / Far
		ImGui::TableNextRow();

		ImGui::TableSetColumnIndex(0);
		ImGui::Text("Near");

		ImGui::TableSetColumnIndex(1);
		float Width = ImGui::GetContentRegionAvail().x;
		float Near = ViewportsPanel ? Camera.Projection.NearClip : CamCom->GetNearZ();

		ImGui::SetNextItemWidth(-1.0f);
		bool Changed = ImGui::InputFloat("##Near", &Near);

		ImGui::TableNextRow();

		ImGui::TableSetColumnIndex(0);
		ImGui::Text("Far");

		ImGui::TableSetColumnIndex(1);
		float Far = ViewportsPanel ? Camera.Projection.FarClip : CamCom->GetFarZ();
		ImGui::SetNextItemWidth(-1.0f);
		Changed |= ImGui::InputFloat("##Far", &Far);

		if (Changed && std::isfinite(Near) && std::isfinite(Far) && Near > 0 && Far > Near)
		{
			if (ViewportsPanel)
			{
				Camera.Projection.NearClip = Near;
				Camera.Projection.FarClip = Far;
				bChanged = true;
			}
			else
			{
				CamCom->SetNearZ(Near);
				CamCom->SetFarZ(Far);
			}
		}
		ImGui::EndTable();
	}

	// Camera Transform Table
	ImGui::Dummy(ImVec2(0.0f, SubsectionGap));
	ImGui::TextDisabled("Transform");

	bool bLocationChanged = false;
	bool bRotationChanged = false;

	if (ImGui::BeginTable("Camera Transform", 2))
	{
		ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthFixed, 100.0f);
		ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);

		ImGui::TableNextRow();

		// Camera Location
		ImGui::TableSetColumnIndex(0);
		ImGui::Text("Location");

		ImGui::TableSetColumnIndex(1);
		float Available = ImGui::GetContentRegionAvail().x;
		float ItemWidth = (Available - ImGui::GetStyle().ItemSpacing.x * 2.0f) / 3.0f;

		FVector Location = ViewportsPanel ?
			FVector(Camera.Location.X, Camera.Location.Y, Camera.Location.Z) :
			CamCom->GetRelativeLocation();

		ImGui::SetNextItemWidth(ItemWidth);
		bLocationChanged |= ImGui::DragFloat("##LocationX", &Location.X, 0.1f);

		ImGui::SameLine();

		ImGui::SetNextItemWidth(ItemWidth);
		bLocationChanged |= ImGui::DragFloat("##LocationY", &Location.Y, 0.1f);

		ImGui::SameLine();

		ImGui::SetNextItemWidth(ItemWidth);
		bLocationChanged |= ImGui::DragFloat("##LocationZ", &Location.Z, 0.1f);

		if (bLocationChanged && std::isfinite(Location.X) && std::isfinite(Location.Y) && std::isfinite(Location.Z))
		{
			if (ViewportsPanel)
			{
				Camera.Location = { Location.X, Location.Y, Location.Z };
				bChanged = true;
			}
			else CamCom->SetRelativeLocation(Location);
		}

		ImGui::TableNextRow();

		// Camera Rotation
		ImGui::TableSetColumnIndex(0);
		ImGui::Text("Rotation");

		ImGui::TableSetColumnIndex(1);
		FRotator Rotation = ViewportsPanel ?
			FQuat(Camera.Rotation.X, Camera.Rotation.Y, Camera.Rotation.Z, Camera.Rotation.W).ToFRotator() :
			CamCom->GetRelativeRotation();

		ImGui::SetNextItemWidth(ItemWidth);
		bRotationChanged |= ImGui::DragFloat("##Pitch", &Rotation.Pitch, 0.1f);

		ImGui::SameLine();

		ImGui::SetNextItemWidth(ItemWidth);
		bRotationChanged |= ImGui::DragFloat("##Yaw", &Rotation.Yaw, 0.1f);

		ImGui::SameLine();

		ImGui::SetNextItemWidth(ItemWidth);
		ImGui::BeginDisabled(!bOrthogonal);
		bRotationChanged |= ImGui::DragFloat("##Roll", &Rotation.Roll, 0.1f);
		ImGui::EndDisabled();

		if (bRotationChanged && std::isfinite(Rotation.Pitch) && std::isfinite(Rotation.Yaw) && std::isfinite(Rotation.Roll))
		{
			if (ViewportsPanel)
			{
				if (!bOrthogonal)
				{
					Rotation.Pitch = FMath::Clamp(Rotation.Pitch, -89.0f, 89.0f);
					Rotation.Roll = 0.0f;
				}
				const FQuat Q = Rotation.Quaternion();
				Camera.Rotation = { Q.X, Q.Y, Q.Z, Q.W };
				bChanged = true;
			}
			else CamCom->SetRelativeRotation(Rotation);
		}
		ImGui::EndTable();
	}
	if (ViewportsPanel && bChanged)
	{
		ViewportsPanel->GetEditorClient(CameraViewIndex)->ApplyCameraProperties(Camera, bRotationChanged, ViewportsPanel->GetViewport(CameraViewIndex)->GetOutputSize());
	}
}
