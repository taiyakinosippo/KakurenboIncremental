// グリッドとブロックの配置を管理するワールドサブシステム。
// （WorldSubsystem: ワールドごとに 1 つ自動で作られるシングルトン。GetWorld()->GetSubsystem<>() で取得）
//
// 座標の約束:
//   マス (X, Y) は Origin から CellSize ごとに並ぶ。Origin は (0,0) マスの角で、床の上面の高さ。
//   段 (Level) 0 は床の上の 1 段目。ブロックは常に下から詰まっていて、宙に浮かない。

#pragma once

#include "CoreMinimal.h"
#include "GridPathfinder.h"
#include "Subsystems/WorldSubsystem.h"
#include "KakurenboGridSubsystem.generated.h"

class APlaceableBlock;

USTRUCT()
struct FKakurenboBlockColumn
{
	GENERATED_BODY()

	/** 下の段から順に並ぶ */
	UPROPERTY()
	TArray<TObjectPtr<APlaceableBlock>> Blocks;
};

DECLARE_MULTICAST_DELEGATE(FOnKakurenboGridChanged);

UCLASS()
class KAKURENBOINCREMENTAL_API UKakurenboGridSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	void Configure(const FVector& InOrigin, int32 InSizeX, int32 InSizeY, float InCellSize, int32 InMaxStackHeight);
	bool IsConfigured() const { return SizeX > 0; }

	// ===== 座標変換 =====

	FIntPoint WorldToCell(const FVector& WorldLocation) const;
	/** マスの中心、段 Level のブロックの中心の座標 */
	FVector CellToWorld(const FIntPoint& Cell, int32 Level = 0) const;
	/** マスの中心の床面の座標 */
	FVector CellFloorCenter(const FIntPoint& Cell) const;
	bool IsInside(const FIntPoint& Cell) const;

	int32 GetSizeX() const { return SizeX; }
	int32 GetSizeY() const { return SizeY; }
	float GetCellSize() const { return CellSize; }
	int32 GetMaxStackHeight() const { return MaxStackHeight; }

	// ===== ブロック =====

	int32 GetColumnHeight(const FIntPoint& Cell) const;
	APlaceableBlock* GetTopBlock(const FIntPoint& Cell) const;
	int32 GetBlockCount() const;

	/** そのマスの一番上にブロックを置けるか。置けない理由を返す */
	bool CanPlaceBlock(const FIntPoint& Cell, FText* OutReason = nullptr) const;

	/** そのマスの一番上にブロックを置く */
	APlaceableBlock* PlaceBlock(const FIntPoint& Cell, int32 WallTypeIndex, double MaxHP, const FLinearColor& Color);

	/** ブロックを取り除く（上の段は 1 段落ちる） */
	bool RemoveBlock(APlaceableBlock* Block);

	/**
	 * 範囲攻撃。Center から Radius 以内に中心があるブロックに Damage を与え、壊れたものを取り除く。
	 * @return 壊れたブロックの数
	 */
	int32 DamageBlocksInRadius(const FVector& Center, float Radius, double Damage);

	/** すべてのブロックを消す */
	void ClearAllBlocks();

	// ===== 鬼の経路探索用 =====

	/**
	 * 現在の配置から経路探索用のコスト表を作る。
	 * 壁のマスのコスト = 壊すのに必要な攻撃回数 × CostPerAttack
	 */
	FKakurenboPathGrid BuildPathGrid(double AttackDamage, float CostPerAttack) const;

	/** 配置が変わるたびに増える番号（鬼が経路を作り直すタイミングの判定に使う） */
	int32 GetGridVersion() const { return GridVersion; }

	FOnKakurenboGridChanged OnGridChanged;

	/** 指定位置からいちばん遠い「空いている」マス */
	FIntPoint FindFarthestFreeCell(const FVector& From) const;

	/** ランダムな空きマス（見つからなければ false） */
	bool FindRandomFreeCell(FIntPoint& OutCell, const FRandomStream* Stream = nullptr) const;

private:
	int32 ToIndex(const FIntPoint& Cell) const { return Cell.Y * SizeX + Cell.X; }
	void RemoveAtLevel(const FIntPoint& Cell, int32 Level);
	void MarkChanged();

	FVector Origin = FVector::ZeroVector;
	int32 SizeX = 0;
	int32 SizeY = 0;
	float CellSize = 100.f;
	int32 MaxStackHeight = 4;
	int32 GridVersion = 0;

	UPROPERTY()
	TArray<FKakurenboBlockColumn> Columns;
};
