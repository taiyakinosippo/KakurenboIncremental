// 単体テスト（UE の Automation Test）。Tools/RunUnitTests.ps1 で実行する。
// エディタの Tools → Session Frontend → Automation からも「Kakurenbo」で検索して実行できる。

#include "Engine/DataTable.h"
#include "GridPathfinder.h"
#include "KakurenboBalance.h"
#include "KakurenboLayout.h"
#include "KakurenboSynth.h"
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

	// 小さな値は小数第 1 位まで出す（強化の 1 → 1.5 が「1 → 1」に見えないように）
	TestEqual(TEXT("stat 1.5"), UKakurenboLibrary::FormatStatNumber(1.5), TEXT("1.5"));
	TestEqual(TEXT("stat 2"), UKakurenboLibrary::FormatStatNumber(2.0), TEXT("2"));
	TestEqual(TEXT("stat 2.56"), UKakurenboLibrary::FormatStatNumber(2.56), TEXT("2.6"));
	TestEqual(TEXT("stat 0.04"), UKakurenboLibrary::FormatStatNumber(0.04), TEXT("0"));
	TestEqual(TEXT("stat 2.25"), UKakurenboLibrary::FormatStatNumber(2.25), TEXT("2.3"));
	TestEqual(TEXT("stat 1234"), UKakurenboLibrary::FormatStatNumber(1234.0), TEXT("1.23K"));
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
	Rows[1].OniTypes = { EOniType::Scout, EOniType::Breaker, EOniType::Careful };

	// 表の中はその行
	TestEqual(TEXT("stage 1 from table"), KakurenboBalance::ResolveStage(Rows, 1, Growth).HideDuration, 30.f);
	TestEqual(TEXT("stage 2 from table"), KakurenboBalance::ResolveStage(Rows, 2, Growth).OniTypes.Num(), 3);

	// 表より後は最後の行から伸ばす（ステージ 4 = 2 ステージぶん）
	const FKakurenboStageRow S4 = KakurenboBalance::ResolveStage(Rows, 4, Growth);
	TestEqual(TEXT("stage 4 hide duration"), S4.HideDuration, 45.f);
	TestTrue(TEXT("stage 4 reward"), FMath::IsNearlyEqual(S4.ClearReward, 6400.0));
	TestTrue(TEXT("stage 4 damage"), FMath::IsNearlyEqual(S4.OniDamage, 4.096, 0.0001));
	TestEqual(TEXT("stage 4 hearing"), S4.OniHearingRadius, 1500.f);
	TestEqual(TEXT("stage 4 speed bonus"), S4.OniSpeedBonus, 45.f);
	TestEqual(TEXT("stage 4 keeps last row's oni types"), S4.OniTypes.Num(), 3);

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
			TestTrue(TEXT("stage 1 has onis and time"), Rows[0]->OniTypes.Num() >= 1 && Rows[0]->HideDuration > 0.f);
		}
	}
	if (UDataTable* Upgrades = Load(FKakurenboUpgradeRow::StaticStruct(), TEXT("Upgrades.csv")))
	{
		TestNotNull(TEXT("Mash row"), Upgrades->FindRow<FKakurenboUpgradeRow>(TEXT("Mash"), TEXT("Test")));
		TestNotNull(TEXT("Time row"), Upgrades->FindRow<FKakurenboUpgradeRow>(TEXT("Time"), TEXT("Test")));
		// 壁の補強はやめた（壁を硬くするのは転生）
		TestNull(TEXT("no Wall reinforcement row"), Upgrades->FindRow<FKakurenboUpgradeRow>(TEXT("Wall"), TEXT("Test"), false));
	}
	if (UDataTable* Prestige = Load(FKakurenboPrestigeRow::StaticStruct(), TEXT("Prestige.csv")))
	{
		const FKakurenboPrestigeRow* Row = Prestige->FindRow<FKakurenboPrestigeRow>(TEXT("Prestige"), TEXT("Test"));
		TestNotNull(TEXT("Prestige row"), Row);
		if (Row)
		{
			TestTrue(TEXT("prestige gives points from some stage"), Row->MinStage >= 2 && Row->PointsPerStage >= 1);
		}
	}
	if (UDataTable* Upgrades = Load(FKakurenboPrestigeUpgradeRow::StaticStruct(), TEXT("PrestigeUpgrades.csv")))
	{
		// すべての永続強化の行があり、行名が EPrestigeUpgrade の名前と一致している
		for (int32 i = 0; i < static_cast<int32>(EPrestigeUpgrade::Count); ++i)
		{
			const FString Name = StaticEnum<EPrestigeUpgrade>()->GetNameStringByValue(i);
			const FKakurenboPrestigeUpgradeRow* Row = Upgrades->FindRow<FKakurenboPrestigeUpgradeRow>(*Name, TEXT("Test"));
			TestNotNull(FString::Printf(TEXT("prestige upgrade row %s"), *Name), Row);
			if (Row)
			{
				TestTrue(FString::Printf(TEXT("%s has a name and a price"), *Name), !Row->DisplayName.IsEmpty() && Row->BaseCost >= 1.0);
			}
		}
		const FKakurenboPrestigeUpgradeRow* Jump = Upgrades->FindRow<FKakurenboPrestigeUpgradeRow>(TEXT("Jump"), TEXT("Test"));
		TestTrue(TEXT("jump is a one-time unlock"), Jump && Jump->MaxLevel == 1);
		const FKakurenboPrestigeUpgradeRow* Cooldown = Upgrades->FindRow<FKakurenboPrestigeUpgradeRow>(TEXT("DashCooldown"), TEXT("Test"));
		TestTrue(TEXT("dash cooldown gets shorter"), Cooldown && Cooldown->ValueGrowth < 1.0 && Cooldown->BaseValue > 0.0);
	}
	if (UDataTable* Traps = Load(FKakurenboTrapRow::StaticStruct(), TEXT("Traps.csv")))
	{
		TArray<FKakurenboTrapRow*> Rows;
		Traps->GetAllRows<FKakurenboTrapRow>(TEXT("Test"), Rows);
		TestTrue(TEXT("traps has rows"), Rows.Num() >= 2);
		bool bHasSticky = false, bHasDecoy = false;
		for (const FKakurenboTrapRow* Row : Rows)
		{
			TestFalse(TEXT("trap has a name"), Row->DisplayName.IsEmpty());
			TestTrue(TEXT("trap has a price that grows"), Row->Cost > 0.0 && Row->CostGrowth >= 1.0);
			TestTrue(TEXT("trap has a trigger radius"), Row->TriggerRadius > 0.f);
			if (Row->Kind == ETrapKind::Sticky)
			{
				bHasSticky = true;
				TestTrue(TEXT("sticky trap stuns"), Row->StunSeconds > 0.f);
			}
			else if (Row->Kind == ETrapKind::Decoy)
			{
				bHasDecoy = true;
				TestTrue(TEXT("decoy makes noise"), Row->NoiseInterval > 0.f && Row->NoiseLoudness > 0.f);
			}
		}
		TestTrue(TEXT("Kind column parsed (Sticky and Decoy present)"), bHasSticky && bHasDecoy);
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
		// 消音壁がある（NoiseDamping 列が読めている）
		const FWallTypeDef* const* Quiet = Rows.FindByPredicate([](const FWallTypeDef* Row) { return Row->NoiseDamping > 0.f; });
		TestTrue(TEXT("a soundproof wall exists"), Quiet && (*Quiet)->NoiseDamping <= 1.f);
		TestTrue(TEXT("shop fits the number keys (2 upgrades + walls + traps <= 9)"), Rows.Num() + 2 + 2 <= 9);
	}
	if (UDataTable* OniTypes = Load(FKakurenboOniTypeRow::StaticStruct(), TEXT("OniTypes.csv")))
	{
		// 4 種類すべての行があり、行名が鬼の種類の名前と一致している
		for (const TCHAR* Name : { TEXT("Balanced"), TEXT("Scout"), TEXT("Breaker"), TEXT("Careful") })
		{
			const FKakurenboOniTypeRow* Row = OniTypes->FindRow<FKakurenboOniTypeRow>(Name, TEXT("Test"));
			TestNotNull(FString::Printf(TEXT("oni type row %s"), Name), Row);
			TestTrue(FString::Printf(TEXT("enum has %s"), Name), StaticEnum<EOniType>()->GetValueByNameString(Name) != INDEX_NONE);
		}
		if (const FKakurenboOniTypeRow* Careful = OniTypes->FindRow<FKakurenboOniTypeRow>(TEXT("Careful"), TEXT("Test")))
		{
			TestTrue(TEXT("careful breaks one wall at a time"), Careful->bSingleTargetAttack);
		}
	}
	if (UDataTable* Stages = Load(FKakurenboStageRow::StaticStruct(), TEXT("Stages.csv")))
	{
		// "(Balanced,Scout)" の書き方で鬼の種類のリストが読めている
		TArray<FKakurenboStageRow*> Rows;
		Stages->GetAllRows<FKakurenboStageRow>(TEXT("Test"), Rows);
		TestTrue(TEXT("stage 1 oni types parsed"), Rows.Num() > 0 && Rows[0]->OniTypes.Num() >= 2);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKakurenboSynthTest, "Kakurenbo.Sound.Synth", TestFlags)
