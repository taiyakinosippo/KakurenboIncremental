// ゲーム全体で共有する型（enum / struct）

#pragma once

#include "CoreMinimal.h"
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

/** 鬼の状態（HUD 表示にも使う） */
UENUM(BlueprintType)
enum class EOniState : uint8
{
	Wander      UMETA(DisplayName = "Wander"),      // うろうろ
	Investigate UMETA(DisplayName = "Investigate"), // 音のした方へ向かう
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

/** 購入できる壁の種類。GameMode の WallTypes 配列で定義する */
USTRUCT(BlueprintType)
struct FWallTypeDef
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
