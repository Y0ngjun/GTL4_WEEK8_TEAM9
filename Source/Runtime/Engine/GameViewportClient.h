#pragma once

#include "Engine/ViewportClient.h"

class UWorld;
class UCameraComponent;

// Viewport가 단독 소유한다. 월드와 카메라는 빌려 쓰며 카메라 사본을 저장하지 않는다.
class FGameViewportClient final : public FViewportClient
{
public:
	explicit FGameViewportClient(UWorld& InWorld) : World(&InWorld) {}
	UWorld* GetWorld() const override { return World; }
	UCameraComponent* GetCamera() const;
	bool HasCamera() const override { return GetCamera() != nullptr; }
	void SetInputEnabled(bool bEnabled) override;
	void UpdateInput(const FViewportInput& Input, FVector2 OutputSize) override { SetInputEnabled(Input.bEnabled); }
	FSceneView CreateSceneView(FVector2 OutputSize) const override;


private:
	UWorld* World;
};
