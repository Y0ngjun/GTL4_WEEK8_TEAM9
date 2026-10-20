#include "EnginePCH.h"
#include "ShadowAtlasAllocator.h"

namespace
{
	bool IsPowerOfTwo(uint32 Value)
	{
		return Value != 0 && (Value & (Value - 1)) == 0;
	}
}

bool FShadowAtlasAllocator::Initialize(uint32 InAtlasSize, uint32 InMinBlockSize)
{
	if (!IsPowerOfTwo(InAtlasSize) || !IsPowerOfTwo(InMinBlockSize) || InMinBlockSize > InAtlasSize)
	{
		return false;
	}

	AtlasSize = InAtlasSize;
	MinBlockSize = InMinBlockSize;

	Reset();

	return true;
}

void FShadowAtlasAllocator::Reset()
{
	Nodes.Reset();
	if (AtlasSize == 0) return;
	FNode RootNode;
	RootNode.Rect = { 0, 0, AtlasSize };
	RootNode.State = ENodeState::Free;
	RootNode.FirstChild = ~0u;
	Nodes.Push(RootNode);
}

bool FShadowAtlasAllocator::Allocate(uint32 RequestedSize, FShadowAtlasRect& OutRect)
{
	OutRect = {};

	if (Nodes.IsEmpty() || !IsPowerOfTwo(RequestedSize) || RequestedSize < MinBlockSize || RequestedSize > AtlasSize)
	{
		return false;
	}
	return AllocateNode(0, RequestedSize, OutRect);
}

bool FShadowAtlasAllocator::AllocateNode(uint32 NodeIndex, uint32 RequestedSize, FShadowAtlasRect& OutRect)
{
    // 재귀 호출 중 Nodes가 늘어날 수 있으므로 참조를 보관하지 않는다.
    const FNode Node = Nodes[NodeIndex];

    if (Node.State == ENodeState::Used ||
        Node.Rect.Size < RequestedSize)
    {
        return false;
    }

    if (Node.State == ENodeState::Free)
    {
        if (Node.Rect.Size == RequestedSize)
        {
            Nodes[NodeIndex].State = ENodeState::Used;
            OutRect = Node.Rect;
            return true;
        }

        SplitNode(NodeIndex);
    }

    const uint32 FirstChild = Nodes[NodeIndex].FirstChild;

    for (uint32 Child = 0; Child < 4; ++Child)
    {
        if (AllocateNode(FirstChild + Child, RequestedSize, OutRect))
        {
            return true;
        }
    }

    return false;
}

void FShadowAtlasAllocator::SplitNode(uint32 NodeIndex)
{
    const FShadowAtlasRect ParentRect = Nodes[NodeIndex].Rect;
    const uint32 HalfSize = ParentRect.Size / 2;
    const uint32 FirstChild = Nodes.Num();

    // 순서: 왼쪽 위, 오른쪽 위, 왼쪽 아래, 오른쪽 아래.
    for (uint32 Child = 0; Child < 4; ++Child)
    {
        FNode NewNode;
        NewNode.Rect.X = ParentRect.X + (Child % 2) * HalfSize;
        NewNode.Rect.Y = ParentRect.Y + (Child / 2) * HalfSize;
        NewNode.Rect.Size = HalfSize;

        Nodes.Add(NewNode);
    }

    Nodes[NodeIndex].FirstChild = FirstChild;
    Nodes[NodeIndex].State = ENodeState::Split;
}
