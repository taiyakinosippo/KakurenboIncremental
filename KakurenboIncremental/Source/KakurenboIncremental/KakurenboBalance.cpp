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
	return Row;
}

int32 KakurenboBalance::GetPrestigePoints(int32 Stage, const FKakurenboPrestigeRow& Settings)
{
	if (Stage < Settings.MinStage)
	{
		return 0;
	}
	return (Stage - Settings.MinStage + 1) * FMath::Max(1, Settings.PointsPerStage);
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
