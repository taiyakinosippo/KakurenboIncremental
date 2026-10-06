#include "KakurenboGridSubsystem.h"

#include "Engine/World.h"
#include "KakurenboLayout.h"
#include "KakurenboSaveGame.h"
#include "PlaceableBlock.h"

void UKakurenboGridSubsystem::Configure(const FVector& InOrigin, int32 InSizeX, int32 InSizeY, float InCellSize, float InBlockHeight, int32 InMaxStackHeight)
{
	ClearAllBlocks();
	Origin = InOrigin;
	SizeX = FMath::Max(1, InSizeX);
	SizeY = FMath::Max(1, InSizeY);
	CellSize = InCellSize;
	BlockHeight = InBlockHeight;
	MaxStackHeight = FMath::Max(1, InMaxStackHeight);
	Columns.SetNum(SizeX * SizeY);
	MarkChanged();
}

// ---------------------------------------------------------------- 座標変換

FIntPoint UKakurenboGridSubsystem::WorldToCell(const FVector& WorldLocation) const
{
	return FIntPoint(
		FMath::FloorToInt((WorldLocation.X - Origin.X) / CellSize),
		FMath::FloorToInt((WorldLocation.Y - Origin.Y) / CellSize));
}

FVector UKakurenboGridSubsystem::CellToWorld(const FIntPoint& Cell, int32 Level) const
{
	return FVector(
		Origin.X + (Cell.X + 0.5f) * CellSize,
		Origin.Y + (Cell.Y + 0.5f) * CellSize,
		Origin.Z + (Level + 0.5f) * BlockHeight);
}

FVector UKakurenboGridSubsystem::CellFloorCenter(const FIntPoint& Cell) const
{
	return FVector(Origin.X + (Cell.X + 0.5f) * CellSize, Origin.Y + (Cell.Y + 0.5f) * CellSize, Origin.Z);
}

bool UKakurenboGridSubsystem::IsInside(const FIntPoint& Cell) const
{
	return Cell.X >= 0 && Cell.Y >= 0 && Cell.X < SizeX && Cell.Y < SizeY;
}

// ---------------------------------------------------------------- ブロック

int32 UKakurenboGridSubsystem::GetColumnHeight(const FIntPoint& Cell) const
{
	return IsInside(Cell) ? Columns[ToIndex(Cell)].Blocks.Num() : 0;
}

APlaceableBlock* UKakurenboGridSubsystem::GetTopBlock(const FIntPoint& Cell) const
{
	if (!IsInside(Cell))
	{
		return nullptr;
	}
	const TArray<TObjectPtr<APlaceableBlock>>& Blocks = Columns[ToIndex(Cell)].Blocks;
	return Blocks.Num() > 0 ? Blocks.Last().Get() : nullptr;
}

int32 UKakurenboGridSubsystem::GetBlockCount() const
{
	int32 Count = 0;
	for (const FKakurenboBlockColumn& Column : Columns)
	{
		Count += Column.Blocks.Num();
	}
	return Count;
}

bool UKakurenboGridSubsystem::CanPlaceBlock(const FIntPoint& Cell, FText* OutReason) const
{
	if (!IsInside(Cell))
	{
		if (OutReason) *OutReason = NSLOCTEXT("Kakurenbo", "PlaceOutside", "範囲外です");
		return false;
	}
	if (GetColumnHeight(Cell) >= MaxStackHeight)
	{
		if (OutReason) *OutReason = NSLOCTEXT("Kakurenbo", "PlaceTooHigh", "これ以上積めません");
		return false;
	}
	return true;
}

APlaceableBlock* UKakurenboGridSubsystem::SpawnBlockActor(const FIntPoint& Cell, int32 Level, int32 WallTypeIndex, double MaxHP, const FLinearColor& Color)
{
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	APlaceableBlock* Block = GetWorld()->SpawnActor<APlaceableBlock>(APlaceableBlock::StaticClass(), CellToWorld(Cell, Level), FRotator::ZeroRotator, Params);
	if (!Block)
	{
		return nullptr;
	}
	// 立方体メッシュは 100cm なので、マスの幅とブロックの高さに合わせて伸ばす
	Block->SetActorScale3D(FVector(CellSize / 100.f, CellSize / 100.f, BlockHeight / 100.f));
	Block->InitBlock(WallTypeIndex, MaxHP, Color);
	Block->Cell = Cell;
	Block->Level = Level;
	return Block;
}

