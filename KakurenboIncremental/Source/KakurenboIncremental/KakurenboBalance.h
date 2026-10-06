// バランスデータ（ステージ・強化・壁）の読み込みと計算。
//
// 数値は <プロジェクト>/Data/*.csv に書いてあり、Excel や VSCode で編集できる。
// （エディタで CSV を DataTable アセットとして取り込み、GameMode の StageTable などに設定すると、そちらが優先される）

#pragma once

#include "CoreMinimal.h"
#include "KakurenboTypes.h"

class UDataTable;

namespace KakurenboBalance
{
	/** 表に無いステージ（最後の行より後）の伸ばし方 */
	struct FStageGrowth
	{
		float HideDurationPerStage = 5.f;
		double ClearRewardGrowth = 4.0;
		double OniDamageGrowth = 1.6;
		float OniHearingRadiusPerStage = 100.f;
		float OniSpeedBonusPerStage = 15.f;
	};

	/**
	 * ステージ番号（1 始まり）の設定を求める。
	 * 表にあればその行、表より後なら最後の行から FStageGrowth で 1 ステージずつ伸ばした値。表が空ならステージ 1 の既定値から伸ばす
	 */
	KAKURENBOINCREMENTAL_API FKakurenboStageRow ResolveStage(const TArray<FKakurenboStageRow>& Rows, int32 Stage, const FStageGrowth& Growth);

	/** <プロジェクト>/Data/<FileName> のフルパス */
	KAKURENBOINCREMENTAL_API FString GetDataFilePath(const FString& FileName);

	/**
	 * CSV ファイルを読んで、行の型 RowStruct の DataTable を作る（アセットではなくメモリ上の一時的なもの）。
	 * @param OutProblems 読み込み時の問題（列名の間違いなど）
	 * @return 読み込めなければ nullptr
	 */
	KAKURENBOINCREMENTAL_API UDataTable* LoadCsvAsDataTable(UObject* Outer, UScriptStruct* RowStruct, const FString& FilePath, TArray<FString>& OutProblems);
}
