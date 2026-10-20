#pragma once

#include "LightComponent.h"

class FLineBatcher;

class UDirectionalLightComponent : public ULightComponent
{
	DECLARE_CLASS(UDirectionalLightComponent, ULightComponent)
	REFLECT_START(UDirectionalLightComponent)
        PROPERTY(bUseCSM)
        PROPERTY(CascadeCount)
        PROPERTY(CascadeMaxDistance)
        PROPERTY(CascadeDistributionExponent)
        PROPERTY(CascadeBlendRatio)
    REFLECT_END()

public:
	UDirectionalLightComponent();
	virtual ~UDirectionalLightComponent() override = default;

	FDirectionalLightInfo GetDirectionalLightInfo() const;
    FShadowCascadeSettings GetCascadeSettings() const;
    bool UsesCSM() const { return bUseCSM; }
    void PostEditChangeProperty(const FProperty& Property) override;
	virtual FLightSceneProxy* CreateSceneProxy() override;

	void DrawDebug(FLineBatcher* LineBatcher) const;

private:
    bool bUseCSM = false;
    int32 CascadeCount = 4;
    float CascadeMaxDistance = 100.0f;
    float CascadeDistributionExponent = 3.0f;
    float CascadeBlendRatio = 0.1f;
};
