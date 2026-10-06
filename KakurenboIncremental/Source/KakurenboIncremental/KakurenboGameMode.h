// ゲームのルールと進行を管理するクラス。
// パートの切り替え・コインの計算・購入処理・鬼とお宝の出現はすべてここに集める。
// 数値はすべて UPROPERTY なので、BP の派生クラスやエディタの詳細パネルで調整できる。

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "KakurenboTypes.h"
#include "KakurenboGameMode.generated.h"

class AKakurenboArena;
class AKakurenboGameState;
class AOniCharacter;
class APlaceableBlock;
class ATreasureActor;

UCLASS()
class KAKURENBOINCREMENTAL_API AKakurenboGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AKakurenboGameMode();

	// ===== かくれんぼのルール =====

	/** ステージ 1 の制限時間（秒） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rules")
	float HideDurationBase = 30.f;

	/** ステージが 1 上がるごとに延びる制限時間（秒） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rules")
	float HideDurationPerStage = 5.f;

	/** かくれんぼ開始から鬼が出てくるまでの猶予（秒） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rules")
	float HideStartDelay = 3.f;

	// ===== 収入 =====

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Economy")
	double MashIncomeBase = 1.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Economy")
	double MashIncomeGrowth = 1.5;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Economy")
	double TimeIncomeBase = 1.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Economy")
	double TimeIncomeGrowth = 1.5;

	/** 逃げ切り報酬（ステージ 1） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Economy")
	double ClearRewardBase = 100.0;

	/** 逃げ切り報酬のステージごとの倍率 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Economy")
	double ClearRewardGrowth = 4.0;

	// ===== 強化の価格 =====

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Economy")
	double MashUpgradeBaseCost = 15.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Economy")
	double MashUpgradeCostGrowth = 1.7;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Economy")
	double TimeUpgradeBaseCost = 25.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Economy")
	double TimeUpgradeCostGrowth = 1.7;

	// ===== お宝 =====

	/** 1 ラウンドに出現するお宝の数 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Treasure")
	int32 NumTreasures = 3;

	/** お宝 1 個の価値（そのステージの逃げ切り報酬に対する割合） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Treasure")
	double TreasureRewardRatio = 0.3;

	/** お宝はプレイヤー・鬼・他のお宝からこのマス数以上離して置く */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Treasure")
	float TreasureMinDistanceCells = 4.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Treasure")
	TSubclassOf<ATreasureActor> TreasureClass;

	// ===== 壁 =====

	/** 購入できる壁の種類（ショップの並び順） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wall")
	TArray<FWallTypeDef> WallTypes;

	// ===== 鬼（ステージが上がるほど強くなる） =====

	/** スポーンする鬼のクラス（BP の派生クラスで見た目を変えてよい） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Oni")
	TSubclassOf<AOniCharacter> OniClass;

	/** 鬼の数 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Oni")
	int32 NumOnis = 2;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Oni")
	float OniWanderSpeedBase = 200.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Oni")
	float OniInvestigateSpeedBase = 340.f;

	/** 追いかけるときの速さ。プレイヤー（420）より少し遅いので、うまく逃げれば振り切れる */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Oni")
	float OniChaseSpeedBase = 400.f;

	/** ステージごとの移動速度の増加（cm/秒） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Oni")
	float OniSpeedPerStage = 15.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Oni")
	float OniSightRadius = 900.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Oni")
	float OniSightHalfAngle = 40.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Oni")
	float OniHearingRadiusBase = 1200.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Oni")
	float OniHearingRadiusPerStage = 100.f;

	/** 鬼の攻撃力（ステージ 1）。壁の耐久値と比べる */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Oni")
	double OniAttackDamageBase = 1.0;

	/** 攻撃力のステージごとの倍率 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Oni")
	double OniAttackDamageGrowth = 1.6;

	/** 連打したとき、音の届く範囲を床に表示する */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Oni")
	bool bShowNoiseRing = true;

	/** 鬼の経路などをデバッグ表示する */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Oni")
	bool bDebugOni = false;

	// ===== 舞台 =====

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Arena")
	int32 GridSizeX = 24;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Arena")
	int32 GridSizeY = 24;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Arena")
	float CellSize = 100.f;

	/** ブロック 1 段の高さ（cm）。プレイヤーの背の高さ（160cm）と同じにして、1 段で体が隠れるようにする */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Arena")
	float BlockHeight = 160.f;

	/** ブロックを積める最大の段数 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Arena")
	int32 MaxStackHeight = 3;

	// ===== 計算 =====

	UFUNCTION(BlueprintPure, Category = "Kakurenbo")
	double GetMashIncome() const;

	UFUNCTION(BlueprintPure, Category = "Kakurenbo")
	double GetTimeIncomePerSecond() const;

	UFUNCTION(BlueprintPure, Category = "Kakurenbo")
	double GetMashUpgradeCost() const;

	UFUNCTION(BlueprintPure, Category = "Kakurenbo")
	double GetTimeUpgradeCost() const;

	UFUNCTION(BlueprintPure, Category = "Kakurenbo")
	double GetClearReward() const;

	UFUNCTION(BlueprintPure, Category = "Kakurenbo")
	double GetTreasureValue() const;

	UFUNCTION(BlueprintPure, Category = "Kakurenbo")
	float GetHideDuration() const;

	UFUNCTION(BlueprintPure, Category = "Kakurenbo")
	double GetOniAttackDamage() const;

	UFUNCTION(BlueprintPure, Category = "Kakurenbo")
	float GetOniHearingRadius() const;

	/** 今いる鬼（かくれんぼ中・リザルト中のみ） */
	const TArray<TObjectPtr<AOniCharacter>>& GetOnis() const { return Onis; }

	/** 今あるお宝（かくれんぼ中のみ） */
	const TArray<TObjectPtr<ATreasureActor>>& GetTreasures() const { return Treasures; }

	// ===== 購入 =====

	/** 購入パートの商品一覧（表示順＝番号キー順） */
	UFUNCTION(BlueprintPure, Category = "Kakurenbo")
	TArray<FShopItemView> GetShopItems() const;

	/** 商品をインデックス（0 始まり）で購入する */
	UFUNCTION(BlueprintCallable, Category = "Kakurenbo")
	bool TryBuyShopItem(int32 Index);

	UFUNCTION(BlueprintCallable, Category = "Kakurenbo")
	bool TryBuyMashUpgrade();

	UFUNCTION(BlueprintCallable, Category = "Kakurenbo")
	bool TryBuyTimeUpgrade();

	UFUNCTION(BlueprintCallable, Category = "Kakurenbo")
	bool TryBuyWall(int32 WallTypeIndex);

	// ===== 設置 =====

	/** 設置パート：在庫の壁をマスの一番上に置けるか（置けない理由も返す） */
	bool CanPlaceWall(const FIntPoint& Cell, int32 WallTypeIndex, FText* OutReason = nullptr) const;

	/** 設置パート：在庫の壁をマスの一番上に置く */
	UFUNCTION(BlueprintCallable, Category = "Kakurenbo")
	bool PlaceWall(FIntPoint Cell, int32 WallTypeIndex);

	/** 設置パート：置いた壁を回収して在庫に戻す */
	UFUNCTION(BlueprintCallable, Category = "Kakurenbo")
	bool PickUpWall(APlaceableBlock* Block);

	// ===== かくれんぼ =====

	/** 連打 1 回分の処理（コイン獲得＋音を出す） */
	UFUNCTION(BlueprintCallable, Category = "Kakurenbo")
	void HandleMash(const FVector& NoiseLocation);

	/** お宝を取得する（お宝がプレイヤーに触れたときに呼ぶ） */
	void CollectTreasure(ATreasureActor* Treasure);

	// ===== パート遷移 =====

	UFUNCTION(BlueprintCallable, Category = "Kakurenbo")
	void StartShopPhase();

	/** 設置パートへ。壊れた壁は在庫があれば自動で修復する */
	UFUNCTION(BlueprintCallable, Category = "Kakurenbo")
	void StartBuildPhase();

	UFUNCTION(BlueprintCallable, Category = "Kakurenbo")
	void StartHidePhase();

	/** かくれんぼ終了。bCleared=true なら逃げ切り、false なら見つかった */
	UFUNCTION(BlueprintCallable, Category = "Kakurenbo")
	void EndHidePhase(bool bCleared);

	/** 「次へ」操作。今のパートから次のパートへ進める */
	UFUNCTION(BlueprintCallable, Category = "Kakurenbo")
	void AdvancePhase();

	UFUNCTION(BlueprintPure, Category = "Kakurenbo")
	EKakurenboPhase GetPhase() const;

	UFUNCTION(BlueprintPure, Category = "Kakurenbo")
	AKakurenboArena* GetArena() const { return Arena; }

	/** デバッグ用：コインを増やす */
	void DebugAddCoins(double Amount);

	/** デバッグ用：残り時間を減らす */
	void DebugSkipTime(float Seconds);

protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	/** PlayerStart が置かれていないレベルでは舞台の中央に出現させる */
	virtual void RestartPlayer(AController* NewPlayer) override;

	void SetPhase(EKakurenboPhase NewPhase);
	AKakurenboGameState* GS() const;
	ACharacter* GetPlayerCharacter() const;

	void SpawnOnis();
	void DespawnOnis();
	void HandleOniFoundHider();
	void HandleOniDestroyedWalls(int32 Count);

	void SpawnTreasures();
	void ClearTreasures();

	/** 壊れた壁を設計図どおりに在庫から直す */
	void RepairWalls();

	/** プレイヤーの体がそのマスの範囲（縦方向は問わない）に入っているか */
	bool IsPlayerInCellColumn(const FIntPoint& Cell) const;

	UPROPERTY()
	TObjectPtr<AKakurenboArena> Arena;

	UPROPERTY()
	TArray<TObjectPtr<AOniCharacter>> Onis;

	UPROPERTY()
	TArray<TObjectPtr<ATreasureActor>> Treasures;

	bool bOnisSpawnedThisRound = false;
	FRandomStream TreasureRandom;
};