bool FKakurenboSynthTest::RunTest(const FString& Parameters)
{
	// すべての効果音が、ちゃんと聞こえる大きさ・決めた長さで作れるか
	for (int32 i = 0; i < static_cast<int32>(EKakurenboSfx::Count); ++i)
	{
		const EKakurenboSfx Sfx = static_cast<EKakurenboSfx>(i);
		const FString Name = UEnum::GetValueAsString(Sfx);
		const TArray<int16> Samples = KakurenboSynth::RenderSfx(Sfx);
		const float Seconds = static_cast<float>(Samples.Num()) / KakurenboSynth::DefaultSampleRate;
		const float Expected = KakurenboSynth::GetDuration(Sfx);

		int32 Peak = 0;
		for (const int16 S : Samples)
		{
			Peak = FMath::Max(Peak, FMath::Abs(static_cast<int32>(S)));
		}
		TestTrue(FString::Printf(TEXT("%s has a recipe"), *Name), Expected > 0.f && Expected < 2.f);
		TestTrue(FString::Printf(TEXT("%s length %.3f s (recipe %.3f s)"), *Name, Seconds, Expected), Seconds >= Expected && Seconds <= Expected + 0.02f);
		TestTrue(FString::Printf(TEXT("%s is audible (peak %d)"), *Name, Peak), Peak > 3000);
		TestTrue(FString::Printf(TEXT("%s ends silent"), *Name), Samples.Num() > 0 && FMath::Abs(static_cast<int32>(Samples.Last())) < 50);
		// 毎回同じ波形になる（テストや見直しで差が出ないように）
		TestTrue(FString::Printf(TEXT("%s is deterministic"), *Name), KakurenboSynth::RenderSfx(Sfx) == Samples);
	}
	// 高さを変えても長さは変わらない
	TestEqual(TEXT("pitch keeps the length"), KakurenboSynth::RenderSfx(EKakurenboSfx::Mash, 1.5f).Num(), KakurenboSynth::RenderSfx(EKakurenboSfx::Mash).Num());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKakurenboPocketTest, "Kakurenbo.Path.EnclosedPockets", TestFlags)
