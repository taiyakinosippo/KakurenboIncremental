#include "KakurenboGameMode.h"

#include "Animation/AnimInstance.h"
#include "Components/CapsuleComponent.h"
#include "DrawDebugHelpers.h"
#include "Animation/AnimSequence.h"
#include "Engine/SkeletalMesh.h"
#include "Materials/MaterialInterface.h"
#include "Misc/PackageName.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerStart.h"
#include "HiderCharacter.h"
#include "KakurenboArena.h"
#include "KakurenboBalance.h"
#include "KakurenboFx.h"
#include "KakurenboGameState.h"
#include "KakurenboGridSubsystem.h"
#include "KakurenboHUD.h"
#include "KakurenboLibrary.h"
#include "KakurenboOniBlackboard.h"
#include "KakurenboPlayerController.h"
#include "KakurenboSaveGame.h"
#include "KakurenboSoundSubsystem.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "OniCharacter.h"
#include "PlaceableBlock.h"
#include "TrapActor.h"
#include "TreasureActor.h"

DEFINE_LOG_CATEGORY_STATIC(LogKakurenbo, Log, All);

namespace
{
	/** 演出の色 */
	const FLinearColor NoiseRingColor(1.f, 0.85f, 0.3f);
	const FLinearColor TreasureColor(1.f, 0.78f, 0.1f);

	/** 効果音（画面全体 / その場所から） */
	void Sfx2D(const UObject* Context, EKakurenboSfx Sfx, float Volume = 1.f, float Pitch = 1.f)
	{
		UKakurenboSoundSubsystem::Play2D(Context, Sfx, Volume, Pitch);
	}

	void Sfx3D(const UObject* Context, EKakurenboSfx Sfx, const FVector& Location, float Volume = 1.f, float Pitch = 1.f)
	{
		UKakurenboSoundSubsystem::Play3D(Context, Sfx, Location, Volume, Pitch);
	}

	UKakurenboFxSubsystem* Fx(const UObject* Context)
	{
		return Context->GetWorld()->GetSubsystem<UKakurenboFxSubsystem>();
	}
}

AKakurenboGameMode::AKakurenboGameMode()
{
	PrimaryActorTick.bCanEverTick = true;

	// このゲームで使うクラスを登録する（BP の派生クラスに差し替えてもよい）
	DefaultPawnClass = AHiderCharacter::StaticClass();
	PlayerControllerClass = AKakurenboPlayerController::StaticClass();
	GameStateClass = AKakurenboGameState::StaticClass();
	HUDClass = AKakurenboHUD::StaticClass();
	OniClass = AOniCharacter::StaticClass();
	TreasureClass = ATreasureActor::StaticClass();

	MashUpgradeName = NSLOCTEXT("Kakurenbo", "ShopMash", "連打コイン強化");
	TimeUpgradeName = NSLOCTEXT("Kakurenbo", "ShopTime", "時間コイン強化");

	// 罠の既定値（Data/Traps.csv が読めなかったときに使う）
	auto AddTrap = [this](const TCHAR* Name, ETrapKind Kind, double Cost, double Growth, float Stun, float Interval, float Loudness, float Radius, FLinearColor Color)
	{
		FKakurenboTrapRow& Def = TrapTypes.AddDefaulted_GetRef();
		Def.DisplayName = FText::FromString(Name);
		Def.Kind = Kind;
		Def.Cost = Cost;
		Def.CostGrowth = Growth;
		Def.StunSeconds = Stun;
		Def.NoiseInterval = Interval;
		Def.NoiseLoudness = Loudness;
		Def.TriggerRadius = Radius;
		Def.Color = Color;
	};
	AddTrap(TEXT("トリモチ"), ETrapKind::Sticky, 60.0, 1.3, 4.f, 0.f, 0.f, 45.f, FLinearColor(1.f, 0.85f, 0.15f));
	AddTrap(TEXT("おとり"), ETrapKind::Decoy, 100.0, 1.35, 0.f, 2.5f, 1.2f, 70.f, FLinearColor(0.65f, 0.3f, 1.f));

	// 壁の既定値（Data/Walls.csv が読めなかったときに使う）
	auto AddWall = [this](const TCHAR* Name, double HP, double Cost, FLinearColor Color, float NoiseDamping)
	{
		FWallTypeDef& Def = WallTypes.AddDefaulted_GetRef();
		Def.DisplayName = FText::FromString(Name);
		Def.MaxHP = HP;
		Def.Cost = Cost;
		Def.Color = Color;
		Def.NoiseDamping = NoiseDamping;
	};
	AddWall(TEXT("木の壁"), 1.0, 10.0, FLinearColor(0.55f, 0.35f, 0.18f), 0.f);
	AddWall(TEXT("石の壁"), 4.0, 120.0, FLinearColor(0.22f, 0.22f, 0.25f), 0.f);
	AddWall(TEXT("鉄の壁"), 16.0, 1500.0, FLinearColor(0.25f, 0.35f, 0.55f), 0.f);
	AddWall(TEXT("消音壁"), 3.0, 300.0, FLinearColor(0.85f, 0.8f, 0.65f), 0.8f);

	// 転生のお店の既定値（Data/PrestigeUpgrades.csv が読めなかったときに使う。並びは EPrestigeUpgrade の順）
	auto AddPrestigeUpgrade = [this](const TCHAR* Name, int32 MaxLevel, double Cost, double CostGrowth, double Value, double ValueGrowth)
	{
		FKakurenboPrestigeUpgradeRow& Row = PrestigeUpgrades.AddDefaulted_GetRef();
		Row.DisplayName = FText::FromString(Name);
		Row.MaxLevel = MaxLevel;
		Row.BaseCost = Cost;
		Row.CostGrowth = CostGrowth;
		Row.BaseValue = Value;
		Row.ValueGrowth = ValueGrowth;
	};
	AddPrestigeUpgrade(TEXT("壁の硬さ"), 0, 1.0, 1.5, 1.0, 1.5);      // WallHP: 耐久 ×1.5^Lv
	AddPrestigeUpgrade(TEXT("お宝の強化"), 0, 1.0, 1.5, 1.0, 1.5);    // Treasure: お宝の価値 ×1.5^Lv
	AddPrestigeUpgrade(TEXT("ダッシュの速さ"), 4, 2.0, 1.6, 1.25, 1.12); // DashSpeed: Lv1 で解放（×1.4）
	AddPrestigeUpgrade(TEXT("ダッシュの回復"), 6, 1.0, 1.5, 8.0, 0.85); // DashCooldown: 8 秒 ×0.85^Lv
	AddPrestigeUpgrade(TEXT("ジャンプ"), 1, 2.0, 1.0, 1.0, 1.0);       // Jump: Lv1 で解放
	check(PrestigeUpgrades.Num() == static_cast<int32>(EPrestigeUpgrade::Count));

	// 鬼の種類の既定値（Data/OniTypes.csv が読めなかったときに使う）
	auto AddOniType = [this](EOniType Type, const TCHAR* Name, float Speed, double Damage, float Sight, float Angle, float Hearing,
		bool bSingle, float Radius, float Windup, float Pocket, FLinearColor Color, float Body)
	{
		FKakurenboOniTypeRow& Row = OniTypeRows.Add(Type);
		Row.DisplayName = FText::FromString(Name);
		Row.SpeedScale = Speed;
		Row.DamageScale = Damage;
		Row.SightRadius = Sight;
		Row.SightHalfAngle = Angle;
		Row.HearingScale = Hearing;
		Row.bSingleTargetAttack = bSingle;
		Row.AttackRadius = Radius;
		Row.AttackWindup = Windup;
		Row.PocketInspectChance = Pocket;
		Row.Color = Color;
		Row.BodyScale = Body;
	};
	AddOniType(EOniType::Balanced, TEXT("標準鬼"), 1.0f, 1.0, 900.f, 40.f, 1.f, false, 160.f, 0.8f, 0.5f, FLinearColor(0.9f, 0.1f, 0.08f), 1.0f);
	AddOniType(EOniType::Scout, TEXT("スピード鬼"), 1.25f, 0.5, 1300.f, 55.f, 1.f, false, 160.f, 0.8f, 0.f, FLinearColor(0.1f, 0.75f, 0.95f), 0.9f);
	AddOniType(EOniType::Breaker, TEXT("パワー鬼"), 0.65f, 2.5, 800.f, 70.f, 0.f, false, 200.f, 1.0f, 0.5f, FLinearColor(1.f, 0.5f, 0.05f), 1.2f);
	AddOniType(EOniType::Careful, TEXT("慎重鬼"), 1.15f, 1.0, 900.f, 40.f, 1.f, true, 160.f, 0.6f, 1.f, FLinearColor(0.95f, 0.4f, 0.75f), 1.0f);
}

AKakurenboGameState* AKakurenboGameMode::GS() const
{
	return GetGameState<AKakurenboGameState>();
}

ACharacter* AKakurenboGameMode::GetPlayerCharacter() const
{
	const APlayerController* PC = GetWorld()->GetFirstPlayerController();
	return PC ? Cast<ACharacter>(PC->GetPawn()) : nullptr;
}

void AKakurenboGameMode::BeginPlay()
{
	Super::BeginPlay();

	TreasureRandom.GenerateNewSeed();
	LoadBalanceData();
	LoadOniAppearance();

	// 自動テストなどでセーブを読み書きしたくないときは、起動オプション -KakuNoSave を付ける。
	// -KakuSaveSlot=名前 を付けると、そのスロットを使う（再起動をまたぐテスト用。-KakuNoSave より優先）
	if (FParse::Param(FCommandLine::Get(), TEXT("KakuNoSave")))
	{
		bSaveEnabled = false;
	}
	FString SlotOverride;
	if (FParse::Value(FCommandLine::Get(), TEXT("KakuSaveSlot="), SlotOverride) && !SlotOverride.IsEmpty())
	{
		SaveSlotName = SlotOverride;
		bSaveEnabled = true;
	}

	// レベルに舞台が置かれていなければ自動で作る
	for (TActorIterator<AKakurenboArena> It(GetWorld()); It; ++It)
	{
		Arena = *It;
		break;
	}
	if (!Arena)
	{
		// SpawnActorDeferred: 生成 → プロパティ設定 → FinishSpawning（ここで BeginPlay が走る）
		Arena = GetWorld()->SpawnActorDeferred<AKakurenboArena>(AKakurenboArena::StaticClass(), FTransform::Identity);
		Arena->GridSizeX = GridSizeX;
		Arena->GridSizeY = GridSizeY;
		Arena->CellSize = CellSize;
		Arena->FinishSpawning(FTransform::Identity);
	}

	if (AKakurenboGameState* State = GS())
	{
		State->WallStock.SetNum(WallTypes.Num());
		State->TrapStock.SetNum(TrapTypes.Num());
		State->PrestigeLevels.SetNum(static_cast<int32>(EPrestigeUpgrade::Count));
	}

	// グリッド（ブロック配置・鬼の経路探索）を舞台に合わせて初期化
	if (UKakurenboGridSubsystem* Grid = GetWorld()->GetSubsystem<UKakurenboGridSubsystem>())
	{
		Grid->Configure(Arena->GetGridOrigin(), Arena->GridSizeX, Arena->GridSizeY, Arena->CellSize, BlockHeight, MaxStackHeight);
		Grid->SetReservedCells(Arena->GetOniGateCells()); // 鬼の出入り口の前には何も置けない
		Grid->OnBlockHit.AddUObject(this, &AKakurenboGameMode::HandleBlockHit);
	}

	// セーブがあれば続きから（設置パートで再開）。無ければ「何もない空間で連打」から始める
	if (LoadProgress())
	{
		ShowNotice(FText::Format(NSLOCTEXT("Kakurenbo", "NoticeLoaded", "セーブデータから再開しました（ステージ {0}）"), GS()->Stage));
		StartBuildPhase();
	}
	else
	{
		StartHidePhase();
	}
}

