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
	float GetShadowBias() const { return ShadowBias; }
	float GetShadowSlopeBias() const { return ShadowSlopeBias; }
	float GetShadowMaxSlope() const { return ShadowMaxSlope; }
	bool IsVisible() const { return bVisible; }

protected:
	void UpdateShadowSettings(const ULightComponent* Light)
	{
		bCastShadow = Light->CastsShadow();
		ShadowBias = Light->GetShadowBias();
		ShadowSlopeBias = Light->GetShadowSlopeBias();
		ShadowMaxSlope = Light->GetShadowMaxSlope();
	}

	// 초기값을 false로 설정하여, Ambient에서 그림자를 생성하지 않도록 만든다.
	bool bCastShadow = false;
	float ShadowBias = 0.0f;
	float ShadowSlopeBias = 0.0f;
	float ShadowMaxSlope = 0.0f;
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
		UpdateShadowSettings(Light);
		bVisible = Light->IsLightVisible();
	}

	const FDirectionalLightInfo& GetInfo() const { return Info; }

private:
	FDirectionalLightInfo Info;
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
		bVisible = Light->IsLightVisible();
	}

	const FPointLightInfo& GetInfo() const { return Info; }

private:
	FPointLightInfo Info;
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

private:
	FSpotLightInfo Info;
    FVector CullingSphereCenter{};
    float CullingSphereRadius = 0.0f;
    float SphereOffset = 0.0f;
    float CachedRadius = -1.0f;
    float CachedCosOuter = -2.0f;
};
