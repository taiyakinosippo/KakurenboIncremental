// グリッド上の A* 経路探索（ワールドに依存しない純粋なアルゴリズム部分）。
// 鬼は NavMesh ではなくこれで移動する。壁のマスにも「壊すコスト」を付けて通れるようにすることで、
// 「回り道が長すぎるなら壁を壊して進む」という判断が自然に出てくる。

#pragma once

#include "CoreMinimal.h"

struct KAKURENBOINCREMENTAL_API FKakurenboPathGrid
{
	int32 SizeX = 0;
	int32 SizeY = 0;

	/**
	 * マスごとの追加コスト。
	 *   0      : 通れる
	 *   > 0    : 壁がある（壊せば通れる。値が大きいほど壊すのに時間がかかる）
	 *   < 0    : 通れない
	 */
	TArray<float> ExtraCost;

	/**
	 * 一方通行のマス（M15：一方通行ののれん）。-1 なら普通のマス、0〜3 なら通れる向き（0 +X / 1 +Y / 2 -X / 3 -Y）。
	 * そのマスへは「通れる向き」にまっすぐ（縦横で）入り、同じ向きへまっすぐ出るしかない。空なら一方通行のマスは無い
	 */
	TArray<int8> OneWayDir;

	/**
	 * 一方通行のマスを逆向きに通るとき（のれんを壊して通る）の追加コスト。負なら逆向きには通れない（歩くだけの経路）。
	 * M15：のれんは「その先が完全に行き止まり」のとき（壊す経路を使うとき）だけ壊される
	 */
	TArray<float> OneWayBreakCost;

	void Init(int32 InSizeX, int32 InSizeY)
	{
		SizeX = InSizeX;
		SizeY = InSizeY;
		ExtraCost.Init(0.f, SizeX * SizeY);
		OneWayDir.Reset();
		OneWayBreakCost.Reset();
	}

	/** マスを一方通行にする（Dir: 0 +X / 1 +Y / 2 -X / 3 -Y。BreakCost：逆向きに壊して通るコスト。負なら通れない） */
	void SetOneWay(const FIntPoint& P, int32 Dir, float BreakCost = -1.f)
	{
		if (OneWayDir.Num() != SizeX * SizeY)
		{
			OneWayDir.Init(-1, SizeX * SizeY);
			OneWayBreakCost.Init(-1.f, SizeX * SizeY);
		}
		OneWayDir[ToIndex(P)] = static_cast<int8>(Dir);
		OneWayBreakCost[ToIndex(P)] = BreakCost;
	}

	/** 一方通行の向き（普通のマスなら -1） */
	int32 GetOneWay(const FIntPoint& P) const { return OneWayDir.Num() == SizeX * SizeY && IsInside(P) ? OneWayDir[ToIndex(P)] : -1; }

	/** 向きの番号 → マスの差 */
	static FIntPoint DirToStep(int32 Dir)
	{
		static const FIntPoint Steps[4] = { { 1, 0 }, { 0, 1 }, { -1, 0 }, { 0, -1 } };
		return Steps[((Dir % 4) + 4) % 4];
	}

	/**
	 * P から P + D へ動くときの、一方通行のマスの追加コスト。0 なら向きどおり（そのまま通れる）、正なら逆向き（のれんを壊して通る）、負なら通れない。
	 * のれんへは縦横でまっすぐ出入りする（斜めは通れない）。bIgnoreFrom なら P ののれんは見ない（今いるマスからは出られる）
	 */
	float OneWayStepCost(const FIntPoint& P, const FIntPoint& D, bool bIgnoreFrom = false) const
	{
		const int32 From = bIgnoreFrom ? -1 : GetOneWay(P);
		const int32 To = GetOneWay(P + D);
		if (From < 0 && To < 0)
		{
			return 0.f;
		}
		if (D.X != 0 && D.Y != 0)
		{
			return -1.f;
		}
		float Cost = 0.f;
		for (const TPair<int32, FIntPoint>& Gate : { TPair<int32, FIntPoint>(From, P), TPair<int32, FIntPoint>(To, P + D) })
		{
			if (Gate.Key >= 0 && D != DirToStep(Gate.Key))
			{
				const float Break = OneWayBreakCost.IsValidIndex(ToIndex(Gate.Value)) ? OneWayBreakCost[ToIndex(Gate.Value)] : -1.f;
				if (Break < 0.f)
				{
					return -1.f;
				}
				Cost += FMath::Max(Break, 0.01f);
			}
		}
		return Cost;
	}

