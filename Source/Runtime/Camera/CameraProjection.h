#pragma once

#include "Math/Matrix.h"

enum class EProjectionMode { Perspective, Orthographic };

// CameraComponent와 편집 클라이언트가 공유하는 투영 값. 화면 비율은 호출별로 받는다.
struct FCameraProjection
{
    EProjectionMode Mode = EProjectionMode::Perspective;
    float FovDegrees = 60.0f;
    float OrthoWidth = 16.0f;
    float NearClip = 0.1f;
    float FarClip = 10000.0f;

    FMatrix GetMatrix(float AspectRatio) const;
};
