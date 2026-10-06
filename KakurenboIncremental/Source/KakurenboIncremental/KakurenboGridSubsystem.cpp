#include "KakurenboGridSubsystem.h"

#include "Engine/World.h"
#include "PlaceableBlock.h"

void UKakurenboGridSubsystem::Configure(const FVector& InOrigin, int32 InSizeX, int32 InSizeY, float InCellSize, int32 InMaxStackHeight)
{
	ClearAllBlocks();
	Origin = InOrigin;
	SizeX = FMath::Max(1, InSizeX);
	SizeY = FMath::Max(1, InSizeY);
	CellSize = InCellSize;
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
		Origin.Z + (Level + 0.5f) * CellSize);
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

APlaceableBlock* UKakurenboGridSubsystem::PlaceBlock(const FIntPoint& Cell, int32 WallTypeIndex, double MaxHP, const FLinearColor& Color)
{
	if (!CanPlaceBlock(Cell))
	{
		return nullptr;
	}

	FKakurenboBlockColumn& Column = Columns[ToIndex(Cell)];
	const int32 Level = Column.Blocks.Num();

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	APlaceableBlock* Block = GetWorld()->SpawnActor<APlaceableBlock>(APlaceableBlock::StaticClass(), CellToWorld(Cell, Level), FRotator::ZeroRotator, Params);
	if (!Block)
	{
		return nullptr;
	}
	// 立方体メッシュは 100cm なので、マスの大きさに合わせる
	Block->SetActorScale3D(FVector(CellSize / 100.f));
	Block->InitBlock(WallTypeIndex, MaxHP, Color);
	Block->Cell = Cell;
	Block->Level = Level;
	Column.Blocks.Add(Block);

	MarkChanged();
	return Block;
}

bool UKakurenboGridSubsystem::RemoveBlock(APlaceableBlock* Block)
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
	RemoveAtLevel(Block->Cell, Level);
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
	for (int32 i = Level; i < Blocks.Num(); ++i)
	{
		if (APlaceableBlock* Above = Blocks[i])
		{
			Above->Level = i;
			Above->SetActorLocation(CellToWorld(Cell, i));
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
		}
	}

	if (Destroyed > 0)
	{
		MarkChanged();
	}
	return Destroyed;
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
	}
	MarkChanged();
}

void UKakurenboGridSubsystem::MarkChanged()
{
	++GridVersion;
	OnGridChanged.Broadcast();
}

// ---------------------------------------------------------------- 経路探索

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
			// 範囲攻撃は 1 回でだいたい下 2 段に当たるので、必要な攻撃回数をざっくり見積もる
			double HitsNeeded = 0.0;
			for (const APlaceableBlock* Block : Blocks)
			{
				if (Block)
				{
					HitsNeeded += FMath::CeilToDouble(Block->HP / SafeDamage);
				}
			}
			const float Attacks = FMath::Max(1.f, static_cast<float>(FMath::CeilToDouble(HitsNeeded / 2.0)));
			Grid.SetExtra(FIntPoint(X, Y), Attacks * CostPerAttack);
		}
	}
	return Grid;
}

FIntPoint UKakurenboGridSubsystem::FindFarthestFreeCell(const FVector& From) const
{
	FIntPoint Best(0, 0);
	float BestDistSq = -1.f;
	for (int32 Y = 0; Y < SizeY; ++Y)
	{
		for (int32 X = 0; X < SizeX; ++X)
		{
			const FIntPoint Cell(X, Y);
			if (GetColumnHeight(Cell) > 0)
			{
				continue;
			}
			const float DistSq = FVector::DistSquared2D(CellFloorCenter(Cell), From);
			if (DistSq > BestDistSq)
			{
				BestDistSq = DistSq;
				Best = Cell;
			}
		}
	}
	return Best;
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
