#pragma once

#include "Component/AmbientLightComponent.h"
#include "Component/DirectionalLightComponent.h"
#include "Component/SpotLightComponent.h"

class FScene;

class FLightSceneProxy
{
public:
	FLightSceneProxy(ULightComponentBase* InComponent, ELightType InType) : Component(InComponent), Type(InType) {}
	virtual ~FLightSceneProxy() = default;

	virtual void UpdateFromComponent() = 0;

	ULightComponentBase* GetComponent() const { return Component; }
	ELightType GetLightType() const { return Type; }
	FScene* GetScene() const { return Scene; }
	bool CastsShadow() const { return bCastShadow; }
	uint32 GetShadowMapResolution() const { return ShadowMapResolution; }
	float GetShadowBias() const { return ShadowBias; }
	float GetShadowSlopeBias() const { return ShadowSlopeBias; }
	float GetShadowMaxSlope() const { return ShadowMaxSlope; }
	float GetShadowNormalOffset() const { return ShadowNormalOffset; }
	uint32 GetShadowFilterMode() const { return ShadowFilterMode; }
	bool IsVisible() const { return bVisible; }

protected:
	void UpdateShadowSettings(const ULightComponent* Light)
	{
		bCastShadow = Light->CastsShadow();
		ShadowMapResolution = Light->GetShadowMapResolution();
		ShadowBias = Light->GetShadowBias();
		ShadowSlopeBias = Light->GetShadowSlopeBias();
		ShadowMaxSlope = Light->GetShadowMaxSlope();
		ShadowNormalOffset = Light->GetShadowNormalOffset();
		ShadowFilterMode = Light->GetShadowFilterMode();
	}

	// 초기값을 false로 설정하여, Ambient에서 그림자를 생성하지 않도록 만든다.
	bool bCastShadow = false;
	uint32 ShadowMapResolution = 1024;
	float ShadowBias = 0.0f;
	float ShadowSlopeBias = 0.0f;
	float ShadowMaxSlope = 0.0f;
	float ShadowNormalOffset = 0.0f;
	uint32 ShadowFilterMode = 0;
	ULightComponentBase* Component = nullptr;
	bool bVisible = true;

private:
	friend class FScene;
	FScene* Scene = nullptr;
	ELightType Type;
	bool bQueuedForUpdate = false;
};

class FAmbientLightSceneProxy final : public FLightSceneProxy
{
public:
	explicit FAmbientLightSceneProxy(UAmbientLightComponent* InComponent) : FLightSceneProxy(InComponent, ELightType::Ambient) {}

	void UpdateFromComponent() override
	{
		const UAmbientLightComponent* Light = static_cast<const UAmbientLightComponent*>(Component);
		Info = Light->GetAmbientLightInfo();
		bVisible = Light->IsLightVisible();
	}

	const FAmbientLightInfo& GetInfo() const { return Info; }

private:
	FAmbientLightInfo Info;
};

class FDirectionalLightSceneProxy final : public FLightSceneProxy
{
public:
	explicit FDirectionalLightSceneProxy(UDirectionalLightComponent* InComponent) : FLightSceneProxy(InComponent, ELightType::Directional) {}

	void UpdateFromComponent() override
	{
		const UDirectionalLightComponent* Light = static_cast<const UDirectionalLightComponent*>(Component);
		Info = Light->GetDirectionalLightInfo();
        CascadeSettings = Light->GetCascadeSettings();
        bUseCSM = Light->UsesCSM();
		UpdateShadowSettings(Light);
		LightSourceAngle = Light->GetLightSourceAngle();
		bVisible = Light->IsLightVisible();
	}

	const FDirectionalLightInfo& GetInfo() const { return Info; }
    const FShadowCascadeSettings& GetCascadeSettings() const { return CascadeSettings; }
    bool UsesCSM() const { return bUseCSM; }
	float GetLightSourceAngle() const { return LightSourceAngle; }

private:
	FDirectionalLightInfo Info;
	float LightSourceAngle = 0.0f;
    FShadowCascadeSettings CascadeSettings;
    bool bUseCSM = false;
};

class FPointLightSceneProxy final : public FLightSceneProxy
{
public:
	explicit FPointLightSceneProxy(UPointLightComponent* InComponent) : FLightSceneProxy(InComponent, ELightType::Point) {}

	void UpdateFromComponent() override
	{
		const UPointLightComponent* Light = static_cast<const UPointLightComponent*>(Component);
		Info = Light->GetPointLightInfo();
		UpdateShadowSettings(Light);
		SourceRadius = Light->GetSourceRadius();
		bVisible = Light->IsLightVisible();
	}

	const FPointLightInfo& GetInfo() const { return Info; }
	float GetSourceRadius() const { return SourceRadius; }

private:
	FPointLightInfo Info;
	float SourceRadius = 0.0f;
};

class FSpotLightSceneProxy final : public FLightSceneProxy
{
public:
	explicit FSpotLightSceneProxy(USpotLightComponent* InComponent) : FLightSceneProxy(InComponent, ELightType::Spot) {}

	void UpdateFromComponent() override
	{
		const USpotLightComponent* Light = static_cast<const USpotLightComponent*>(Component);
		Info = Light->GetSpotLightInfo();
		UpdateShadowSettings(Light);
		SourceRadius = Light->GetSourceRadius();
        // Outer cone과 감쇠 반경으로 제한된 조명 범위를 감싸는 구를 계산한다.
        if (CachedRadius != Info.AttenuationRadius || CachedCosOuter != Info.CosOuterCone)
        {
            CachedRadius = Info.AttenuationRadius;
            CachedCosOuter = Info.CosOuterCone;
            if (Info.CosOuterCone >= std::cos(PI / 4.0f))
            {
                SphereOffset = Info.AttenuationRadius / (2.0f * Info.CosOuterCone);
                CullingSphereRadius = SphereOffset;
            }
            else
            {
                SphereOffset = Info.AttenuationRadius * Info.CosOuterCone;
                CullingSphereRadius = Info.AttenuationRadius * std::sqrt(1.0f - Info.CosOuterCone * Info.CosOuterCone);
            }
        }
        CullingSphereCenter = Info.Position + Info.Direction * SphereOffset;
		bVisible = Light->IsLightVisible();
	}

	const FSpotLightInfo& GetInfo() const { return Info; }
    const FVector& GetCullingSphereCenter() const { return CullingSphereCenter; }
    float GetCullingSphereRadius() const { return CullingSphereRadius; }
	float GetSourceRadius() const { return SourceRadius; }

private:
	FSpotLightInfo Info;
	float SourceRadius = 0.0f;
    FVector CullingSphereCenter{};
    float CullingSphereRadius = 0.0f;
    float SphereOffset = 0.0f;
    float CachedRadius = -1.0f;
    float CachedCosOuter = -2.0f;
};
