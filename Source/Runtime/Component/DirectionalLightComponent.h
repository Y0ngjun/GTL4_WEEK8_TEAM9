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
        PROPERTY(LightSourceAngle)
    REFLECT_END()

public:
	UDirectionalLightComponent();
	virtual ~UDirectionalLightComponent() override = default;

	FDirectionalLightInfo GetDirectionalLightInfo() const;
    FShadowCascadeSettings GetCascadeSettings() const;
    bool UsesCSM() const { return bUseCSM; }
    uint32 GetShadowMapResolution() const override
    {
        return bUseCSM ? ShadowCascadeResolution : Super::GetShadowMapResolution();
    }
    void PostEditChangeProperty(const FProperty& Property) override;
	virtual FLightSceneProxy* CreateSceneProxy() override;

	void DrawDebug(FLineBatcher* LineBatcher) const;

	float GetLightSourceAngle() const { return LightSourceAngle; }
	void SetLightSourceAngle(float Value) { LightSourceAngle = Value; MarkSceneProxyDirty(); }

private:
    bool bUseCSM = false;
    // 직렬화 호환용 필드. CSM 배치는 항상 4개를 사용한다.
    int32 CascadeCount = static_cast<int32>(ShadowCascadeCount);
    float CascadeMaxDistance = 100.0f;
    float CascadeDistributionExponent = 3.0f;
    float CascadeBlendRatio = 0.1f;
	// 광원의 각지름(도). 태양 ≈ 0.5357. 0이면 딱딱한 그림자.
	float LightSourceAngle = 0.5357f;
};