APlaceableBlock* UKakurenboGridSubsystem::PlaceBlock(const FIntPoint& Cell, int32 WallTypeIndex, double MaxHP, const FLinearColor& Color)
{
	if (!CanPlaceBlock(Cell))
	{
		return nullptr;
	}

	FKakurenboBlockColumn& Column = Columns[ToIndex(Cell)];
	APlaceableBlock* Block = SpawnBlockActor(Cell, Column.Blocks.Num(), WallTypeIndex, MaxHP, Color);
	if (!Block)
	{
		return nullptr;
	}
	Column.Blocks.Add(Block);

	// プレイヤーが手で変えた列は、今の状態を新しい設計図にする
	SyncDesignToBlocks(Cell);
	MarkChanged();
	return Block;
}

bool UKakurenboGridSubsystem::PickUpBlock(APlaceableBlock* Block)
{
	if (!Block || !IsInside(Block->Cell))
	{
		return false;
	}
	const int32 Level = Columns[ToIndex(Block->Cell)].Blocks.IndexOfByKey(Block);
	if (Level == INDEX_NONE)
	{
		return false;
	}
	const FIntPoint Cell = Block->Cell;
	RemoveAtLevel(Cell, Level);
	SyncDesignToBlocks(Cell);
	MarkChanged();
	return true;
}

void UKakurenboGridSubsystem::RemoveAtLevel(const FIntPoint& Cell, int32 Level)
{
	TArray<TObjectPtr<APlaceableBlock>>& Blocks = Columns[ToIndex(Cell)].Blocks;
	if (APlaceableBlock* Block = Blocks[Level])
	{
		Block->Destroy();
	}
	Blocks.RemoveAt(Level);

	// 上の段を 1 段ずつ落とす
	RestackColumn(Cell);
}

void UKakurenboGridSubsystem::RestackColumn(const FIntPoint& Cell)
{
	TArray<TObjectPtr<APlaceableBlock>>& Blocks = Columns[ToIndex(Cell)].Blocks;
	for (int32 i = 0; i < Blocks.Num(); ++i)
	{
		if (APlaceableBlock* Block = Blocks[i])
		{
			Block->Level = i;
			Block->SetActorLocation(CellToWorld(Cell, i));
		}
	}
}

void UKakurenboGridSubsystem::SyncDesignToBlocks(const FIntPoint& Cell)
{
	FKakurenboBlockColumn& Column = Columns[ToIndex(Cell)];
	Column.Design.Reset();
	for (const APlaceableBlock* Block : Column.Blocks)
	{
		if (Block)
		{
			Column.Design.Add(Block->WallTypeIndex);
		}
	}
}

int32 UKakurenboGridSubsystem::DamageBlocksInRadius(const FVector& Center, float Radius, double Damage)
{
	int32 Destroyed = 0;
	const float RadiusSq = Radius * Radius;

	// 範囲に入りうるマスだけを調べる
	const FIntPoint MinCell = WorldToCell(Center - FVector(Radius));
	const FIntPoint MaxCell = WorldToCell(Center + FVector(Radius));
	for (int32 Y = FMath::Max(0, MinCell.Y); Y <= FMath::Min(SizeY - 1, MaxCell.Y); ++Y)
	{
		for (int32 X = FMath::Max(0, MinCell.X); X <= FMath::Min(SizeX - 1, MaxCell.X); ++X)
		{
			const FIntPoint Cell(X, Y);
			TArray<TObjectPtr<APlaceableBlock>>& Blocks = Columns[ToIndex(Cell)].Blocks;

			// 当たった段を先に全部調べてから、上から順に消す（消すと上の段が落ちてくるため）
			TArray<int32> BrokenLevels;
			for (int32 Level = 0; Level < Blocks.Num(); ++Level)
			{
				if (FVector::DistSquared(CellToWorld(Cell, Level), Center) <= RadiusSq)
				{
					if (Blocks[Level] && Blocks[Level]->ApplyBlockDamage(Damage))
					{
						BrokenLevels.Add(Level);
					}
				}
			}
			for (int32 i = BrokenLevels.Num() - 1; i >= 0; --i)
			{
				RemoveAtLevel(Cell, BrokenLevels[i]);
				++Destroyed;
			}
			// 設計図はそのまま（あとで修復できるように）
		}
	}

	if (Destroyed > 0)
	{
		MarkChanged();
	}
	return Destroyed;
}

