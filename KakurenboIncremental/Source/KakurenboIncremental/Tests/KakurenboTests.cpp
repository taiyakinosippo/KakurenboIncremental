// 単体テスト（UE の Automation Test）。Tools/RunUnitTests.ps1 で実行する。
// エディタの Tools → Session Frontend → Automation からも「Kakurenbo」で検索して実行できる。

#include "Engine/DataTable.h"
#include "GridPathfinder.h"
#include "KakurenboBalance.h"
#include "KakurenboLayout.h"
#include "UObject/Package.h"
#include "KakurenboLibrary.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	constexpr EAutomationTestFlags TestFlags = EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKakurenboFormatBigNumberTest, "Kakurenbo.Library.FormatBigNumber", TestFlags)
bool FKakurenboFormatBigNumberTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("0"), UKakurenboLibrary::FormatBigNumber(0.0), TEXT("0"));
	TestEqual(TEXT("999.9"), UKakurenboLibrary::FormatBigNumber(999.9), TEXT("999"));
	TestEqual(TEXT("1000"), UKakurenboLibrary::FormatBigNumber(1000.0), TEXT("1.00K"));
	TestEqual(TEXT("1234567"), UKakurenboLibrary::FormatBigNumber(1234567.0), TEXT("1.23M"));
	TestEqual(TEXT("1e6"), UKakurenboLibrary::FormatBigNumber(1e6), TEXT("1.00M"));
	TestEqual(TEXT("1e18"), UKakurenboLibrary::FormatBigNumber(1e18), TEXT("1.00Qi"));
	TestEqual(TEXT("1e21"), UKakurenboLibrary::FormatBigNumber(1e21), TEXT("1.00e21"));
	TestEqual(TEXT("-2500"), UKakurenboLibrary::FormatBigNumber(-2500.0), TEXT("-2.50K"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKakurenboExpCurveTest, "Kakurenbo.Library.ExpCurve", TestFlags)
bool FKakurenboExpCurveTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("Lv0"), UKakurenboLibrary::ExpCurve(10.0, 2.0, 0), 10.0);
	TestEqual(TEXT("Lv3"), UKakurenboLibrary::ExpCurve(10.0, 2.0, 3), 80.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKakurenboPathOpenTest, "Kakurenbo.Path.OpenGrid", TestFlags)
