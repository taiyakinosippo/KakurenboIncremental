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

	/** 消音壁：音を消せる残りの回数（0 以下なら普通の壁で、減らない） */
	UPROPERTY(BlueprintReadOnly, Category = "Block")
	double SoundHP = 0.0;

	UPROPERTY(BlueprintReadOnly, Category = "Block")
	double MaxSoundHP = 0.0;

	/** Surface があれば（そのパソコンにテクスチャがあれば）その見た目、無ければ InColor の箱 */
	void InitBlock(int32 InWallTypeIndex, double InMaxHP, const FLinearColor& InColor, double InSoundHP = 0.0, const struct FKakurenboSurfaceRow* Surface = nullptr);

	/** テクスチャの見た目になっているか（テスト用） */
	bool IsTextured() const { return bTextured; }

	/** ダメージを与える。壊れたら true（アクターの破棄はグリッド側が行う） */
	bool ApplyBlockDamage(double Damage);

	/** 消音壁：音を消した。音を消せる回数が尽きたら true（壊れる。アクターの破棄はグリッド側が行う） */
	bool ApplySoundDamage(double Amount);

	/** 音を消すと減っていく壁か */
	bool IsSoundBreakable() const { return MaxSoundHP > 0.0; }

	bool IsDamaged() const { return HP < MaxHP; }

	/** 耐久を Factor 倍にする（転生のお店で壁の硬さを買ったとき。傷の割合はそのまま） */
	void ScaleHP(double Factor);

	/** 音を消せる回数を Factor 倍にする（転生のお店で消音壁の丈夫さを買ったとき） */
	void ScaleSoundHP(double Factor);

	const FLinearColor& GetBaseColor() const { return BaseColor; }

private:
	void UpdateColor();

	FLinearColor BaseColor = FLinearColor::White;

	/** マテリアルの "Color" パラメータの傷ついていないときの値（色の箱なら BaseColor、テクスチャなら模様に掛ける色） */
	FLinearColor MaterialTint = FLinearColor::White;
	bool bTextured = false;
};