	/** P から P + D へ、向きどおりに通れるか */
	bool CanStep(const FIntPoint& P, const FIntPoint& D) const { return OneWayStepCost(P, D) == 0.f; }

	bool IsInside(const FIntPoint& P) const { return P.X >= 0 && P.Y >= 0 && P.X < SizeX && P.Y < SizeY; }
	int32 ToIndex(const FIntPoint& P) const { return P.Y * SizeX + P.X; }
	FIntPoint FromIndex(int32 Index) const { return FIntPoint(Index % SizeX, Index / SizeX); }
	float GetExtra(const FIntPoint& P) const { return ExtraCost[ToIndex(P)]; }
	void SetExtra(const FIntPoint& P, float Cost) { ExtraCost[ToIndex(P)] = Cost; }
	bool IsFree(const FIntPoint& P) const { return IsInside(P) && GetExtra(P) == 0.f; }
};

namespace KakurenboPathfinding
{
	/**
	 * Start から Goal までの最小コスト経路を求める（8 方向移動）。
	 * - 斜め移動は、通り抜ける 2 つの角のマスが両方とも空いているときだけ許可する
	 * - 壁のマスへは縦横からのみ入れる（斜めから壁を壊しには行かない）
	 * - Goal 自体が壁でもよい（その場合は壁の手前で攻撃することになる）
	 * @param OutPath Start を含まず Goal を含むマスの列
	 * @return 経路が見つかったか
	 */
	KAKURENBOINCREMENTAL_API bool FindPath(const FKakurenboPathGrid& Grid, const FIntPoint& Start, const FIntPoint& Goal, TArray<FIntPoint>& OutPath, float* OutTotalCost = nullptr);

	/** 経路が壁のマスを含むか */
	KAKURENBOINCREMENTAL_API bool PathContainsWalls(const FKakurenboPathGrid& Grid, const TArray<FIntPoint>& Path);

	/**
	 * 通れるマス（追加コスト 0）を縦横にたどって、まとまり（連結成分）ごとに番号を付ける。
	 * 斜め移動は「両隣が空いているときだけ」なので、縦横のつながりだけ見れば十分。
	 * @param OutLabels マスごとのまとまりの番号（通れないマスは INDEX_NONE）
	 * @param OutSizes  まとまりごとのマス数
	 */
	KAKURENBOINCREMENTAL_API void LabelFreeRegions(const FKakurenboPathGrid& Grid, TArray<int32>& OutLabels, TArray<int32>& OutSizes);

	/** 一番大きいまとまりの番号（通れるマスが無ければ INDEX_NONE） */
	KAKURENBOINCREMENTAL_API int32 FindLargestRegion(const TArray<int32>& Sizes);

	/**
	 * 壁に囲まれて、一番大きいまとまりから歩いては入れない空きマスのまとまり（＝空洞）を返す。
	 * 例: 角に斜めに壁を並べると、角の三角の部分が空洞になる
	 */
	KAKURENBOINCREMENTAL_API TArray<TArray<FIntPoint>> FindEnclosedPockets(const FKakurenboPathGrid& Grid);

	/**
	 * 消音壁：Cell が空洞（一番大きいまとまり以外）の中なら、その空洞を囲んでいる壁（空洞のマスの周り 8 マスにある壁）の
	 * CellDamping の平均を返す。囲まれていなければ 0。
	 * @param CellDamping マスごとの「音を小さくする割合」（0〜1。壁の無いマスは使わない。負のマス＝家具などは囲む壁として数えない）
	 * @param OutBoundaryWalls 囲んでいる壁の数
	 * @param OutBoundaryCells 囲んでいる壁のマス
	 */
	KAKURENBOINCREMENTAL_API float ComputeEnclosureDamping(const FKakurenboPathGrid& Grid, const FIntPoint& Cell, const TArray<float>& CellDamping,
		int32* OutBoundaryWalls = nullptr, TArray<FIntPoint>* OutBoundaryCells = nullptr);
}