bool FKakurenboPathOpenTest::RunTest(const FString& Parameters)
{
	FKakurenboPathGrid Grid;
	Grid.Init(10, 10);

	TArray<FIntPoint> Path;
	float Cost = 0.f;
	TestTrue(TEXT("found"), KakurenboPathfinding::FindPath(Grid, FIntPoint(0, 0), FIntPoint(9, 9), Path, &Cost));
	// 斜めに 9 マス
	TestEqual(TEXT("length"), Path.Num(), 9);
	TestEqual(TEXT("goal is last"), Path.Last(), FIntPoint(9, 9));
	TestTrue(TEXT("cost"), FMath::IsNearlyEqual(Cost, 9.f * UE_SQRT_2, 0.01f));

	TestTrue(TEXT("same cell"), KakurenboPathfinding::FindPath(Grid, FIntPoint(3, 3), FIntPoint(3, 3), Path));
	TestEqual(TEXT("same cell empty"), Path.Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKakurenboPathWallTest, "Kakurenbo.Path.BreakOrDetour", TestFlags)
bool FKakurenboPathWallTest::RunTest(const FString& Parameters)
{
	// x=5 の列に縦一列の壁。y=9 だけ空いている
	FKakurenboPathGrid Grid;
	Grid.Init(10, 10);
	for (int32 Y = 0; Y < 9; ++Y)
	{
		Grid.SetExtra(FIntPoint(5, Y), 4.f);
	}

	TArray<FIntPoint> Path;
	// (4,0) → (6,0): 壁を壊せば 2 マス + 4、回り道なら 18 マス以上 → 壊す方が安い
	TestTrue(TEXT("found"), KakurenboPathfinding::FindPath(Grid, FIntPoint(4, 0), FIntPoint(6, 0), Path));
	TestTrue(TEXT("breaks wall"), KakurenboPathfinding::PathContainsWalls(Grid, Path));

	// (4,8) → (6,8): 隙間 (5,9) を通る回り道が安い
	TestTrue(TEXT("found 2"), KakurenboPathfinding::FindPath(Grid, FIntPoint(4, 8), FIntPoint(6, 8), Path));
	TestFalse(TEXT("detours"), KakurenboPathfinding::PathContainsWalls(Grid, Path));

	// 壁が非常に固いなら遠くても回り道する
	for (int32 Y = 0; Y < 9; ++Y)
	{
		Grid.SetExtra(FIntPoint(5, Y), 100.f);
	}
	TestTrue(TEXT("found 3"), KakurenboPathfinding::FindPath(Grid, FIntPoint(4, 0), FIntPoint(6, 0), Path));
	TestFalse(TEXT("detours hard wall"), KakurenboPathfinding::PathContainsWalls(Grid, Path));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKakurenboPathCornerTest, "Kakurenbo.Path.NoCornerCutting", TestFlags)
bool FKakurenboPathCornerTest::RunTest(const FString& Parameters)
{
	// (1,0) と (0,1) が通れない → (0,0) から (1,1) へ斜めにすり抜けてはいけない
	FKakurenboPathGrid Grid;
	Grid.Init(3, 3);
	Grid.SetExtra(FIntPoint(1, 0), -1.f);
	Grid.SetExtra(FIntPoint(0, 1), -1.f);

	TArray<FIntPoint> Path;
	TestFalse(TEXT("blocked"), KakurenboPathfinding::FindPath(Grid, FIntPoint(0, 0), FIntPoint(1, 1), Path));

	// 片方だけ壁（壊せる）でも斜めには通らない
	Grid.SetExtra(FIntPoint(1, 0), 0.f);
	Grid.SetExtra(FIntPoint(0, 1), 3.f);
	TestTrue(TEXT("found"), KakurenboPathfinding::FindPath(Grid, FIntPoint(0, 0), FIntPoint(1, 1), Path));
	TestEqual(TEXT("goes around"), Path.Num(), 2);
	TestEqual(TEXT("via (1,0)"), Path[0], FIntPoint(1, 0));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKakurenboPathWallGoalTest, "Kakurenbo.Path.WallAsGoal", TestFlags)
bool FKakurenboPathWallGoalTest::RunTest(const FString& Parameters)
{
	// ゴールが壁（プレイヤーが積んだブロックの上にいる）でも経路は見つかる
	FKakurenboPathGrid Grid;
	Grid.Init(5, 5);
	Grid.SetExtra(FIntPoint(4, 4), 4.f);

	TArray<FIntPoint> Path;
	TestTrue(TEXT("found"), KakurenboPathfinding::FindPath(Grid, FIntPoint(0, 0), FIntPoint(4, 4), Path));
	TestEqual(TEXT("ends at wall"), Path.Last(), FIntPoint(4, 4));
	// 壁へは斜めに入らないので、最後の 1 歩は縦か横
	const FIntPoint Before = Path.Num() >= 2 ? Path[Path.Num() - 2] : FIntPoint(0, 0);
	TestTrue(TEXT("orthogonal entry"), FMath::Abs(Before.X - 4) + FMath::Abs(Before.Y - 4) == 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKakurenboRepairPlanTest, "Kakurenbo.Layout.RepairPlan", TestFlags)
bool FKakurenboRepairPlanTest::RunTest(const FString& Parameters)
{
	using namespace KakurenboLayout;
	constexpr int32 Wood = 0, Stone = 1;

	// 設計図 [木, 石]、下の木が壊れて石が落ちてきた → 木を作り直して [木, 石] に戻す
	{
		TArray<int32> Stock = { 1, 0 };
		TArray<FRepairEntry> Plan;
		PlanColumnRepair({ Wood, Stone }, { Stone }, Stock, Plan);
		TestEqual(TEXT("plan length"), Plan.Num(), 2);
		TestTrue(TEXT("bottom wood rebuilt"), Plan[0].Step == ERepairStep::Build && Plan[0].Type == Wood);
		TestTrue(TEXT("stone kept"), Plan[1].Step == ERepairStep::Keep && Plan[1].LiveIndex == 0);
		TestEqual(TEXT("wood stock used"), Stock[Wood], 0);
	}
	// 在庫が足りない → 作れない分は Missing（設計図には残る）
	{
		TArray<int32> Stock = { 0, 0 };
		TArray<FRepairEntry> Plan;
		PlanColumnRepair({ Wood, Stone }, { Stone }, Stock, Plan);
		TestTrue(TEXT("wood missing"), Plan[0].Step == ERepairStep::Missing);
		TestTrue(TEXT("stone still kept"), Plan[1].Step == ERepairStep::Keep);
	}
	// 全部壊れた列を、在庫の範囲で下から直す
	{
		TArray<int32> Stock = { 1, 5 };
		TArray<FRepairEntry> Plan;
		PlanColumnRepair({ Wood, Wood, Stone }, {}, Stock, Plan);
		TestTrue(TEXT("first wood built"), Plan[0].Step == ERepairStep::Build);
		TestTrue(TEXT("second wood missing (no stock)"), Plan[1].Step == ERepairStep::Missing);
		TestTrue(TEXT("stone built"), Plan[2].Step == ERepairStep::Build);
		TestEqual(TEXT("stone stock used"), Stock[Stone], 4);
	}
	// 壊れていない列はそのまま
	{
		TArray<int32> Stock = { 3, 3 };
		TArray<FRepairEntry> Plan;
		PlanColumnRepair({ Stone, Wood }, { Stone, Wood }, Stock, Plan);
		TestTrue(TEXT("all kept"), Plan[0].Step == ERepairStep::Keep && Plan[1].Step == ERepairStep::Keep);
		TestEqual(TEXT("no stock used"), Stock[Wood] + Stock[Stone], 6);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKakurenboResolveStageTest, "Kakurenbo.Balance.ResolveStage", TestFlags)
bool FKakurenboResolveStageTest::RunTest(const FString& Parameters)
{
	KakurenboBalance::FStageGrowth Growth; // 既定: +5 秒 / ×4 / ×1.6 / +100 / +15

	TArray<FKakurenboStageRow> Rows;
	Rows.AddDefaulted(2);
	Rows[1].HideDuration = 35.f;
	Rows[1].ClearReward = 400.0;
	Rows[1].OniDamage = 1.6;
	Rows[1].OniHearingRadius = 1300.f;
	Rows[1].OniSpeedBonus = 15.f;
	Rows[1].NumOnis = 3;

	// 表の中はその行
	TestEqual(TEXT("stage 1 from table"), KakurenboBalance::ResolveStage(Rows, 1, Growth).HideDuration, 30.f);
	TestEqual(TEXT("stage 2 from table"), KakurenboBalance::ResolveStage(Rows, 2, Growth).NumOnis, 3);

	// 表より後は最後の行から伸ばす（ステージ 4 = 2 ステージぶん）
	const FKakurenboStageRow S4 = KakurenboBalance::ResolveStage(Rows, 4, Growth);
	TestEqual(TEXT("stage 4 hide duration"), S4.HideDuration, 45.f);
	TestTrue(TEXT("stage 4 reward"), FMath::IsNearlyEqual(S4.ClearReward, 6400.0));
	TestTrue(TEXT("stage 4 damage"), FMath::IsNearlyEqual(S4.OniDamage, 4.096, 0.0001));
	TestEqual(TEXT("stage 4 hearing"), S4.OniHearingRadius, 1500.f);
	TestEqual(TEXT("stage 4 speed bonus"), S4.OniSpeedBonus, 45.f);
	TestEqual(TEXT("stage 4 keeps last row's oni count"), S4.NumOnis, 3);

	// 表が空ならステージ 1 の既定値から伸ばす
	const FKakurenboStageRow S3 = KakurenboBalance::ResolveStage({}, 3, Growth);
	TestEqual(TEXT("empty table stage 3 duration"), S3.HideDuration, 40.f);
	TestTrue(TEXT("empty table stage 3 reward"), FMath::IsNearlyEqual(S3.ClearReward, 1600.0));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKakurenboCsvFilesTest, "Kakurenbo.Balance.CsvFiles", TestFlags)
bool FKakurenboCsvFilesTest::RunTest(const FString& Parameters)
{
	// Data/*.csv が壊れていないか（列名の間違い・カンマの付け忘れなど）
	auto Load = [this](UScriptStruct* RowStruct, const TCHAR* FileName) -> UDataTable*
	{
		TArray<FString> Problems;
		UDataTable* Table = KakurenboBalance::LoadCsvAsDataTable(GetTransientPackage(), RowStruct, KakurenboBalance::GetDataFilePath(FileName), Problems);
		for (const FString& Problem : Problems)
		{
			AddError(FString::Printf(TEXT("%s: %s"), FileName, *Problem));
		}
		TestNotNull(FString::Printf(TEXT("%s loads"), FileName), Table);
		return Table;
	};

	if (UDataTable* Stages = Load(FKakurenboStageRow::StaticStruct(), TEXT("Stages.csv")))
	{
		TArray<FKakurenboStageRow*> Rows;
		Stages->GetAllRows<FKakurenboStageRow>(TEXT("Test"), Rows);
		TestTrue(TEXT("stages has rows"), Rows.Num() >= 1);
		if (Rows.Num() > 0)
		{
			TestTrue(TEXT("stage 1 has onis and time"), Rows[0]->NumOnis >= 1 && Rows[0]->HideDuration > 0.f);
		}
	}
	if (UDataTable* Upgrades = Load(FKakurenboUpgradeRow::StaticStruct(), TEXT("Upgrades.csv")))
	{
		TestNotNull(TEXT("Mash row"), Upgrades->FindRow<FKakurenboUpgradeRow>(TEXT("Mash"), TEXT("Test")));
		TestNotNull(TEXT("Time row"), Upgrades->FindRow<FKakurenboUpgradeRow>(TEXT("Time"), TEXT("Test")));
	}
	if (UDataTable* Walls = Load(FWallTypeDef::StaticStruct(), TEXT("Walls.csv")))
	{
		TArray<FWallTypeDef*> Rows;
		Walls->GetAllRows<FWallTypeDef>(TEXT("Test"), Rows);
		TestTrue(TEXT("walls has rows"), Rows.Num() >= 1);
		if (Rows.Num() > 0)
		{
			TestTrue(TEXT("wall 1 has HP, cost and a color"), Rows[0]->MaxHP > 0.0 && Rows[0]->Cost > 0.0 && Rows[0]->Color.R > 0.f);
			TestFalse(TEXT("wall 1 has a name"), Rows[0]->DisplayName.IsEmpty());
		}
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
