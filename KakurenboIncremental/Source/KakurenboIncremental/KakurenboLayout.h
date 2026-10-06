// 壁の配置（設計図）と、壊れた壁の自動修復の計画を立てる純粋なロジック（ワールドに依存しない）。
//
// 1 マスの「列」を下から順の壁の種類の配列で表す。
//   設計図   : プレイヤーが置いたときの列（例: [木, 石]）
//   現在の列 : 鬼に壊されずに残っている列。壊れると上の段が落ちるので、設計図から一部が抜けた並びになる（例: [石]）
// 修復では、設計図の順に見ていき、残っているものはそのまま使い、抜けているものを在庫から作る。

#pragma once

#include "CoreMinimal.h"

namespace KakurenboLayout
{
	enum class ERepairStep : uint8
	{
		Keep,    // 残っているブロックをそのまま使う
		Build,   // 在庫から新しく作る
		Missing, // 在庫が足りず作れない（設計図には残す）
	};

	struct FRepairEntry
	{
		ERepairStep Step = ERepairStep::Keep;
		int32 Type = 0;           // 壁の種類
		int32 LiveIndex = INDEX_NONE; // Keep のとき、現在の列の何番目を使うか
	};

	/**
	 * 1 列ぶんの修復計画を立てる。
	 * @param Design     設計図（下から順の壁の種類）
	 * @param LiveTypes  現在残っているブロックの種類（下から順）
	 * @param InOutStock 種類ごとの在庫。Build にした分だけ減る
	 * @param OutPlan    設計図と同じ長さの計画（＋設計図に無い残りブロックがあれば末尾に Keep で追加）
	 */
	KAKURENBOINCREMENTAL_API void PlanColumnRepair(const TArray<int32>& Design, const TArray<int32>& LiveTypes, TArray<int32>& InOutStock, TArray<FRepairEntry>& OutPlan);
}
