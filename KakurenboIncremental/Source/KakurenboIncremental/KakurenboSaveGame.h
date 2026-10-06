// セーブデータ。パートが変わるたびに自動で保存し、次に起動したときに読み込む。
// 保存先: <プロジェクト>/Saved/SaveGames/<スロット名>.sav

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "KakurenboSaveGame.generated.h"

/** 1 マスぶんの壁 */
USTRUCT()
struct FKakurenboSavedColumn
{
	GENERATED_BODY()

	UPROPERTY()
	FIntPoint Cell = FIntPoint::ZeroValue;

	/** 設計図（プレイヤーが置いた壁の種類。下の段から順） */
	UPROPERTY()
	TArray<int32> Design;

	/** 保存した時点で残っていた壁の種類（下の段から順。壊れた分は入っていない） */
	UPROPERTY()
	TArray<int32> Live;

	/** 罠の設計図（置いた罠の種類。無ければ INDEX_NONE） */
	UPROPERTY()
	int32 TrapDesign = INDEX_NONE;

	/** 保存した時点で罠が残っていたか（発動して消えていたら false） */
	UPROPERTY()
	bool bTrapLive = false;
};

UCLASS()
class KAKURENBOINCREMENTAL_API UKakurenboSaveGame : public USaveGame
{
	GENERATED_BODY()

public:
	/** 保存形式の版（項目を増やしたときに古いデータを見分けるため）。2: 罠・壁の補強を追加 */
	UPROPERTY()
	int32 SaveVersion = 2;

	UPROPERTY()
	double Coins = 0.0;

	UPROPERTY()
	int32 Stage = 1;

	UPROPERTY()
	int32 MashIncomeLevel = 0;

	UPROPERTY()
	int32 TimeIncomeLevel = 0;

	/** 壁の補強のレベル（版 1 のセーブには無いので 0 になる） */
	UPROPERTY()
	int32 WallReinforceLevel = 0;

	UPROPERTY()
	TArray<int32> WallStock;

	/** 罠の在庫（インデックスは GameMode の TrapTypes と対応） */
	UPROPERTY()
	TArray<int32> TrapStock;

	UPROPERTY()
	TArray<FKakurenboSavedColumn> Columns;

	UPROPERTY()
	bool bHasPlayerLocation = false;

	UPROPERTY()
	FVector PlayerLocation = FVector::ZeroVector;
};
