// ゲームの「現在の状態」を保持するクラス。ルールや計算は GameMode 側に置く。
// HUD や BP はここを読んで表示する。

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameStateBase.h"
#include "KakurenboTypes.h"
#include "KakurenboGameState.generated.h"

// C# の event に相当。BP からもバインドできる
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnKakurenboPhaseChanged, EKakurenboPhase, NewPhase);

UCLASS()
class KAKURENBOINCREMENTAL_API AKakurenboGameState : public AGameStateBase
{
	GENERATED_BODY()

public:
	// ---- 進行 ----
	UPROPERTY(BlueprintReadOnly, Category = "Kakurenbo")
	EKakurenboPhase Phase = EKakurenboPhase::Hide;

	UPROPERTY(BlueprintReadOnly, Category = "Kakurenbo")
	int32 Stage = 1;

	/** かくれんぼ開始前のカウントダウン（0 になったら鬼が出てくる） */
	UPROPERTY(BlueprintReadOnly, Category = "Kakurenbo")
	float HideStartCountdown = 0.f;

	/** 逃げ切りまでの残り時間 */
	UPROPERTY(BlueprintReadOnly, Category = "Kakurenbo")
	float HideTimeRemaining = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Kakurenbo")
	float HideTimeLimit = 0.f;

	// ---- お金 ----
	UPROPERTY(BlueprintReadOnly, Category = "Kakurenbo")
	double Coins = 0.0;

	/** このラウンドで稼いだコイン（リザルト表示用） */
	UPROPERTY(BlueprintReadOnly, Category = "Kakurenbo")
	double CoinsEarnedThisRound = 0.0;

	UPROPERTY(BlueprintReadOnly, Category = "Kakurenbo")
	int32 MashCountThisRound = 0;

	// ---- 強化レベル ----
	UPROPERTY(BlueprintReadOnly, Category = "Kakurenbo")
	int32 MashIncomeLevel = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Kakurenbo")
	int32 TimeIncomeLevel = 0;

	/** 壁の在庫（インデックスは GameMode の WallTypes と対応） */
	UPROPERTY(BlueprintReadOnly, Category = "Kakurenbo")
	TArray<int32> WallStock;

	// ---- リザルト ----
	UPROPERTY(BlueprintReadOnly, Category = "Kakurenbo")
	bool bLastRoundCleared = false;

	UPROPERTY(BlueprintReadOnly, Category = "Kakurenbo")
	double LastClearReward = 0.0;

	UPROPERTY(BlueprintReadOnly, Category = "Kakurenbo")
	int32 LastRoundWallsDestroyed = 0;

	// ---- お宝 ----
	/** このラウンドに出現したお宝の数 */
	UPROPERTY(BlueprintReadOnly, Category = "Kakurenbo")
	int32 TreasuresThisRound = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Kakurenbo")
	int32 TreasuresCollectedThisRound = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Kakurenbo")
	double TreasureCoinsThisRound = 0.0;

	// ---- 壁の自動修復（設置パートの開始時） ----
	UPROPERTY(BlueprintReadOnly, Category = "Kakurenbo")
	int32 LastRepairedWalls = 0;

	/** 在庫不足などで直せなかった壁の数 */
	UPROPERTY(BlueprintReadOnly, Category = "Kakurenbo")
	int32 LastUnrepairedWalls = 0;

	UPROPERTY(BlueprintAssignable, Category = "Kakurenbo")
	FOnKakurenboPhaseChanged OnPhaseChanged;

	void AddCoins(double Amount, bool bCountAsEarned);
};