bool FKakurenboPocketTest::RunTest(const FString& Parameters)
{
	// 角に斜めに壁を並べる: (0,3) (1,2) (2,1) (3,0) → 角の三角形（x+y<3 の 6 マス）が空洞になる
	FKakurenboPathGrid Grid;
	Grid.Init(10, 10);
	for (int32 k = 0; k <= 3; ++k)
	{
		Grid.SetExtra(FIntPoint(k, 3 - k), -1.f);
	}
	TArray<TArray<FIntPoint>> Pockets = KakurenboPathfinding::FindEnclosedPockets(Grid);
	TestEqual(TEXT("one pocket"), Pockets.Num(), 1);
	if (Pockets.Num() == 1)
	{
		TestEqual(TEXT("pocket size"), Pockets[0].Num(), 6);
		TestTrue(TEXT("corner is in the pocket"), Pockets[0].Contains(FIntPoint(0, 0)));
	}

	// 1 マス開けると入り口ができて空洞ではなくなる
	Grid.SetExtra(FIntPoint(1, 2), 0.f);
	TestEqual(TEXT("opening removes the pocket"), KakurenboPathfinding::FindEnclosedPockets(Grid).Num(), 0);

	// 3x3 の輪（中央 1 マス）も空洞
	FKakurenboPathGrid Ring;
	Ring.Init(10, 10);
	for (int32 DY = -1; DY <= 1; ++DY)
	{
		for (int32 DX = -1; DX <= 1; ++DX)
		{
			if (DX != 0 || DY != 0)
			{
				Ring.SetExtra(FIntPoint(5 + DX, 5 + DY), -1.f);
			}
		}
	}
	Pockets = KakurenboPathfinding::FindEnclosedPockets(Ring);
	TestTrue(TEXT("ring center is a pocket"), Pockets.Num() == 1 && Pockets[0].Num() == 1 && Pockets[0][0] == FIntPoint(5, 5));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKakurenboEnclosureDampingTest, "Kakurenbo.Path.EnclosureDamping", TestFlags)