void AKakurenboGameMode::RestartPlayer(AController* NewPlayer)
{
	bool bHasPlayerStart = false;
	for (TActorIterator<APlayerStart> It(GetWorld()); It; ++It)
	{
		bHasPlayerStart = true;
		break;
	}
	if (bHasPlayerStart)
	{
		Super::RestartPlayer(NewPlayer);
		return;
	}
	// 中央のマスの真ん中・床の少し上
	const FVector SpawnLocation(CellSize * 0.5f, CellSize * 0.5f, 120.f);
	RestartPlayerAtTransform(NewPlayer, FTransform(FRotator::ZeroRotator, SpawnLocation));
}

void AKakurenboGameMode::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	AKakurenboGameState* State = GS();
	if (!State || State->Phase != EKakurenboPhase::Hide)
	{
		return;
	}

	// 開始前のカウントダウン。0 になったら鬼が出てくる
	if (State->HideStartCountdown > 0.f)
	{
		State->HideStartCountdown = FMath::Max(0.f, State->HideStartCountdown - DeltaSeconds);
		TickCountdownSounds();
		return;
	}
	if (!bOnisSpawnedThisRound)
	{
		bOnisSpawnedThisRound = true;
		SpawnOnis();
		Sfx2D(this, EKakurenboSfx::RoundStart);
	}

	// 時間収入（毎フレーム、経過時間ぶんだけ加算）
	State->AddCoins(GetTimeIncomePerSecond() * DeltaSeconds, true);

	State->HideTimeRemaining -= DeltaSeconds;
	TickCountdownSounds();
	if (State->HideTimeRemaining <= 0.f)
	{
		State->HideTimeRemaining = 0.f;
		EndHidePhase(true);
	}
}

void AKakurenboGameMode::TickCountdownSounds()
{
	const AKakurenboGameState* State = GS();

	// 開始前: 3, 2, 1 で 1 回ずつ
	if (State->HideStartCountdown > 0.f)
	{
		const int32 Second = FMath::CeilToInt(State->HideStartCountdown);
		if (Second != LastCountdownSecond)
		{
			LastCountdownSecond = Second;
			Sfx2D(this, EKakurenboSfx::CountdownBeep);
		}
		return;
	}

	// 残り 5 秒から 1 秒ごとに
	const int32 Second = FMath::CeilToInt(State->HideTimeRemaining);
	if (Second <= 5 && Second >= 1 && Second != LastTimeTickSecond)
	{
		LastTimeTickSecond = Second;
		Sfx2D(this, EKakurenboSfx::TimeTick);
	}
}

// ---------------------------------------------------------------- バランスデータ

void AKakurenboGameMode::LoadBalanceData()
{
	// アセットが設定されていればそれを、無ければ Data/ の CSV から一時的な DataTable を作って使う
	auto GetTable = [this](UDataTable* Asset, UScriptStruct* RowStruct, const TCHAR* FileName) -> UDataTable*
	{
		if (Asset)
		{
			return Asset;
		}
		TArray<FString> Problems;
		UDataTable* Table = KakurenboBalance::LoadCsvAsDataTable(this, RowStruct, KakurenboBalance::GetDataFilePath(FileName), Problems);
		for (const FString& Problem : Problems)
		{
			UE_LOG(LogKakurenbo, Warning, TEXT("%s: %s"), FileName, *Problem);
		}
		if (Table)
		{
			LoadedTables.Add(Table);
		}
		else
		{
			UE_LOG(LogKakurenbo, Warning, TEXT("%s を読めなかったので既定値を使います"), FileName);
		}
		return Table;
	};

	if (UDataTable* Table = GetTable(StageTable, FKakurenboStageRow::StaticStruct(), TEXT("Stages.csv")))
	{
		// 行の順番＝ステージ番号
		TArray<FKakurenboStageRow*> Rows;
		Table->GetAllRows<FKakurenboStageRow>(TEXT("Stages"), Rows);
		StageRows.Reset();
		for (const FKakurenboStageRow* Row : Rows)
		{
			StageRows.Add(*Row);
		}
	}

	if (UDataTable* Table = GetTable(UpgradeTable, FKakurenboUpgradeRow::StaticStruct(), TEXT("Upgrades.csv")))
	{
		if (const FKakurenboUpgradeRow* Mash = Table->FindRow<FKakurenboUpgradeRow>(TEXT("Mash"), TEXT("Upgrades")))
		{
			MashUpgradeName = Mash->DisplayName;
			MashUpgradeBaseCost = Mash->BaseCost;
			MashUpgradeCostGrowth = Mash->CostGrowth;
			MashIncomeBase = Mash->BaseValue;
			MashIncomeGrowth = Mash->ValueGrowth;
		}
		if (const FKakurenboUpgradeRow* Time = Table->FindRow<FKakurenboUpgradeRow>(TEXT("Time"), TEXT("Upgrades")))
		{
			TimeUpgradeName = Time->DisplayName;
			TimeUpgradeBaseCost = Time->BaseCost;
			TimeUpgradeCostGrowth = Time->CostGrowth;
			TimeIncomeBase = Time->BaseValue;
			TimeIncomeGrowth = Time->ValueGrowth;
		}
	}

	if (UDataTable* Table = GetTable(PrestigeTable, FKakurenboPrestigeRow::StaticStruct(), TEXT("Prestige.csv")))
	{
		if (const FKakurenboPrestigeRow* Row = Table->FindRow<FKakurenboPrestigeRow>(TEXT("Prestige"), TEXT("Prestige")))
		{
			PrestigeSettings = *Row;
		}
	}

	if (UDataTable* Table = GetTable(PrestigeUpgradeTable, FKakurenboPrestigeUpgradeRow::StaticStruct(), TEXT("PrestigeUpgrades.csv")))
	{
		// 行名（WallHP など）を強化の種類に対応させる
		const UEnum* UpgradeEnum = StaticEnum<EPrestigeUpgrade>();
		for (const TPair<FName, uint8*>& Pair : Table->GetRowMap())
		{
			const int64 Value = UpgradeEnum->GetValueByNameString(Pair.Key.ToString());
			if (Value == INDEX_NONE || Value >= static_cast<int64>(EPrestigeUpgrade::Count))
			{
				UE_LOG(LogKakurenbo, Warning, TEXT("PrestigeUpgrades.csv: unknown upgrade '%s'"), *Pair.Key.ToString());
				continue;
			}
			PrestigeUpgrades[Value] = *reinterpret_cast<const FKakurenboPrestigeUpgradeRow*>(Pair.Value);
		}
	}

	if (UDataTable* Table = GetTable(WallTable, FWallTypeDef::StaticStruct(), TEXT("Walls.csv")))
	{
		TArray<FWallTypeDef*> Rows;
		Table->GetAllRows<FWallTypeDef>(TEXT("Walls"), Rows);
		if (Rows.Num() > 0)
		{
			WallTypes.Reset();
			for (const FWallTypeDef* Row : Rows)
			{
				WallTypes.Add(*Row);
			}
		}
	}

	if (UDataTable* Table = GetTable(OniTypeTable, FKakurenboOniTypeRow::StaticStruct(), TEXT("OniTypes.csv")))
	{
		// 行名（Balanced など）を鬼の種類に対応させる
		const UEnum* TypeEnum = StaticEnum<EOniType>();
		for (const TPair<FName, uint8*>& Pair : Table->GetRowMap())
		{
			const int64 Value = TypeEnum->GetValueByNameString(Pair.Key.ToString());
			if (Value == INDEX_NONE)
			{
				UE_LOG(LogKakurenbo, Warning, TEXT("OniTypes.csv: unknown oni type '%s'"), *Pair.Key.ToString());
				continue;
			}
			OniTypeRows.Add(static_cast<EOniType>(Value), *reinterpret_cast<const FKakurenboOniTypeRow*>(Pair.Value));
		}
	}

	if (UDataTable* Table = GetTable(TrapTable, FKakurenboTrapRow::StaticStruct(), TEXT("Traps.csv")))
	{
		TArray<FKakurenboTrapRow*> Rows;
		Table->GetAllRows<FKakurenboTrapRow>(TEXT("Traps"), Rows);
		if (Rows.Num() > 0)
		{
			TrapTypes.Reset();
			for (const FKakurenboTrapRow* Row : Rows)
			{
				TrapTypes.Add(*Row);
			}
		}
	}

	UE_LOG(LogKakurenbo, Log, TEXT("Balance data: %d stages, %d wall types, %d oni types, %d trap types, mash %.2f x%.2f (cost %.1f x%.2f)"),
		StageRows.Num(), WallTypes.Num(), OniTypeRows.Num(), TrapTypes.Num(), MashIncomeBase, MashIncomeGrowth, MashUpgradeBaseCost, MashUpgradeCostGrowth);
}

FKakurenboStageRow AKakurenboGameMode::GetStageSettingsFor(int32 Stage) const
{
	KakurenboBalance::FStageGrowth Growth;
	Growth.HideDurationPerStage = StageHideDurationGrowth;
	Growth.ClearRewardGrowth = StageClearRewardGrowth;
	Growth.OniDamageGrowth = StageOniDamageGrowth;
	Growth.OniHearingRadiusPerStage = StageOniHearingGrowth;
	Growth.OniSpeedBonusPerStage = StageOniSpeedGrowth;
	return KakurenboBalance::ResolveStage(StageRows, Stage, Growth);
}

FKakurenboStageRow AKakurenboGameMode::GetStageSettings() const
{
	const AKakurenboGameState* State = GS();
	return GetStageSettingsFor(State ? State->Stage : 1);
}

