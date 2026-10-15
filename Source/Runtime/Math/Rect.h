#pragma once

// 뷰포트 레이아웃과 렌더 호출이 공유하는 픽셀 영역.
struct FRect
{
    float X = 0, Y = 0, Width = 0, Height = 0;
    bool IsValid() const { return Width > 0.0f && Height > 0.0f; }
};
