// 単体テスト（UE の Automation Test）。Tools/RunUnitTests.ps1 で実行する。
// エディタの Tools → Session Frontend → Automation からも「Kakurenbo」で検索して実行できる。

#include "GridPathfinder.h"
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

#endif // WITH_DEV_AUTOMATION_TESTS
