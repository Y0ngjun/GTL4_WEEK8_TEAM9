#include "EnginePCH.h"
#include "PointLightComponent.h"
#include "Engine/LightSceneProxy.h"

UPointLightComponent::UPointLightComponent()
{
    // Spot과 동일한 거리 깊이 비교에 사용할 월드 거리 초기값.
    ShadowBias = 0.001f;
    ShadowSlopeBias = 0.01f;
    ShadowMaxSlope = 2.0f;
}

void UPointLightComponent::PostEditChangeProperty(const FProperty& Property)
{
	Super::PostEditChangeProperty(Property);

	// 음수 반경은 감쇠 계산을 뒤집으므로 막는다.
	if (AttenuationRadius < 0.0f)
		AttenuationRadius = 0.0f;
}

FPointLightInfo UPointLightComponent::GetPointLightInfo() const
{
	FPointLightInfo Info;
	Info.Color = GetShaderColorWithIntensity();
	Info.AttenuationRadius = AttenuationRadius;
	Info.Position = GetWorldLocation();
	return Info;
}

FLightSceneProxy* UPointLightComponent::CreateSceneProxy()
{
	return new FPointLightSceneProxy(this);
}
