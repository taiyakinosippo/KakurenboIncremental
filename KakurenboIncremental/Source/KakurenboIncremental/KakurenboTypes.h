// ゲーム全体で共有する型（enum / struct）

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "KakurenboTypes.generated.h"

/** ゲームのパート。購入 → 設置 → かくれんぼ → リザルト → 購入 … と循環する */
UENUM(BlueprintType)
enum class EKakurenboPhase : uint8
{
	Shop   UMETA(DisplayName = "Shop"),
	Build  UMETA(DisplayName = "Build"),
	Hide   UMETA(DisplayName = "Hide"),
	Result UMETA(DisplayName = "Result"),
};

/** プレイヤーの視点 */
UENUM(BlueprintType)
enum class EHiderViewMode : uint8
{
	Overhead    UMETA(DisplayName = "Overhead"),    // 俯瞰（購入・リザルト）：斜め上から自分を見下ろす
	ThirdPerson UMETA(DisplayName = "ThirdPerson"), // 三人称（かくれんぼ）：自分の背後の少し上から見る。マウスで回せる
	TopDown     UMETA(DisplayName = "TopDown"),     // 真上から（設置）：プレイヤーは表示せず、カメラだけを WASD で動かす
};

/** 鬼の状態（HUD 表示にも使う） */
UENUM(BlueprintType)
enum class EOniState : uint8
{
	Wander      UMETA(DisplayName = "Wander"),      // うろうろ
	Investigate UMETA(DisplayName = "Investigate"), // 音のした方へ向かう
	Chase       UMETA(DisplayName = "Chase"),       // プレイヤーを見つけて追いかける
	Inspect     UMETA(DisplayName = "Inspect"),     // 怪しい場所（壁に囲まれた空洞・見つけた壁）を調べに行く
	Attack      UMETA(DisplayName = "Attack"),      // 壁を壊している
	Stunned     UMETA(DisplayName = "Stunned"),     // 罠にかかって動けない
};

/** 鬼の種類（パックマンのお化けのように、それぞれ動き方が違う） */
UENUM(BlueprintType)
enum class EOniType : uint8
{
	Balanced UMETA(DisplayName = "Balanced"), // 標準：速さも壁を壊す力も普通
	Scout    UMETA(DisplayName = "Scout"),    // スピード：足が速く視界が広い。マップ全体を大まかに回る。壁を壊す力は弱い
	Breaker  UMETA(DisplayName = "Breaker"),  // パワー：遅いが壁を壊す力が強い。見つけた壁を壊しに行く。自分から探さない
	Careful  UMETA(DisplayName = "Careful"),  // 慎重：近くから隅々まで調べる。調べた場所を仲間の慎重鬼と共有。壁は 1 個ずつ壊す
};

/** 罠の種類（Data/Traps.csv の Kind 列） */
UENUM(BlueprintType)
enum class ETrapKind : uint8
{
	Sticky UMETA(DisplayName = "Sticky"), // トリモチ：踏んだ鬼をしばらく動けなくする（1 回使うと消える）
	Decoy  UMETA(DisplayName = "Decoy"),  // おとり：一定の間隔で音を出して鬼を呼び寄せる（鬼が触れると壊れる）
};