// ---------------------------------------------------------------- 計算

double AKakurenboGameMode::GetMashIncome() const
{
	const AKakurenboGameState* State = GS();
	return UKakurenboLibrary::ExpCurve(MashIncomeBase, MashIncomeGrowth, State ? State->MashIncomeLevel : 0);
}

double AKakurenboGameMode::GetTimeIncomePerSecond() const
{
	const AKakurenboGameState* State = GS();
	return UKakurenboLibrary::ExpCurve(TimeIncomeBase, TimeIncomeGrowth, State ? State->TimeIncomeLevel : 0);
}

double AKakurenboGameMode::GetMashUpgradeCost() const
{
	const AKakurenboGameState* State = GS();
	return UKakurenboLibrary::ExpCurve(MashUpgradeBaseCost, MashUpgradeCostGrowth, State ? State->MashIncomeLevel : 0);
}

double AKakurenboGameMode::GetTimeUpgradeCost() const
{
	const AKakurenboGameState* State = GS();
	return UKakurenboLibrary::ExpCurve(TimeUpgradeBaseCost, TimeUpgradeCostGrowth, State ? State->TimeIncomeLevel : 0);
}

double AKakurenboGameMode::GetWallHPMultiplier() const
{
	return GetPrestigeValue(EPrestigeUpgrade::WallHP);
}

int32 AKakurenboGameMode::GetPrestigeLevel(EPrestigeUpgrade Upgrade) const
{
	const AKakurenboGameState* State = GS();
	const int32 Index = static_cast<int32>(Upgrade);
	return (State && State->PrestigeLevels.IsValidIndex(Index)) ? State->PrestigeLevels[Index] : 0;
}

double AKakurenboGameMode::GetPrestigeValue(EPrestigeUpgrade Upgrade, int32 Level) const
{
	const int32 Index = static_cast<int32>(Upgrade);
	return PrestigeUpgrades.IsValidIndex(Index) ? KakurenboBalance::GetPrestigeUpgradeValue(PrestigeUpgrades[Index], Level) : 1.0;
}

int32 AKakurenboGameMode::GetPrestigeUpgradeCost(EPrestigeUpgrade Upgrade) const
{
	const int32 Index = static_cast<int32>(Upgrade);
	return PrestigeUpgrades.IsValidIndex(Index) ? KakurenboBalance::GetPrestigeUpgradeCost(PrestigeUpgrades[Index], GetPrestigeLevel(Upgrade)) : INDEX_NONE;
}

TArray<FShopItemView> AKakurenboGameMode::GetPrestigeShopItems() const
{
	TArray<FShopItemView> Items;
	auto Fmt = [](double V) { return FText::FromString(UKakurenboLibrary::FormatStatNumber(V)); };
	for (int32 i = 0; i < static_cast<int32>(EPrestigeUpgrade::Count) && PrestigeUpgrades.IsValidIndex(i); ++i)
	{
		const EPrestigeUpgrade Upgrade = static_cast<EPrestigeUpgrade>(i);
		const FKakurenboPrestigeUpgradeRow& Row = PrestigeUpgrades[i];
		const int32 Level = GetPrestigeLevel(Upgrade);
		const int32 Cost = GetPrestigeUpgradeCost(Upgrade);
		const double Now = GetPrestigeValue(Upgrade, Level);
		const double Next = GetPrestigeValue(Upgrade, Level + 1);

		FShopItemView& Item = Items.AddDefaulted_GetRef();
		Item.bPrestigeItem = true;
		Item.bMaxed = Cost == INDEX_NONE;
		Item.DisplayName = Row.DisplayName;
		Item.Cost = Item.bMaxed ? 0.0 : Cost;
		Item.OwnedText = Item.bMaxed ? FText::Format(NSLOCTEXT("Kakurenbo", "PrestigeLevelMax", "Lv.{0}（最大）"), Level)
			: FText::Format(NSLOCTEXT("Kakurenbo", "ShopLevel", "Lv.{0}"), Level);

		switch (Upgrade)
		{
		case EPrestigeUpgrade::WallHP:
			Item.Description = FText::Format(NSLOCTEXT("Kakurenbo", "PrestigeWallDesc", "すべての壁の耐久 ×{0} → ×{1}（置いてある壁も）"), Fmt(Now), Fmt(Next));
			break;
		case EPrestigeUpgrade::Treasure:
			Item.Description = FText::Format(NSLOCTEXT("Kakurenbo", "PrestigeTreasureDesc", "お宝の価値 ×{0} → ×{1}"), Fmt(Now), Fmt(Next));
			break;
		case EPrestigeUpgrade::DashSpeed:
			Item.Description = Item.bMaxed ? FText::Format(NSLOCTEXT("Kakurenbo", "PrestigeDashMax", "ダッシュの速さ ×{0}"), Fmt(Now))
				: Level == 0 ? FText::Format(NSLOCTEXT("Kakurenbo", "PrestigeDashUnlock", "Shift でダッシュできるようになる（速さ ×{0}・大きな音が出る）"), Fmt(Next))
				: FText::Format(NSLOCTEXT("Kakurenbo", "PrestigeDashDesc", "ダッシュの速さ ×{0} → ×{1}"), Fmt(Now), Fmt(Next));
			break;
		case EPrestigeUpgrade::DashCooldown:
			Item.Description = Item.bMaxed ? FText::Format(NSLOCTEXT("Kakurenbo", "PrestigeCooldownMax", "ダッシュのクールタイム {0} 秒"), Fmt(Now))
				: FText::Format(NSLOCTEXT("Kakurenbo", "PrestigeCooldownDesc", "ダッシュのクールタイム {0} 秒 → {1} 秒"), Fmt(Now), Fmt(Next));
			if (!IsDashUnlocked())
			{
				Item.Description = FText::Format(NSLOCTEXT("Kakurenbo", "PrestigeNeedDash", "{0}（ダッシュの速さを買うと使える）"), Item.Description);
			}
			break;
		case EPrestigeUpgrade::Jump:
			Item.Description = NSLOCTEXT("Kakurenbo", "PrestigeJumpDesc", "Space でジャンプできるようになる（壁 1 段に飛び乗れる）");
			break;
		default:
			break;
		}
	}
	return Items;
}

bool AKakurenboGameMode::TryBuyPrestigeUpgrade(int32 Index)
{
	AKakurenboGameState* State = GS();
	if (!State || State->Phase != EKakurenboPhase::Shop || Index < 0 || Index >= static_cast<int32>(EPrestigeUpgrade::Count))
	{
		return false;
	}
	const EPrestigeUpgrade Upgrade = static_cast<EPrestigeUpgrade>(Index);
	const int32 Cost = GetPrestigeUpgradeCost(Upgrade);
	bool bBought = false;
	if (Cost != INDEX_NONE && State->PrestigePoints >= Cost)
	{
		const double OldWallMultiplier = GetWallHPMultiplier();
		State->PrestigePoints -= Cost;
		State->PrestigeLevels.SetNum(static_cast<int32>(EPrestigeUpgrade::Count));
		State->PrestigeLevels[Index]++;
		bBought = true;

		if (Upgrade == EPrestigeUpgrade::WallHP)
		{
			// 置いてある壁もすぐに硬くする
			if (UKakurenboGridSubsystem* Grid = GetWorld()->GetSubsystem<UKakurenboGridSubsystem>())
			{
				Grid->ScaleAllBlockHP(GetWallHPMultiplier() / FMath::Max(OldWallMultiplier, 0.0001));
			}
		}
		ApplyPrestigeToPlayer();
		UE_LOG(LogKakurenbo, Log, TEXT("Prestige upgrade %s -> Lv%d (%d pt left)"), *UEnum::GetValueAsString(Upgrade), State->PrestigeLevels[Index], State->PrestigePoints);
	}
	Sfx2D(this, bBought ? EKakurenboSfx::Buy : EKakurenboSfx::BuyFail, 1.f, bBought ? 0.8f : 1.f);
	return bBought;
}

void AKakurenboGameMode::ApplyPrestigeToPlayer()
{
	AHiderCharacter* Hider = Cast<AHiderCharacter>(GetPlayerCharacter());
	if (!Hider)
	{
		return;
	}
	// ダッシュ・ジャンプは最初は使えない。転生のお店で解放する
	Hider->bDashUnlocked = IsDashUnlocked();
	Hider->DashSpeedMultiplier = static_cast<float>(GetPrestigeValue(EPrestigeUpgrade::DashSpeed));
	Hider->DashCooldown = static_cast<float>(GetPrestigeValue(EPrestigeUpgrade::DashCooldown));
	Hider->JumpMaxCount = IsJumpUnlocked() ? 1 : 0; // JumpMaxCount: 空中も含めて続けて跳べる回数（0 なら跳べない）
}

TArray<FWallTypeDef> AKakurenboGameMode::GetEffectiveWallTypes() const
{
	TArray<FWallTypeDef> Types = WallTypes;
	const double Multiplier = GetWallHPMultiplier();
	for (FWallTypeDef& Def : Types)
	{
		Def.MaxHP *= Multiplier;
	}
	return Types;
}

int32 AKakurenboGameMode::GetOwnedTrapCount(int32 TrapTypeIndex) const
{
	const AKakurenboGameState* State = GS();
	const UKakurenboGridSubsystem* Grid = GetWorld()->GetSubsystem<UKakurenboGridSubsystem>();
	const int32 Stock = (State && State->TrapStock.IsValidIndex(TrapTypeIndex)) ? State->TrapStock[TrapTypeIndex] : 0;
	return Stock + (Grid ? Grid->GetLiveTrapCount(TrapTypeIndex) : 0);
}

double AKakurenboGameMode::GetTrapCost(int32 TrapTypeIndex) const
{
	if (!TrapTypes.IsValidIndex(TrapTypeIndex))
	{
		return 0.0;
	}
	const FKakurenboTrapRow& Def = TrapTypes[TrapTypeIndex];
	return UKakurenboLibrary::ExpCurve(Def.Cost, Def.CostGrowth, GetOwnedTrapCount(TrapTypeIndex));
}

double AKakurenboGameMode::GetClearReward() const
{
	return GetStageSettings().ClearReward;
}

double AKakurenboGameMode::GetTreasureValue() const
{
	return GetClearReward() * TreasureRewardRatio * GetPrestigeValue(EPrestigeUpgrade::Treasure);
}

float AKakurenboGameMode::GetHideDuration() const
{
	return GetStageSettings().HideDuration;
}

double AKakurenboGameMode::GetOniAttackDamage() const
{
	return GetStageSettings().OniDamage;
}

float AKakurenboGameMode::GetOniHearingRadius() const
{
	return GetStageSettings().OniHearingRadius;
}