int32 UKakurenboGridSubsystem::DamageBottomBlock(const FIntPoint& Cell, double Damage)
{
	if (!IsInside(Cell))
	{
		return 0;
	}
	TArray<TObjectPtr<APlaceableBlock>>& Blocks = Columns[ToIndex(Cell)].Blocks;
	if (Blocks.Num() == 0 || !Blocks[0] || !Blocks[0]->ApplyBlockDamage(Damage))
	{
		return 0;
	}
	RemoveAtLevel(Cell, 0);
	MarkChanged();
	return 1;
}

void UKakurenboGridSubsystem::ClearAllBlocks()
{
	for (FKakurenboBlockColumn& Column : Columns)
	{
		for (APlaceableBlock* Block : Column.Blocks)
		{
			if (Block)
			{
				Block->Destroy();
			}
		}
		Column.Blocks.Reset();
		Column.Design.Reset();
	}
	MarkChanged();
}

void UKakurenboGridSubsystem::MarkChanged()
{
	++GridVersion;
	OnGridChanged.Broadcast();
}

// ---------------------------------------------------------------- 設計図と修復

int32 UKakurenboGridSubsystem::GetMissingCount(const FIntPoint& Cell) const
{
	if (!IsInside(Cell))
	{
		return 0;
	}
	const FKakurenboBlockColumn& Column = Columns[ToIndex(Cell)];
	return FMath::Max(0, Column.Design.Num() - Column.Blocks.Num());
}

int32 UKakurenboGridSubsystem::GetTotalMissing() const
{
	int32 Total = 0;
	for (const FKakurenboBlockColumn& Column : Columns)
	{
		Total += FMath::Max(0, Column.Design.Num() - Column.Blocks.Num());
	}
	return Total;
}

int32 UKakurenboGridSubsystem::GetDesignHeight(const FIntPoint& Cell) const
{
	return IsInside(Cell) ? Columns[ToIndex(Cell)].Design.Num() : 0;
}

void UKakurenboGridSubsystem::RepairFromDesign(TArray<int32>& InOutStock, const TArray<FWallTypeDef>& WallTypes,
	TFunctionRef<bool(const FIntPoint& Cell, int32 Level)> CanSpawnAt, int32& OutRepaired, int32& OutMissing)
{
	OutRepaired = 0;
	OutMissing = 0;

	for (int32 Y = 0; Y < SizeY; ++Y)
	{
		for (int32 X = 0; X < SizeX; ++X)
		{
			const FIntPoint Cell(X, Y);
			FKakurenboBlockColumn& Column = Columns[ToIndex(Cell)];
			if (Column.Design.Num() <= Column.Blocks.Num())
			{
				continue; // 壊れていない
			}

			// プレイヤーがこの列に立っている（上に乗っている）なら、閉じ込めないよう今回は直さない
			if (!CanSpawnAt(Cell, 0))
			{
				OutMissing += Column.Design.Num() - Column.Blocks.Num();
				continue;
			}

			TArray<int32> LiveTypes;
			for (const APlaceableBlock* Block : Column.Blocks)
			{
				LiveTypes.Add(Block ? Block->WallTypeIndex : INDEX_NONE);
			}

			TArray<KakurenboLayout::FRepairEntry> Plan;
			KakurenboLayout::PlanColumnRepair(Column.Design, LiveTypes, InOutStock, Plan);

			TArray<TObjectPtr<APlaceableBlock>> NewBlocks;
			for (const KakurenboLayout::FRepairEntry& Entry : Plan)
			{
				switch (Entry.Step)
				{
				case KakurenboLayout::ERepairStep::Keep:
					NewBlocks.Add(Column.Blocks[Entry.LiveIndex]);
					break;

				case KakurenboLayout::ERepairStep::Build:
				{
					APlaceableBlock* Block = nullptr;
					if (WallTypes.IsValidIndex(Entry.Type) && NewBlocks.Num() < MaxStackHeight)
					{
						const FWallTypeDef& Def = WallTypes[Entry.Type];
						Block = SpawnBlockActor(Cell, NewBlocks.Num(), Entry.Type, Def.MaxHP, Def.Color);
					}
					if (Block)
					{
						NewBlocks.Add(Block);
						++OutRepaired;
					}
					else
					{
						++InOutStock[Entry.Type]; // 作れなかったので在庫を返す
						++OutMissing;
					}
					break;
				}

				case KakurenboLayout::ERepairStep::Missing:
					++OutMissing;
					break;
				}
			}
			Column.Blocks = MoveTemp(NewBlocks);
			RestackColumn(Cell);
		}
	}

	if (OutRepaired > 0)
	{
		MarkChanged();
	}
}

