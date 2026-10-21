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
		PROPERTY(ShadowFilterMode)
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
	// 0: PCF 2x2 (Hardware), 1: PCF 3x3 (Point), 2: PCF 5x5 (Point), 3: PCF 5x5 (Comparison), 4: PCSS
	uint32 GetShadowFilterMode() const { return ShadowFilterMode >= 0 && ShadowFilterMode <= 4 ? static_cast<uint32>(ShadowFilterMode) : 4u; }
	void SetShadowFilterMode(uint32 Value) { if (Value > 4) return; ShadowFilterMode = static_cast<int32>(Value); MarkSceneProxyDirty(); }

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
	// 그림자 필터링 방식. 0: PCF 2x2 (Hardware), 1: PCF 3x3 (Point), 2: PCF 5x5 (Point), 3: PCF 5x5 (Comparison), 4: PCSS.
	// PCSS의 반영 폭은 Directional의 LightSourceAngle, Spot/Point의 SourceRadius로 정한다. 디테일 패널은 드롭다운으로 그린다.
	int32 ShadowFilterMode = 4;
};
