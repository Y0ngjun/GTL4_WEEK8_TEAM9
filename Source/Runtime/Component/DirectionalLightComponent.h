#pragma once

#include "LightComponent.h"

class FLineBatcher;

class UDirectionalLightComponent : public ULightComponent
{
	DECLARE_CLASS(UDirectionalLightComponent, ULightComponent)
	REFLECT_START(UDirectionalLightComponent)
		PROPERTY(LightSourceAngle)
	REFLECT_END()

public:
	UDirectionalLightComponent();
	virtual ~UDirectionalLightComponent() override = default;

	FDirectionalLightInfo GetDirectionalLightInfo() const;
	virtual FLightSceneProxy* CreateSceneProxy() override;

	void DrawDebug(FLineBatcher* LineBatcher) const;

	float GetLightSourceAngle() const { return LightSourceAngle; }
	void SetLightSourceAngle(float Value) { LightSourceAngle = Value; MarkSceneProxyDirty(); }

private:
	// 광원의 각지름(도). 태양 ≈ 0.5357. 0이면 딱딱한 그림자.
	float LightSourceAngle = 0.5357f;
};
