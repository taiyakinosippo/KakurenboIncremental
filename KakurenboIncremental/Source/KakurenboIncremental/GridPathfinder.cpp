#include "GridPathfinder.h"

#include "Algo/Reverse.h"

namespace
{
	struct FOpenNode
	{
		int32 Index;
		float F; // G + H
	};

	/** 8 方向移動のヒューリスティック（オクタイル距離）。追加コストは 0 以上なので許容的 */
	float Octile(const FIntPoint& A, const FIntPoint& B)
	{
		const float DX = FMath::Abs(A.X - B.X);
		const float DY = FMath::Abs(A.Y - B.Y);
		return (DX + DY) + (UE_SQRT_2 - 2.f) * FMath::Min(DX, DY);
	}
}

bool KakurenboPathfinding::FindPath(const FKakurenboPathGrid& Grid, const FIntPoint& Start, const FIntPoint& Goal, TArray<FIntPoint>& OutPath, float* OutTotalCost)
{
	OutPath.Reset();
	if (!Grid.IsInside(Start) || !Grid.IsInside(Goal) || Grid.GetExtra(Goal) < 0.f)
	{
		return false;
	}
	if (Start == Goal)
	{
		if (OutTotalCost) *OutTotalCost = 0.f;
		return true;
	}

	const int32 NumCells = Grid.SizeX * Grid.SizeY;
	TArray<float> GScore;
	GScore.Init(TNumericLimits<float>::Max(), NumCells);
	TArray<int32> CameFrom;
	CameFrom.Init(INDEX_NONE, NumCells);
	TArray<bool> Closed;
	Closed.Init(false, NumCells);

	// 優先度付きキュー（TArray を二分ヒープとして使う）
	TArray<FOpenNode> Open;
	auto Less = [](const FOpenNode& A, const FOpenNode& B) { return A.F < B.F; };

	const int32 StartIndex = Grid.ToIndex(Start);
	const int32 GoalIndex = Grid.ToIndex(Goal);
	GScore[StartIndex] = 0.f;
	Open.HeapPush({ StartIndex, Octile(Start, Goal) }, Less);

	static const FIntPoint Dirs[8] = {
		{ 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 },
		{ 1, 1 }, { 1, -1 }, { -1, 1 }, { -1, -1 } };

	while (Open.Num() > 0)
	{
		FOpenNode Current;
		Open.HeapPop(Current, Less);
		if (Closed[Current.Index])
		{
			continue; // 古いエントリ（より良い経路で上書き済み）
		}
		if (Current.Index == GoalIndex)
		{
			break;
		}
		Closed[Current.Index] = true;

		const FIntPoint P = Grid.FromIndex(Current.Index);
		for (const FIntPoint& D : Dirs)
		{
			const FIntPoint N = P + D;
			if (!Grid.IsInside(N))
			{
				continue;
			}
			const float Extra = Grid.GetExtra(N);
			if (Extra < 0.f)
			{
				continue;
			}

			const bool bDiagonal = D.X != 0 && D.Y != 0;
			if (bDiagonal)
			{
				// 壁の角をすり抜けない／壁へ斜めに入らない（一方通行ののれんの角もすり抜けない）
				const FIntPoint CornerA(P.X + D.X, P.Y);
				const FIntPoint CornerB(P.X, P.Y + D.Y);
				if (Extra > 0.f || !Grid.IsFree(CornerA) || !Grid.IsFree(CornerB) || Grid.GetOneWay(CornerA) >= 0 || Grid.GetOneWay(CornerB) >= 0)
				{
					continue;
				}
			}
			// 一方通行ののれん：決まった向きにまっすぐしか通れない。壊す経路なら、逆向きはのれんを壊すコストを払って通る
			// （今いるマスがのれんの上なら、出る向きは問わない＝閉じ込められない）
			const float GateCost = Grid.OneWayStepCost(P, D, P == Start);
			if (GateCost < 0.f)
			{
				continue;
			}

			const int32 NIndex = Grid.ToIndex(N);
			if (Closed[NIndex])
			{
				continue;
			}

			const float Tentative = GScore[Current.Index] + (bDiagonal ? UE_SQRT_2 : 1.f) + Extra + GateCost;
			if (Tentative < GScore[NIndex])
			{
				GScore[NIndex] = Tentative;
				CameFrom[NIndex] = Current.Index;
				Open.HeapPush({ NIndex, Tentative + Octile(N, Goal) }, Less);
			}
		}
	}

	if (CameFrom[GoalIndex] == INDEX_NONE)
	{
		return false;
	}

	// ゴールから逆にたどって経路を復元する
	for (int32 Index = GoalIndex; Index != StartIndex; Index = CameFrom[Index])
	{
		OutPath.Add(Grid.FromIndex(Index));
	}
	Algo::Reverse(OutPath);

	if (OutTotalCost)
	{
		*OutTotalCost = GScore[GoalIndex];
	}
	return true;
}

