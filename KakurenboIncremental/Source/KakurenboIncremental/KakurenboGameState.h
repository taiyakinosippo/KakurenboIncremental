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

	/** 壁の補強のレベル（コインのお店。すべての壁の耐久が元の耐久の +50% ずつ増える。転生すると 0 に戻る） */
	UPROPERTY(BlueprintReadOnly, Category = "Kakurenbo")
	int32 WallReinforceLevel = 0;

	// ---- 転生（転生しても残る） ----
	/** 使える転生ポイント（転生のお店で永続強化に使う） */
	UPROPERTY(BlueprintReadOnly, Category = "Kakurenbo")
	int32 PrestigePoints = 0;

	/** これまでにもらった転生ポイントの合計 */
	UPROPERTY(BlueprintReadOnly, Category = "Kakurenbo")
	int32 TotalPrestigePoints = 0;

	/** 転生した回数 */
	UPROPERTY(BlueprintReadOnly, Category = "Kakurenbo")
	int32 PrestigeCount = 0;

	/** 転生のお店の強化レベル（インデックスは EPrestigeUpgrade） */
	UPROPERTY(BlueprintReadOnly, Category = "Kakurenbo")
	TArray<int32> PrestigeLevels;

	/** 壁の在庫（インデックスは GameMode の WallTypes と対応） */
	UPROPERTY(BlueprintReadOnly, Category = "Kakurenbo")
	TArray<int32> WallStock;

	/** 罠の在庫（インデックスは GameMode の TrapTypes と対応） */
	UPROPERTY(BlueprintReadOnly, Category = "Kakurenbo")
	TArray<int32> TrapStock;

	// ---- リザルト ----
	UPROPERTY(BlueprintReadOnly, Category = "Kakurenbo")
	bool bLastRoundCleared = false;

	UPROPERTY(BlueprintReadOnly, Category = "Kakurenbo")
	double LastClearReward = 0.0;

	UPROPERTY(BlueprintReadOnly, Category = "Kakurenbo")
	int32 LastRoundWallsDestroyed = 0;

	/** このラウンドで発動した罠の数 */
	UPROPERTY(BlueprintReadOnly, Category = "Kakurenbo")
	int32 TrapsTriggeredThisRound = 0;

	/** このラウンドの足音の数（着地を含む） */
	UPROPERTY(BlueprintReadOnly, Category = "Kakurenbo")
	int32 StepsThisRound = 0;

	/** このラウンドでよじ登った回数 */
	UPROPERTY(BlueprintReadOnly, Category = "Kakurenbo")
	int32 ClimbsThisRound = 0;

	/** このラウンドで探知鬼が仲間を呼んだ回数 */
	UPROPERTY(BlueprintReadOnly, Category = "Kakurenbo")
	int32 SummonsThisRound = 0;

	// ---- お宝 ----
	/** このラウンドに出現したお宝の数 */
	UPROPERTY(BlueprintReadOnly, Category = "Kakurenbo")
	int32 TreasuresThisRound = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Kakurenbo")
	int32 TreasuresCollectedThisRound = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Kakurenbo")
	double TreasureCoinsThisRound = 0.0;

	// ---- 宝石（M15：お宝を取るともらえる。宝石のお店で、ふしぎな道具と交換する。転生するとなくなる。宝石で買った道具も） ----
	UPROPERTY(BlueprintReadOnly, Category = "Kakurenbo")
	int32 Gems = 0;

	/** このラン（転生するまで）にゴールドラッシュを買った数（値段がこれで上がる） */
	UPROPERTY(BlueprintReadOnly, Category = "Kakurenbo")
	int32 GoldRushBoughtThisRun = 0;

	/** このラウンドでもらった宝石（コンプリートのボーナスを含む） */
	UPROPERTY(BlueprintReadOnly, Category = "Kakurenbo")
	int32 GemsThisRound = 0;

	/** このラウンドでお宝を全部取った（コンプリートのボーナスをもらった） */
	UPROPERTY(BlueprintReadOnly, Category = "Kakurenbo")
	bool bAllTreasuresThisRound = false;

	/** コンプリートのボーナスのコイン */
	UPROPERTY(BlueprintReadOnly, Category = "Kakurenbo")
	double TreasureBonusCoinsThisRound = 0.0;

	/** このラウンドの時間収入・連打収入（リザルトの内訳） */
	UPROPERTY(BlueprintReadOnly, Category = "Kakurenbo")
	double TimeCoinsThisRound = 0.0;

	UPROPERTY(BlueprintReadOnly, Category = "Kakurenbo")
	double MashCoinsThisRound = 0.0;

	// ---- 壁の自動修復（設置パートの開始時） ----
	UPROPERTY(BlueprintReadOnly, Category = "Kakurenbo")
	int32 LastRepairedWalls = 0;

	/** 在庫不足などで直せなかった壁の数 */
	UPROPERTY(BlueprintReadOnly, Category = "Kakurenbo")
	int32 LastUnrepairedWalls = 0;

	/** 在庫から置き直した罠の数 / 在庫不足で置き直せなかった罠の数 */
	UPROPERTY(BlueprintReadOnly, Category = "Kakurenbo")
	int32 LastRefilledTraps = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Kakurenbo")
	int32 LastUnrefilledTraps = 0;

	// ---- 画面上部のお知らせ（「セーブデータから再開しました」など） ----
	UPROPERTY(BlueprintReadOnly, Category = "Kakurenbo")
	FText NoticeText;

	/** このワールド時刻（秒）まで表示する */
	UPROPERTY(BlueprintReadOnly, Category = "Kakurenbo")
	float NoticeUntilTime = 0.f;

	UPROPERTY(BlueprintAssignable, Category = "Kakurenbo")
	FOnKakurenboPhaseChanged OnPhaseChanged;

	void AddCoins(double Amount, bool bCountAsEarned);
};
