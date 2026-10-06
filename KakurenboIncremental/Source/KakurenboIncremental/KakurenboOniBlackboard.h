// 慎重鬼どうしで共有する「調べた場所」の記録（ワールドに 1 つ）。
// 慎重鬼は近くの調べていないマスから順に回り、調べたマスをここに書き込む。
// 仲間が調べたマスや、仲間が向かっているマスの近くには行かないので、同じ場所を何度も調べない。

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "KakurenboOniBlackboard.generated.h"

class AOniCharacter;

UCLASS()
class KAKURENBOINCREMENTAL_API UKakurenboOniBlackboard : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	/** ラウンドの始めに記録を消す */
	void Reset(int32 InSizeX, int32 InSizeY);

	/** マスを「調べた」と記録する */
	void MarkChecked(const FIntPoint& Cell, float Now);

	/** MemorySeconds 以内に誰かが調べたマスか */
	bool IsChecked(const FIntPoint& Cell, float Now, float MemorySeconds) const;

	/** 鬼がこれから向かうマスを登録する（他の慎重鬼はその近くを避ける） */
	void SetReservedTarget(const AOniCharacter* Oni, const FIntPoint& Cell);
	void ClearReservedTarget(const AOniCharacter* Oni);

	/** 自分以外の鬼が向かっているマスの近く（RadiusCells 以内）か */
	bool IsNearOthersTarget(const AOniCharacter* Self, const FIntPoint& Cell, int32 RadiusCells) const;

	int32 GetCheckedCount(float Now, float MemorySeconds) const;

private:
	bool IsInside(const FIntPoint& Cell) const { return Cell.X >= 0 && Cell.Y >= 0 && Cell.X < SizeX && Cell.Y < SizeY; }

	int32 SizeX = 0;
	int32 SizeY = 0;

	/** マスごとの最後に調べた時刻（まだなら負の値） */
	TArray<float> CheckedTime;

	TMap<TWeakObjectPtr<const AOniCharacter>, FIntPoint> ReservedTargets;
};
