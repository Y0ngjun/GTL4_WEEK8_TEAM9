#include "EnginePCH.h"
#include "PointLightComponent.h"
#include "Engine/LightSceneProxy.h"

UPointLightComponent::UPointLightComponent()
{
    ShadowMapResolution = 512;
    // Bias는 월드 거리, SlopeBias는 텍셀 크기 배수 k(1 = 이론상 최소값)다.
    ShadowBias = 0.001f;
    ShadowSlopeBias = 1.0f;
    ShadowMaxSlope = 2.0f;
}

void UPointLightComponent::PostEditChangeProperty(const FProperty& Property)
{
	Super::PostEditChangeProperty(Property);

	// 음수 반경은 감쇠 계산을 뒤집으므로 막는다.
	if (AttenuationRadius < 0.0f)
		AttenuationRadius = 0.0f;
	if (SourceRadius < 0.0f)
		SourceRadius = 0.0f;
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