int32 AKakurenboGameMode::GetNumOnis() const
{
	return GetStageSettings().OniTypes.Num();
}

FKakurenboOniTypeRow AKakurenboGameMode::GetOniTypeRow(EOniType Type) const
{
	if (const FKakurenboOniTypeRow* Row = OniTypeRows.Find(Type))
	{
		return *Row;
	}
	return FKakurenboOniTypeRow();
}

int32 AKakurenboGameMode::GetNumTreasures() const
{
	return FMath::Max(0, GetStageSettings().NumTreasures);
}

EKakurenboPhase AKakurenboGameMode::GetPhase() const
{
	const AKakurenboGameState* State = GS();
	return State ? State->Phase : EKakurenboPhase::Shop;
}

// ---------------------------------------------------------------- 購入

// 商品の並び: [0] 連打強化, [1] 時間収入強化, [2〜] 壁, その後に罠
namespace KakurenboShop
{
	constexpr int32 MashUpgrade = 0;
	constexpr int32 TimeUpgrade = 1;
	constexpr int32 NumUpgrades = 2;
}

int32 AKakurenboGameMode::GetShopIndexOfWall(int32 WallTypeIndex) const
{
	return KakurenboShop::NumUpgrades + WallTypeIndex;
}

int32 AKakurenboGameMode::GetShopIndexOfTrap(int32 TrapTypeIndex) const
{
	return KakurenboShop::NumUpgrades + WallTypes.Num() + TrapTypeIndex;
}

TArray<FShopItemView> AKakurenboGameMode::GetShopItems() const
{
	TArray<FShopItemView> Items;
	const AKakurenboGameState* State = GS();
	if (!State)
	{
		return Items;
	}

	// 収入・耐久は小数に意味があるので、小さな値は小数第 1 位まで出す（1 → 1.5 が「1 → 1」に見えないように）
	auto Fmt = [](double V) { return FText::FromString(UKakurenboLibrary::FormatStatNumber(V)); };
	// 今のステージの鬼の攻撃何回で壊れるか
	auto Hits = [this](double HP) { return FMath::Max(1, FMath::CeilToInt(HP / FMath::Max(GetOniAttackDamage(), 0.0001))); };

	{
		FShopItemView& Item = Items.AddDefaulted_GetRef();
		Item.DisplayName = MashUpgradeName;
		Item.Cost = GetMashUpgradeCost();
		Item.Description = FText::Format(NSLOCTEXT("Kakurenbo", "ShopMashDesc", "1回 {0} → {1} コイン"),
			Fmt(GetMashIncome()), Fmt(UKakurenboLibrary::ExpCurve(MashIncomeBase, MashIncomeGrowth, State->MashIncomeLevel + 1)));
		Item.OwnedText = FText::Format(NSLOCTEXT("Kakurenbo", "ShopLevel", "Lv.{0}"), State->MashIncomeLevel);
	}
	{
		FShopItemView& Item = Items.AddDefaulted_GetRef();
		Item.DisplayName = TimeUpgradeName;
		Item.Cost = GetTimeUpgradeCost();
		Item.Description = FText::Format(NSLOCTEXT("Kakurenbo", "ShopTimeDesc", "毎秒 {0} → {1} コイン"),
			Fmt(GetTimeIncomePerSecond()), Fmt(UKakurenboLibrary::ExpCurve(TimeIncomeBase, TimeIncomeGrowth, State->TimeIncomeLevel + 1)));
		Item.OwnedText = FText::Format(NSLOCTEXT("Kakurenbo", "ShopLevel", "Lv.{0}"), State->TimeIncomeLevel);
	}
	const UKakurenboGridSubsystem* Grid = GetWorld()->GetSubsystem<UKakurenboGridSubsystem>();
	// 壊れた（使った）数と、在庫を足しても直しきれない数を書く
	auto SetRepairText = [](FShopItemView& Item, int32 Missing, int32 Stock, const FText& Verb)
	{
		if (Missing <= 0)
		{
			return;
		}
		const int32 Short = FMath::Max(0, Missing - Stock);
		Item.bNeedsMoreForRepair = Short > 0;
		Item.RepairText = Short > 0
			? FText::Format(NSLOCTEXT("Kakurenbo", "ShopRepairShort", "{0} {1} 個（直すにはあと {2} 個）"), Verb, Missing, Short)
			: FText::Format(NSLOCTEXT("Kakurenbo", "ShopRepairOk", "{0} {1} 個（在庫で直せる）"), Verb, Missing);
	};

	const TArray<FWallTypeDef> Walls = GetEffectiveWallTypes();
	for (int32 i = 0; i < Walls.Num(); ++i)
	{
		const FWallTypeDef& Def = Walls[i];
		FShopItemView& Item = Items.AddDefaulted_GetRef();
		Item.DisplayName = Def.DisplayName;
		Item.Cost = Def.Cost;
		Item.Description = FText::Format(NSLOCTEXT("Kakurenbo", "ShopWallDesc", "耐久 {0}（今の鬼の攻撃 {1} 回で壊れる）"),
			Fmt(Def.MaxHP), Hits(Def.MaxHP));
		if (Def.NoiseDamping > 0.f)
		{
			Item.Description = FText::Format(NSLOCTEXT("Kakurenbo", "ShopQuietWallDesc", "{0}　囲まれた中にいると音が最大 {1}% 小さくなる"),
				Item.Description, FMath::RoundToInt(Def.NoiseDamping * 100.f));
		}
		const int32 Stock = State->WallStock.IsValidIndex(i) ? State->WallStock[i] : 0;
		Item.OwnedText = FText::Format(NSLOCTEXT("Kakurenbo", "ShopStock", "在庫 {0}"), Stock);
		SetRepairText(Item, Grid ? Grid->GetMissingWallCount(i) : 0, Stock, NSLOCTEXT("Kakurenbo", "ShopBroken", "壊れた"));
	}
	for (int32 i = 0; i < TrapTypes.Num(); ++i)
	{
		const FKakurenboTrapRow& Def = TrapTypes[i];
		FShopItemView& Item = Items.AddDefaulted_GetRef();
		Item.DisplayName = Def.DisplayName;
		Item.Cost = GetTrapCost(i);
		Item.Description = Def.Kind == ETrapKind::Sticky
			? FText::Format(NSLOCTEXT("Kakurenbo", "ShopStickyDesc", "踏んだ鬼が {0} 秒動けない（その間は捕まらない）。1 回で消える"), FText::AsNumber(Def.StunSeconds))
			: FText::Format(NSLOCTEXT("Kakurenbo", "ShopDecoyDesc", "{0} 秒ごとに音を出して鬼を呼ぶ。鬼が触れると壊れる"), FText::AsNumber(Def.NoiseInterval));
		const int32 Stock = State->TrapStock.IsValidIndex(i) ? State->TrapStock[i] : 0;
		Item.OwnedText = FText::Format(NSLOCTEXT("Kakurenbo", "ShopTrapStock", "在庫 {0}（置いてある {1}）"), Stock, GetOwnedTrapCount(i) - Stock);
		SetRepairText(Item, Grid ? Grid->GetMissingTrapCount(i) : 0, Stock, NSLOCTEXT("Kakurenbo", "ShopUsed", "使った"));
	}
	return Items;
}

bool AKakurenboGameMode::TryBuyShopItem(int32 Index)
{
	bool bBought = false;
	if (Index == KakurenboShop::MashUpgrade)
	{
		bBought = TryBuyMashUpgrade();
	}
	else if (Index == KakurenboShop::TimeUpgrade)
	{
		bBought = TryBuyTimeUpgrade();
	}
	else if (Index - KakurenboShop::NumUpgrades < WallTypes.Num())
	{
		bBought = TryBuyWall(Index - KakurenboShop::NumUpgrades);
	}
	else
	{
		bBought = TryBuyTrap(Index - KakurenboShop::NumUpgrades - WallTypes.Num());
	}

	if (GetPhase() == EKakurenboPhase::Shop && Index >= 0 && Index < GetShopItems().Num())
	{
		Sfx2D(this, bBought ? EKakurenboSfx::Buy : EKakurenboSfx::BuyFail);
	}
	return bBought;
}

bool AKakurenboGameMode::TryBuyTrap(int32 TrapTypeIndex)
{
	AKakurenboGameState* State = GS();
	if (!State || State->Phase != EKakurenboPhase::Shop || !TrapTypes.IsValidIndex(TrapTypeIndex))
	{
		return false;
	}
	const double Cost = GetTrapCost(TrapTypeIndex);
	if (State->Coins < Cost)
	{
		return false;
	}
	State->Coins -= Cost;
	State->TrapStock.SetNum(TrapTypes.Num());
	State->TrapStock[TrapTypeIndex]++;
	return true;
}

bool AKakurenboGameMode::TryBuyMashUpgrade()
{
	AKakurenboGameState* State = GS();
	const double Cost = GetMashUpgradeCost();
	if (!State || State->Phase != EKakurenboPhase::Shop || State->Coins < Cost)
	{
		return false;
	}
	State->Coins -= Cost;
	State->MashIncomeLevel++;
	return true;
}

bool AKakurenboGameMode::TryBuyTimeUpgrade()
{
	AKakurenboGameState* State = GS();
	const double Cost = GetTimeUpgradeCost();
	if (!State || State->Phase != EKakurenboPhase::Shop || State->Coins < Cost)
	{
		return false;
	}
	State->Coins -= Cost;
	State->TimeIncomeLevel++;
	return true;
}

bool AKakurenboGameMode::TryBuyWall(int32 WallTypeIndex)
{
	AKakurenboGameState* State = GS();
	if (!State || State->Phase != EKakurenboPhase::Shop || !WallTypes.IsValidIndex(WallTypeIndex))
	{
		return false;
	}
	const double Cost = WallTypes[WallTypeIndex].Cost;
	if (State->Coins < Cost)
	{
		return false;
	}
	State->Coins -= Cost;
	State->WallStock.SetNum(WallTypes.Num());
	State->WallStock[WallTypeIndex]++;
	return true;
}

// ---------------------------------------------------------------- 設置

bool AKakurenboGameMode::IsPlayerInCellColumn(const FIntPoint& Cell) const
{
	const UKakurenboGridSubsystem* Grid = GetWorld()->GetSubsystem<UKakurenboGridSubsystem>();
	const ACharacter* Player = GetPlayerCharacter();
	if (!Grid || !Player)
	{
		return false;
	}
	// プレイヤーの円（カプセルを真上から見たもの）とマスの正方形が交わるか
	const float Half = Grid->GetCellSize() * 0.5f;
	const FVector Center = Grid->CellFloorCenter(Cell);
	const FVector P = Player->GetActorLocation();
	const float Radius = Player->GetSimpleCollisionRadius();
	const float DX = FMath::Max(0.f, FMath::Abs(P.X - Center.X) - Half);
	const float DY = FMath::Max(0.f, FMath::Abs(P.Y - Center.Y) - Half);
	return DX * DX + DY * DY < Radius * Radius;
}