bool FKakurenboEnclosureDampingTest::RunTest(const FString& Parameters)
{
	// 3x3 の輪（中央 (5,5)）。8 個のうち 6 個を消音壁（0.8）、2 個を普通の壁（0）にする
	FKakurenboPathGrid Grid;
	Grid.Init(10, 10);
	TArray<float> Damping;
	Damping.Init(0.f, 100);
	int32 Placed = 0;
	for (int32 DY = -1; DY <= 1; ++DY)
	{
		for (int32 DX = -1; DX <= 1; ++DX)
		{
			if (DX == 0 && DY == 0)
			{
				continue;
			}
			const FIntPoint P(5 + DX, 5 + DY);
			Grid.SetExtra(P, -1.f);
			Damping[Grid.ToIndex(P)] = (Placed++ < 6) ? 0.8f : 0.f;
		}
	}
	int32 Walls = 0;
	const float Inside = KakurenboPathfinding::ComputeEnclosureDamping(Grid, FIntPoint(5, 5), Damping, &Walls);
	TestEqual(TEXT("8 walls surround the center"), Walls, 8);
	TestTrue(FString::Printf(TEXT("damping is the average (%.3f, expected 0.6)"), Inside), FMath::IsNearlyEqual(Inside, 0.6f, 0.001f));

	// 囲まれていない場所・壁の上では効かない
	TestEqual(TEXT("outside the ring: no damping"), KakurenboPathfinding::ComputeEnclosureDamping(Grid, FIntPoint(0, 0), Damping), 0.f);
	TestEqual(TEXT("on a wall: no damping"), KakurenboPathfinding::ComputeEnclosureDamping(Grid, FIntPoint(4, 4), Damping), 0.f);

	// 1 か所開けると囲まれていないので効かない
	Grid.SetExtra(FIntPoint(6, 5), 0.f);
	TestEqual(TEXT("opened ring: no damping"), KakurenboPathfinding::ComputeEnclosureDamping(Grid, FIntPoint(5, 5), Damping), 0.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKakurenboPrestigePointsTest, "Kakurenbo.Balance.PrestigePoints", TestFlags)
bool FKakurenboPrestigePointsTest::RunTest(const FString& Parameters)
{
	FKakurenboPrestigeRow Settings;
	Settings.MinStage = 5;
	Settings.PointsPerStage = 1;
	TestEqual(TEXT("before the min stage: 0"), KakurenboBalance::GetPrestigePoints(4, Settings), 0);
	TestEqual(TEXT("at the min stage: 1"), KakurenboBalance::GetPrestigePoints(5, Settings), 1);
	TestEqual(TEXT("stage 8: 4"), KakurenboBalance::GetPrestigePoints(8, Settings), 4);
	Settings.PointsPerStage = 2;
	TestEqual(TEXT("2 points per stage at stage 6: 4"), KakurenboBalance::GetPrestigePoints(6, Settings), 4);

	// 転生のお店：価格は切り上げ、最大レベルなら買えない、効果は BaseValue × ValueGrowth^Lv
	FKakurenboPrestigeUpgradeRow Row;
	Row.BaseCost = 1.0;
	Row.CostGrowth = 1.5;
	Row.BaseValue = 1.0;
	Row.ValueGrowth = 1.5;
	TestEqual(TEXT("Lv0 -> 1 costs 1"), KakurenboBalance::GetPrestigeUpgradeCost(Row, 0), 1);
	TestEqual(TEXT("Lv1 -> 2 costs ceil(1.5) = 2"), KakurenboBalance::GetPrestigeUpgradeCost(Row, 1), 2);
	TestEqual(TEXT("Lv2 -> 3 costs ceil(2.25) = 3"), KakurenboBalance::GetPrestigeUpgradeCost(Row, 2), 3);
	TestTrue(TEXT("value at Lv2 = 2.25"), FMath::IsNearlyEqual(KakurenboBalance::GetPrestigeUpgradeValue(Row, 2), 2.25));
	Row.MaxLevel = 1;
	TestEqual(TEXT("max level: cannot buy more"), KakurenboBalance::GetPrestigeUpgradeCost(Row, 1), static_cast<int32>(INDEX_NONE));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
