#pragma once

#include <format>
#include "Editor/EditorUI/EditorPanel.h"

#include "Engine/World.h"

struct FTransform;
class FRenderer;
class FDepthViewRenderer;
class ULightComponent;

using DetailsSelectionCallback = std::function<void(UActorComponent*)>;
using DetailsDeleteComponentCallback = std::function<void(UActorComponent*)>;

class FDetailsPanel : public IEditorPanel
{
public:
	FDetailsPanel() = default;
	~FDetailsPanel();

	bool Init() override;
	void Tick(float DeltaTime) override;
	void OnRender() override;

	void SelectComponent(UActorComponent* Component);
	void DrawCompoenetList(AActor* SelectedActor, UActorComponent*& OutSelectedComponent);

	const char* GetPanelName() const override { return "Details"; }

	void SetTarget(AActor* InTargetActor) 
	{ 
		TargetActor = InTargetActor;
		TargetComponent = InTargetActor ? InTargetActor->GetRootComponent() : nullptr;
	}
	void SetTargetComponent(UActorComponent* InTargetComponent) { TargetComponent = InTargetComponent; }

	void SetSelectionCallback(DetailsSelectionCallback InCallback) { Callback = InCallback; }
	void SetDeleteComponentCallback(DetailsDeleteComponentCallback InCallback) { DeleteCallback = InCallback; }

	void SetWorld(UWorld* InWorld) { World = InWorld; }

	// 소유권은 Engine에 유지하고 자원 조회와 미리보기 PS 바인딩에만 사용한다.
	void SetShadowPreviewSources(FRenderer* InRenderer, FDepthViewRenderer* InDepthViewRenderer)
	{
		PreviewRenderer = InRenderer;
		DepthPreviewRenderer = InDepthViewRenderer;
	}

	ImFont* GetCustomFont() { return CustomFont; }

private:
	void DrawSceneComponentNode(USceneComponent* Component, USceneComponent* Root, UActorComponent*& OutSelectedComponent);
	void TryReparent(USceneComponent* DroppedComponent, USceneComponent* DragComponent);
	void DrawShadowDepthPreview(ULightComponent* Light);

	FRenderer* PreviewRenderer = nullptr;
	FDepthViewRenderer* DepthPreviewRenderer = nullptr;

	UWorld* World = nullptr;
	ImFont* CustomFont = nullptr;

	AActor* TargetActor = nullptr;
	UActorComponent* TargetComponent = nullptr;
	DetailsSelectionCallback Callback;
	DetailsDeleteComponentCallback DeleteCallback;

	UActorComponent* PendingDeleteComponent = nullptr;
	UActorComponent* EditingNameComponent = nullptr;
	char ComponentNameEditBuffer[256] = {};
	bool bFocusNameEdit = false;
	bool bIsEditingName = false;

	USceneComponent* PendingDroppedComponent = nullptr;
	USceneComponent* PendingDragComponent = nullptr;
};