bool AKakurenboGameMode::CanPlaceWall(const FIntPoint& Cell, int32 WallTypeIndex, FText* OutReason) const
{
	const AKakurenboGameState* State = GS();
	const UKakurenboGridSubsystem* Grid = GetWorld()->GetSubsystem<UKakurenboGridSubsystem>();
	if (!State || !Grid || State->Phase != EKakurenboPhase::Build)
	{
		return false;
	}
	if (!State->WallStock.IsValidIndex(WallTypeIndex) || State->WallStock[WallTypeIndex] <= 0)
	{
		if (OutReason) *OutReason = NSLOCTEXT("Kakurenbo", "PlaceNoStock", "在庫がありません（購入パートで買えます）");
		return false;
	}
	if (!Grid->CanPlaceBlock(Cell, OutReason))
	{
		return false;
	}

	// プレイヤーと重なる場所には置けない（真上から見て重なり、高さも重なるか）
	if (const ACharacter* Player = GetPlayerCharacter())
	{
		const FVector BoxCenter = Grid->CellToWorld(Cell, Grid->GetColumnHeight(Cell));
		const bool bOverlapZ = FMath::Abs(Player->GetActorLocation().Z - BoxCenter.Z) < Player->GetSimpleCollisionHalfHeight() + Grid->GetBlockHeight() * 0.5f;
		if (IsPlayerInCellColumn(Cell) && bOverlapZ)
		{
			if (OutReason) *OutReason = NSLOCTEXT("Kakurenbo", "PlacePlayer", "スタート位置（自分）には置けません（T でスタート位置を動かせます）");
			return false;
		}
	}
	return true;
}

bool AKakurenboGameMode::MovePlayerStart(const FIntPoint& Cell, FText* OutReason)
{
	const AKakurenboGameState* State = GS();
	const UKakurenboGridSubsystem* Grid = GetWorld()->GetSubsystem<UKakurenboGridSubsystem>();
	ACharacter* Player = GetPlayerCharacter();
	if (!State || !Grid || !Player || State->Phase != EKakurenboPhase::Build)
	{
		return false;
	}
	if (!Grid->IsInside(Cell))
	{
		if (OutReason) *OutReason = NSLOCTEXT("Kakurenbo", "PlaceOutside", "範囲外です");
		return false;
	}
	if (Grid->GetColumnHeight(Cell) > 0 || Grid->GetDesignHeight(Cell) > 0)
	{
		if (OutReason) *OutReason = NSLOCTEXT("Kakurenbo", "StartOnWall", "壁（壊れた壁の設計図）のあるマスからは始められません");
		return false;
	}
	if (Grid->IsReservedCell(Cell))
	{
		if (OutReason) *OutReason = NSLOCTEXT("Kakurenbo", "StartOnGate", "鬼の出入り口の前からは始められません");
		return false;
	}
	const FVector Floor = Grid->CellFloorCenter(Cell);
	Player->SetActorLocation(FVector(Floor.X, Floor.Y, Floor.Z + Player->GetSimpleCollisionHalfHeight() + 2.f), false, nullptr, ETeleportType::TeleportPhysics);
	Sfx2D(this, EKakurenboSfx::Place, 0.6f, 1.6f);
	return true;
}

bool AKakurenboGameMode::PlaceWall(FIntPoint Cell, int32 WallTypeIndex)
{
	if (!CanPlaceWall(Cell, WallTypeIndex))
	{
		return false;
	}
	UKakurenboGridSubsystem* Grid = GetWorld()->GetSubsystem<UKakurenboGridSubsystem>();
	const FWallTypeDef& Def = WallTypes[WallTypeIndex];
	APlaceableBlock* Block = Grid->PlaceBlock(Cell, WallTypeIndex, Def.MaxHP * GetWallHPMultiplier(), Def.Color);
	if (!Block)
	{
		return false;
	}
	GS()->WallStock[WallTypeIndex]--;
	Sfx3D(this, EKakurenboSfx::Place, Block->GetActorLocation());
	return true;
}

bool AKakurenboGameMode::PickUpWall(APlaceableBlock* Block)
{
	AKakurenboGameState* State = GS();
	UKakurenboGridSubsystem* Grid = GetWorld()->GetSubsystem<UKakurenboGridSubsystem>();
	if (!State || !Grid || !Block || State->Phase != EKakurenboPhase::Build)
	{
		return false;
	}
	const int32 TypeIndex = Block->WallTypeIndex;
	const FVector Location = Block->GetActorLocation();
	if (!Grid->PickUpBlock(Block))
	{
		return false;
	}
	// 回収した壁は新品として在庫に戻る
	if (State->WallStock.IsValidIndex(TypeIndex))
	{
		State->WallStock[TypeIndex]++;
	}
	Sfx3D(this, EKakurenboSfx::PickUp, Location);
	return true;
}

bool AKakurenboGameMode::CanPlaceTrap(const FIntPoint& Cell, int32 TrapTypeIndex, FText* OutReason) const
{
	const AKakurenboGameState* State = GS();
	const UKakurenboGridSubsystem* Grid = GetWorld()->GetSubsystem<UKakurenboGridSubsystem>();
	if (!State || !Grid || State->Phase != EKakurenboPhase::Build)
	{
		return false;
	}
	if (!State->TrapStock.IsValidIndex(TrapTypeIndex) || State->TrapStock[TrapTypeIndex] <= 0)
	{
		if (OutReason) *OutReason = NSLOCTEXT("Kakurenbo", "PlaceNoTrapStock", "在庫がありません（購入パートで買えます）");
		return false;
	}
	return Grid->CanPlaceTrap(Cell, OutReason);
}

bool AKakurenboGameMode::PlaceTrap(FIntPoint Cell, int32 TrapTypeIndex)
{
	if (!CanPlaceTrap(Cell, TrapTypeIndex))
	{
		return false;
	}
	UKakurenboGridSubsystem* Grid = GetWorld()->GetSubsystem<UKakurenboGridSubsystem>();
	ATrapActor* Trap = Grid->PlaceTrap(Cell, TrapTypeIndex, TrapTypes[TrapTypeIndex]);
	if (!Trap)
	{
		return false;
	}
	GS()->TrapStock[TrapTypeIndex]--;
	Sfx3D(this, EKakurenboSfx::Place, Trap->GetActorLocation(), 0.8f, 1.4f);
	return true;
}

bool AKakurenboGameMode::PickUpTrap(FIntPoint Cell)
{
	AKakurenboGameState* State = GS();
	UKakurenboGridSubsystem* Grid = GetWorld()->GetSubsystem<UKakurenboGridSubsystem>();
	if (!State || !Grid || State->Phase != EKakurenboPhase::Build)
	{
		return false;
	}
	int32 ReturnedType = INDEX_NONE;
	if (!Grid->PickUpTrap(Cell, ReturnedType))
	{
		return false;
	}
	// 残っていた罠は在庫に戻る（発動して消えていた罠は設計図から消えるだけ）
	if (State->TrapStock.IsValidIndex(ReturnedType))
	{
		State->TrapStock[ReturnedType]++;
	}
	Sfx3D(this, EKakurenboSfx::PickUp, Grid->CellFloorCenter(Cell));
	return true;
}

void AKakurenboGameMode::RefillTraps()
{
	AKakurenboGameState* State = GS();
	UKakurenboGridSubsystem* Grid = GetWorld()->GetSubsystem<UKakurenboGridSubsystem>();
	if (!State || !Grid)
	{
		return;
	}
	State->TrapStock.SetNum(TrapTypes.Num());
	int32 Refilled = 0;
	int32 Missing = 0;
	Grid->RefillTrapsFromDesign(State->TrapStock, TrapTypes, Refilled, Missing);
	State->LastRefilledTraps = Refilled;
	State->LastUnrefilledTraps = Missing;
	if (Refilled > 0 || Missing > 0)
	{
		UE_LOG(LogKakurenbo, Log, TEXT("Traps refilled: %d, not refilled: %d"), Refilled, Missing);
	}
}

void AKakurenboGameMode::RepairWalls()
{
	AKakurenboGameState* State = GS();
	UKakurenboGridSubsystem* Grid = GetWorld()->GetSubsystem<UKakurenboGridSubsystem>();
	if (!State || !Grid)
	{
		return;
	}
	State->WallStock.SetNum(WallTypes.Num());

	int32 Repaired = 0;
	int32 Missing = 0;
	// プレイヤーが立っている（上に乗っている）列は、閉じ込めないよう直さない
	Grid->RepairFromDesign(State->WallStock, GetEffectiveWallTypes(),
		[this](const FIntPoint& Cell, int32 Level) { return !IsPlayerInCellColumn(Cell); },
		Repaired, Missing);

	State->LastRepairedWalls = Repaired;
	State->LastUnrepairedWalls = Missing;
	if (Repaired > 0 || Missing > 0)
	{
		UE_LOG(LogKakurenbo, Log, TEXT("Walls repaired: %d, not repaired: %d"), Repaired, Missing);
	}
}

// ---------------------------------------------------------------- かくれんぼ

void AKakurenboGameMode::HandleMash(const FVector& NoiseLocation)
{
	AKakurenboGameState* State = GS();
	if (!State || State->Phase != EKakurenboPhase::Hide)
	{
		return;
	}
	State->AddCoins(GetMashIncome(), true);
	State->MashCountThisRound++;

	// 連打の音が鬼に届く（消音壁で囲まれていれば小さくなる）
	const float Loudness = GetPlayerNoiseMultiplier();
	EmitNoise(NoiseLocation, Loudness);
	Sfx2D(this, EKakurenboSfx::Mash, 0.6f * FMath::Max(Loudness, 0.4f), FMath::FRandRange(0.92f, 1.08f));
	ShowNoiseRing(NoiseLocation, Loudness, NoiseRingColor, 14.f);
}

void AKakurenboGameMode::HandleDash(const FVector& NoiseLocation, float Loudness)
{
	if (GetPhase() != EKakurenboPhase::Hide)
	{
		return;
	}
	// ダッシュは大きな音が出る（消音壁で囲まれていれば小さくなる）
	const float Scaled = Loudness * GetPlayerNoiseMultiplier();
	EmitNoise(NoiseLocation, Scaled);
	Sfx2D(this, EKakurenboSfx::Dash, 0.9f);
	ShowNoiseRing(NoiseLocation, Scaled, FLinearColor(1.f, 0.45f, 0.2f), 22.f);
}

