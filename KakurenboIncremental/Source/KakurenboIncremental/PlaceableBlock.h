// グリッドに置けるブロック（壁）。耐久値を持ち、鬼の攻撃で壊れる。
// 罠は別のクラス（ATrapActor）。

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "PlaceableBlock.generated.h"

class UStaticMeshComponent;

UCLASS()
class KAKURENBOINCREMENTAL_API APlaceableBlock : public AActor
{
	GENERATED_BODY()

public:
	APlaceableBlock();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Block")
	TObjectPtr<UStaticMeshComponent> Mesh;

	/** GameMode の WallTypes のどれか（在庫に戻すときに使う） */
	UPROPERTY(BlueprintReadOnly, Category = "Block")
	int32 WallTypeIndex = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Block")
	double MaxHP = 1.0;

	UPROPERTY(BlueprintReadOnly, Category = "Block")
	double HP = 1.0;

	/** 置かれているマスと段（0 = 床の上の 1 段目） */
	UPROPERTY(BlueprintReadOnly, Category = "Block")
	FIntPoint Cell = FIntPoint::ZeroValue;

	UPROPERTY(BlueprintReadOnly, Category = "Block")
	int32 Level = 0;

	void InitBlock(int32 InWallTypeIndex, double InMaxHP, const FLinearColor& InColor);

	/** ダメージを与える。壊れたら true（アクターの破棄はグリッド側が行う） */
	bool ApplyBlockDamage(double Damage);

	bool IsDamaged() const { return HP < MaxHP; }

	/** 耐久を Factor 倍にする（壁の補強を買ったとき。傷の割合はそのまま） */
	void ScaleHP(double Factor);

	const FLinearColor& GetBaseColor() const { return BaseColor; }

private:
	void UpdateColor();

	FLinearColor BaseColor = FLinearColor::White;
};
