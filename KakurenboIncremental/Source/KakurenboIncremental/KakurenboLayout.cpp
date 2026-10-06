#include "KakurenboLayout.h"

void KakurenboLayout::PlanColumnRepair(const TArray<int32>& Design, const TArray<int32>& LiveTypes, TArray<int32>& InOutStock, TArray<FRepairEntry>& OutPlan)
{
	OutPlan.Reset();

	// 現在の列は設計図から一部が抜けた並びなので、前から順に突き合わせる
	int32 LiveCursor = 0;
	for (const int32 Type : Design)
	{
		FRepairEntry& Entry = OutPlan.AddDefaulted_GetRef();
		Entry.Type = Type;

		if (LiveTypes.IsValidIndex(LiveCursor) && LiveTypes[LiveCursor] == Type)
		{
			Entry.Step = ERepairStep::Keep;
			Entry.LiveIndex = LiveCursor++;
		}
		else if (InOutStock.IsValidIndex(Type) && InOutStock[Type] > 0)
		{
			Entry.Step = ERepairStep::Build;
			--InOutStock[Type];
		}
		else
		{
			Entry.Step = ERepairStep::Missing;
		}
	}

	// 設計図に無いブロックが残っていた場合（通常は起きない）も捨てずに上に積む
	for (; LiveCursor < LiveTypes.Num(); ++LiveCursor)
	{
		FRepairEntry& Entry = OutPlan.AddDefaulted_GetRef();
		Entry.Step = ERepairStep::Keep;
		Entry.Type = LiveTypes[LiveCursor];
		Entry.LiveIndex = LiveCursor;
	}
}
