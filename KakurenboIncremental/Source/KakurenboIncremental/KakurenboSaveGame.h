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
};

UCLASS()
class KAKURENBOINCREMENTAL_API UKakurenboSaveGame : public USaveGame
{
	GENERATED_BODY()

public:
	/** 保存形式の版（項目を増やしたときに古いデータを見分けるため） */
	UPROPERTY()
	int32 SaveVersion = 1;

	UPROPERTY()
	double Coins = 0.0;

	UPROPERTY()
	int32 Stage = 1;

	UPROPERTY()
	int32 MashIncomeLevel = 0;

	UPROPERTY()
	int32 TimeIncomeLevel = 0;

	UPROPERTY()
	TArray<int32> WallStock;

	UPROPERTY()
	TArray<FKakurenboSavedColumn> Columns;

	UPROPERTY()
	bool bHasPlayerLocation = false;

	UPROPERTY()
	FVector PlayerLocation = FVector::ZeroVector;
};