// ---------------------------------------------------------------- セーブ・ロード

void UKakurenboGridSubsystem::ExportLayout(TArray<FKakurenboSavedColumn>& OutColumns) const
{
	OutColumns.Reset();
	for (int32 Index = 0; Index < Columns.Num(); ++Index)
	{
		const FKakurenboBlockColumn& Column = Columns[Index];
		if (Column.Design.Num() == 0 && Column.Blocks.Num() == 0)
		{
			continue;
		}
		FKakurenboSavedColumn& Saved = OutColumns.AddDefaulted_GetRef();
		Saved.Cell = FIntPoint(Index % SizeX, Index / SizeX);
		Saved.Design = Column.Design;
		for (const APlaceableBlock* Block : Column.Blocks)
		{
			if (Block)
			{
				Saved.Live.Add(Block->WallTypeIndex);
			}
		}
	}
}

void UKakurenboGridSubsystem::ImportLayout(const TArray<FKakurenboSavedColumn>& SavedColumns, const TArray<FWallTypeDef>& WallTypes)
{
	ClearAllBlocks();
	for (const FKakurenboSavedColumn& Saved : SavedColumns)
	{
		if (!IsInside(Saved.Cell))
		{
			continue;
		}
		FKakurenboBlockColumn& Column = Columns[ToIndex(Saved.Cell)];
		Column.Design = Saved.Design;
		for (const int32 Type : Saved.Live)
		{
			if (!WallTypes.IsValidIndex(Type) || Column.Blocks.Num() >= MaxStackHeight)
			{
				continue; // 壁の種類が CSV から消えていたら作らない
			}
			const FWallTypeDef& Def = WallTypes[Type];
			if (APlaceableBlock* Block = SpawnBlockActor(Saved.Cell, Column.Blocks.Num(), Type, Def.MaxHP, Def.Color))
			{
				Column.Blocks.Add(Block);
			}
		}
	}
	MarkChanged();
}

// ---------------------------------------------------------------- 経路探索・出現位置

FKakurenboPathGrid UKakurenboGridSubsystem::BuildPathGrid(double AttackDamage, float CostPerAttack) const
{
	FKakurenboPathGrid Grid;
	Grid.Init(SizeX, SizeY);
	const double SafeDamage = FMath::Max(AttackDamage, 0.0001);

	for (int32 Y = 0; Y < SizeY; ++Y)
	{
		for (int32 X = 0; X < SizeX; ++X)
		{
			const TArray<TObjectPtr<APlaceableBlock>>& Blocks = Columns[Y * SizeX + X].Blocks;
			if (Blocks.Num() == 0)
			{
				continue;
			}
			// 範囲攻撃は一番下の段に当たり、壊すと上の段が落ちてくる。全部壊すのに必要な攻撃回数を数える
			double Attacks = 0.0;
			for (const APlaceableBlock* Block : Blocks)
			{
				if (Block)
				{
					Attacks += FMath::CeilToDouble(Block->HP / SafeDamage);
				}
			}
			Grid.SetExtra(FIntPoint(X, Y), FMath::Max(1.f, static_cast<float>(Attacks)) * CostPerAttack);
		}
	}
	return Grid;
}

FKakurenboPathGrid UKakurenboGridSubsystem::BuildWalkGrid() const
{
	FKakurenboPathGrid Grid;
	Grid.Init(SizeX, SizeY);
	for (int32 Index = 0; Index < Columns.Num(); ++Index)
	{
		if (Columns[Index].Blocks.Num() > 0)
		{
			Grid.ExtraCost[Index] = -1.f; // 壁は通れない
		}
	}
	return Grid;
}

TArray<TArray<FIntPoint>> UKakurenboGridSubsystem::FindEnclosedPockets() const
{
	return KakurenboPathfinding::FindEnclosedPockets(BuildWalkGrid());
}

