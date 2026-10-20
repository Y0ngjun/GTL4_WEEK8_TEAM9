#pragma once

#include "LightComponentBase.h"

class ULightComponent : public ULightComponentBase
{
	DECLARE_CLASS(ULightComponent, ULightComponentBase)
	REFLECT_START(ULightComponent)
		PROPERTY(bCastShadow)
		PROPERTY(ShadowMapResolution)
		PROPERTY(ShadowBias)
		PROPERTY(ShadowSlopeBias)
		PROPERTY(ShadowMaxSlope)
		PROPERTY(ShadowNormalOffset)
		PROPERTY(bUsePCSS)
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
	uint32 GetShadowMapResolution() const
	{
		const uint32 Value = static_cast<uint32>(ShadowMapResolution);
		return Value >= 256 && Value <= 2048 && (Value & (Value - 1)) == 0 ? Value : 1024u;
	}
	void SetShadowMapResolution(uint32 Value)
	{
		if (Value < 256 || Value > 2048 || (Value & (Value - 1)) != 0) return;
		ShadowMapResolution = static_cast<int32>(Value);
		MarkSceneProxyDirty();
	}
	float GetShadowBias() const { return ShadowBias; }
	void SetShadowBias(float Value) { ShadowBias = Value; MarkSceneProxyDirty(); }
	float GetShadowSlopeBias() const { return ShadowSlopeBias; }
	void SetShadowSlopeBias(float Value) { ShadowSlopeBias = Value; MarkSceneProxyDirty(); }
	float GetShadowMaxSlope() const { return ShadowMaxSlope; }
	void SetShadowMaxSlope(float Value) { ShadowMaxSlope = Value; MarkSceneProxyDirty(); }
	float GetShadowNormalOffset() const { return ShadowNormalOffset; }
	void SetShadowNormalOffset(float Value) { ShadowNormalOffset = Value; MarkSceneProxyDirty(); }
	bool UsesPCSS() const { return bUsePCSS; }
	void SetUsePCSS(bool bValue) { bUsePCSS = bValue; MarkSceneProxyDirty(); }

protected:
	bool bCastShadow = true;
	// Point는 각 면, CSM은 각 Cascade에 적용한다.
	int32 ShadowMapResolution = 1024;
	// Bias는 월드 거리 단위다.
	// SlopeBias는 무차원 k: Slope Bias = k * (T/2) * tan(theta), T는 쉐도우 맵 텍셀의 월드 크기. k = 1이 이론상 최소값이다.
	// MaxSlope는 tan(theta)의 무차원 상한이다.
	// 임시값: 실제 기본값은 각 Light의 그림자 구현 단계에서 정한다.
	float ShadowBias = 0.0f;
	float ShadowSlopeBias = 0.0f;
	float ShadowMaxSlope = 0.0f;
	// 무차원 k: 조회 위치를 법선 방향으로 k * (T/2) * sin(theta)만큼 띄운다. k = 1이 이론상 최소값이다.
	// 0이면 끈다. Slope Bias와 함께 켜면 두 보정이 더해진다.
	float ShadowNormalOffset = 0.0f;
	// 켜면 PCSS(차폐물 거리에 따라 반영 폭이 바뀌는 그림자), 끄면 기본 조회(Directional: 2x2 PCF, Spot/Point: 단일 비교)를 쓴다.
	// 반영 폭은 Directional의 LightSourceAngle, Spot/Point의 SourceRadius로 정한다.
	bool bUsePCSS = true;
};
