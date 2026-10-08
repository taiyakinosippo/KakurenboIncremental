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
//
// 罠:
//   壁の無い床のマスに 1 個だけ置ける（壁とは同じマスに置けない）。通るのは邪魔しない。
//   発動して消えても設計図は残り、RefillTrapsFromDesign で在庫から置き直せる。

#pragma once

#include "CoreMinimal.h"
#include "GridPathfinder.h"
#include "KakurenboTypes.h"
#include "Subsystems/WorldSubsystem.h"
#include "KakurenboGridSubsystem.generated.h"

class APlaceableBlock;
class ATrapActor;

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

	/** 設計図：置いた罠の種類（無ければ INDEX_NONE） */
	UPROPERTY()
	int32 TrapDesign = INDEX_NONE;

	/** 今ある罠（発動して消えたら null。設計図は残る） */
	UPROPERTY()
	TObjectPtr<ATrapActor> Trap;
};

DECLARE_MULTICAST_DELEGATE(FOnKakurenboGridChanged);
/** 壁が攻撃されたとき（ブロックの中心・色・壊れたか） */
DECLARE_MULTICAST_DELEGATE_ThreeParams(FOnKakurenboBlockHit, const FVector& /*Location*/, const FLinearColor& /*Color*/, bool /*bDestroyed*/);

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
	/** そのマスの段 Level のブロック（無ければ null） */
	APlaceableBlock* GetBlock(const FIntPoint& Cell, int32 Level) const;
	int32 GetBlockCount() const;

	/** そのマスの一番上にブロックを置けるか。置けない理由を返す */
	bool CanPlaceBlock(const FIntPoint& Cell, FText* OutReason = nullptr) const;

	/** プレイヤーが置く：マスの一番上にブロックを置き、設計図もその状態にする（SoundHP は消音壁の音を消せる回数） */
	APlaceableBlock* PlaceBlock(const FIntPoint& Cell, int32 WallTypeIndex, double MaxHP, const FLinearColor& Color, double SoundHP = 0.0);

	/** 今あるその種類の壁の数 */
	int32 GetLiveBlockCount(int32 WallTypeIndex) const;

	/** プレイヤーが回収する：ブロックを取り除き（上の段は 1 段落ちる）、設計図もその状態にする */
	bool PickUpBlock(APlaceableBlock* Block);

	/**
	 * 鬼の範囲攻撃。Center から Radius 以内に中心があるブロックに Damage を与え、壊れたものを取り除く。
	 * 設計図は変えない（あとで修復できるように）。
	 * @return 壊れたブロックの数
	 */
	int32 DamageBlocksInRadius(const FVector& Center, float Radius, double Damage);

	/**
	 * 1 個だけ攻撃。そのマスの一番下のブロックに Damage を与え、壊れたら取り除く（上の段は落ちる）。
	 * @return 壊れたブロックの数（0 か 1）
	 */
	int32 DamageBottomBlock(const FIntPoint& Cell, double Damage);

	/** すべてのブロック・罠と設計図を消す */
	void ClearAllBlocks();

	/** 今あるブロック・罠だけを消し、設計図は残す（転生したとき。在庫を買い直せば設置パートで自動で直る） */
	void ClearLiveKeepDesign();

	/**
	 * 指定したマスの壁・罠と設計図を消す。消した壁・罠（今あった分）の数を種類ごとに足して返す（在庫に戻す用）
	 * @return 何か消したマスの数
	 */
	int32 ClearCells(const TArray<FIntPoint>& Cells, TArray<int32>& InOutWallRefund, TArray<int32>& InOutTrapRefund);

	/** 置いてある壁の耐久をすべて Factor 倍にする（転生のお店で壁の硬さを買ったとき） */
	void ScaleAllBlockHP(double Factor);

	/** 置いてある消音壁の音を消せる回数をすべて Factor 倍にする（転生のお店で消音壁の丈夫さを買ったとき） */
	void ScaleAllBlockSoundHP(double Factor);

	/**
	 * 消音壁が音を消した：マスの一番下の段が消音壁なら、音を消せる回数を Amount 減らす。尽きたら壊れる（上の段は落ちる）。
	 * 設計図は変えない（あとで修復できるように）
	 * @return 壊れた壁の数
	 */
	int32 DamageSound(const TArray<FIntPoint>& Cells, double Amount);

	// ===== 鬼の出入り口 =====

	/** 壁も罠も置けないマス（鬼の出入り口の前）を決める */
	void SetReservedCells(const TArray<FIntPoint>& Cells);
	const TArray<FIntPoint>& GetReservedCells() const { return ReservedCells; }
	bool IsReservedCell(const FIntPoint& Cell) const { return ReservedCells.Contains(Cell); }

	/** 壁の種類ごとの見た目（木・石・鉄の模様）。無い種類は色の箱 */
	void SetWallSurface(int32 WallTypeIndex, const FKakurenboSurfaceRow* Surface)
	{
		if (Surface) { WallSurfaces.Add(WallTypeIndex, *Surface); } else { WallSurfaces.Remove(WallTypeIndex); }
	}

	/** 壁が攻撃されたとき（演出と音に使う） */
	FOnKakurenboBlockHit OnBlockHit;

	// ===== 館の家具・部屋の壁（壊せない・通れない） =====

	/** 通れないマス（家具・部屋の壁）を決める。置いてある壁・罠には触らない（先に片付けておく） */
	void SetObstacles(const TArray<FIntPoint>& Cells);

	/** 家具・部屋の壁のマスか */
	bool IsObstacle(const FIntPoint& Cell) const { return IsInside(Cell) && Obstacles.IsValidIndex(ToIndex(Cell)) && Obstacles[ToIndex(Cell)]; }

	/** 歩けるマスか（舞台の中で、壁も家具も無い） */
	bool IsWalkable(const FIntPoint& Cell) const { return IsInside(Cell) && GetColumnHeight(Cell) == 0 && !IsObstacle(Cell); }

	int32 GetObstacleCount() const;

	// ===== 罠 =====

	/** そのマスに今ある罠（発動して消えていれば null） */
	ATrapActor* GetTrap(const FIntPoint& Cell) const;

	/** そのマスの罠の設計図（無ければ INDEX_NONE） */
	int32 GetTrapDesign(const FIntPoint& Cell) const;

	/** 罠か罠の設計図があるマスか */
	bool HasTrap(const FIntPoint& Cell) const;

	/** 罠を置けるか（壁も壁の設計図も罠も無い床のマス）。置けない理由を返す */
	bool CanPlaceTrap(const FIntPoint& Cell, FText* OutReason = nullptr) const;

	/** プレイヤーが置く：罠を置いて設計図にも書く */
	ATrapActor* PlaceTrap(const FIntPoint& Cell, int32 TrapTypeIndex, const FKakurenboTrapRow& Def);

	/**
	 * プレイヤーが回収する：罠と設計図を消す。
	 * @param OutReturnedType 今あった罠の種類（在庫に戻す分）。発動済みで設計図だけだったら INDEX_NONE
	 * @return 罠か設計図があって消したら true
	 */
	bool PickUpTrap(const FIntPoint& Cell, int32& OutReturnedType);

	/** 罠が発動して消える（設計図は残る） */
	void ConsumeTrap(ATrapActor* Trap);

	/** 今ある罠の数（種類ごと） */
	int32 GetLiveTrapCount(int32 TrapTypeIndex) const;

	/** 設計図にあるのに発動して消えている罠の数（全部 / 種類ごと） */
	int32 GetMissingTrapCount() const;
	int32 GetMissingTrapCount(int32 TrapTypeIndex) const;

	/** 今ある罠すべて */
	TArray<ATrapActor*> GetAllTraps() const;

	/** 消えた罠を設計図どおりに在庫から置き直す */
	void RefillTrapsFromDesign(TArray<int32>& InOutStock, const TArray<FKakurenboTrapRow>& TrapTypes, int32& OutRefilled, int32& OutMissing);

	// ===== 設計図と修復 =====

	/** 設計図にあるのに壊れて無くなっているブロックの数（マスごと / 全体） */
	int32 GetMissingCount(const FIntPoint& Cell) const;
	int32 GetTotalMissing() const;

	/** 設計図にあるのに壊れて無くなっている、その種類の壁の数 */
	int32 GetMissingWallCount(int32 WallTypeIndex) const;

	// ===== 消音壁 =====

	/**
	 * Cell が壁に囲まれた空洞の中なら、囲んでいる壁（一番下の段の種類）の NoiseDamping の平均を返す。囲まれていなければ 0。
	 * @param OutBoundaryWalls 囲んでいる壁の数
	 */
	float GetEnclosureNoiseDamping(const FIntPoint& Cell, const TArray<FWallTypeDef>& WallTypes, int32* OutBoundaryWalls = nullptr, TArray<FIntPoint>* OutWallCells = nullptr) const;

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

	// ===== セーブ・ロード =====

	/** 壁か罠のあるマス（設計図か今あるもののどちらかがある）を書き出す */
	void ExportLayout(TArray<struct FKakurenboSavedColumn>& OutColumns) const;

	/** 書き出した配置を復元する（今ある壁・罠はすべて消してから作り直す。耐久は満タン） */
	void ImportLayout(const TArray<struct FKakurenboSavedColumn>& Columns, const TArray<FWallTypeDef>& WallTypes, const TArray<FKakurenboTrapRow>& TrapTypes);

	// ===== 鬼の経路探索・出現位置 =====

	/**
	 * 現在の配置から経路探索用のコスト表を作る。
	 * 壁のマスのコスト = 壊すのに必要な攻撃回数 × CostPerAttack
	 */
	FKakurenboPathGrid BuildPathGrid(double AttackDamage, float CostPerAttack) const;

	/** 壁を「通れない」ものとした経路探索用の表（入り口から入る経路を探すのに使う） */
	FKakurenboPathGrid BuildWalkGrid() const;

	/** 壁に囲まれて歩いては入れない空きマスのまとまり（空洞）。一番大きな空き地は含まない */
	TArray<TArray<FIntPoint>> FindEnclosedPockets() const;

	/** 一番大きな空き地（壁に囲まれていない場所）のマスか */
	bool IsInMainArea(const FIntPoint& Cell) const;

	/** マスごとの「一番大きな空き地に含まれる」（インデックスは Y * SizeX + X） */
	TArray<bool> GetMainAreaMask() const { return BuildMainAreaMask(); }

	/**
	 * そのマスを含む「建物」：つながっている壁（斜めも含めて隣り合う壁のまとまり）と、それに接する空洞のマス。
	 * 慎重鬼が「仲間が壊している壁とその周り（ドーナツ形の壁の真ん中など）」を避けるのに使う
	 */
	TArray<FIntPoint> GetStructureAround(const FIntPoint& Cell) const;

	/** 配置が変わるたびに増える番号（鬼が経路を作り直すタイミングの判定に使う） */
	int32 GetGridVersion() const { return GridVersion; }

	FOnKakurenboGridChanged OnGridChanged;

	/**
	 * 空きマスを Count 個選ぶ。AvoidPoints と、選んだマスどうしから、なるべく遠いマスを順に選ぶ（鬼の出現位置用）。
	 * 壁に囲まれた空洞の中と、罠のあるマスは選ばない（一番大きな空き地の中だけ）
	 */
	TArray<FIntPoint> FindSpreadFreeCells(const TArray<FVector>& AvoidPoints, int32 Count) const;

	/**
	 * ランダムな空きマスを Count 個選ぶ。AvoidPoints から MinDistanceCells マス以上、選んだマスどうしも離す（お宝用）。
	 * 壁に囲まれた空洞の中・罠のあるマス・鬼の出入り口の前は選ばない（一番大きな空き地の中だけ）
	 */
	TArray<FIntPoint> FindRandomFreeCells(int32 Count, const TArray<FVector>& AvoidPoints, float MinDistanceCells, FRandomStream& Stream) const;

	/** ランダムな空きマス（見つからなければ false） */
	bool FindRandomFreeCell(FIntPoint& OutCell, const FRandomStream* Stream = nullptr) const;

private:
	int32 ToIndex(const FIntPoint& Cell) const { return Cell.Y * SizeX + Cell.X; }
	/** 一番大きな空き地に含まれるマスなら true（マスごと） */
	TArray<bool> BuildMainAreaMask() const;
	APlaceableBlock* SpawnBlockActor(const FIntPoint& Cell, int32 Level, int32 WallTypeIndex, double MaxHP, const FLinearColor& Color, double SoundHP = 0.0);
	ATrapActor* SpawnTrapActor(const FIntPoint& Cell, int32 TrapTypeIndex, const FKakurenboTrapRow& Def);
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

	/** 壁も罠も置けないマス（鬼の出入り口の前） */
	TArray<FIntPoint> ReservedCells;

	TMap<int32, FKakurenboSurfaceRow> WallSurfaces;

	/** マスごとの「家具・部屋の壁がある」 */
	TArray<bool> Obstacles;

	UPROPERTY()
	TArray<FKakurenboBlockColumn> Columns;
};