void AKakurenboGameMode::ShowNoiseRing(const FVector& Location, float Loudness, const FLinearColor& Color, float Thickness) const
{
	// 音の届く範囲を床に輪で表示する
	UKakurenboFxSubsystem* F = Fx(this);
	if (!bShowNoiseRing || !F)
	{
		return;
	}
	const float Radius = FMath::Max(GetOniHearingRadius() * Loudness, 30.f);
	F->Ring(Location, Radius * 0.85f, Radius, 0.3f, Color, Thickness);
}

float AKakurenboGameMode::GetPlayerNoiseMultiplier() const
{
	const UKakurenboGridSubsystem* Grid = GetWorld()->GetSubsystem<UKakurenboGridSubsystem>();
	const ACharacter* Player = GetPlayerCharacter();
	if (!Grid || !Player || !Grid->IsConfigured())
	{
		return 1.f;
	}
	const float Damping = Grid->GetEnclosureNoiseDamping(Grid->WorldToCell(Player->GetActorLocation()), WallTypes);
	return FMath::Clamp(1.f - Damping, 0.f, 1.f);
}

int32 AKakurenboGameMode::GetPlayerEnclosureWallCount() const
{
	const UKakurenboGridSubsystem* Grid = GetWorld()->GetSubsystem<UKakurenboGridSubsystem>();
	const ACharacter* Player = GetPlayerCharacter();
	if (!Grid || !Player || !Grid->IsConfigured())
	{
		return 0;
	}
	int32 Walls = 0;
	Grid->GetEnclosureNoiseDamping(Grid->WorldToCell(Player->GetActorLocation()), WallTypes, &Walls);
	return Walls;
}

TArray<FIntPoint> AKakurenboGameMode::GetOniGateCells() const
{
	return Arena ? Arena->GetOniGateCells() : TArray<FIntPoint>();
}

void AKakurenboGameMode::EmitNoise(const FVector& Location, float Loudness)
{
	for (AOniCharacter* Oni : Onis)
	{
		if (Oni)
		{
			Oni->HearNoise(Location, Loudness);
		}
	}
}

void AKakurenboGameMode::CollectTreasure(ATreasureActor* Treasure)
{
	AKakurenboGameState* State = GS();
	if (!State || State->Phase != EKakurenboPhase::Hide || !Treasure || !Treasures.Contains(Treasure))
	{
		return;
	}
	State->AddCoins(Treasure->Value, true);
	State->TreasuresCollectedThisRound++;
	State->TreasureCoinsThisRound += Treasure->Value;
	UE_LOG(LogKakurenbo, Log, TEXT("Treasure collected (+%s)"), *UKakurenboLibrary::FormatBigNumber(Treasure->Value));

	// きらきらを飛ばして、音と大きな文字で知らせる
	const FVector Location = Treasure->GetActorLocation() + FVector(0.f, 0.f, 60.f);
	if (UKakurenboFxSubsystem* F = Fx(this))
	{
		FKakurenboBurstParams Params;
		Params.Color = TreasureColor;
		Params.Count = 16;
		Params.Size = 10.f;
		Params.Speed = 380.f;
		Params.UpBias = 0.85f;
		Params.Gravity = 600.f;
		Params.Lifetime = 0.8f;
		F->Burst(Location, Params);
		F->Ring(Location, 20.f, 160.f, 0.35f, TreasureColor, 6.f);
	}
	Sfx2D(this, EKakurenboSfx::Treasure);
	ShowPopup(FString::Printf(TEXT("お宝 +%s"), *UKakurenboLibrary::FormatBigNumber(Treasure->Value)), TreasureColor);

	Treasures.Remove(Treasure);
	Treasure->Destroy();
}

void AKakurenboGameMode::HandleTrapTriggered(ATrapActor* Trap, AOniCharacter* Oni)
{
	AKakurenboGameState* State = GS();
	UKakurenboGridSubsystem* Grid = GetWorld()->GetSubsystem<UKakurenboGridSubsystem>();
	if (!State || !Grid || !Trap || State->Phase != EKakurenboPhase::Hide)
	{
		return;
	}
	State->TrapsTriggeredThisRound++;
	const FVector Location = Trap->GetActorLocation() + FVector(0.f, 0.f, 20.f);
	const FKakurenboTrapRow Def = Trap->Def;

	FKakurenboBurstParams Params;
	Params.Color = Def.Color;
	Params.Count = 14;
	Params.Size = 11.f;
	Params.Speed = 320.f;
	Params.UpBias = 0.8f;
	Params.Lifetime = 0.7f;
	Params.bSpheres = true;

	if (Def.Kind == ETrapKind::Sticky)
	{
		// 鬼をしばらく動けなくする
		if (Oni)
		{
			Oni->Stun(Def.StunSeconds);
		}
		Sfx3D(this, EKakurenboSfx::TrapSticky, Location);
		ShowPopup(FString::Printf(TEXT("%s にかかった！"), *Def.DisplayName.ToString()), Def.Color);
	}
	else
	{
		// おとりが壊された
		Sfx3D(this, EKakurenboSfx::DecoyBreak, Location);
		Params.bSpheres = false;
	}
	if (UKakurenboFxSubsystem* F = Fx(this))
	{
		F->Burst(Location, Params);
	}
	UE_LOG(LogKakurenbo, Log, TEXT("Trap %s at (%d,%d) triggered by %s"),
		*Def.DisplayName.ToString(), Trap->Cell.X, Trap->Cell.Y, Oni ? *Oni->GetName() : TEXT("none"));

	// 罠は消える（設計図は残るので、次の設置パートで在庫から置き直される）
	Grid->ConsumeTrap(Trap);
}

void AKakurenboGameMode::HandleDecoyPing(ATrapActor* Trap)
{
	if (!Trap || GetPhase() != EKakurenboPhase::Hide)
	{
		return;
	}
	const FVector Location = Trap->GetActorLocation();
	EmitNoise(Location, Trap->Def.NoiseLoudness);
	Sfx3D(this, EKakurenboSfx::DecoyPing, Location + FVector(0.f, 0.f, 40.f));
	if (UKakurenboFxSubsystem* F = Fx(this))
	{
		// 音の届く範囲（標準の聞こえる距離 × 大きさ）まで輪を広げる
		F->Ring(Location, 40.f, GetOniHearingRadius() * Trap->Def.NoiseLoudness, 0.6f, Trap->Def.Color, 10.f);
	}
}

void AKakurenboGameMode::HandleBlockHit(const FVector& Location, const FLinearColor& Color, bool bDestroyed)
{
	UKakurenboFxSubsystem* F = Fx(this);
	FKakurenboBurstParams Params;
	Params.Color = Color;
	if (bDestroyed)
	{
		// 壊れた：大きな破片がたくさん飛び散る
		Params.Count = 10;
		Params.Size = 22.f;
		Params.Speed = 380.f;
		Params.UpBias = 0.5f;
		Params.Lifetime = 1.f;
		Sfx3D(this, EKakurenboSfx::BlockBreak, Location, 1.f, FMath::FRandRange(0.9f, 1.1f));
	}
	else
	{
		// 傷ついただけ：小さな欠片が少し
		Params.Count = 4;
		Params.Size = 12.f;
		Params.Speed = 250.f;
		Params.Lifetime = 0.5f;
		Sfx3D(this, EKakurenboSfx::BlockHit, Location, 1.f, FMath::FRandRange(0.9f, 1.1f));
	}
	if (F)
	{
		F->Burst(Location, Params);
	}
}

void AKakurenboGameMode::FlashScreen(const FLinearColor& Color, float Duration) const
{
	if (const APlayerController* PC = GetWorld()->GetFirstPlayerController())
	{
		if (AKakurenboHUD* HUD = PC->GetHUD<AKakurenboHUD>())
		{
			HUD->Flash(Color, Duration);
		}
	}
}

void AKakurenboGameMode::ShowPopup(const FString& Text, const FLinearColor& Color) const
{
	if (const APlayerController* PC = GetWorld()->GetFirstPlayerController())
	{
		if (AKakurenboHUD* HUD = PC->GetHUD<AKakurenboHUD>())
		{
			HUD->AddPopup(Text, Color);
		}
	}
}

void AKakurenboGameMode::SpawnTreasures()
{
	ClearTreasures();

	UKakurenboGridSubsystem* Grid = GetWorld()->GetSubsystem<UKakurenboGridSubsystem>();
	const ACharacter* Player = GetPlayerCharacter();
	if (!Grid || !Player || !TreasureClass)
	{
		return;
	}

	// 毎回ランダムな空きマス。プレイヤーのすぐ近くと、お宝どうしは離す
	const TArray<FIntPoint> Cells = Grid->FindRandomFreeCells(GetNumTreasures(), { Player->GetActorLocation() }, TreasureMinDistanceCells, TreasureRandom);
	for (const FIntPoint& Cell : Cells)
	{
		ATreasureActor* Treasure = GetWorld()->SpawnActorDeferred<ATreasureActor>(TreasureClass, FTransform(Grid->CellFloorCenter(Cell)));
		if (!Treasure)
		{
			continue;
		}
		Treasure->Value = GetTreasureValue();
		Treasure->FinishSpawning(FTransform(Grid->CellFloorCenter(Cell)));
		Treasures.Add(Treasure);
	}

	if (AKakurenboGameState* State = GS())
	{
		State->TreasuresThisRound = Treasures.Num();
	}
}

void AKakurenboGameMode::ClearTreasures()
{
	for (ATreasureActor* Treasure : Treasures)
	{
		if (Treasure)
		{
			Treasure->Destroy();
		}
	}
	Treasures.Reset();
}

// ---------------------------------------------------------------- 鬼

