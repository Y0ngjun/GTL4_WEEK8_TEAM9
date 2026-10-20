#pragma once
#include "Core/Types.h"
#include "Containers/Array.h"

struct FShadowAtlasRect
{
	uint32 X = 0;
	uint32 Y = 0;
	uint32 Size = 0;

	bool IsValid() const { return Size != 0; }
};

// 한 Atlas의 정사각형 영역을 Quadtree로 배정한다.
class FShadowAtlasAllocator
{
public:
	// AtlasSize와 MinBlockSize는 2의 거듭제곱이어야 한다.
	// 초기화하면 이전 할당은 모두 사라진다.
	bool Initialize(uint32 InAtlasSize, uint32 InMinBlockSize);
	
	// 현재 크기를 유지하면서 전체 공간을 빈 상태로 되돌린다.
	void Reset();

	// 요청 크기는 MinBlockSize 이상인 2의 거듭제곱이다.
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

