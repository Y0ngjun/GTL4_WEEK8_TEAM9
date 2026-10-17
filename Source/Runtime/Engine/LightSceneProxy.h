#pragma once

#include "Component/AmbientLightComponent.h"
#include "Component/DirectionalLightComponent.h"
#include "Component/SpotLightComponent.h"
#include "Render/ShadowSettings.h"

class FScene;

enum class ELightType : uint8
{
	Ambient, Directional, Point, Spot,
};

class FLightSceneProxy
{
public:
	FLightSceneProxy(ULightComponentBase* InComponent, ELightType InType) : Component(InComponent), Type(InType) {}
	virtual ~FLightSceneProxy() = default;

	virtual void UpdateFromComponent() = 0;

	ULightComponentBase* GetComponent() const { return Component; }
	ELightType GetLightType() const { return Type; }
	FScene* GetScene() const { return Scene; }
	const FShadowSettings& GetShadowSettings() const{ return ShadowSettings; }
	bool IsVisible() const { return bVisible; }

protected:
	ULightComponentBase* Component = nullptr;
	FShadowSettings ShadowSettings;
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
		ShadowSettings.bCastShadows = false;
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
		ShadowSettings = Light->GetShadowSettings();
		ShadowDistance = Light->GetShadowDistance();

		bVisible = Light->IsLightVisible();
	}

	float GetShadowDistance() const { return ShadowDistance; }
	const FDirectionalLightInfo& GetInfo() const { return Info; }

protected :
	float ShadowDistance = 100.0f;
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
		ShadowSettings = Light->GetShadowSettings();
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
		ShadowSettings = Light->GetShadowSettings();
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