/** 効果音の種類（音のファイルが無くても、プログラムで波形を作って鳴らす） */
UENUM(BlueprintType)
enum class EKakurenboSfx : uint8
{
	Mash          UMETA(DisplayName = "Mash"),          // 連打
	Treasure      UMETA(DisplayName = "Treasure"),      // お宝を取った
	Alert         UMETA(DisplayName = "Alert"),         // 鬼に見つかって追いかけられ始めた
	BlockHit      UMETA(DisplayName = "BlockHit"),      // 壁が攻撃された（壊れていない）
	BlockBreak    UMETA(DisplayName = "BlockBreak"),    // 壁が壊れた
	Caught        UMETA(DisplayName = "Caught"),        // 見つかった（アウト）
	Clear         UMETA(DisplayName = "Clear"),         // 逃げ切った
	TrapSticky    UMETA(DisplayName = "TrapSticky"),    // トリモチにかかった
	DecoyPing     UMETA(DisplayName = "DecoyPing"),     // おとりが音を出した
	DecoyBreak    UMETA(DisplayName = "DecoyBreak"),    // おとりが壊された
	CountdownBeep UMETA(DisplayName = "CountdownBeep"), // 開始前のカウントダウン
	RoundStart    UMETA(DisplayName = "RoundStart"),    // 鬼が出てきた
	TimeTick      UMETA(DisplayName = "TimeTick"),      // 残り 5 秒からの秒読み
	Buy           UMETA(DisplayName = "Buy"),           // 購入できた
	BuyFail       UMETA(DisplayName = "BuyFail"),       // コインが足りない
	Place         UMETA(DisplayName = "Place"),         // 壁・罠を置いた
	PickUp        UMETA(DisplayName = "PickUp"),        // 壁・罠を回収した
	Dash          UMETA(DisplayName = "Dash"),          // ダッシュした（大きな音）
	Prestige      UMETA(DisplayName = "Prestige"),      // 転生した
	Count         UMETA(Hidden)
};

/** 購入パートの商品 1 つ分の表示用データ */
USTRUCT(BlueprintType)
struct FShopItemView
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Shop")
	FText DisplayName;

	/** 効果の説明（現在値 → 購入後の値 など） */
	UPROPERTY(BlueprintReadOnly, Category = "Shop")
	FText Description;

	UPROPERTY(BlueprintReadOnly, Category = "Shop")
	double Cost = 0.0;

	/** 強化ならレベル、壁なら在庫数 */
	UPROPERTY(BlueprintReadOnly, Category = "Shop")
	FText OwnedText;

	/** 壁・罠：壊れた（使った）数と、全部直すのにあと何個要るか（無ければ空） */
	UPROPERTY(BlueprintReadOnly, Category = "Shop")
	FText RepairText;

	/** 壁・罠：在庫が足りず、直せないものがある */
	UPROPERTY(BlueprintReadOnly, Category = "Shop")
	bool bNeedsMoreForRepair = false;
};

/**
 * 購入できる壁の種類。Data/Walls.csv（または GameMode の WallTable）の 1 行。
 * FTableRowBase を継承すると、DataTable（表形式のデータ）の行として使える
 */
USTRUCT(BlueprintType)
struct FWallTypeDef : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wall")
	FText DisplayName;

	/** 耐久値。鬼の攻撃力がこれ以上だと一撃で壊れる */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wall")
	double MaxHP = 1.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wall")
	double Cost = 10.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wall")
	FLinearColor Color = FLinearColor(0.5f, 0.35f, 0.2f);

	/**
	 * 音を小さくする割合（0〜1。消音壁）。
	 * プレイヤーが壁に囲まれた場所（空洞）にいるときだけ、囲んでいる壁の平均の割合だけ連打・ダッシュの音が小さくなる
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wall")
	float NoiseDamping = 0.f;
};

/** 転生の数値。Data/Prestige.csv の 1 行（行名 Prestige） */
USTRUCT(BlueprintType)
struct FKakurenboPrestigeRow : public FTableRowBase
{
	GENERATED_BODY()

	/** このステージまで来たら転生できる */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Prestige")
	int32 MinStage = 5;

	/** 転生ポイント = (今のステージ - MinStage + 1) × PointsPerStage */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Prestige")
	int32 PointsPerStage = 1;

	/** すべての壁の耐久の倍率 = WallHPGrowth ^ 転生ポイントの合計 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Prestige")
	double WallHPGrowth = 1.5;
};

/** 強化 1 種類の数値。Data/Upgrades.csv（または GameMode の UpgradeTable）の 1 行 */
USTRUCT(BlueprintType)
struct FKakurenboUpgradeRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Upgrade")
	FText DisplayName;

