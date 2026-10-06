// グリッドとブロックの配置を管理するワールドサブシステム。
// （WorldSubsystem: ワールドごとに 1 つ自動で作られるシングルトン。GetWorld()->GetSubsystem<>() で取得）
//
// 座標の約束:
//   マス (X, Y) は Origin から CellSize ごとに並ぶ。Origin は (0,0) マスの角で、床の上面の高さ。
//   段 (Level) 0 は床の上の 1 段目。1 段の高さは BlockHeight（プレイヤーの背の高さ）。
//   ブロックは常に下から詰まっていて、宙に浮かない。
//
// 設計図:
//   プレイヤーが置いた（回収した）ときの列の状態を「設計図」として覚えておく。
//   鬼に壊されても設計図は変わらないので、在庫があれば RepairFromDesign で元に戻せる。

#pragma once

#include "CoreMinimal.h"
#include "GridPathfinder.h"
#include "KakurenboTypes.h"
#include "Subsystems/WorldSubsystem.h"
#include "KakurenboGridSubsystem.generated.h"

class APlaceableBlock;

USTRUCT()
struct FKakurenboBlockColumn
{
	GENERATED_BODY()

	/** 今あるブロック（下の段から順） */
	UPROPERTY()
	TArray<TObjectPtr<APlaceableBlock>> Blocks;

	/** 設計図：プレイヤーが置いた壁の種類（下の段から順） */
	UPROPERTY()
	TArray<int32> Design;
};

DECLARE_MULTICAST_DELEGATE(FOnKakurenboGridChanged);

UCLASS()
class KAKURENBOINCREMENTAL_API UKakurenboGridSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	void Configure(const FVector& InOrigin, int32 InSizeX, int32 InSizeY, float InCellSize, float InBlockHeight, int32 InMaxStackHeight);
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
	float GetBlockHeight() const { return BlockHeight; }
	int32 GetMaxStackHeight() const { return MaxStackHeight; }

	// ===== ブロック =====

	int32 GetColumnHeight(const FIntPoint& Cell) const;
	APlaceableBlock* GetTopBlock(const FIntPoint& Cell) const;
	int32 GetBlockCount() const;

	/** そのマスの一番上にブロックを置けるか。置けない理由を返す */
	bool CanPlaceBlock(const FIntPoint& Cell, FText* OutReason = nullptr) const;

	/** プレイヤーが置く：マスの一番上にブロックを置き、設計図もその状態にする */
	APlaceableBlock* PlaceBlock(const FIntPoint& Cell, int32 WallTypeIndex, double MaxHP, const FLinearColor& Color);

	/** プレイヤーが回収する：ブロックを取り除き（上の段は 1 段落ちる）、設計図もその状態にする */
	bool PickUpBlock(APlaceableBlock* Block);

	/**
	 * 鬼の範囲攻撃。Center から Radius 以内に中心があるブロックに Damage を与え、壊れたものを取り除く。
	 * 設計図は変えない（あとで修復できるように）。
	 * @return 壊れたブロックの数
	 */
	int32 DamageBlocksInRadius(const FVector& Center, float Radius, double Damage);

	/** すべてのブロックと設計図を消す */
	void ClearAllBlocks();

	// ===== 設計図と修復 =====

	/** 設計図にあるのに壊れて無くなっているブロックの数（マスごと / 全体） */
	int32 GetMissingCount(const FIntPoint& Cell) const;
	int32 GetTotalMissing() const;

	/** 設計図の段数 */
	int32 GetDesignHeight(const FIntPoint& Cell) const;

	/**
	 * 壊れたブロックを設計図どおりに在庫から作り直す。
	 * @param InOutStock  種類ごとの在庫（使った分だけ減る）
	 * @param WallTypes   壁の種類の定義（耐久・色）
	 * @param CanSpawnAt  その位置（マス・段）に作ってよいか（プレイヤーと重なる場所を避けるため）
	 */
	void RepairFromDesign(TArray<int32>& InOutStock, const TArray<FWallTypeDef>& WallTypes,
		TFunctionRef<bool(const FIntPoint& Cell, int32 Level)> CanSpawnAt, int32& OutRepaired, int32& OutMissing);

	// ===== 鬼の経路探索・出現位置 =====

	/**
	 * 現在の配置から経路探索用のコスト表を作る。
	 * 壁のマスのコスト = 壊すのに必要な攻撃回数 × CostPerAttack
	 */
	FKakurenboPathGrid BuildPathGrid(double AttackDamage, float CostPerAttack) const;

	/** 配置が変わるたびに増える番号（鬼が経路を作り直すタイミングの判定に使う） */
	int32 GetGridVersion() const { return GridVersion; }

	FOnKakurenboGridChanged OnGridChanged;

	/**
	 * 空きマスを Count 個選ぶ。AvoidPoints と、選んだマスどうしから、なるべく遠いマスを順に選ぶ（鬼の出現位置用）
	 */
	TArray<FIntPoint> FindSpreadFreeCells(const TArray<FVector>& AvoidPoints, int32 Count) const;

	/**
	 * ランダムな空きマスを Count 個選ぶ。AvoidPoints から MinDistanceCells マス以上、選んだマスどうしも離す（お宝用）
	 */
	TArray<FIntPoint> FindRandomFreeCells(int32 Count, const TArray<FVector>& AvoidPoints, float MinDistanceCells, FRandomStream& Stream) const;

	/** ランダムな空きマス（見つからなければ false） */
	bool FindRandomFreeCell(FIntPoint& OutCell, const FRandomStream* Stream = nullptr) const;

private:
	int32 ToIndex(const FIntPoint& Cell) const { return Cell.Y * SizeX + Cell.X; }
	APlaceableBlock* SpawnBlockActor(const FIntPoint& Cell, int32 Level, int32 WallTypeIndex, double MaxHP, const FLinearColor& Color);
	void RemoveAtLevel(const FIntPoint& Cell, int32 Level);
	void RestackColumn(const FIntPoint& Cell);
	void SyncDesignToBlocks(const FIntPoint& Cell);
	void MarkChanged();

	FVector Origin = FVector::ZeroVector;
	int32 SizeX = 0;
	int32 SizeY = 0;
	float CellSize = 100.f;
	float BlockHeight = 160.f;
	int32 MaxStackHeight = 3;
	int32 GridVersion = 0;

	UPROPERTY()
	TArray<FKakurenboBlockColumn> Columns;
};
