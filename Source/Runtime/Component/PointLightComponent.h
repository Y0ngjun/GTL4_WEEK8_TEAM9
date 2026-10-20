#pragma once

#include "LightComponent.h"

class UPointLightComponent : public ULightComponent
{
	DECLARE_CLASS(UPointLightComponent, ULightComponent)
	REFLECT_START(UPointLightComponent)
		PROPERTY(AttenuationRadius)
		PROPERTY(SourceRadius)
	REFLECT_END()

public:
	UPointLightComponent();
	virtual ~UPointLightComponent() override = default;

	float GetAttenuationRadius() const { return AttenuationRadius; }
	float GetSourceRadius() const { return SourceRadius; }
	void SetSourceRadius(float Value) { SourceRadius = Value; MarkSceneProxyDirty(); }

	FPointLightInfo GetPointLightInfo() const;
	virtual FLightSceneProxy* CreateSceneProxy() override;

	virtual void PostEditChangeProperty(const FProperty& Property) override;

protected:
	float AttenuationRadius = 10.0f;
	// 광원 구의 반지름(월드 거리). PCSS 반영 폭에 쓴다. 0이면 딱딱한 그림자.
	float SourceRadius = 0.1f;
};
