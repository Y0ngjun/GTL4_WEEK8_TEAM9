#pragma once

#include "Core/Types.h"
#include "Math/Vector.h"
#include "Math/Matrix.h"
#include "Render/Buffer.h"
#include <d3d11.h>
#include <wrl/client.h>

inline constexpr uint32 LIGHT_CULLING_TILE_SIZE = 64;   // tile = 64x64

struct FCullingLight
{
    FVector SphereCenter;
    float Radius = 0.0f;
};

struct FTileCullingConstants
{
    uint32 ViewSize[2] = {};
    uint32 TileCountX = 0;
    uint32 LightCount = 0;

    FMatrix Projection;

    uint32 MaxLightsPerTile = 0;
    uint32 TileSize = LIGHT_CULLING_TILE_SIZE;
    uint32 Padding[2] = {};
};


class FShaderProgram;
class FTexture2D;
struct FTileLightCullingResources;


class FTileLightCulling
{
public:
    bool Init();
    bool Execute(FTileLightCullingResources& Resources, const uint32 ViewWidth, const uint32 ViewHeight, const FMatrix& Projection, FTexture2D* Depth, const FCullingLight* Lights, const uint32 LightCount);
private:
    void UploadInputs(FTileLightCullingResources& Resources, const FCullingLight* Lights, const FTileCullingConstants& Constants);

    FShaderProgram* ComputeShader = nullptr;

    // 타일 컬링 상수를 업로드할 버퍼
    TUniquePtr<FConstantBuffer> ConstantsBuffer;

    const uint32 MaxLightsPerTile = 64;         // 이 값은 tile size와 관련없이 임의로 정할 수 있는 값이다
};



static_assert(sizeof(FCullingLight) == 16);
static_assert(offsetof(FCullingLight, Radius) == 12);

static_assert(sizeof(FTileCullingConstants) == 96);
static_assert(offsetof(FTileCullingConstants, Projection) == 16);
static_assert(offsetof(FTileCullingConstants, MaxLightsPerTile) == 80);