void AKakurenboGameMode::SpawnOnis()
{
	DespawnOnis();

	UKakurenboGridSubsystem* Grid = GetWorld()->GetSubsystem<UKakurenboGridSubsystem>();
	AHiderCharacter* Hider = Cast<AHiderCharacter>(GetPlayerCharacter());
	if (!Grid || !Hider || !OniClass)
	{
		return;
	}

	// 慎重鬼が共有する「調べた場所」の記録を新しくする
	if (UKakurenboOniBlackboard* Blackboard = GetWorld()->GetSubsystem<UKakurenboOniBlackboard>())
	{
		Blackboard->Reset(Grid->GetSizeX(), Grid->GetSizeY());
	}

	// 鬼は必ず出入り口（赤い門）の前のマスから出てくる。種類はステージの表で決まる。
	// 門の前は壁を置けないが、古いセーブなどで塞がっていたら、門に一番近い空きマスから出す
	const FKakurenboStageRow Settings = GetStageSettings();
	const TArray<FIntPoint> GateCells = GetOniGateCells();
	const FVector GateLocation = Arena ? Arena->GetOniGateLocation() : Hider->GetActorLocation();
	TArray<FIntPoint> Cells;
	for (int32 i = 0; i < Settings.OniTypes.Num(); ++i)
	{
		FIntPoint Cell = GateCells.Num() > 0 ? GateCells[i % GateCells.Num()] : FIntPoint::ZeroValue;
		if (GateCells.Num() == 0 || Grid->GetColumnHeight(Cell) > 0)
		{
			float BestDistSq = TNumericLimits<float>::Max();
			for (int32 Y = 0; Y < Grid->GetSizeY(); ++Y)
			{
				for (int32 X = 0; X < Grid->GetSizeX(); ++X)
				{
					const FIntPoint C(X, Y);
					const float DistSq = FVector::DistSquared2D(Grid->CellFloorCenter(C), GateLocation);
					if (Grid->GetColumnHeight(C) == 0 && !Cells.Contains(C) && DistSq < BestDistSq)
					{
						BestDistSq = DistSq;
						Cell = C;
					}
				}
			}
		}
		Cells.Add(Cell);
	}

	for (int32 i = 0; i < Cells.Num(); ++i)
	{
		const FIntPoint Cell = Cells[i];
		const EOniType Type = Settings.OniTypes[i];
		const FKakurenboOniTypeRow TypeRow = GetOniTypeRow(Type);
		const FVector Location = Grid->CellFloorCenter(Cell) + FVector(0.f, 0.f, 100.f);
		// 門を背にして、舞台の内側を向いて出てくる
		const FRotator Facing(0.f, (FVector(GateLocation.X, GateLocation.Y, Location.Z) - Location).Rotation().Yaw + 180.f, 0.f);

		AOniCharacter* Oni = GetWorld()->SpawnActorDeferred<AOniCharacter>(OniClass, FTransform(Facing, Location), nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn);
		if (!Oni)
		{
			continue;
		}
		Oni->ApplyTypeSettings(Type, TypeRow);
		Oni->WanderSpeed = OniWanderSpeedBase * TypeRow.SpeedScale + Settings.OniSpeedBonus;
		Oni->InvestigateSpeed = OniInvestigateSpeedBase * TypeRow.SpeedScale + Settings.OniSpeedBonus;
		Oni->ChaseSpeed = OniChaseSpeedBase * TypeRow.SpeedScale + Settings.OniSpeedBonus;
		Oni->HearingRadius = Settings.OniHearingRadius * TypeRow.HearingScale;
		Oni->AttackDamage = Settings.OniDamage * TypeRow.DamageScale;
		Oni->bDrawDebug = bDebugOni;
		if (LoadedOniLook.Mesh)
		{
			FKakurenboOniLook Look = LoadedOniLook;
			Look.Material = LoadedOniMaterials.FindRef(Type);
			Oni->SetSkeletalAppearance(Look);
		}
		Oni->FinishSpawning(FTransform(Facing, Location));

		// 鬼からの通知を受け取る（C# の event += に相当）
		Oni->OnFoundHider.AddUObject(this, &AKakurenboGameMode::HandleOniFoundHider);
		Oni->OnDestroyedWalls.AddUObject(this, &AKakurenboGameMode::HandleOniDestroyedWalls);
		Oni->Activate(Hider);
		Onis.Add(Oni);

		UE_LOG(LogKakurenbo, Log, TEXT("Oni %s spawned at cell (%d,%d), damage %.2f, chase speed %.0f"),
			*UEnum::GetValueAsString(Type), Cell.X, Cell.Y, Oni->AttackDamage, Oni->ChaseSpeed);
	}

	// 鬼どうしは体がすり抜ける（違う方向へ行こうとしてすれ違うときに、押し合って動けなくならないように）
	for (AOniCharacter* A : Onis)
	{
		for (AOniCharacter* B : Onis)
		{
			if (A && B && A != B)
			{
				A->GetCapsuleComponent()->IgnoreActorWhenMoving(B, true);
			}
		}
	}
}

void AKakurenboGameMode::LoadOniAppearance()
{
	// ini（DefaultGame.ini）に鬼のメッシュが書いてあれば読み込む。
	// アセットが無い（そのパソコンでは Fab から追加していない）ときは円柱のまま
	LoadedOniLook = FKakurenboOniLook();
	LoadedOniMaterials.Reset();
	if (OniSkeletalMesh.IsNull())
	{
		return;
	}
	if (!FPackageName::DoesPackageExist(OniSkeletalMesh.ToSoftObjectPath().GetLongPackageName()))
	{
		UE_LOG(LogKakurenbo, Log, TEXT("Oni mesh '%s' is not in this project. Using the placeholder shape."), *OniSkeletalMesh.ToString());
		return;
	}
	// 書いてあって見つかったものだけ読む
	auto Load = [](const auto& Soft) { return Soft.IsNull() ? nullptr : Soft.LoadSynchronous(); };
	LoadedOniLook.Mesh = Load(OniSkeletalMesh);
	if (!LoadedOniLook.Mesh)
	{
		UE_LOG(LogKakurenbo, Warning, TEXT("Oni mesh '%s' could not be loaded. Using the placeholder shape."), *OniSkeletalMesh.ToString());
		return;
	}
	LoadedOniLook.AnimClass = Load(OniAnimClass);
	LoadedOniLook.Idle = Load(OniAnimIdle);
	LoadedOniLook.Run = Load(OniAnimRun);
	LoadedOniLook.Attack = Load(OniAnimAttack);
	LoadedOniLook.Win = Load(OniAnimWin);
	LoadedOniLook.Stunned = Load(OniAnimStunned);
	LoadedOniLook.Accessory = Load(OniAccessoryMesh);
	LoadedOniLook.Scale = OniMeshScale;
	LoadedOniLook.ZOffset = OniMeshZOffset;
	LoadedOniLook.Yaw = OniMeshYaw;
	LoadedOniLook.RunAnimSpeed = OniRunAnimSpeed;

	// 種類ごとの色違い（OniTypes.csv の MeshMaterial）
	for (const TPair<EOniType, FKakurenboOniTypeRow>& Pair : OniTypeRows)
	{
		if (UMaterialInterface* Material = Load(Pair.Value.MeshMaterial))
		{
			LoadedOniMaterials.Add(Pair.Key, Material);
		}
	}
	UE_LOG(LogKakurenbo, Log, TEXT("Oni look: mesh %s, anims idle=%d run=%d attack=%d win=%d stunned=%d, %d type materials"),
		*LoadedOniLook.Mesh->GetName(), LoadedOniLook.Idle != nullptr, LoadedOniLook.Run != nullptr, LoadedOniLook.Attack != nullptr,
		LoadedOniLook.Win != nullptr, LoadedOniLook.Stunned != nullptr, LoadedOniMaterials.Num());
}

void AKakurenboGameMode::DespawnOnis()
{
	for (AOniCharacter* Oni : Onis)
	{
		if (Oni)
		{
			Oni->Destroy();
		}
	}
	Onis.Reset();
}

void AKakurenboGameMode::HandleOniFoundHider()
{
	UE_LOG(LogKakurenbo, Log, TEXT("Hider was found!"));
	EndHidePhase(false);
}

void AKakurenboGameMode::HandleOniDestroyedWalls(int32 Count)
{
	if (AKakurenboGameState* State = GS())
	{
		State->LastRoundWallsDestroyed += Count;
	}
}

// ---------------------------------------------------------------- パート遷移

void AKakurenboGameMode::SetPhase(EKakurenboPhase NewPhase)
{
	AKakurenboGameState* State = GS();
	if (!State)
	{
		return;
	}
	State->Phase = NewPhase;
	UE_LOG(LogKakurenbo, Log, TEXT("Phase -> %s (Stage %d, Coins %s)"),
		*UEnum::GetValueAsString(NewPhase), State->Stage, *UKakurenboLibrary::FormatBigNumber(State->Coins));
	State->OnPhaseChanged.Broadcast(NewPhase);

	// かくれんぼ以外のパートに入るたびに自動で保存する（かくれんぼの途中で終わっても、直前の状態から再開できる）
	if (NewPhase != EKakurenboPhase::Hide)
	{
		SaveProgress();
	}
}

void AKakurenboGameMode::StartShopPhase()
{
	DespawnOnis();
	ClearTreasures();
	SetPhase(EKakurenboPhase::Shop);
}

void AKakurenboGameMode::StartBuildPhase()
{
	// 購入パートで買い足した在庫も使って、壊れた壁と使った罠を設計図どおりに直す
	RepairWalls();
	RefillTraps();
	SetPhase(EKakurenboPhase::Build);
}

void AKakurenboGameMode::StartHidePhase()
{
	AKakurenboGameState* State = GS();
	if (!State)
	{
		return;
	}
	// 設置パートで置いた壁を保存してから始める
	SaveProgress();

	State->CoinsEarnedThisRound = 0.0;
	State->MashCountThisRound = 0;
	State->LastRoundWallsDestroyed = 0;
	State->TrapsTriggeredThisRound = 0;
	State->TreasuresCollectedThisRound = 0;
	State->TreasureCoinsThisRound = 0.0;
	State->HideTimeLimit = GetHideDuration();
	State->HideTimeRemaining = State->HideTimeLimit;
	State->HideStartCountdown = HideStartDelay;
	bOnisSpawnedThisRound = false;
	LastCountdownSecond = 0;
	LastTimeTickSecond = 0;
	ApplyPrestigeToPlayer(); // ダッシュ・ジャンプ（転生のお店で解放）
	if (AHiderCharacter* Hider = Cast<AHiderCharacter>(GetPlayerCharacter()))
	{
		Hider->ResetDash(); // ラウンドの始めはすぐダッシュできる
	}

	// お宝は最初から置いておく（鬼が来る前に取りに行ける）
	SpawnTreasures();
	SetPhase(EKakurenboPhase::Hide);
}

