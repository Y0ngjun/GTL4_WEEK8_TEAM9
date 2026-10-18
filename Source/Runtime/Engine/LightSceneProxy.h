#pragma once

#include "Component/AmbientLightComponent.h"
#include "Component/DirectionalLightComponent.h"
#include "Component/SpotLightComponent.h"

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
	bool IsVisible() const { return bVisible; }

protected:
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
		const FVector Forward = Info.Direction.GetSafeNormal();

		const FVector UpGuide = (std::abs(Forward.Z) > 0.99f) ? FVector(1.0f, 0.0f, 0.0f) : FVector(0.0f, 0.0f, 1.0f);
		// 카메라와 같은 규약(Right = Up × Forward). 반대로 하면 좌우가 뒤집혀 후면 컬링이 앞면을 버린다.
		const FVector Right = FVector::Cross(UpGuide, Forward).GetSafeNormal();
		const FVector Up = FVector::Cross(Forward, Right);

		FMatrix Rotation = FMatrix::Identity;
		Rotation.M[0][0] = Forward.X; Rotation.M[0][1] = Forward.Y; Rotation.M[0][2] = Forward.Z;
		Rotation.M[1][0] = Right.X;   Rotation.M[1][1] = Right.Y;   Rotation.M[1][2] = Right.Z;
		Rotation.M[2][0] = Up.X;      Rotation.M[2][1] = Up.Y;      Rotation.M[2][2] = Up.Z;

		const FMatrix View = FMatrix::MakeTranslation(-Info.Position) * Rotation.GetTransposed();

		// FCameraProjection 원근과 같은 배치(+X 깊이, D3D [0,1]). slice가 정사각형이라 가로세로 배율이 같다.
		 // 반각이 90도에 붙으면 tan이 폭주
		const float CosHalf = std::max(Info.CosOuterCone, 0.02f);
		// tan(acos(c))
		const float TanHalf = std::sqrt(1.0f - CosHalf * CosHalf) / CosHalf;      
		const float Scale = 1.0f / TanHalf;
		const float NearZ = 0.1f;
		const float FarZ = std::max(Info.AttenuationRadius, NearZ + 0.01f);
		const float ZScale = FarZ / (FarZ - NearZ);
		const float ZOffset = -NearZ * FarZ / (FarZ - NearZ);
		const FMatrix Projection(
			0.0f, 0.0f, ZScale, 1.0f,
			Scale, 0.0f, 0.0f, 0.0f,
			0.0f, Scale, 0.0f, 0.0f,
			0.0f, 0.0f, ZOffset, 0.0f);

		ShadowViewProj = View * Projection;
        CullingSphereCenter = Info.Position + Info.Direction * SphereOffset;
		bVisible = Light->IsLightVisible();
	}

	const FSpotLightInfo& GetInfo() const { return Info; }
    const FVector& GetCullingSphereCenter() const { return CullingSphereCenter; }
    float GetCullingSphereRadius() const { return CullingSphereRadius; }
	// FScene이 AllocateSlot/FreeSlot에 넘겨 slot을 바꿀 때 쓴다.
	int32& GetShadowSlot() { return ShadowSlot; }
	// 렌더 코드는 읽기만 한다. -1이면 그림자 slice가 없다.
	int32 GetShadowSlot() const { return ShadowSlot; }
	const FMatrix& GetShadowViewProj() const { return ShadowViewProj; }

private:
	FSpotLightInfo Info;
    FVector CullingSphereCenter{};
    float CullingSphereRadius = 0.0f;
    float SphereOffset = 0.0f;
    float CachedRadius = -1.0f;
    float CachedCosOuter = -2.0f;

	//FSpotShadowMapArray에 할당된 슬롯 번호
	int32  ShadowSlot = -1; 
	FMatrix ShadowViewProj;
};