void KakurenboPathfinding::LabelFreeRegions(const FKakurenboPathGrid& Grid, TArray<int32>& OutLabels, TArray<int32>& OutSizes)
{
	const int32 NumCells = Grid.SizeX * Grid.SizeY;
	OutLabels.Init(INDEX_NONE, NumCells);
	OutSizes.Reset();

	static const FIntPoint Dirs[4] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } };
	TArray<int32> Stack;
	for (int32 Index = 0; Index < NumCells; ++Index)
	{
		// のれん（一方通行）のマスは、向きがあるのでまとまりをつながない（のれんの先の行き止まりは空洞になる）
		if (OutLabels[Index] != INDEX_NONE || Grid.ExtraCost[Index] != 0.f || Grid.GetOneWay(Grid.FromIndex(Index)) >= 0)
		{
			continue;
		}
		// 新しいまとまり：ここから塗りつぶす（深さ優先）
		const int32 Label = OutSizes.Add(0);
		OutLabels[Index] = Label;
		Stack.Reset();
		Stack.Add(Index);
		while (Stack.Num() > 0)
		{
			const int32 Current = Stack.Pop(EAllowShrinking::No);
			++OutSizes[Label];
			const FIntPoint P = Grid.FromIndex(Current);
			for (const FIntPoint& D : Dirs)
			{
				const FIntPoint N = P + D;
				// のれんのマスにも広がらない（広がると、のれんで囲んだ場所が広い場所とつながって空洞にならない）
				if (!Grid.IsFree(N) || Grid.GetOneWay(N) >= 0)
				{
					continue;
				}
				const int32 NIndex = Grid.ToIndex(N);
				if (OutLabels[NIndex] == INDEX_NONE)
				{
					OutLabels[NIndex] = Label;
					Stack.Add(NIndex);
				}
			}
		}
	}
}

int32 KakurenboPathfinding::FindLargestRegion(const TArray<int32>& Sizes)
{
	int32 Best = INDEX_NONE;
	for (int32 i = 0; i < Sizes.Num(); ++i)
	{
		if (Best == INDEX_NONE || Sizes[i] > Sizes[Best])
		{
			Best = i;
		}
	}
	return Best;
}

TArray<TArray<FIntPoint>> KakurenboPathfinding::FindEnclosedPockets(const FKakurenboPathGrid& Grid)
{
	TArray<int32> Labels, Sizes;
	LabelFreeRegions(Grid, Labels, Sizes);
	const int32 Largest = FindLargestRegion(Sizes);

	TArray<TArray<FIntPoint>> Pockets;
	Pockets.SetNum(Sizes.Num());
	for (int32 Index = 0; Index < Labels.Num(); ++Index)
	{
		if (Labels[Index] != INDEX_NONE && Labels[Index] != Largest)
		{
			Pockets[Labels[Index]].Add(Grid.FromIndex(Index));
		}
	}
	Pockets.RemoveAll([](const TArray<FIntPoint>& Pocket) { return Pocket.Num() == 0; });
	return Pockets;
}

float KakurenboPathfinding::ComputeEnclosureDamping(const FKakurenboPathGrid& Grid, const FIntPoint& Cell, const TArray<float>& CellDamping,
	int32* OutBoundaryWalls, TArray<FIntPoint>* OutBoundaryCells)
{
	if (OutBoundaryWalls)
	{
		*OutBoundaryWalls = 0;
	}
	if (OutBoundaryCells)
	{
		OutBoundaryCells->Reset();
	}
	if (!Grid.IsFree(Cell))
	{
		return 0.f; // 壁の上や外にいる
	}
	TArray<int32> Labels, Sizes;
	LabelFreeRegions(Grid, Labels, Sizes);
	const int32 Region = Labels[Grid.ToIndex(Cell)];
	if (Region == FindLargestRegion(Sizes))
	{
		return 0.f; // 囲まれていない（一番広い空間にいる）
	}

	// 空洞のマスの周り 8 マスにある壁を数える（同じ壁は 1 回だけ）
	TSet<int32> Walls;
	for (int32 Index = 0; Index < Labels.Num(); ++Index)
	{
		if (Labels[Index] != Region)
		{
			continue;
		}
		const FIntPoint P = Grid.FromIndex(Index);
		for (int32 DY = -1; DY <= 1; ++DY)
		{
			for (int32 DX = -1; DX <= 1; ++DX)
			{
				const FIntPoint N = P + FIntPoint(DX, DY);
				// CellDamping が負のマス（家具・部屋の壁）は数えない
				if (Grid.IsInside(N) && Grid.GetExtra(N) != 0.f && !(CellDamping.IsValidIndex(Grid.ToIndex(N)) && CellDamping[Grid.ToIndex(N)] < 0.f))
				{
					Walls.Add(Grid.ToIndex(N));
				}
			}
		}
	}
	if (Walls.Num() == 0)
	{
		return 0.f; // 舞台の外周だけで囲まれている（普通は起きない）
	}
	float Sum = 0.f;
	for (const int32 Index : Walls)
	{
		Sum += CellDamping.IsValidIndex(Index) ? FMath::Clamp(CellDamping[Index], 0.f, 1.f) : 0.f;
		if (OutBoundaryCells)
		{
			OutBoundaryCells->Add(Grid.FromIndex(Index));
		}
	}
	if (OutBoundaryWalls)
	{
		*OutBoundaryWalls = Walls.Num();
	}
	return Sum / Walls.Num();
}

bool KakurenboPathfinding::PathContainsWalls(const FKakurenboPathGrid& Grid, const TArray<FIntPoint>& Path)
{
	for (const FIntPoint& P : Path)
	{
		if (Grid.GetExtra(P) > 0.f)
		{
			return true;
		}
	}
	return false;
}
