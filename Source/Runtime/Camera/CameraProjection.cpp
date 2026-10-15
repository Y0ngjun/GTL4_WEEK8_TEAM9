#include "EnginePCH.h"
#include "Camera/CameraProjection.h"
#include "Math/EngineMath.h"
#include <cassert>

FMatrix FCameraProjection::GetMatrix(const float AspectRatio) const
{
    assert(AspectRatio > 0.0f && NearClip > 0.0f && FarClip > NearClip);
    // 기존 UCameraComponent의 +X depth, row-vector, D3D [0,1] 투영.
    if (Mode == EProjectionMode::Perspective)
    {
        assert(FovDegrees > 0.0f && FovDegrees < 180.0f);
        const float YScale = 1.0f / std::tan(FMath::DegreesToRadians(FovDegrees) * 0.5f);
        const float XScale = YScale / AspectRatio;
        const float ZScale = FarClip / (FarClip - NearClip);
        const float ZOffset = -NearClip * FarClip / (FarClip - NearClip);
        return FMatrix(0, 0, ZScale, 1, XScale, 0, 0, 0, 0, YScale, 0, 0, 0, 0, ZOffset, 0);
    }
    assert(OrthoWidth > 0.0f);
    const float XScale = 2.0f / OrthoWidth;
    const float YScale = 2.0f / (OrthoWidth / AspectRatio);
    const float ZScale = 1.0f / (FarClip - NearClip);
    const float ZOffset = -NearClip / (FarClip - NearClip);
    return FMatrix(0, 0, ZScale, 0, XScale, 0, 0, 0, 0, YScale, 0, 0, 0, 0, ZOffset, 1);
}
