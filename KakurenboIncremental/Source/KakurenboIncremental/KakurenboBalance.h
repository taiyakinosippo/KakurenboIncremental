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
		/** 表より後は、このステージ数ごとに鬼が 1 体増える（0 なら増えない）。増える種類は最後の行の鬼（探知鬼を除く）を順に */
		int32 ExtraOniEveryStages = 3;
	};

	/**
	 * ステージ番号（1 始まり）の設定を求める。
	 * 表にあればその行、表より後なら最後の行から FStageGrowth で 1 ステージずつ伸ばした値。表が空ならステージ 1 の既定値から伸ばす
	 */
	KAKURENBOINCREMENTAL_API FKakurenboStageRow ResolveStage(const TArray<FKakurenboStageRow>& Rows, int32 Stage, const FStageGrowth& Growth);

	/** ステージ Stage で転生したときにもらえる転生ポイント（MinStage より前なら 0） */
	KAKURENBOINCREMENTAL_API int32 GetPrestigePoints(int32 Stage, const FKakurenboPrestigeRow& Settings);

	/** 転生した後のはじめのコイン（転生した回数 × StartCoinsPerPrestige。転生するたびに自動で増える） */
	KAKURENBOINCREMENTAL_API double GetPrestigeStartCoins(int32 PrestigeCount, const FKakurenboPrestigeRow& Settings);

	/** 転生のお店：今のレベルから 1 つ上げる価格（転生ポイント。最大レベルなら INDEX_NONE） */
	KAKURENBOINCREMENTAL_API int32 GetPrestigeUpgradeCost(const FKakurenboPrestigeUpgradeRow& Row, int32 Level);

	/** 転生のお店：そのレベルの効果（BaseValue × ValueGrowth ^ レベル） */
	KAKURENBOINCREMENTAL_API double GetPrestigeUpgradeValue(const FKakurenboPrestigeUpgradeRow& Row, int32 Level);

	/** <プロジェクト>/Data/<FileName> のフルパス */
	KAKURENBOINCREMENTAL_API FString GetDataFilePath(const FString& FileName);

	/**
	 * CSV ファイルを読んで、行の型 RowStruct の DataTable を作る（アセットではなくメモリ上の一時的なもの）。
	 * @param OutProblems 読み込み時の問題（列名の間違いなど）
	 * @return 読み込めなければ nullptr
	 */
	KAKURENBOINCREMENTAL_API UDataTable* LoadCsvAsDataTable(UObject* Outer, UScriptStruct* RowStruct, const FString& FilePath, TArray<FString>& OutProblems);
}
