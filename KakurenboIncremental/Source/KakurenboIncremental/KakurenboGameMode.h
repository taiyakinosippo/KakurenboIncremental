// ゲームのルールと進行を管理するクラス。
// パートの切り替え・コインの計算・購入処理・鬼とお宝の出現・罠・セーブとロードはすべてここに集める。
// 演出（破片・輪・画面の点滅）と効果音を鳴らすきっかけもここから出す。
//
// 数値（ステージ・強化・壁・鬼・罠）は <プロジェクト>/Data/*.csv から読み込む（Data/README.md 参照）。
// CSV が読めないときは、このクラスに書いてある既定値を使う。

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "KakurenboTypes.h"
#include "KakurenboGameMode.generated.h"

class AKakurenboArena;
class AKakurenboGameState;
class AOniCharacter;
class APlaceableBlock;
class ATrapActor;
class ATreasureActor;
class UDataTable;
class USoundBase;

UCLASS()
class KAKURENBOINCREMENTAL_API AKakurenboGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AKakurenboGameMode();

	// ===== バランスデータ =====

	/**
	 * ステージごとの設定（行の順番＝ステージ番号）。起動時に Data/Stages.csv から読み込む。
	 * ここに DataTable アセットを設定すると CSV より優先される（行の型: KakurenboStageRow）
	 */
	UPROPERTY(EditAnywhere, Category = "Balance")
	TObjectPtr<UDataTable> StageTable;

	/** 強化の数値。Data/Upgrades.csv の代わりに使う DataTable（行の型: KakurenboUpgradeRow。行名 Mash / Time） */
	UPROPERTY(EditAnywhere, Category = "Balance")
	TObjectPtr<UDataTable> UpgradeTable;

	/** 壁の種類。Data/Walls.csv の代わりに使う DataTable（行の型: WallTypeDef） */
	UPROPERTY(EditAnywhere, Category = "Balance")
	TObjectPtr<UDataTable> WallTable;

	/** 鬼の種類ごとの数値。Data/OniTypes.csv の代わりに使う DataTable（行の型: KakurenboOniTypeRow。行名 Balanced / Scout / Breaker / Careful） */
	UPROPERTY(EditAnywhere, Category = "Balance")
	TObjectPtr<UDataTable> OniTypeTable;

	/** 罠の種類。Data/Traps.csv の代わりに使う DataTable（行の型: KakurenboTrapRow） */
	UPROPERTY(EditAnywhere, Category = "Balance")
	TObjectPtr<UDataTable> TrapTable;

	/** 読み込んだ鬼の種類ごとの数値 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Balance")
	TMap<EOniType, FKakurenboOniTypeRow> OniTypeRows;

	/** 読み込んだステージの表（CSV・DataTable が無ければ空 → ステージ 1 の既定値から伸ばす） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Balance")
	TArray<FKakurenboStageRow> StageRows;

	/** 表より後のステージで、1 ステージごとに伸ばす量 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Balance|Stage Growth")
	float StageHideDurationGrowth = 5.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Balance|Stage Growth")
	double StageClearRewardGrowth = 4.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Balance|Stage Growth")
	double StageOniDamageGrowth = 1.6;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Balance|Stage Growth")
	float StageOniHearingGrowth = 100.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Balance|Stage Growth")
	float StageOniSpeedGrowth = 15.f;

	// ===== かくれんぼのルール =====

	/** かくれんぼ開始から鬼が出てくるまでの猶予（秒） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rules")
	float HideStartDelay = 3.f;

	// ===== 強化（Upgrades.csv で上書きされる） =====

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Economy")
	double MashIncomeBase = 1.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Economy")
	double MashIncomeGrowth = 1.5;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Economy")
	double TimeIncomeBase = 1.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Economy")
	double TimeIncomeGrowth = 1.5;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Economy")
	double MashUpgradeBaseCost = 15.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Economy")
	double MashUpgradeCostGrowth = 1.7;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Economy")
	double TimeUpgradeBaseCost = 25.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Economy")
	double TimeUpgradeCostGrowth = 1.7;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Economy")
	FText MashUpgradeName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Economy")
	FText TimeUpgradeName;

	/** 壁の補強（すべての壁の耐久を上げる）。Upgrades.csv の Wall 行で上書きされる */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Economy")
	FText WallUpgradeName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Economy")
	double WallUpgradeBaseCost = 200.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Economy")
	double WallUpgradeCostGrowth = 3.0;

	/** 壁の耐久の倍率 = WallHPBase × WallHPGrowth^レベル */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Economy")
	double WallHPBase = 1.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Economy")
	double WallHPGrowth = 1.5;

	// ===== お宝 =====

	/** お宝 1 個の価値（そのステージの逃げ切り報酬に対する割合） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Treasure")
	double TreasureRewardRatio = 0.3;

	/** お宝はプレイヤー・他のお宝からこのマス数以上離して置く */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Treasure")
	float TreasureMinDistanceCells = 4.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Treasure")
	TSubclassOf<ATreasureActor> TreasureClass;

	// ===== 壁（Walls.csv で上書きされる） =====

	/** 購入できる壁の種類（ショップの並び順）。耐久は補強前の値（補強後は GetEffectiveWallTypes） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wall")
	TArray<FWallTypeDef> WallTypes;

	// ===== 罠（Traps.csv で上書きされる） =====

	/** 購入できる罠の種類（ショップ・設置パートの並び順） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trap")
	TArray<FKakurenboTrapRow> TrapTypes;

	// ===== 音 =====

	/** 効果音を音アセットに差し替える（設定していない音はプログラムで作った音を鳴らす） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sound")
	TMap<EKakurenboSfx, TObjectPtr<USoundBase>> SoundOverrides;

	// ===== 鬼 =====

	/** スポーンする鬼のクラス（BP の派生クラスで見た目を変えてよい） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Oni")
	TSubclassOf<AOniCharacter> OniClass;

	/** うろうろするときの速さ（プレイヤーは 420）。鬼の種類ごとの倍率（SpeedScale）を掛ける */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Oni")
	float OniWanderSpeedBase = 420.f;

	/** 音を調べに行くときの速さ */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Oni")
	float OniInvestigateSpeedBase = 700.f;

	/** 追いかけるときの速さ（プレイヤーの 2 倍）。見つかったら走って逃げ切るのは難しく、壁の陰に隠れて見失わせる */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Oni")
	float OniChaseSpeedBase = 840.f;

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

	// ===== セーブ =====

	/** パートが変わるたびに自動で保存し、起動時に読み込む（起動オプション -KakuNoSave で無効） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Save")
	bool bSaveEnabled = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Save")
	FString SaveSlotName = TEXT("Kakurenbo");

	// ===== 計算 =====

	/** 今のステージの設定 */
	FKakurenboStageRow GetStageSettings() const;
	/** 指定したステージの設定（表より後は伸ばした値） */
	FKakurenboStageRow GetStageSettingsFor(int32 Stage) const;

	UFUNCTION(BlueprintPure, Category = "Kakurenbo")
	double GetMashIncome() const;

	UFUNCTION(BlueprintPure, Category = "Kakurenbo")
	double GetTimeIncomePerSecond() const;

	UFUNCTION(BlueprintPure, Category = "Kakurenbo")
	double GetMashUpgradeCost() const;

	UFUNCTION(BlueprintPure, Category = "Kakurenbo")
	double GetTimeUpgradeCost() const;

	UFUNCTION(BlueprintPure, Category = "Kakurenbo")
	double GetWallUpgradeCost() const;

	/** 今の壁の耐久の倍率（壁の補強のレベルで決まる） */
	UFUNCTION(BlueprintPure, Category = "Kakurenbo")
	double GetWallHPMultiplier() const;

	/** 補強を反映した壁の種類（耐久 = 元の耐久 × 倍率） */
	TArray<FWallTypeDef> GetEffectiveWallTypes() const;

	/** 罠の今の価格（持っている数が増えるほど高くなる） */
	UFUNCTION(BlueprintPure, Category = "Kakurenbo")
	double GetTrapCost(int32 TrapTypeIndex) const;

	/** 持っている罠の数（在庫＋置いてある数。発動して消えた分は数えない） */
	UFUNCTION(BlueprintPure, Category = "Kakurenbo")
	int32 GetOwnedTrapCount(int32 TrapTypeIndex) const;

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

	UFUNCTION(BlueprintPure, Category = "Kakurenbo")
	int32 GetNumOnis() const;

	/** 鬼の種類ごとの数値（表に無ければ既定値） */
	FKakurenboOniTypeRow GetOniTypeRow(EOniType Type) const;

	UFUNCTION(BlueprintPure, Category = "Kakurenbo")
	int32 GetNumTreasures() const;

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

	/** 壁の補強を買う（置いてある壁の耐久もすぐに上がる） */
	UFUNCTION(BlueprintCallable, Category = "Kakurenbo")
	bool TryBuyWallUpgrade();

	UFUNCTION(BlueprintCallable, Category = "Kakurenbo")
	bool TryBuyTrap(int32 TrapTypeIndex);

	/** 商品の番号（0 始まり）。並び: 強化（連打・時間・壁の補強）→ 壁 → 罠 */
	int32 GetShopIndexOfWall(int32 WallTypeIndex) const;
	int32 GetShopIndexOfTrap(int32 TrapTypeIndex) const;

	// ===== 設置 =====

	/** 設置パート：在庫の壁をマスの一番上に置けるか（置けない理由も返す） */
	bool CanPlaceWall(const FIntPoint& Cell, int32 WallTypeIndex, FText* OutReason = nullptr) const;

	/** 設置パート：在庫の壁をマスの一番上に置く */
	UFUNCTION(BlueprintCallable, Category = "Kakurenbo")
	bool PlaceWall(FIntPoint Cell, int32 WallTypeIndex);

	/** 設置パート：置いた壁を回収して在庫に戻す */
	UFUNCTION(BlueprintCallable, Category = "Kakurenbo")
	bool PickUpWall(APlaceableBlock* Block);

	/** 設置パート：在庫の罠を床のマスに置けるか（置けない理由も返す） */
	bool CanPlaceTrap(const FIntPoint& Cell, int32 TrapTypeIndex, FText* OutReason = nullptr) const;

	/** 設置パート：在庫の罠を置く */
	UFUNCTION(BlueprintCallable, Category = "Kakurenbo")
	bool PlaceTrap(FIntPoint Cell, int32 TrapTypeIndex);

	/** 設置パート：罠を回収する（残っていれば在庫に戻す。発動済みなら設計図から消すだけ） */
	UFUNCTION(BlueprintCallable, Category = "Kakurenbo")
	bool PickUpTrap(FIntPoint Cell);

	// ===== かくれんぼ =====

	/** 連打 1 回分の処理（コイン獲得＋音を出す） */
	UFUNCTION(BlueprintCallable, Category = "Kakurenbo")
	void HandleMash(const FVector& NoiseLocation);

	/** 音を鳴らして鬼に聞かせる（Loudness 1 = 連打と同じ距離まで届く） */
	void EmitNoise(const FVector& Location, float Loudness);

	/** お宝を取得する（お宝がプレイヤーに触れたときに呼ぶ） */
	void CollectTreasure(ATreasureActor* Treasure);

	/** 罠が発動した（トリモチ: 鬼が動けなくなる / おとり: 鬼に壊された）。罠はここで消える */
	void HandleTrapTriggered(ATrapActor* Trap, AOniCharacter* Oni);

	/** おとりが音を出した */
	void HandleDecoyPing(ATrapActor* Trap);

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

	// ===== セーブ・ロード =====

	/** 今の状態を保存する（bSaveEnabled のときだけ） */
	UFUNCTION(BlueprintCallable, Category = "Kakurenbo")
	bool SaveProgress();

	/** 保存した状態を読み込んで反映する。セーブが無ければ false */
	UFUNCTION(BlueprintCallable, Category = "Kakurenbo")
	bool LoadProgress();

	/** セーブを消して、最初（ステージ 1・何もない状態）からやり直す */
	UFUNCTION(BlueprintCallable, Category = "Kakurenbo")
	void ResetProgress();

	/** 画面上部に一時的なお知らせを出す */
	void ShowNotice(const FText& Text, float Seconds = 5.f);

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

	/** CSV（または DataTable アセット）から数値を読み込んで反映する */
	void LoadBalanceData();

	void SpawnOnis();
	void DespawnOnis();
	void HandleOniFoundHider();
	void HandleOniDestroyedWalls(int32 Count);

	/** 壁が攻撃された（破片を飛ばして音を鳴らす） */
	void HandleBlockHit(const FVector& Location, const FLinearColor& Color, bool bDestroyed);

	void SpawnTreasures();
	void ClearTreasures();

	/** 壊れた壁を設計図どおりに在庫から直す */
	void RepairWalls();

	/** 発動して消えた罠を設計図どおりに在庫から置き直す */
	void RefillTraps();

	/** 開始前のカウントダウン・残り時間の秒読みの音 */
	void TickCountdownSounds();

	/** 画面の点滅・大きな文字（HUD に頼む） */
	void FlashScreen(const FLinearColor& Color, float Duration) const;
	void ShowPopup(const FString& Text, const FLinearColor& Color) const;

	/** プレイヤーの体がそのマスの範囲（縦方向は問わない）に入っているか */
	bool IsPlayerInCellColumn(const FIntPoint& Cell) const;

	UPROPERTY()
	TObjectPtr<AKakurenboArena> Arena;

	UPROPERTY()
	TArray<TObjectPtr<AOniCharacter>> Onis;

	UPROPERTY()
	TArray<TObjectPtr<ATreasureActor>> Treasures;

	/** CSV から作った一時的な DataTable（GC で消えないように持っておく） */
	UPROPERTY()
	TArray<TObjectPtr<UDataTable>> LoadedTables;

	bool bOnisSpawnedThisRound = false;
	FRandomStream TreasureRandom;

	/** 最後に音を鳴らしたカウントダウン・秒読みの秒数（同じ秒で何度も鳴らさないため） */
	int32 LastCountdownSecond = 0;
	int32 LastTimeTickSecond = 0;
};
