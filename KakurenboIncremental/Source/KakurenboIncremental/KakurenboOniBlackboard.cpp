#include "KakurenboOniBlackboard.h"

#include "OniCharacter.h"

void UKakurenboOniBlackboard::Reset(int32 InSizeX, int32 InSizeY)
{
	SizeX = InSizeX;
	SizeY = InSizeY;
	CheckedTime.Init(-1.f, SizeX * SizeY);
	ReservedTargets.Reset();
}

void UKakurenboOniBlackboard::MarkChecked(const FIntPoint& Cell, float Now)
{
	if (IsInside(Cell))
	{
		CheckedTime[Cell.Y * SizeX + Cell.X] = Now;
	}
}

bool UKakurenboOniBlackboard::IsChecked(const FIntPoint& Cell, float Now, float MemorySeconds) const
{
	if (!IsInside(Cell))
	{
		return true;
	}
	const float Time = CheckedTime[Cell.Y * SizeX + Cell.X];
	return Time >= 0.f && Now - Time <= MemorySeconds;
}

void UKakurenboOniBlackboard::SetReservedTarget(const AOniCharacter* Oni, const FIntPoint& Cell)
{
	ReservedTargets.Add(Oni, Cell);
}

void UKakurenboOniBlackboard::ClearReservedTarget(const AOniCharacter* Oni)
{
	ReservedTargets.Remove(Oni);
}

bool UKakurenboOniBlackboard::IsNearOthersTarget(const AOniCharacter* Self, const FIntPoint& Cell, int32 RadiusCells) const
{
	for (const TPair<TWeakObjectPtr<const AOniCharacter>, FIntPoint>& Pair : ReservedTargets)
	{
		if (Pair.Key.Get() == Self || !Pair.Key.IsValid())
		{
			continue;
		}
		if (FMath::Abs(Pair.Value.X - Cell.X) <= RadiusCells && FMath::Abs(Pair.Value.Y - Cell.Y) <= RadiusCells)
		{
			return true;
		}
	}
	return false;
}

int32 UKakurenboOniBlackboard::GetCheckedCount(float Now, float MemorySeconds) const
{
	int32 Count = 0;
	for (const float Time : CheckedTime)
	{
		Count += (Time >= 0.f && Now - Time <= MemorySeconds) ? 1 : 0;
	}
	return Count;
}
