#include "EnginePCH.h"
#include "ShadowResources.h"
#include "RenderCommand.h"

bool FShadowResources::EnsureCapacity(uint32 Resolution, uint32 SliceCount, uint32 CubeCount)
{
    if (Resolution == 0 || Resolution > D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION || SliceCount > D3D11_REQ_TEXTURE2D_ARRAY_AXIS_DIMENSION || CubeCount > D3D11_REQ_TEXTURE2D_ARRAY_AXIS_DIMENSION / FTextureCubeArray::FaceCount) return false;
    FTextureArrayCreateInfo Info{Resolution, Resolution, DXGI_FORMAT_R32_TYPELESS, DXGI_FORMAT_R32_FLOAT, DXGI_FORMAT_D32_FLOAT};
    TUniquePtr<FTexture2DArray> New2D;
    TUniquePtr<FTextureCubeArray> NewCube;
    if (SliceCount && (!Depth2D || Depth2D->GetWidth() != Resolution || Depth2D->GetArraySize() < SliceCount))
    {
        New2D = MakeUnique<FTexture2DArray>(RenderCommand::GetDevice(), Info, SliceCount);
        if (!New2D->IsValid()) return false;
    }
    if (CubeCount && (!DepthCube || DepthCube->GetWidth() != Resolution || DepthCube->GetCubeCount() < CubeCount))
    {
        NewCube = MakeUnique<FTextureCubeArray>(RenderCommand::GetDevice(), Info, CubeCount);
        if (!NewCube->IsValid()) return false;
    }
    if (New2D) Depth2D = std::move(New2D);
    if (NewCube) DepthCube = std::move(NewCube);
    return true;
}
