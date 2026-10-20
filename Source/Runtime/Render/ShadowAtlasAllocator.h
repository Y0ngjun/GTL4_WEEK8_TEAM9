#pragma once
#include "Core/Types.h"
#include <Containers/Array.h>

struct FShadowAtlasRect
{
	uint32 X = 0;
	uint32 Y = 0;
	uint32 Size = 0;

	bool IsValid() const { return Size != 0; }
};

// Atlas 크기 안에서 요청한 크기의 영역을 배정한다.
// 요청한 크기에 맞는 빈 사각형을 
class FShadowAtlasAllocator
{
public:
	// AtlasSize와 MinBlockSize는 2의 거듭제곱이어야 한다.
	// 초기화하면 이전 할당은 모두 사라진다.
	bool Initialize(uint32 InAtlasSize, uint32 InMinBlockSize);
	
	// 현재 크기를 유지하면서 전체 공간을 빈 상태로 되돌린다.
	void Reset();

	// RequtedSize는 MinBlockSize 이상이어야 한다. OutRect은 할당된 영역을 반환한다.
	bool Allocate(uint32 RequestedSize, FShadowAtlasRect& OutRect);

private:
	enum class ENodeState : uint8
	{
		Free,
		Split,
		Used,
	};

	struct FNode
	{
		FShadowAtlasRect Rect;
		ENodeState State = ENodeState::Free;

		uint32 FirstChild = ~0u;
	};

	bool AllocateNode(uint32 NodeIndex, uint32 RequestedSize, FShadowAtlasRect& OutRect);

	void SplitNode(uint32 NodeIndex);

	uint32 AtlasSize = 0;
	uint32 MinBlockSize = 0;

	TArray<FNode> Nodes;
};

