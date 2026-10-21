#pragma once

#include "Buffer.h"
#include "PipelineState.h"

class FTexture2D;
struct FSceneView;
struct FShadowFrameData;

class FShadowCascadeDebugRenderer
{
public:
    bool Init();
    void OnRender(FTexture2D* SceneDepth, const FSceneView& View, const FShadowFrameData& Frame);
    static void SetEnabled(bool bEnabled) { bEnabledForViews = bEnabled; }
    static bool IsEnabled() { return bEnabledForViews; }

private:
    inline static bool bEnabledForViews = false;
    FPipelineState PipelineState;
    TUniquePtr<FConstantBuffer> ConstantBuffer;
};
