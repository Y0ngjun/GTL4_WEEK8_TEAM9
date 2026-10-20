#pragma once

#include "LightComponentBase.h"

class ULightComponent : public ULightComponentBase
{
	DECLARE_CLASS(ULightComponent, ULightComponentBase)
	REFLECT_START(ULightComponent)
		PROPERTY(bCastShadow)
		PROPERTY(ShadowBias)
		PROPERTY(ShadowSlopeBias)
		PROPERTY(ShadowMaxSlope)
	REFLECT_END()

public:
	ULightComponent() = default;
	virtual ~ULightComponent() override = default;

	FVector GetLightDirection() const
	{
		const FMatrix World = GetWorldMatrix();
		return FVector(World[0][0], World[0][1], World[0][2]).GetSafeNormal();
	}

	bool CastsShadow() const { return bCastShadow; }
	void SetCastShadow(bool bValue) { bCastShadow = bValue; MarkSceneProxyDirty(); }
	float GetShadowBias() const { return ShadowBias; }
	void SetShadowBias(float Value) { ShadowBias = Value; MarkSceneProxyDirty(); }
	float GetShadowSlopeBias() const { return ShadowSlopeBias; }
	void SetShadowSlopeBias(float Value) { ShadowSlopeBias = Value; MarkSceneProxyDirty(); }
	float GetShadowMaxSlope() const { return ShadowMaxSlope; }
	void SetShadowMaxSlope(float Value) { ShadowMaxSlope = Value; MarkSceneProxyDirty(); }

protected:
	bool bCastShadow = true;
	// Bias는 월드 거리 단위다.
	// SlopeBias는 무차원 k: Slope Bias = k * (T/2) * tan(theta), T는 쉐도우 맵 텍셀의 월드 크기. k = 1이 이론상 최소값이다.
	// MaxSlope는 tan(theta)의 무차원 상한이다.
	// 임시값: 실제 기본값은 각 Light의 그림자 구현 단계에서 정한다.
	float ShadowBias = 0.0f;
	float ShadowSlopeBias = 0.0f;
	float ShadowMaxSlope = 0.0f;
};
