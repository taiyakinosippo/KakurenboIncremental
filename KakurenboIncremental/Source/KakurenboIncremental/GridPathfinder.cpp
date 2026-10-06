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
				// 壁の角をすり抜けない／壁へ斜めに入らない
				if (Extra > 0.f || !Grid.IsFree(FIntPoint(P.X + D.X, P.Y)) || !Grid.IsFree(FIntPoint(P.X, P.Y + D.Y)))
				{
					continue;
				}
			}

			const int32 NIndex = Grid.ToIndex(N);
			if (Closed[NIndex])
			{
				continue;
			}

			const float Tentative = GScore[Current.Index] + (bDiagonal ? UE_SQRT_2 : 1.f) + Extra;
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