	/** Lv.0 から Lv.1 に上げる価格 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Upgrade")
	double BaseCost = 10.0;

	/** レベルが 1 上がるごとの価格の倍率 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Upgrade")
	double CostGrowth = 1.5;

	/** Lv.0 のときの効果（連打 1 回のコイン、毎秒のコインなど） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Upgrade")
	double BaseValue = 1.0;

	/** レベルが 1 上がるごとの効果の倍率 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Upgrade")
	double ValueGrowth = 1.5;
};

/** ステージ 1 つ分の設定。Data/Stages.csv（または GameMode の StageTable）の 1 行（行の順番＝ステージ番号） */
USTRUCT(BlueprintType)
struct FKakurenboStageRow : public FTableRowBase
{
	GENERATED_BODY()

	/** 制限時間（秒） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stage")
	float HideDuration = 30.f;

	/** 出てくる鬼の種類と数。CSV では "(Balanced,Scout)" のように書く */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stage")
	TArray<EOniType> OniTypes = { EOniType::Balanced, EOniType::Scout };

	/** お宝の数 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stage")
	int32 NumTreasures = 3;

	/** 逃げ切り報酬（コイン） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stage")
	double ClearReward = 100.0;

	/** 鬼の攻撃力（壁の耐久値と比べる） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stage")
	double OniDamage = 1.0;

	/** 鬼に連打の音が聞こえる距離（cm） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stage")
	float OniHearingRadius = 1200.f;

	/** 鬼の移動速度に足す値（cm/秒。うろうろ・調べる・追いかける の全部に足す） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stage")
	float OniSpeedBonus = 0.f;
};

/** 鬼の種類ごとの数値。Data/OniTypes.csv の 1 行（行名は Balanced / Scout / Breaker / Careful） */
USTRUCT(BlueprintType)
struct FKakurenboOniTypeRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OniType")
	FText DisplayName;

	/** 移動速度の倍率（基本の速さ: うろうろ 420 / 調べる 700 / 追いかける 840） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OniType")
	float SpeedScale = 1.f;

	/** 壁を壊す力の倍率（ステージの攻撃力に掛ける） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OniType")
	double DamageScale = 1.0;

	/** 視界の距離（cm） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OniType")
	float SightRadius = 900.f;

	/** 視界の半角（度） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OniType")
	float SightHalfAngle = 40.f;

	/** 音が聞こえる距離の倍率（0 なら音を気にしない） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OniType")
	float HearingScale = 1.f;

	/** true なら目の前の壁 1 個だけを壊す（範囲攻撃しない） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OniType")
	bool bSingleTargetAttack = false;

	/** 範囲攻撃の半径（cm） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OniType")
	float AttackRadius = 160.f;

	/** 攻撃の溜め時間（秒） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OniType")
	float AttackWindup = 0.8f;

	/** うろうろ中、壁に囲まれた空洞があればそこを調べに行く確率（0〜1） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OniType")
	float PocketInspectChance = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OniType")
	FLinearColor Color = FLinearColor(0.9f, 0.1f, 0.08f);

	/** 見た目の太さの倍率 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OniType")
	float BodyScale = 1.f;
};

/** 罠 1 種類の数値。Data/Traps.csv の 1 行（行の並び順が購入パート・設置パートでの並び順） */
USTRUCT(BlueprintType)
struct FKakurenboTrapRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trap")
	FText DisplayName;

	/** 罠の働き（Sticky = トリモチ / Decoy = おとり） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trap")
	ETrapKind Kind = ETrapKind::Sticky;

	/** 1 個目の価格 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trap")
	double Cost = 60.0;

	/** 持っている数（在庫＋置いてある数）が 1 増えるごとの価格の倍率 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trap")
	double CostGrowth = 1.3;

	/** トリモチ：鬼を動けなくする時間（秒） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trap")
	float StunSeconds = 4.f;

	/** おとり：音を出す間隔（秒） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trap")
	float NoiseInterval = 2.5f;

	/** おとり：音の大きさ（1 なら連打の音と同じ距離まで届く） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trap")
	float NoiseLoudness = 1.f;

	/** 鬼の体の中心がこの距離（cm）まで来たら発動する */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trap")
	float TriggerRadius = 45.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trap")
	FLinearColor Color = FLinearColor(1.f, 0.9f, 0.2f);
};