TArray<bool> UKakurenboGridSubsystem::BuildMainAreaMask() const
{
	TArray<int32> Labels, Sizes;
	KakurenboPathfinding::LabelFreeRegions(BuildWalkGrid(), Labels, Sizes);
	const int32 Largest = KakurenboPathfinding::FindLargestRegion(Sizes);
	TArray<bool> Mask;
	Mask.SetNum(Labels.Num());
	for (int32 i = 0; i < Labels.Num(); ++i)
	{
		Mask[i] = (Labels[i] != INDEX_NONE && Labels[i] == Largest);
	}
	return Mask;
}

TArray<FIntPoint> UKakurenboGridSubsystem::FindSpreadFreeCells(const TArray<FVector>& AvoidPoints, int32 Count) const
{
	const TArray<bool> MainArea = BuildMainAreaMask();
	TArray<FVector> Avoid = AvoidPoints;
	TArray<FIntPoint> Result;
	for (int32 k = 0; k < Count; ++k)
	{
		FIntPoint Best(0, 0);
		float BestScore = -1.f;
		for (int32 Y = 0; Y < SizeY; ++Y)
		{
			for (int32 X = 0; X < SizeX; ++X)
			{
				const FIntPoint Cell(X, Y);
				if (!MainArea[ToIndex(Cell)] || Result.Contains(Cell))
				{
					continue;
				}
				// 避けたい点のうち一番近いものまでの距離が、一番大きいマスを選ぶ
				float MinDistSq = TNumericLimits<float>::Max();
				for (const FVector& P : Avoid)
				{
					MinDistSq = FMath::Min(MinDistSq, static_cast<float>(FVector::DistSquared2D(CellFloorCenter(Cell), P)));
				}
				if (MinDistSq > BestScore)
				{
					BestScore = MinDistSq;
					Best = Cell;
				}
			}
		}
		if (BestScore < 0.f)
		{
			break;
		}
		Result.Add(Best);
		Avoid.Add(CellFloorCenter(Best));
	}
	return Result;
}

TArray<FIntPoint> UKakurenboGridSubsystem::FindRandomFreeCells(int32 Count, const TArray<FVector>& AvoidPoints, float MinDistanceCells, FRandomStream& Stream) const
{
	const float MinDistCm = MinDistanceCells * CellSize;
	const TArray<bool> MainArea = BuildMainAreaMask();

	// 条件を満たす空きマスを集めて、ランダムに並べ替える
	TArray<FIntPoint> Candidates;
	for (int32 Y = 0; Y < SizeY; ++Y)
	{
		for (int32 X = 0; X < SizeX; ++X)
		{
			const FIntPoint Cell(X, Y);
			if (!MainArea[ToIndex(Cell)])
			{
				continue;
			}
			bool bTooClose = false;
			for (const FVector& P : AvoidPoints)
			{
				if (FVector::Dist2D(CellFloorCenter(Cell), P) < MinDistCm)
				{
					bTooClose = true;
					break;
				}
			}
			if (!bTooClose)
			{
				Candidates.Add(Cell);
			}
		}
	}
	for (int32 i = Candidates.Num() - 1; i > 0; --i)
	{
		Candidates.Swap(i, Stream.RandRange(0, i));
	}

	// 選んだマスどうしも離す
	TArray<FIntPoint> Result;
	for (const FIntPoint& Cell : Candidates)
	{
		if (Result.Num() >= Count)
		{
			break;
		}
		bool bTooClose = false;
		for (const FIntPoint& Picked : Result)
		{
			if (FVector::Dist2D(CellFloorCenter(Cell), CellFloorCenter(Picked)) < MinDistCm)
			{
				bTooClose = true;
				break;
			}
		}
		if (!bTooClose)
		{
			Result.Add(Cell);
		}
	}
	return Result;
}

bool UKakurenboGridSubsystem::FindRandomFreeCell(FIntPoint& OutCell, const FRandomStream* Stream) const
{
	for (int32 Try = 0; Try < 64; ++Try)
	{
		const FIntPoint Cell(
			Stream ? Stream->RandRange(0, SizeX - 1) : FMath::RandRange(0, SizeX - 1),
			Stream ? Stream->RandRange(0, SizeY - 1) : FMath::RandRange(0, SizeY - 1));
		if (GetColumnHeight(Cell) == 0)
		{
			OutCell = Cell;
			return true;
		}
	}
	return false;
}