void AKakurenboGameMode::EndHidePhase(bool bCleared)
{
	AKakurenboGameState* State = GS();
	if (!State || State->Phase != EKakurenboPhase::Hide)
	{
		return;
	}

	State->bLastRoundCleared = bCleared;
	State->LastClearReward = 0.0;
	if (bCleared)
	{
		// 逃げ切り：大きな報酬と次のステージ
		State->LastClearReward = GetClearReward();
		State->AddCoins(State->LastClearReward, true);
		State->Stage++;

		// 金色に光らせて、紙吹雪を飛ばす
		Sfx2D(this, EKakurenboSfx::Clear);
		FlashScreen(FLinearColor(1.f, 0.85f, 0.3f, 0.45f), 0.8f);
		if (const ACharacter* Player = GetPlayerCharacter())
		{
			if (UKakurenboFxSubsystem* F = Fx(this))
			{
				for (const FLinearColor& Color : { FLinearColor(1.f, 0.3f, 0.3f), FLinearColor(0.3f, 0.8f, 1.f), FLinearColor(1.f, 0.85f, 0.2f) })
				{
					FKakurenboBurstParams Params;
					Params.Color = Color;
					Params.Count = 10;
					Params.Size = 9.f;
					Params.Speed = 520.f;
					Params.UpBias = 0.9f;
					Params.Gravity = 500.f;
					Params.Lifetime = 1.4f;
					F->Burst(Player->GetActorLocation() + FVector(0.f, 0.f, 80.f), Params);
				}
			}
		}
	}
	else
	{
		// 見つかった場合もペナルティなし（稼いだコインはそのまま）
		Sfx2D(this, EKakurenboSfx::Caught);
		FlashScreen(FLinearColor(1.f, 0.1f, 0.05f, 0.55f), 0.7f);
	}

	// 見つかったときは鬼の姿を少しの間見せたいので、リザルト画面を抜けるときに消す
	if (bCleared)
	{
		DespawnOnis();
	}
	else
	{
		for (AOniCharacter* Oni : Onis)
		{
			if (Oni)
			{
				Oni->Deactivate();
			}
		}
	}
	// 取れなかったお宝は消える
	ClearTreasures();

	SetPhase(EKakurenboPhase::Result);
}

void AKakurenboGameMode::AdvancePhase()
{
	switch (GetPhase())
	{
	case EKakurenboPhase::Result: StartShopPhase(); break;
	case EKakurenboPhase::Shop:   StartBuildPhase(); break;
	case EKakurenboPhase::Build:  StartHidePhase(); break;
	case EKakurenboPhase::Hide:   break; // かくれんぼ中は進められない
	}
}

// ---------------------------------------------------------------- 転生

bool AKakurenboGameMode::CanPrestige() const
{
	return GetPrestigePointsOnReset() > 0;
}

int32 AKakurenboGameMode::GetPrestigePointsOnReset() const
{
	const AKakurenboGameState* State = GS();
	return State ? KakurenboBalance::GetPrestigePoints(State->Stage, PrestigeSettings) : 0;
}

bool AKakurenboGameMode::Prestige()
{
	AKakurenboGameState* State = GS();
	UKakurenboGridSubsystem* Grid = GetWorld()->GetSubsystem<UKakurenboGridSubsystem>();
	const int32 Gained = GetPrestigePointsOnReset();
	if (!State || !Grid || State->Phase != EKakurenboPhase::Shop || Gained <= 0)
	{
		return false;
	}
	State->PrestigePoints += Gained;
	State->TotalPrestigePoints += Gained;
	State->PrestigeCount++;

	// 壁が硬くなる代わりに、ほかは最初から（置いた壁と罠は消えるが、設計図は残る）
	DespawnOnis();
	ClearTreasures();
	Grid->ClearLiveKeepDesign();
	State->Coins = 0.0;
	State->Stage = 1;
	State->MashIncomeLevel = 0;
	State->TimeIncomeLevel = 0;
	State->WallStock.Init(0, WallTypes.Num());
	State->TrapStock.Init(0, TrapTypes.Num());
	State->LastRepairedWalls = 0;
	State->LastUnrepairedWalls = 0;
	State->LastRefilledTraps = 0;
	State->LastUnrefilledTraps = 0;

	UE_LOG(LogKakurenbo, Log, TEXT("Prestige #%d: +%d points (%d to spend, %d in total)"),
		State->PrestigeCount, Gained, State->PrestigePoints, State->TotalPrestigePoints);
	Sfx2D(this, EKakurenboSfx::Prestige);
	FlashScreen(FLinearColor(0.7f, 0.5f, 1.f, 0.6f), 1.2f);
	ShowNotice(FText::Format(NSLOCTEXT("Kakurenbo", "NoticePrestige", "転生しました！ 転生ポイント +{0}（購入パートの「転生のお店」で使えます）"), Gained), 6.f);

	// ステージ 1 のかくれんぼから（ゲームの最初と同じ）
	State->Phase = EKakurenboPhase::Result;
	StartHidePhase();
	return true;
}

// ---------------------------------------------------------------- セーブ・ロード

bool AKakurenboGameMode::SaveProgress()
{
	AKakurenboGameState* State = GS();
	UKakurenboGridSubsystem* Grid = GetWorld()->GetSubsystem<UKakurenboGridSubsystem>();
	if (!bSaveEnabled || !State || !Grid)
	{
		return false;
	}

	UKakurenboSaveGame* Save = Cast<UKakurenboSaveGame>(UGameplayStatics::CreateSaveGameObject(UKakurenboSaveGame::StaticClass()));
	Save->Coins = State->Coins;
	Save->Stage = State->Stage;
	Save->MashIncomeLevel = State->MashIncomeLevel;
	Save->TimeIncomeLevel = State->TimeIncomeLevel;
	Save->PrestigePoints = State->PrestigePoints;
	Save->TotalPrestigePoints = State->TotalPrestigePoints;
	Save->PrestigeCount = State->PrestigeCount;
	Save->PrestigeLevels = State->PrestigeLevels;
	Save->WallStock = State->WallStock;
	Save->TrapStock = State->TrapStock;
	Grid->ExportLayout(Save->Columns);
	if (const ACharacter* Player = GetPlayerCharacter())
	{
		Save->bHasPlayerLocation = true;
		Save->PlayerLocation = Player->GetActorLocation();
	}

	const bool bSaved = UGameplayStatics::SaveGameToSlot(Save, SaveSlotName, 0);
	if (!bSaved)
	{
		UE_LOG(LogKakurenbo, Warning, TEXT("Failed to save to slot '%s'"), *SaveSlotName);
	}
	return bSaved;
}

bool AKakurenboGameMode::LoadProgress()
{
	AKakurenboGameState* State = GS();
	UKakurenboGridSubsystem* Grid = GetWorld()->GetSubsystem<UKakurenboGridSubsystem>();
	if (!bSaveEnabled || !State || !Grid || !UGameplayStatics::DoesSaveGameExist(SaveSlotName, 0))
	{
		return false;
	}
	const UKakurenboSaveGame* Save = Cast<UKakurenboSaveGame>(UGameplayStatics::LoadGameFromSlot(SaveSlotName, 0));
	if (!Save)
	{
		UE_LOG(LogKakurenbo, Warning, TEXT("Save slot '%s' could not be read"), *SaveSlotName);
		return false;
	}

	State->Coins = Save->Coins;
	State->Stage = FMath::Max(1, Save->Stage);
	State->MashIncomeLevel = Save->MashIncomeLevel;
	State->TimeIncomeLevel = Save->TimeIncomeLevel;
	// 転生のお店の強化は壁を作り直す前に戻す（耐久の倍率に使う）
	State->PrestigePoints = Save->PrestigePoints;
	State->TotalPrestigePoints = Save->SaveVersion >= 4 ? Save->TotalPrestigePoints : Save->PrestigePoints;
	State->PrestigeCount = Save->PrestigeCount;
	State->PrestigeLevels = Save->PrestigeLevels;
	State->PrestigeLevels.SetNum(static_cast<int32>(EPrestigeUpgrade::Count));
	State->WallStock = Save->WallStock;
	State->WallStock.SetNum(WallTypes.Num()); // CSV で壁の種類が増減していても合わせる
	State->TrapStock = Save->TrapStock;
	State->TrapStock.SetNum(TrapTypes.Num());
	Grid->ImportLayout(Save->Columns, GetEffectiveWallTypes(), TrapTypes);

	if (Save->bHasPlayerLocation)
	{
		if (ACharacter* Player = GetPlayerCharacter())
		{
			Player->SetActorLocation(Save->PlayerLocation, false, nullptr, ETeleportType::TeleportPhysics);
		}
	}
	ApplyPrestigeToPlayer();
	UE_LOG(LogKakurenbo, Log, TEXT("Loaded save '%s': stage %d, coins %s, %d wall columns"),
		*SaveSlotName, State->Stage, *UKakurenboLibrary::FormatBigNumber(State->Coins), Save->Columns.Num());
	return true;
}

void AKakurenboGameMode::ResetProgress()
{
	AKakurenboGameState* State = GS();
	UKakurenboGridSubsystem* Grid = GetWorld()->GetSubsystem<UKakurenboGridSubsystem>();
	if (!State || !Grid)
	{
		return;
	}
	if (bSaveEnabled)
	{
		UGameplayStatics::DeleteGameInSlot(SaveSlotName, 0);
	}

	DespawnOnis();
	ClearTreasures();
	Grid->ClearAllBlocks();

	State->Coins = 0.0;
	State->Stage = 1;
	State->MashIncomeLevel = 0;
	State->TimeIncomeLevel = 0;
	State->PrestigePoints = 0;
	State->TotalPrestigePoints = 0;
	State->PrestigeCount = 0;
	State->PrestigeLevels.Init(0, static_cast<int32>(EPrestigeUpgrade::Count));
	State->WallStock.Init(0, WallTypes.Num());
	State->TrapStock.Init(0, TrapTypes.Num());
	State->LastRepairedWalls = 0;
	State->LastUnrepairedWalls = 0;
	State->LastRefilledTraps = 0;
	State->LastUnrefilledTraps = 0;

	if (ACharacter* Player = GetPlayerCharacter())
	{
		Player->SetActorLocation(FVector(CellSize * 0.5f, CellSize * 0.5f, 120.f), false, nullptr, ETeleportType::TeleportPhysics);
	}
	UE_LOG(LogKakurenbo, Log, TEXT("Progress reset"));
	ShowNotice(NSLOCTEXT("Kakurenbo", "NoticeReset", "最初からやり直します"));

	// かくれんぼ中に呼ばれても、いったんリザルト扱いにせずそのまま最初のラウンドを始める
	State->Phase = EKakurenboPhase::Result;
	StartHidePhase();
}

void AKakurenboGameMode::ShowNotice(const FText& Text, float Seconds)
{
	if (AKakurenboGameState* State = GS())
	{
		State->NoticeText = Text;
		State->NoticeUntilTime = GetWorld()->GetTimeSeconds() + Seconds;
	}
}

// ---------------------------------------------------------------- デバッグ

void AKakurenboGameMode::DebugAddCoins(double Amount)
{
	if (AKakurenboGameState* State = GS())
	{
		State->AddCoins(Amount, false);
	}
}

void AKakurenboGameMode::DebugSkipTime(float Seconds)
{
	if (AKakurenboGameState* State = GS())
	{
		State->HideStartCountdown = 0.f;
		State->HideTimeRemaining = FMath::Max(0.01f, State->HideTimeRemaining - Seconds);
	}
}
