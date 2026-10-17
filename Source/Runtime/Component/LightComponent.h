#pragma once

#include "LightComponentBase.h"

class ULightComponent : public ULightComponentBase
{
	DECLARE_CLASS(ULightComponent, ULightComponentBase)
	REFLECT_START(ULightComponent)
		PROPERTY(ShadowMapResolution)
		PROPERTY(ShadowBias)
		PROPERTY(ShadowSlopeBias)
	REFLECT_END()

public:
	ULightComponent() = default;
	virtual ~ULightComponent() override = default;

	FVector GetLightDirection() const
	{
		const FMatrix World = GetWorldMatrix();
		return FVector(World[0][0], World[0][1], World[0][2]).GetSafeNormal();
	}

	FShadowSettings GetShadowSettings() const
	{
		FShadowSettings Settings;
		Settings.bCastShadows = IsCastingShadows();
		Settings.Resolution = ShadowMapResolution; // TODO : clamp를 걸기
		Settings.DepthBias = ShadowBias; //  TODO : 음수 막기
		Settings.SlopeBias = ShadowSlopeBias; // TODO : 음수 막기
		return Settings;
	}

private:
	int32 ShadowMapResolution = 2048;
	float ShadowBias = 0.005f;
	float ShadowSlopeBias = 0.0f;

};
