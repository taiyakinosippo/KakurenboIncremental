#include "KakurenboBalance.h"

#include "Engine/DataTable.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

FKakurenboStageRow KakurenboBalance::ResolveStage(const TArray<FKakurenboStageRow>& Rows, int32 Stage, const FStageGrowth& Growth)
{
	Stage = FMath::Max(1, Stage);
	if (Rows.IsValidIndex(Stage - 1))
	{
		return Rows[Stage - 1];
	}

	// 表の最後の行（無ければステージ 1 の既定値）から、足りないステージ数だけ伸ばす
	FKakurenboStageRow Row = Rows.Num() > 0 ? Rows.Last() : FKakurenboStageRow();
	const int32 Steps = Stage - FMath::Max(1, Rows.Num());
	Row.HideDuration += Growth.HideDurationPerStage * Steps;
	Row.ClearReward *= FMath::Pow(Growth.ClearRewardGrowth, static_cast<double>(Steps));
	Row.OniDamage *= FMath::Pow(Growth.OniDamageGrowth, static_cast<double>(Steps));
	Row.OniHearingRadius += Growth.OniHearingRadiusPerStage * Steps;
	Row.OniSpeedBonus += Growth.OniSpeedBonusPerStage * Steps;
	if (Row.WallLimit > 0)
	{
		Row.WallLimit += Growth.WallLimitPerStage * Steps;
	}
	// 鬼の数に上限は無い（ユーザーの希望：後半の上限を取り払う）
	if (Growth.ExtraOniEveryStages > 0 && Steps > 0)
	{
		TArray<EOniType> Pool;
		for (const EOniType Type : Row.OniTypes)
		{
			if (Type != EOniType::Detector)
			{
				Pool.Add(Type);
			}
		}
		if (Pool.Num() == 0)
		{
			Pool.Add(EOniType::Balanced);
		}
		const int32 Extra = Steps / Growth.ExtraOniEveryStages;
		for (int32 k = 0; k < Extra; ++k)
		{
			Row.OniTypes.Add(Pool[k % Pool.Num()]);
		}
	}
	return Row;
}

int32 KakurenboBalance::GetPrestigePoints(int32 Stage, const FKakurenboPrestigeRow& Settings)
{
	if (Stage < Settings.MinStage)
	{
		return 0;
	}
	// 指数で増える（深く進んでから転生するほど得。1 回の転生で約 4 ステージ先まで進めるように合わせた）。小数の誤差で 1 少なくならないよう少し足す
	const double Points = FMath::Max(0.0, Settings.PointsBase) * FMath::Pow(FMath::Max(1.0, Settings.PointsGrowth), static_cast<double>(Stage - Settings.MinStage));
	return FMath::Max(1, FMath::FloorToInt(Points + 1e-6));
}

double KakurenboBalance::GetPrestigeStartCoins(int32 PrestigeCount, const FKakurenboPrestigeRow& Settings)
{
	return FMath::Max(0, PrestigeCount) * FMath::Max(0.0, Settings.StartCoinsPerPrestige);
}

double KakurenboBalance::GetPrestigeWallHPMultiplier(int32 PrestigeCount, const FKakurenboPrestigeRow& Settings)
{
	return FMath::Pow(FMath::Max(1.0, Settings.WallHPPerPrestige), static_cast<double>(FMath::Max(0, PrestigeCount)));
}

int32 KakurenboBalance::GetPrestigeUpgradeCost(const FKakurenboPrestigeUpgradeRow& Row, int32 Level)
{
	if (Row.MaxLevel > 0 && Level >= Row.MaxLevel)
	{
		return INDEX_NONE;
	}
	// 小数の誤差で 1 多くならないよう、少しだけ引いてから切り上げる
	const double Cost = Row.BaseCost * FMath::Pow(Row.CostGrowth, static_cast<double>(FMath::Max(0, Level)));
	return FMath::Max(1, FMath::CeilToInt(Cost - 1e-6));
}

double KakurenboBalance::GetPrestigeUpgradeValue(const FKakurenboPrestigeUpgradeRow& Row, int32 Level)
{
	return Row.BaseValue * FMath::Pow(Row.ValueGrowth, static_cast<double>(FMath::Max(0, Level)));
}

FString KakurenboBalance::GetDataFilePath(const FString& FileName)
{
	return FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("Data") / FileName);
}

UDataTable* KakurenboBalance::LoadCsvAsDataTable(UObject* Outer, UScriptStruct* RowStruct, const FString& FilePath, TArray<FString>& OutProblems)
{
	FString Csv;
	if (!FFileHelper::LoadFileToString(Csv, *FilePath))
	{
		OutProblems.Add(FString::Printf(TEXT("ファイルを読めません: %s"), *FilePath));
		return nullptr;
	}

	// 一時的な DataTable を作り、行の型を決めてから CSV を流し込む
	UDataTable* Table = NewObject<UDataTable>(Outer);
	Table->RowStruct = RowStruct;
	OutProblems.Append(Table->CreateTableFromCSVString(Csv));
	return Table->GetRowMap().Num() > 0 ? Table : nullptr;
}
