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
	Overhead    UMETA(DisplayName = "Overhead"),    // 俯瞰（購入・設置・リザルト）
	ThirdPerson UMETA(DisplayName = "ThirdPerson"), // 三人称（かくれんぼ）：自分の背後の少し上から見る。マウスで回せる
};

/** 鬼の状態（HUD 表示にも使う） */
UENUM(BlueprintType)
enum class EOniState : uint8
{
	Wander      UMETA(DisplayName = "Wander"),      // うろうろ
	Investigate UMETA(DisplayName = "Investigate"), // 音のした方へ向かう
	Chase       UMETA(DisplayName = "Chase"),       // プレイヤーを見つけて追いかける
	Attack      UMETA(DisplayName = "Attack"),      // 壁を壊している
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

	/** 鬼の数 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stage")
	int32 NumOnis = 2;

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
