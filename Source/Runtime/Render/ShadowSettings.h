#pragma once
#include "Core/Types.h"


struct FShadowSettings
{
	bool bCastShadows = true;
	uint32 Resolution = 2048; // ?
	float DepthBias = 0.005f; // ?
	float SlopeBias = 0.0f; // ?
};