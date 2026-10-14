#include "KakurenboGameMode.h"

#include "Animation/AnimInstance.h"
#include "Components/CapsuleComponent.h"
#include "DrawDebugHelpers.h"
#include "Animation/AnimSequence.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Misc/PackageName.h"
#include "Engine/DataTable.h"
#include "Engine/PostProcessVolume.h"
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
#include "Misc/FileHelper.h"
#include "OniCharacter.h"
#include "PlaceableBlock.h"
#include "KakurenboOniStudio.h"
#include "SmokeCloud.h"
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
	WallReinforceName = NSLOCTEXT("Kakurenbo", "ShopWallReinforce", "壁の補強");

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
	AddTrap(TEXT("トリモチ"), ETrapKind::Sticky, 150.0, 2.2, 4.f, 0.f, 0.f, 45.f, FLinearColor(1.f, 0.85f, 0.15f));
	AddTrap(TEXT("おとり"), ETrapKind::Decoy, 250.0, 2.4, 0.f, 2.5f, 1.2f, 70.f, FLinearColor(0.65f, 0.3f, 1.f));
	// 宝石のお店の道具（M15。価格は宝石）
	auto AddGem = [this, &AddTrap](const TCHAR* Name, ETrapKind Kind, double Cost, double Growth, float Interval, float Loudness, float Radius, float Speed, int32 Uses, float Duration, FLinearColor Color)
	{
		AddTrap(Name, Kind, Cost, Growth, 0.f, Interval, Loudness, 60.f, Color);
		FKakurenboTrapRow& Def = TrapTypes.Last();
		Def.bGemItem = true;
		Def.Radius = Radius;
		Def.Speed = Speed;
		Def.Uses = Uses;
		Def.Duration = Duration;
	};
	AddGem(TEXT("身代わり人形"), ETrapKind::FakeHider, 3.0, 1.5, 0.f, 0.f, 0.f, 680.f, 0, 0.f, FLinearColor(0.45f, 0.75f, 1.f));
	AddGem(TEXT("一方通行ののれん"), ETrapKind::OneWay, 2.0, 1.4, 0.f, 0.f, 0.f, 0.f, 0, 0.f, FLinearColor(0.15f, 0.35f, 0.85f));
	AddGem(TEXT("真っ暗ブロック"), ETrapKind::Darkness, 3.0, 1.5, 0.f, 0.f, 300.f, 0.f, 0, 0.f, FLinearColor(0.12f, 0.08f, 0.18f));
	AddGem(TEXT("迷い猫"), ETrapKind::Cat, 4.0, 1.6, 5.f, 0.9f, 450.f, 150.f, 0, 5.f, FLinearColor(1.f, 0.6f, 0.25f));
	AddGem(TEXT("だるまさんがころんだ"), ETrapKind::Daruma, 3.0, 1.3, 0.f, 0.f, 0.f, 0.f, 0, 3.f, FLinearColor(0.95f, 0.2f, 0.2f));
	AddGem(TEXT("ゴールドラッシュ"), ETrapKind::GoldRush, 2.0, 1.3, 0.f, 0.f, 0.f, 0.f, 0, 0.f, FLinearColor(1.f, 0.8f, 0.15f));

	// 壁の既定値（Data/Walls.csv が読めなかったときに使う）。M14：種類は 通常壁・消音壁・クライミング壁 の 3 つ（数ではなく置き方で勝負）
	auto AddWall = [this](const TCHAR* Name, double HP, double Cost, double CostGrowth, FLinearColor Color, float NoiseDamping, double SoundHP, float ClimbSeconds, bool bLadder)
	{
		FWallTypeDef& Def = WallTypes.AddDefaulted_GetRef();
		Def.DisplayName = FText::FromString(Name);
		Def.MaxHP = HP;
		Def.Cost = Cost;
		Def.CostGrowth = CostGrowth;
		Def.Color = Color;
		Def.NoiseDamping = NoiseDamping;
		Def.SoundHP = SoundHP;
		Def.ClimbSeconds = ClimbSeconds;
		Def.bLadder = bLadder;
	};
	AddWall(TEXT("通常壁"), 3.0, 15.0, 1.12, FLinearColor(0.55f, 0.35f, 0.18f), 0.f, 0.0, 1.f, false);
	AddWall(TEXT("消音壁"), 3.0, 150.0, 1.5, FLinearColor(0.85f, 0.8f, 0.65f), 0.8f, 60.0, 1.f, false); // M14：連打の音が館の大きさで小さくなったので、安く・長持ちに
	AddWall(TEXT("クライミング壁"), 1.5, 40.0, 1.15, FLinearColor(0.95f, 0.55f, 0.2f), 0.f, 0.0, 0.3f, true);

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
	AddPrestigeUpgrade(TEXT("お宝の宝石"), 0, 2.0, 1.6, 1.0, 1.0);    // Treasure: お宝 1 個の宝石 1 + Lv（M15：お宝はコインではなく宝石に）
	AddPrestigeUpgrade(TEXT("煙幕ダッシュ"), 4, 2.0, 1.6, 1.0, 2.0);   // SmokeDuration: Lv1 で解放。煙が残る時間 1 + 2×(Lv-1) 秒
	AddPrestigeUpgrade(TEXT("煙幕の数"), 3, 2.0, 2.0, 1.0, 1.0);       // SmokeCount: 1 ラウンドに 1 + Lv 回
	AddPrestigeUpgrade(TEXT("よじ登る"), 1, 2.0, 1.0, 1.0, 1.0);       // Climb: Lv1 で解放
	AddPrestigeUpgrade(TEXT("消音壁の丈夫さ"), 0, 1.0, 1.5, 1.0, 1.5); // QuietHP: 音を消せる回数 ×1.5^Lv
	AddPrestigeUpgrade(TEXT("連打のお金アップ"), 0, 1.0, 1.5, 1.0, 1.5); // MashIncome: 連打のコイン ×1.5^Lv
	AddPrestigeUpgrade(TEXT("時間のお金アップ"), 0, 1.0, 1.5, 1.0, 1.5); // TimeIncome: 時間のコイン ×1.5^Lv
	check(PrestigeUpgrades.Num() == static_cast<int32>(EPrestigeUpgrade::Count));

	// 鬼の種類の既定値（Data/OniTypes.csv が読めなかったときに使う）
	auto AddOniType = [this](EOniType Type, const TCHAR* Name, float Speed, double Damage, float Sight, float Angle, float Hearing,
		bool bSingle, float Radius, float Windup, float Pocket, FLinearColor Color, float Body, float DecoyHearing, float Stun, bool bDisarm,
		float StepHearing, float Summon)
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
		Row.DecoyHearingScale = DecoyHearing;
		Row.StunScale = Stun;
		Row.bDisarmTraps = bDisarm;
		Row.StepHearingScale = StepHearing;
		Row.SummonRadius = Summon;
	};
	// スピード鬼: 音に敏感（プレイヤーの音は遠くから聞こえ、おとりにはだまされない）・トリモチに弱い
	// パワー鬼: 連打は気にしないが、おとりには遠くから寄っていく・トリモチに強い（すぐ抜け出し、直後に踏んだトリモチは壊す）
	// 宝物鬼: お宝の周りを回る。壁を壊す力が弱く、空洞も調べない。連打・おとりには一切反応しない代わりに足音にとても敏感（M14）
	// 探知鬼: 見張りの場所から動かず、見つけたら周りの鬼を呼ぶ。音は聞かない
	AddOniType(EOniType::Balanced, TEXT("標準鬼"), 1.0f, 1.0, 900.f, 40.f, 1.f, false, 160.f, 0.8f, 0.5f, FLinearColor(0.9f, 0.1f, 0.08f), 1.0f, 1.f, 1.f, false, 0.6f, 0.f);
	AddOniType(EOniType::Scout, TEXT("スピード鬼"), 1.25f, 0.5, 1300.f, 55.f, 1.5f, false, 160.f, 0.8f, 0.f, FLinearColor(0.1f, 0.75f, 0.95f), 0.9f, 0.f, 1.5f, false, 1.f, 0.f);
	// M15：パワー鬼は目と耳（おとり）を弱く・溜めを長くした代わりに、前方 2×2×2 マスの壁にまとめて当たる
	AddOniType(EOniType::Breaker, TEXT("パワー鬼"), 0.65f, 2.5, 550.f, 45.f, 0.f, false, 200.f, 1.8f, 0.5f, FLinearColor(1.f, 0.5f, 0.05f), 1.2f, 1.25f, 0.4f, true, 0.f, 0.f);
	OniTypeRows[EOniType::Breaker].AttackBoxWidth = 2;
	OniTypeRows[EOniType::Breaker].AttackBoxDepth = 2;
	OniTypeRows[EOniType::Breaker].AttackBoxLevels = 2;
	AddOniType(EOniType::Careful, TEXT("慎重鬼"), 1.15f, 1.0, 900.f, 40.f, 1.f, true, 160.f, 0.6f, 1.f, FLinearColor(0.95f, 0.4f, 0.75f), 1.0f, 1.f, 1.f, false, 0.6f, 0.f);
	AddOniType(EOniType::Treasure, TEXT("宝物鬼"), 0.9f, 0.4, 900.f, 45.f, 0.f, false, 140.f, 1.0f, 0.f, FLinearColor(1.f, 0.85f, 0.2f), 1.0f, 0.f, 1.f, false, 3.0f, 0.f);
	AddOniType(EOniType::Detector, TEXT("探知鬼"), 0.9f, 1.0, 1500.f, 32.f, 0.f, false, 160.f, 0.8f, 0.f, FLinearColor(0.6f, 0.3f, 1.f), 1.1f, 0.f, 1.f, false, 0.f, 2400.f);
	// 勘：スピード鬼は低め・慎重鬼は高め・ほかは普通（0.3）
	OniTypeRows[EOniType::Scout].Intuition = 0.1f;
	OniTypeRows[EOniType::Careful].Intuition = 0.6f;
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

	// 鬼のチュートリアル・図鑑の写真を撮るスタジオ（舞台のずっと下）
	OniStudio = GetWorld()->SpawnActor<AKakurenboOniStudio>();
	if (OniStudio)
	{
		OniStudio->Setup(this);
	}

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
		for (int32 i = 0; i < WallTypes.Num(); ++i)
		{
			Grid->SetWallSurface(i, FindSurface(WallTypes[i].Surface)); // 通常壁・消音壁・クライミング壁の模様
			Grid->SetWallLadder(i, WallTypes[i].bLadder);              // クライミング壁は足場の横木
		}
	}

	// プレイヤーの見た目（Fab のキャラクター。無ければ円柱）
	ApplyPlayerLookTo(Cast<AHiderCharacter>(GetPlayerCharacter()));

	// セーブがあれば読み込んでおく（タイトルの後ろにその館が見え、「つづきから」で設置パートから再開）。
	// 無ければステージ 1 のマップ（「はじめから」で何もない空間で連打から始める）
	bHasContinueData = LoadProgress();
	if (!bHasContinueData)
	{
		SwitchToMap(GetMapNameForStage(1));
	}
	if (bStartAtTitle && !FParse::Param(FCommandLine::Get(), TEXT("KakuSkipTitle")))
	{
		EnterTitle();
	}
	else if (bHasContinueData)
	{
		ContinueGame();
	}
	else
	{
		StartNewGame();
	}
}

void AKakurenboGameMode::ApplyPlayerLookTo(AHiderCharacter* Hider, const FLinearColor& Tint)
{
	if (!Hider)
	{
		return;
	}
	if (USkeletalMesh* PlayerMesh = UKakurenboLibrary::LoadIfExists(PlayerSkeletalMesh))
	{
		Hider->ApplyLook(PlayerMesh, MakeSurfaceMaterial(PlayerSurface, Tint), PlayerMeshYaw);
		// 公式のアニメーション（無ければプログラムで作った簡単な動きのまま）
		Hider->ApplyOfficialAnimation(UKakurenboLibrary::LoadIfExists(PlayerAnimSourceMesh), UKakurenboLibrary::LoadIfExists(PlayerAnimMove), UKakurenboLibrary::LoadIfExists(PlayerAnimFall));
	}
}

void AKakurenboGameMode::RegisterFakeHiderCollision(AHiderCharacter* Fake)
{
	if (!Fake)
	{
		return;
	}
	if (ACharacter* Player = GetPlayerCharacter())
	{
		Player->GetCapsuleComponent()->IgnoreActorWhenMoving(Fake, true);
		Fake->GetCapsuleComponent()->IgnoreActorWhenMoving(Player, true);
	}
	for (AOniCharacter* Oni : Onis)
	{
		if (Oni)
		{
			Oni->GetCapsuleComponent()->IgnoreActorWhenMoving(Fake, true);
			Fake->GetCapsuleComponent()->IgnoreActorWhenMoving(Oni, true);
		}
	}
	if (const UKakurenboGridSubsystem* Grid = GetWorld()->GetSubsystem<UKakurenboGridSubsystem>())
	{
		for (ATrapActor* Trap : Grid->GetAllTraps())
		{
			if (Trap && Trap->GetKind() == ETrapKind::OneWay)
			{
				Fake->GetCapsuleComponent()->IgnoreActorWhenMoving(Trap, true);
			}
		}
	}
}

// ---------------------------------------------------------------- タイトル

void AKakurenboGameMode::EnterTitle()
{
	DespawnOnis();
	ClearTreasures();
	SetPhase(EKakurenboPhase::Title);
}

void AKakurenboGameMode::StartNewGame()
{
	// セーブを消して、ステージ 1 の「何もない空間で連打」から
	const bool bHadProgress = bHasContinueData;
	bHasContinueData = false;
	ResetProgress(bHadProgress);
}

void AKakurenboGameMode::ContinueGame()
{
	if (GS())
	{
		ShowNotice(FText::Format(NSLOCTEXT("Kakurenbo", "NoticeLoaded", "つづきから（ステージ {0}）"), GS()->Stage), 4.f);
	}
	StartBuildPhase();
}

void AKakurenboGameMode::LeaveTitleForTest()
{
	if (GetPhase() != EKakurenboPhase::Title)
	{
		return; // もう遊んでいる（-KakuSkipTitle）
	}
	if (bHasContinueData)
	{
		ContinueGame();
	}
	else
	{
		StartNewGame();
	}
}

void AKakurenboGameMode::ReturnToTitle()
{
	AKakurenboGameState* State = GS();
	if (!State || State->Phase == EKakurenboPhase::Title)
	{
		return;
	}
	if (State->Phase == EKakurenboPhase::Hide)
	{
		// かくれんぼの途中でやめたら、そのラウンドは無かったことにする（始めたときに保存した状態へ戻す）
		DespawnOnis();
		ClearTreasures();
		SetMansionDark(false, DarkSightRadius);
		DarumaTimeLeft = 0.f;
		bGoldRushThisRound = false;
		LoadProgress();
	}
	else
	{
		SaveProgress();
	}
	bHasContinueData = true; // セーブが無くても（-KakuNoSave）、この起動の間は続きから遊べる
	EnterTitle();
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
		ShowPopup(TEXT("鬼が来る…"), FLinearColor(1.f, 0.3f, 0.25f));
	}

	// 時間収入（毎フレーム、経過時間ぶんだけ加算）
	State->AddCoins(GetTimeIncomePerSecond() * DeltaSeconds, true);
	State->TimeCoinsThisRound += GetTimeIncomePerSecond() * DeltaSeconds;

	State->HideTimeRemaining -= DeltaSeconds;
	TickCountdownSounds();
	TickDangerHeartbeat(DeltaSeconds);
	TickDaruma(DeltaSeconds);
	if (State->HideTimeRemaining <= 0.f)
	{
		State->HideTimeRemaining = 0.f;
		EndHidePhase(true);
	}
}

float AKakurenboGameMode::ComputeDanger() const
{
	// 鬼ごとの「危なさ」の一番大きいもの。近いほど、こちらへ向かっている（音を調べに来る・追いかけてくる）ほど大きい
	const ACharacter* Player = GetPlayerCharacter();
	if (!Player)
	{
		return 0.f;
	}
	float Danger = 0.f;
	for (const AOniCharacter* Oni : Onis)
	{
		if (!Oni || !Oni->IsActive() || Oni->GetOniState() == EOniState::Stunned)
		{
			continue;
		}
		const float Dist = FVector::Dist2D(Oni->GetActorLocation(), Player->GetActorLocation());
		const float Near = 1.f - FMath::Clamp((Dist - DangerFullDistance) / FMath::Max(DangerMaxDistance - DangerFullDistance, 1.f), 0.f, 1.f);
		float OniDanger = 0.f;
		switch (Oni->GetIntent())
		{
		case EOniState::Chase:       OniDanger = Oni->IsChasingPlayer() ? FMath::Max(Near, 0.6f) : Near * 0.6f; break; // 追われていたら遠くても速い（身代わり人形を追っているなら普通）
		case EOniState::Investigate: OniDanger = Near * 0.85f; break;
		default:                     OniDanger = Near * 0.6f; break;
		}
		Danger = FMath::Max(Danger, OniDanger);
	}
	return Danger;
}

float AKakurenboGameMode::ComputeOniCloseness() const
{
	// 一番近い鬼の近さ（0〜1）。様子は関係なく、距離だけで決める（心臓の音の大きさ）
	const ACharacter* Player = GetPlayerCharacter();
	if (!Player)
	{
		return 0.f;
	}
	float Closeness = 0.f;
	for (const AOniCharacter* Oni : Onis)
	{
		if (!Oni || !Oni->IsActive() || Oni->GetOniState() == EOniState::Stunned)
		{
			continue;
		}
		const float Dist = FVector::Dist2D(Oni->GetActorLocation(), Player->GetActorLocation());
		Closeness = FMath::Max(Closeness, 1.f - FMath::Clamp((Dist - DangerFullDistance) / FMath::Max(HeartbeatMaxDistance - DangerFullDistance, 1.f), 0.f, 1.f));
	}
	return Closeness;
}

void AKakurenboGameMode::TickDangerHeartbeat(float DeltaSeconds)
{
	// 鬼が近い・向かってくると「ドクン」と鳴る。画面を見ていなくても危ないとわかる。
	// M15：大きさは一番近い鬼との距離だけで決める（近いほど大きい。ユーザーの依頼）。速さは様子（向かってくる・追われている）も入れた危なさ
	CurrentDanger = ComputeDanger();
	CurrentCloseness = ComputeOniCloseness();
	HeartbeatTimer -= DeltaSeconds;
	if ((CurrentDanger < HeartbeatMinDanger && CurrentCloseness < HeartbeatMinCloseness) || HeartbeatTimer > 0.f)
	{
		return;
	}
	HeartbeatTimer = FMath::Lerp(HeartbeatSlowInterval, HeartbeatFastInterval, FMath::Max(CurrentDanger, CurrentCloseness * 0.8f));
	++HeartbeatCount;
	// 遠いと小さく、すぐそばではとても大きく（近さの 1.3 乗で上げる：近づくほど急に大きくなる）
	LastHeartbeatVolume = FMath::Lerp(HeartbeatQuietVolume, HeartbeatLoudVolume, FMath::Pow(CurrentCloseness, 1.3f));
	Sfx2D(this, EKakurenboSfx::Heartbeat, LastHeartbeatVolume);
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
		// 壁の補強：効果は足し算（BaseValue + ValueGrowth × レベル。元の耐久に掛ける）
		if (const FKakurenboUpgradeRow* Wall = Table->FindRow<FKakurenboUpgradeRow>(TEXT("Wall"), TEXT("Upgrades"), false))
		{
			WallReinforceName = Wall->DisplayName;
			WallReinforceBaseCost = Wall->BaseCost;
			WallReinforceCostGrowth = Wall->CostGrowth;
			WallReinforceBase = Wall->BaseValue;
			WallReinforcePerLevel = Wall->ValueGrowth;
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

	if (UDataTable* Table = GetTable(MapTable, FKakurenboMapRow::StaticStruct(), TEXT("Maps.csv")))
	{
		// 行の順番＝表より後のステージで回る順番
		MapRows.Reset();
		MapOrder.Reset();
		for (const FName& Name : Table->GetRowNames())
		{
			if (const FKakurenboMapRow* Row = Table->FindRow<FKakurenboMapRow>(Name, TEXT("Maps")))
			{
				MapRows.Add(Name, *Row);
				MapOrder.Add(Name);
			}
		}
	}

	if (UDataTable* Table = GetTable(FurnitureTable, FKakurenboFurnitureRow::StaticStruct(), TEXT("Furniture.csv")))
	{
		// 行の名前の 1 文字目が間取りの文字
		FurnitureRows.Reset();
		for (const TPair<FName, uint8*>& Pair : Table->GetRowMap())
		{
			const FString Key = Pair.Key.ToString();
			if (Key.Len() != 1)
			{
				UE_LOG(LogKakurenbo, Warning, TEXT("Furniture.csv: row name '%s' must be 1 character"), *Key);
				continue;
			}
			FurnitureRows.Add(Key[0], *reinterpret_cast<const FKakurenboFurnitureRow*>(Pair.Value));
		}
	}

	if (UDataTable* Table = GetTable(SurfaceTable, FKakurenboSurfaceRow::StaticStruct(), TEXT("Surfaces.csv")))
	{
		SurfaceRows.Reset();
		for (const TPair<FName, uint8*>& Pair : Table->GetRowMap())
		{
			SurfaceRows.Add(Pair.Key, *reinterpret_cast<const FKakurenboSurfaceRow*>(Pair.Value));
		}
	}

	if (UDataTable* Table = GetTable(DecorTable, FKakurenboDecorRow::StaticStruct(), TEXT("Decor.csv")))
	{
		TArray<FKakurenboDecorRow*> Rows;
		Table->GetAllRows<FKakurenboDecorRow>(TEXT("Decor"), Rows);
		DecorRows.Reset();
		for (const FKakurenboDecorRow* Row : Rows)
		{
			DecorRows.Add(*Row);
		}
	}

	if (UDataTable* Table = GetTable(FloorKindTable, FKakurenboFloorKindRow::StaticStruct(), TEXT("FloorKinds.csv")))
	{
		// 行の名前の 1 文字目が [floor] の文字（大文字と小文字は区別しないので、小文字で書く）
		FloorKindRows.Reset();
		for (const TPair<FName, uint8*>& Pair : Table->GetRowMap())
		{
			const FString Key = Pair.Key.ToString();
			if (Key.Len() == 1)
			{
				FloorKindRows.Add(FChar::ToLower(Key[0]), *reinterpret_cast<const FKakurenboFloorKindRow*>(Pair.Value));
			}
		}
	}
	UE_LOG(LogKakurenbo, Log, TEXT("Balance data: %d stages, %d wall types, %d oni types, %d trap types, %d maps, %d furniture, mash %.2f x%.2f (cost %.1f x%.2f)"),
		StageRows.Num(), WallTypes.Num(), OniTypeRows.Num(), TrapTypes.Num(), MapRows.Num(), FurnitureRows.Num(), MashIncomeBase, MashIncomeGrowth, MashUpgradeBaseCost, MashUpgradeCostGrowth);
}

UMaterialInstanceDynamic* AKakurenboGameMode::MakeSurfaceMaterial(FName Name, const FLinearColor& Tint)
{
	const FKakurenboSurfaceRow* Surface = FindSurface(Name);
	return Surface ? UKakurenboLibrary::CreateSurfaceMaterial(this, *Surface, Tint) : nullptr;
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
	return GetMashIncomeAtLevel(State ? State->MashIncomeLevel : 0) * (bGoldRushThisRound ? GoldRushMashMultiplier : 1.0);
}

double AKakurenboGameMode::GetMashIncomeAtLevel(int32 Level) const
{
	return UKakurenboLibrary::ExpCurve(MashIncomeBase, MashIncomeGrowth, Level) * GetPrestigeValue(EPrestigeUpgrade::MashIncome);
}

double AKakurenboGameMode::GetTimeIncomePerSecond() const
{
	const AKakurenboGameState* State = GS();
	return GetTimeIncomeAtLevel(State ? State->TimeIncomeLevel : 0);
}

double AKakurenboGameMode::GetTimeIncomeAtLevel(int32 Level) const
{
	return UKakurenboLibrary::ExpCurve(TimeIncomeBase, TimeIncomeGrowth, Level) * GetPrestigeValue(EPrestigeUpgrade::TimeIncome);
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

double AKakurenboGameMode::GetWallReinforceFactor(int32 Level) const
{
	// 足し算で伸びる（1 周の中の強化）。かけ算で伸びる鬼の攻撃力（×1.6/ステージ）にはだんだん追いつけなくなる → 転生して「壁の硬さ」（かけ算）
	return FMath::Max(0.01, WallReinforceBase + WallReinforcePerLevel * FMath::Max(0, Level));
}

double AKakurenboGameMode::GetWallReinforceFactor() const
{
	const AKakurenboGameState* State = GS();
	return GetWallReinforceFactor(State ? State->WallReinforceLevel : 0);
}

double AKakurenboGameMode::GetWallReinforceCost() const
{
	const AKakurenboGameState* State = GS();
	return UKakurenboLibrary::ExpCurve(WallReinforceBaseCost, WallReinforceCostGrowth, State ? State->WallReinforceLevel : 0);
}

bool AKakurenboGameMode::TryBuyWallReinforce()
{
	AKakurenboGameState* State = GS();
	const double Cost = GetWallReinforceCost();
	if (!State || State->Phase != EKakurenboPhase::Shop || State->Coins < Cost)
	{
		return false;
	}
	const double Before = GetWallReinforceFactor();
	State->Coins -= Cost;
	State->WallReinforceLevel++;
	// 置いてある壁にもすぐ効かせる（傷の割合はそのまま）
	if (UKakurenboGridSubsystem* Grid = GetWorld()->GetSubsystem<UKakurenboGridSubsystem>())
	{
		Grid->ScaleAllBlockHP(GetWallReinforceFactor() / FMath::Max(Before, 0.0001));
	}
	return true;
}

bool AKakurenboGameMode::IsWallTypeAvailable(int32 WallTypeIndex) const
{
	return WallTypes.IsValidIndex(WallTypeIndex) && (!WallTypes[WallTypeIndex].bLadder || IsClimbUnlocked());
}

int32 AKakurenboGameMode::GetHitsToBreak(double HP) const
{
	return FMath::Max(1, FMath::CeilToInt(HP / FMath::Max(GetOniAttackDamage(), 0.0001)));
}

float AKakurenboGameMode::GetClimbSeconds(int32 WallTypeIndex) const
{
	return WallTypes.IsValidIndex(WallTypeIndex) ? FMath::Max(0.05f, WallTypes[WallTypeIndex].ClimbSeconds) : FurnitureClimbSeconds;
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
			Item.Description = FText::Format(NSLOCTEXT("Kakurenbo", "PrestigeWallDesc", "すべての壁の耐久 ×{0} → ×{1}（かけ算。コインの「壁の補強」の上に掛かる・置いてある壁も）"), Fmt(Now), Fmt(Next));
			break;
		case EPrestigeUpgrade::Treasure:
			Item.Description = FText::Format(NSLOCTEXT("Kakurenbo", "PrestigeTreasureDesc", "お宝 1 個でもらえる宝石 {0} → {1} 個・全部取ったボーナス {2} → {3} 個"),
				GetGemsPerTreasure(Level), GetGemsPerTreasure(Level + 1), GetAllTreasureBonusGems(Level), GetAllTreasureBonusGems(Level + 1));
			break;
		case EPrestigeUpgrade::SmokeDuration:
			Item.Description = Item.bMaxed ? FText::Format(NSLOCTEXT("Kakurenbo", "PrestigeSmokeTimeMax", "煙幕が残る時間 {0} 秒"), Fmt(GetSmokeDuration(Level)))
				: Level == 0 ? FText::Format(NSLOCTEXT("Kakurenbo", "PrestigeDashUnlock", "Shift で煙幕を投げて走る（煙の向こうは鬼から見えない・煙 {0} 秒・大きな音）"), Fmt(GetSmokeDuration(1)))
				: FText::Format(NSLOCTEXT("Kakurenbo", "PrestigeSmokeTimeDesc", "煙幕が残る時間 {0} 秒 → {1} 秒"), Fmt(GetSmokeDuration(Level)), Fmt(GetSmokeDuration(Level + 1)));
			break;
		case EPrestigeUpgrade::SmokeCount:
			Item.Description = Item.bMaxed ? FText::Format(NSLOCTEXT("Kakurenbo", "PrestigeSmokeMax", "煙幕ダッシュ 1 ラウンドに {0} 回"), GetSmokeUsesPerRound(Level))
				: FText::Format(NSLOCTEXT("Kakurenbo", "PrestigeSmokeDesc", "煙幕ダッシュ 1 ラウンドに {0} 回 → {1} 回"), GetSmokeUsesPerRound(Level), GetSmokeUsesPerRound(Level + 1));
			if (!IsDashUnlocked())
			{
				Item.Description = FText::Format(NSLOCTEXT("Kakurenbo", "PrestigeNeedDash", "{0}（煙幕ダッシュを買うと使える）"), Item.Description);
			}
			break;
		case EPrestigeUpgrade::Climb:
			Item.Description = NSLOCTEXT("Kakurenbo", "PrestigeClimbDesc", "壁・低い家具に 1 段ずつよじ登れる（隙間は越えられない）。クライミング壁が買えるようになる");
			break;
		case EPrestigeUpgrade::QuietHP:
		{
			// 一番丈夫な消音壁の「音を消せる回数」で見せる
			double BaseSoundHP = 0.0;
			for (const FWallTypeDef& Def : WallTypes)
			{
				BaseSoundHP = FMath::Max(BaseSoundHP, Def.SoundHP);
			}
			Item.Description = FText::Format(NSLOCTEXT("Kakurenbo", "PrestigeQuietDesc", "消音壁が音を消せる回数 {0} → {1} 回（置いてある壁も）"),
				FMath::RoundToInt(BaseSoundHP * Now), FMath::RoundToInt(BaseSoundHP * Next));
			break;
		}
		case EPrestigeUpgrade::MashIncome:
			Item.Description = FText::Format(NSLOCTEXT("Kakurenbo", "PrestigeMashDesc", "連打で稼げるコイン ×{0} → ×{1}（コインの「連打コイン強化」の上に掛かる）"), Fmt(Now), Fmt(Next));
			break;
		case EPrestigeUpgrade::TimeIncome:
			Item.Description = FText::Format(NSLOCTEXT("Kakurenbo", "PrestigeTimeDesc", "時間で稼げるコイン ×{0} → ×{1}（コインの「時間コイン強化」の上に掛かる）"), Fmt(Now), Fmt(Next));
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
		const double OldValue = GetPrestigeValue(Upgrade);
		State->PrestigePoints -= Cost;
		State->PrestigeLevels.SetNum(static_cast<int32>(EPrestigeUpgrade::Count));
		State->PrestigeLevels[Index]++;
		bBought = true;

		// 置いてある壁にもすぐ効かせる
		UKakurenboGridSubsystem* Grid = GetWorld()->GetSubsystem<UKakurenboGridSubsystem>();
		const double Factor = GetPrestigeValue(Upgrade) / FMath::Max(OldValue, 0.0001);
		if (Grid && Upgrade == EPrestigeUpgrade::WallHP)
		{
			Grid->ScaleAllBlockHP(Factor);
		}
		else if (Grid && Upgrade == EPrestigeUpgrade::QuietHP)
		{
			Grid->ScaleAllBlockSoundHP(Factor);
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
	// 煙幕ダッシュ・よじ登るは最初は使えない。転生のお店で解放する。
	// かくれんぼの途中で回数が増えたら（テストなど）、増えた分だけ今のラウンドでも使えるようにする
	const int32 OldUses = Hider->bDashUnlocked ? Hider->DashUsesPerRound : 0;
	Hider->bDashUnlocked = IsDashUnlocked();
	Hider->DashUsesPerRound = GetSmokeUsesPerRound();
	const int32 NewUses = Hider->bDashUnlocked ? Hider->DashUsesPerRound : 0;
	Hider->AddDashUses(NewUses - OldUses);
	Hider->bClimbUnlocked = IsClimbUnlocked();
	Hider->JumpMaxCount = 0; // ジャンプは無い（M14：壁から壁へ跳んで逃げられないように。よじ登るだけ）
}

int32 AKakurenboGameMode::GetSmokeUsesPerRound(int32 Level) const
{
	const int32 Index = static_cast<int32>(EPrestigeUpgrade::SmokeCount);
	if (!PrestigeUpgrades.IsValidIndex(Index))
	{
		return 1 + Level;
	}
	const FKakurenboPrestigeUpgradeRow& Row = PrestigeUpgrades[Index];
	// 古いセーブの「ダッシュの回復」のレベル（最大 6）を引き継いでも、最大レベルを超えないようにする
	const int32 ClampedLevel = Row.MaxLevel > 0 ? FMath::Min(Level, Row.MaxLevel) : Level;
	return FMath::Max(0, FMath::RoundToInt(Row.BaseValue + Row.ValueGrowth * ClampedLevel));
}

float AKakurenboGameMode::GetSmokeDuration(int32 Level) const
{
	// 「煙幕ダッシュ」の行も足し算：Lv1 で BaseValue 秒、1 Lv ごとに ValueGrowth 秒のびる（1〜7 秒に収める）
	const int32 Index = static_cast<int32>(EPrestigeUpgrade::SmokeDuration);
	float Seconds = 1.f + 2.f * (FMath::Max(Level, 1) - 1);
	if (PrestigeUpgrades.IsValidIndex(Index))
	{
		const FKakurenboPrestigeUpgradeRow& Row = PrestigeUpgrades[Index];
		const int32 ClampedLevel = Row.MaxLevel > 0 ? FMath::Min(FMath::Max(Level, 1), Row.MaxLevel) : FMath::Max(Level, 1);
		Seconds = static_cast<float>(Row.BaseValue + Row.ValueGrowth * (ClampedLevel - 1));
	}
	return FMath::Clamp(Seconds, MinSmokeDuration, MaxSmokeDuration);
}

TArray<FWallTypeDef> AKakurenboGameMode::GetEffectiveWallTypes() const
{
	// 耐久 = 元の耐久 × 壁の補強（足し算で伸びる）× 壁の硬さ（転生。かけ算で伸びる）
	TArray<FWallTypeDef> Types = WallTypes;
	const double Multiplier = GetWallReinforceFactor() * GetWallHPMultiplier();
	const double SoundMultiplier = GetPrestigeValue(EPrestigeUpgrade::QuietHP);
	for (FWallTypeDef& Def : Types)
	{
		Def.MaxHP *= Multiplier;
		Def.SoundHP *= SoundMultiplier; // 消音壁の丈夫さ
	}
	return Types;
}

int32 AKakurenboGameMode::GetOwnedWallCount(int32 WallTypeIndex) const
{
	const AKakurenboGameState* State = GS();
	const UKakurenboGridSubsystem* Grid = GetWorld()->GetSubsystem<UKakurenboGridSubsystem>();
	const int32 Stock = (State && State->WallStock.IsValidIndex(WallTypeIndex)) ? State->WallStock[WallTypeIndex] : 0;
	return Stock + (Grid ? Grid->GetLiveBlockCount(WallTypeIndex) : 0);
}

double AKakurenboGameMode::GetWallCost(int32 WallTypeIndex) const
{
	if (!WallTypes.IsValidIndex(WallTypeIndex))
	{
		return 0.0;
	}
	const FWallTypeDef& Def = WallTypes[WallTypeIndex];
	return UKakurenboLibrary::ExpCurve(Def.Cost, Def.CostGrowth, GetOwnedWallCount(WallTypeIndex));
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

int32 AKakurenboGameMode::GetAllTreasureBonusGems(int32 Level) const
{
	// ユーザーの希望：お宝の強化で全取得ボーナスも増える（お宝 1 個の宝石が 1 → 2 → 3 なら、ボーナスは 2 → 4 → 6）
	const int32 Base = FMath::Max(1, GetGemsPerTreasure(0));
	return FMath::RoundToInt(static_cast<double>(AllTreasureBonusGems) * GetGemsPerTreasure(Level) / Base);
}

int32 AKakurenboGameMode::GetGemsPerTreasure(int32 Level) const
{
	// 転生のお店の「お宝の宝石」：足し算（BaseValue + ValueGrowth × レベル）。GemsPerTreasure は Lv0 の数
	const int32 Index = static_cast<int32>(EPrestigeUpgrade::Treasure);
	if (!PrestigeUpgrades.IsValidIndex(Index))
	{
		return GemsPerTreasure + Level;
	}
	const FKakurenboPrestigeUpgradeRow& Row = PrestigeUpgrades[Index];
	return FMath::Max(1, GemsPerTreasure - 1 + FMath::RoundToInt(Row.BaseValue + Row.ValueGrowth * Level));
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
	return GetStageSettings().OniHearingRadius * GetMapNoiseScale();
}

float AKakurenboGameMode::GetMapNoiseScale() const
{
	const FKakurenboMapRow* Row = MapRows.Find(CurrentMapName);
	const UKakurenboGridSubsystem* Grid = GetWorld() ? GetWorld()->GetSubsystem<UKakurenboGridSubsystem>() : nullptr;
	if (!Row || Row->LayoutFile.IsEmpty() || !Grid || !Grid->IsConfigured())
	{
		return 1.f;
	}
	const int32 Side = FMath::Max(Grid->GetSizeX(), Grid->GetSizeY());
	const float Alpha = FMath::Clamp(static_cast<float>(Side - MapNoiseSmallSide) / FMath::Max(1, MapNoiseLargeSide - MapNoiseSmallSide), 0.f, 1.f);
	return FMath::Lerp(MapNoiseScaleSmall, 1.f, Alpha);
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

// 商品の並び: [0] 連打強化, [1] 時間収入強化, [2] 壁の補強, [3〜] 壁, その後に罠
namespace KakurenboShop
{
	constexpr int32 MashUpgrade = 0;
	constexpr int32 TimeUpgrade = 1;
	constexpr int32 WallReinforce = 2;
	constexpr int32 NumUpgrades = 3;
}

int32 AKakurenboGameMode::GetShopIndexOfWall(int32 WallTypeIndex) const
{
	return KakurenboShop::NumUpgrades + WallTypeIndex;
}

int32 AKakurenboGameMode::GetShopIndexOfTrap(int32 TrapTypeIndex) const
{
	// コインのお店に並ぶのはコインで買う罠だけ（宝石の道具は宝石のお店）
	const int32 Position = GetTrapTypesInShop(false).IndexOfByKey(TrapTypeIndex);
	return Position == INDEX_NONE ? INDEX_NONE : KakurenboShop::NumUpgrades + WallTypes.Num() + Position;
}

TArray<int32> AKakurenboGameMode::GetTrapTypesInShop(bool bGem) const
{
	TArray<int32> Types;
	for (int32 i = 0; i < TrapTypes.Num(); ++i)
	{
		if (TrapTypes[i].bGemItem == bGem)
		{
			Types.Add(i);
		}
	}
	return Types;
}

int32 AKakurenboGameMode::GetGemShopTrapType(int32 Index) const
{
	const TArray<int32> Types = GetTrapTypesInShop(true);
	return Types.IsValidIndex(Index) ? Types[Index] : INDEX_NONE;
}

int32 AKakurenboGameMode::GetGemShopIndexOfTrap(int32 TrapTypeIndex) const
{
	return GetTrapTypesInShop(true).IndexOfByKey(TrapTypeIndex);
}

int32 AKakurenboGameMode::FindTrapTypeOfKind(ETrapKind Kind) const
{
	return TrapTypes.IndexOfByPredicate([Kind](const FKakurenboTrapRow& Row) { return Row.Kind == Kind; });
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
	auto Hits = [this](double HP) { return GetHitsToBreak(HP); };

	{
		FShopItemView& Item = Items.AddDefaulted_GetRef();
		Item.DisplayName = MashUpgradeName;
		Item.Cost = GetMashUpgradeCost();
		Item.Description = FText::Format(NSLOCTEXT("Kakurenbo", "ShopMashDesc", "1回 {0} → {1} コイン"),
			Fmt(GetMashIncomeAtLevel(State->MashIncomeLevel)), Fmt(GetMashIncomeAtLevel(State->MashIncomeLevel + 1)));
		Item.OwnedText = FText::Format(NSLOCTEXT("Kakurenbo", "ShopLevel", "Lv.{0}"), State->MashIncomeLevel);
	}
	{
		FShopItemView& Item = Items.AddDefaulted_GetRef();
		Item.DisplayName = TimeUpgradeName;
		Item.Cost = GetTimeUpgradeCost();
		Item.Description = FText::Format(NSLOCTEXT("Kakurenbo", "ShopTimeDesc", "毎秒 {0} → {1} コイン"),
			Fmt(GetTimeIncomeAtLevel(State->TimeIncomeLevel)), Fmt(GetTimeIncomeAtLevel(State->TimeIncomeLevel + 1)));
		Item.OwnedText = FText::Format(NSLOCTEXT("Kakurenbo", "ShopLevel", "Lv.{0}"), State->TimeIncomeLevel);
	}
	{
		// 壁の補強：通常壁（一番上の行）の耐久で見せる。鬼の攻撃何回で壊れるかも（数字が増えて強くなる手応え）
		FShopItemView& Item = Items.AddDefaulted_GetRef();
		Item.DisplayName = WallReinforceName;
		Item.Cost = GetWallReinforceCost();
		const double BaseHP = WallTypes.Num() > 0 ? WallTypes[0].MaxHP * GetWallHPMultiplier() : 1.0;
		const double Now = BaseHP * GetWallReinforceFactor(State->WallReinforceLevel);
		const double Next = BaseHP * GetWallReinforceFactor(State->WallReinforceLevel + 1);
		Item.Description = FText::Format(NSLOCTEXT("Kakurenbo", "ShopWallReinforceDesc", "{0}の耐久 {1} → {2}（今の鬼の攻撃 {3} → {4} 回）・すべての壁が +{5}%"),
			WallTypes.Num() > 0 ? WallTypes[0].DisplayName : FText::GetEmpty(), Fmt(Now), Fmt(Next), Hits(Now), Hits(Next),
			FMath::RoundToInt(WallReinforcePerLevel * 100.0));
		Item.OwnedText = FText::Format(NSLOCTEXT("Kakurenbo", "ShopLevel", "Lv.{0}"), State->WallReinforceLevel);
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
		Item.Cost = GetWallCost(i);
		Item.Description = FText::Format(NSLOCTEXT("Kakurenbo", "ShopWallDesc", "耐久 {0}（今の鬼の攻撃 {1} 回で壊れる）"),
			Fmt(Def.MaxHP), Hits(Def.MaxHP));
		if (Def.NoiseDamping > 0.f)
		{
			Item.Description = FText::Format(NSLOCTEXT("Kakurenbo", "ShopQuietWallDesc", "{0}　囲むと中の音 −{1}%（音を {2} 回消すと壊れる）"),
				Item.Description, FMath::RoundToInt(Def.NoiseDamping * 100.f), FMath::RoundToInt(Def.SoundHP));
		}
		if (Def.bLadder)
		{
			Item.Description = FText::Format(NSLOCTEXT("Kakurenbo", "ShopLadderWallDesc", "{0}　すばやくよじ登れる（{1} 秒）・鬼は飛び乗れない"), Item.Description, Fmt(Def.ClimbSeconds));
		}
		if (!IsWallTypeAvailable(i))
		{
			Item.bLocked = true;
			Item.Description = NSLOCTEXT("Kakurenbo", "ShopWallLocked", "転生のお店で「よじ登る」を解放すると買える（すばやくよじ登れる壁）");
		}
		const int32 Stock = State->WallStock.IsValidIndex(i) ? State->WallStock[i] : 0;
		Item.OwnedText = FText::Format(NSLOCTEXT("Kakurenbo", "ShopStock", "在庫 {0}"), Stock);
		SetRepairText(Item, Grid ? Grid->GetMissingWallCount(i) : 0, Stock, NSLOCTEXT("Kakurenbo", "ShopBroken", "壊れた"));
	}
	for (const int32 i : GetTrapTypesInShop(false))
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

TArray<FShopItemView> AKakurenboGameMode::GetGemShopItems() const
{
	TArray<FShopItemView> Items;
	const AKakurenboGameState* State = GS();
	const UKakurenboGridSubsystem* Grid = GetWorld()->GetSubsystem<UKakurenboGridSubsystem>();
	if (!State)
	{
		return Items;
	}
	auto Fmt = [](double V) { return FText::FromString(UKakurenboLibrary::FormatStatNumber(V)); };
	for (const int32 i : GetTrapTypesInShop(true))
	{
		const FKakurenboTrapRow& Def = TrapTypes[i];
		FShopItemView& Item = Items.AddDefaulted_GetRef();
		Item.bGemItem = true;
		Item.DisplayName = Def.DisplayName;
		Item.Cost = GetGemTrapCost(i);
		switch (Def.Kind)
		{
		case ETrapKind::FakeHider:
			Item.Description = NSLOCTEXT("Kakurenbo", "GemFakeDesc", "自分そっくりの人形。鬼に見つかると、大きな音を立てて向いている方へまっすぐ逃げる（置いた人形をクリックで向きを変える。捕まると消える）");
			break;
		case ETrapKind::OneWay:
			Item.Description = FText::Format(NSLOCTEXT("Kakurenbo", "GemOneWayDesc", "鬼は矢印の向きにしか通れないのれん（自分はどちらからも通れる）。その先が行き止まりのときだけ、鬼が破りに来る（硬さは通常壁の {0} 倍）"),
				FText::FromString(UKakurenboLibrary::FormatStatNumber(OneWayHPMultiplier)));
			break;
		case ETrapKind::Darkness:
			Item.Description = FText::Format(NSLOCTEXT("Kakurenbo", "GemDarkDesc", "かくれんぼの間、館じゅうが真っ暗（鬼は {0}m 先までしか見えない・自分も見えにくい）。1 回で壊れる"), Fmt(Def.Radius / 100.0));
			break;
		case ETrapKind::Cat:
			Item.Description = FText::Format(NSLOCTEXT("Kakurenbo", "GemCatDesc", "近くをうろうろして「にゃーん」と鳴く猫。寄ってきた鬼は猫をなでて {0} 秒動かない（どの鬼も猫には弱い）。鬼に当たると壊れる"), Fmt(Def.Duration));
			break;
		case ETrapKind::Daruma:
			Item.Description = FText::Format(NSLOCTEXT("Kakurenbo", "GemDarumaDesc", "かくれんぼ中に使う。使った瞬間に自分（キャラクター）の視界にいた鬼が {0} 秒動けなくなる（ぶつかれば見つかる）"), Fmt(Def.Duration));
			break;
		case ETrapKind::GoldRush:
			Item.Description = FText::Format(NSLOCTEXT("Kakurenbo", "GemGoldRushDesc", "置いておくと、そのかくれんぼの間は連打で稼げるコインが {0} 倍（何個置いても {0} 倍）。1 回で壊れる"), Fmt(GoldRushMashMultiplier));
			break;
		default:
			Item.Description = FText::GetEmpty();
			break;
		}
		const int32 Stock = State->TrapStock.IsValidIndex(i) ? State->TrapStock[i] : 0;
		Item.OwnedText = IsPlaceableTrap(i)
			? FText::Format(NSLOCTEXT("Kakurenbo", "ShopTrapStock", "在庫 {0}（置いてある {1}）"), Stock, GetOwnedTrapCount(i) - Stock)
			: FText::Format(NSLOCTEXT("Kakurenbo", "ShopItemStock", "持っている {0}"), Stock);
		const int32 Missing = Grid ? Grid->GetMissingTrapCount(i) : 0;
		if (Missing > 0)
		{
			const int32 Short = FMath::Max(0, Missing - Stock);
			// 迷い猫は鬼に当たると壁と同じように「壊れた」
			const FText Verb = TrapTypes[i].Kind == ETrapKind::Cat ? NSLOCTEXT("Kakurenbo", "ShopBroken", "壊れた") : NSLOCTEXT("Kakurenbo", "ShopUsed", "使った");
			Item.bNeedsMoreForRepair = Short > 0;
			Item.RepairText = Short > 0
				? FText::Format(NSLOCTEXT("Kakurenbo", "ShopRepairShort", "{0} {1} 個（直すにはあと {2} 個）"), Verb, Missing, Short)
				: FText::Format(NSLOCTEXT("Kakurenbo", "ShopRepairOk", "{0} {1} 個（在庫で直せる）"), Verb, Missing);
		}
	}
	return Items;
}

int32 AKakurenboGameMode::TryBuyGemShopItem(int32 Index, int32 Count)
{
	const int32 Type = GetGemShopTrapType(Index);
	int32 Bought = 0;
	while (Type != INDEX_NONE && Bought < FMath::Max(1, Count) && TryBuyTrap(Type))
	{
		++Bought;
	}
	if (GetPhase() == EKakurenboPhase::Shop && Type != INDEX_NONE)
	{
		Sfx2D(this, Bought > 0 ? EKakurenboSfx::Buy : EKakurenboSfx::BuyFail, 1.f, Bought > 0 ? 1.25f : 1.f);
	}
	return Bought;
}

void AKakurenboGameMode::DebugAddGems(int32 Amount)
{
	if (AKakurenboGameState* State = GS())
	{
		State->Gems = FMath::Max(0, State->Gems + Amount);
	}
}

int32 AKakurenboGameMode::TryBuyShopItem(int32 Index, int32 Count)
{
	// 1 個ずつ買う（値段は 1 個ごとに上がる）。買えなくなったらそこまで
	int32 Bought = 0;
	const bool bWasShop = GetPhase() == EKakurenboPhase::Shop;
	while (Bought < FMath::Max(1, Count))
	{
		bool bOk = false;
		if (Index == KakurenboShop::MashUpgrade)
		{
			bOk = TryBuyMashUpgrade();
		}
		else if (Index == KakurenboShop::TimeUpgrade)
		{
			bOk = TryBuyTimeUpgrade();
		}
		else if (Index == KakurenboShop::WallReinforce)
		{
			bOk = TryBuyWallReinforce();
		}
		else if (Index - KakurenboShop::NumUpgrades < WallTypes.Num())
		{
			bOk = TryBuyWall(Index - KakurenboShop::NumUpgrades);
		}
		else
		{
			const TArray<int32> Traps = GetTrapTypesInShop(false);
			const int32 Position = Index - KakurenboShop::NumUpgrades - WallTypes.Num();
			bOk = Traps.IsValidIndex(Position) && TryBuyTrap(Traps[Position]);
		}
		if (!bOk)
		{
			break;
		}
		++Bought;
	}
	if (bWasShop && Index >= 0 && Index < GetShopItems().Num())
	{
		Sfx2D(this, Bought > 0 ? EKakurenboSfx::Buy : EKakurenboSfx::BuyFail);
	}
	return Bought;
}

void AKakurenboGameMode::GetShopItemBulkPrice(int32 Index, int32 Count, int32& OutCount, double& OutCost) const
{
	OutCount = 0;
	OutCost = 0.0;
	const AKakurenboGameState* State = GS();
	if (!State)
	{
		return;
	}
	// 1 個目の価格・値上がりの倍率・今のレベル（持っている数）
	double Base = 0.0;
	double Growth = 1.0;
	int32 Level = 0;
	bool bAvailable = true;
	if (Index == KakurenboShop::MashUpgrade)
	{
		Base = MashUpgradeBaseCost;
		Growth = MashUpgradeCostGrowth;
		Level = State->MashIncomeLevel;
	}
	else if (Index == KakurenboShop::TimeUpgrade)
	{
		Base = TimeUpgradeBaseCost;
		Growth = TimeUpgradeCostGrowth;
		Level = State->TimeIncomeLevel;
	}
	else if (Index == KakurenboShop::WallReinforce)
	{
		Base = WallReinforceBaseCost;
		Growth = WallReinforceCostGrowth;
		Level = State->WallReinforceLevel;
	}
	else if (Index - KakurenboShop::NumUpgrades < WallTypes.Num())
	{
		const int32 Wall = Index - KakurenboShop::NumUpgrades;
		Base = WallTypes[Wall].Cost;
		Growth = WallTypes[Wall].CostGrowth;
		Level = GetOwnedWallCount(Wall);
		bAvailable = IsWallTypeAvailable(Wall);
	}
	else
	{
		const TArray<int32> Traps = GetTrapTypesInShop(false);
		const int32 Position = Index - KakurenboShop::NumUpgrades - WallTypes.Num();
		if (!Traps.IsValidIndex(Position))
		{
			return;
		}
		Base = TrapTypes[Traps[Position]].Cost;
		Growth = TrapTypes[Traps[Position]].CostGrowth;
		Level = GetOwnedTrapCount(Traps[Position]);
	}
	// 買えるだけ（Count まで。多すぎると重いので 999 まで）
	for (int32 k = 0; bAvailable && k < FMath::Min(FMath::Max(1, Count), 999); ++k)
	{
		const double Next = UKakurenboLibrary::ExpCurve(Base, Growth, Level + k);
		if (OutCost + Next > State->Coins)
		{
			break;
		}
		OutCost += Next;
		++OutCount;
	}
	if (OutCount == 0)
	{
		OutCost = UKakurenboLibrary::ExpCurve(Base, Growth, Level);
	}
}

bool AKakurenboGameMode::TryBuyShopItem(int32 Index)
{
	return TryBuyShopItem(Index, 1) > 0;
}

bool AKakurenboGameMode::TryBuyTrap(int32 TrapTypeIndex)
{
	AKakurenboGameState* State = GS();
	if (!State || State->Phase != EKakurenboPhase::Shop || !TrapTypes.IsValidIndex(TrapTypeIndex))
	{
		return false;
	}
	// 宝石の道具は宝石で買う（価格は整数）
	if (TrapTypes[TrapTypeIndex].bGemItem)
	{
		const int32 GemCost = GetGemTrapCost(TrapTypeIndex);
		if (State->Gems < GemCost)
		{
			return false;
		}
		State->Gems -= GemCost;
	}
	else
	{
		const double Cost = GetTrapCost(TrapTypeIndex);
		if (State->Coins < Cost)
		{
			return false;
		}
		State->Coins -= Cost;
	}
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
	if (!State || State->Phase != EKakurenboPhase::Shop || !IsWallTypeAvailable(WallTypeIndex))
	{
		return false;
	}
	const double Cost = GetWallCost(WallTypeIndex);
	if (State->Coins < Cost)
	{
		return false;
	}
	State->Coins -= Cost;
	State->WallStock.SetNum(WallTypes.Num());
	State->WallStock[WallTypeIndex]++;
	return true;
}

void AKakurenboGameMode::GetRefillPlan(int32& OutCount, double& OutCost) const
{
	OutCount = 0;
	OutCost = 0.0;
	const AKakurenboGameState* State = GS();
	const UKakurenboGridSubsystem* Grid = GetWorld()->GetSubsystem<UKakurenboGridSubsystem>();
	if (!State || !Grid)
	{
		return;
	}
	// 種類ごとに「足りない数」だけ、1 個ずつ値上がりしながら買ったときの合計
	auto Add = [&](double BaseCost, double Growth, int32 Owned, int32 Short)
	{
		for (int32 k = 0; k < Short; ++k)
		{
			OutCost += UKakurenboLibrary::ExpCurve(BaseCost, Growth, Owned + k);
		}
		OutCount += FMath::Max(0, Short);
	};
	for (int32 i = 0; i < WallTypes.Num(); ++i)
	{
		const int32 Stock = State->WallStock.IsValidIndex(i) ? State->WallStock[i] : 0;
		Add(WallTypes[i].Cost, WallTypes[i].CostGrowth, GetOwnedWallCount(i), Grid->GetMissingWallCount(i) - Stock);
	}
	for (const int32 i : GetTrapTypesInShop(false)) // 宝石の道具はまとめて補充しない（宝石を勝手に使わない）
	{
		const int32 Stock = State->TrapStock.IsValidIndex(i) ? State->TrapStock[i] : 0;
		Add(TrapTypes[i].Cost, TrapTypes[i].CostGrowth, GetOwnedTrapCount(i), Grid->GetMissingTrapCount(i) - Stock);
	}
}

int32 AKakurenboGameMode::BuyAllMissing()
{
	AKakurenboGameState* State = GS();
	const UKakurenboGridSubsystem* Grid = GetWorld()->GetSubsystem<UKakurenboGridSubsystem>();
	if (!State || !Grid || State->Phase != EKakurenboPhase::Shop)
	{
		return 0;
	}
	int32 Bought = 0;
	for (;;)
	{
		// まだ足りないもののうち、今のコインで買える一番高いものを 1 個買う（を繰り返す）
		double BestCost = -1.0;
		int32 BestWall = INDEX_NONE;
		int32 BestTrap = INDEX_NONE;
		for (int32 i = 0; i < WallTypes.Num(); ++i)
		{
			const int32 Stock = State->WallStock.IsValidIndex(i) ? State->WallStock[i] : 0;
			const double Cost = GetWallCost(i);
			if (IsWallTypeAvailable(i) && Grid->GetMissingWallCount(i) > Stock && Cost <= State->Coins && Cost > BestCost)
			{
				BestCost = Cost;
				BestWall = i;
				BestTrap = INDEX_NONE;
			}
		}
		for (const int32 i : GetTrapTypesInShop(false))
		{
			const int32 Stock = State->TrapStock.IsValidIndex(i) ? State->TrapStock[i] : 0;
			const double Cost = GetTrapCost(i);
			if (Grid->GetMissingTrapCount(i) > Stock && Cost <= State->Coins && Cost > BestCost)
			{
				BestCost = Cost;
				BestTrap = i;
				BestWall = INDEX_NONE;
			}
		}
		const bool bOk = BestWall != INDEX_NONE ? TryBuyWall(BestWall) : BestTrap != INDEX_NONE ? TryBuyTrap(BestTrap) : false;
		if (!bOk)
		{
			break;
		}
		++Bought;
	}
	Sfx2D(this, Bought > 0 ? EKakurenboSfx::Buy : EKakurenboSfx::BuyFail);
	UE_LOG(LogKakurenbo, Log, TEXT("Bought %d items to refill broken walls / used traps"), Bought);
	return Bought;
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
	if (Grid->IsObstacle(Cell))
	{
		if (OutReason) *OutReason = NSLOCTEXT("Kakurenbo", "StartOnFurniture", "家具や部屋の壁のあるマスからは始められません");
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
	const FWallTypeDef Def = GetEffectiveWallTypes()[WallTypeIndex];
	APlaceableBlock* Block = Grid->PlaceBlock(Cell, WallTypeIndex, Def.MaxHP, Def.Color, Def.SoundHP);
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
	if (!IsPlaceableTrap(TrapTypeIndex))
	{
		if (OutReason) *OutReason = NSLOCTEXT("Kakurenbo", "PlaceUseInHide", "かくれんぼ中に使う道具です（置かない）");
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
	const ETrapKind Kind = TrapTypes[TrapTypeIndex].Kind;
	ATrapActor* Trap = Grid->PlaceTrap(Cell, TrapTypeIndex, TrapTypes[TrapTypeIndex], Kind == ETrapKind::OneWay ? OneWayPlaceDir : Kind == ETrapKind::FakeHider ? FakeHiderPlaceDir : 0);
	if (!Trap)
	{
		return false;
	}
	GS()->TrapStock[TrapTypeIndex]--;
	Sfx3D(this, EKakurenboSfx::Place, Trap->GetActorLocation(), 0.8f, 1.4f);
	return true;
}

bool AKakurenboGameMode::RotateTrap(FIntPoint Cell)
{
	UKakurenboGridSubsystem* Grid = GetWorld()->GetSubsystem<UKakurenboGridSubsystem>();
	if (!Grid || GetPhase() != EKakurenboPhase::Build || !Grid->RotateTrap(Cell))
	{
		return false;
	}
	// 次に置くのれん・人形も同じ向き
	const ATrapActor* Trap = Grid->GetTrap(Cell);
	(Trap && Trap->GetKind() == ETrapKind::FakeHider ? FakeHiderPlaceDir : OneWayPlaceDir) = Grid->GetTrapDir(Cell);
	Sfx3D(this, EKakurenboSfx::Place, Grid->CellFloorCenter(Cell), 0.6f, 1.8f);
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

bool AKakurenboGameMode::CanMash() const
{
	const AKakurenboGameState* State = GS();
	return State && State->Phase == EKakurenboPhase::Hide && State->HideStartCountdown <= 0.f;
}

void AKakurenboGameMode::HandleMash(const FVector& NoiseLocation)
{
	AKakurenboGameState* State = GS();
	// 準備の間（鬼が数えている「もういいかい」の間）は連打できない（ユーザーの希望）
	if (!State || !CanMash())
	{
		return;
	}
	State->AddCoins(GetMashIncome(), true);
	State->MashCoinsThisRound += GetMashIncome();
	State->MashCountThisRound++;

	// 連打の音が鬼に届く（消音壁で囲まれていれば小さくなり、消音壁が少し傷む）
	const float Loudness = MakePlayerNoise(NoiseLocation, MashNoiseLoudness, MashSoundDamage, NoiseRingColor, 14.f, EKakurenboNoise::Mash);
	Sfx2D(this, EKakurenboSfx::Mash, 0.6f * FMath::Max(Loudness / FMath::Max(MashNoiseLoudness, 0.01f), 0.4f), FMath::FRandRange(0.92f, 1.08f));
}

void AKakurenboGameMode::HandleDash(const FVector& NoiseLocation, float Loudness)
{
	if (GetPhase() != EKakurenboPhase::Hide)
	{
		return;
	}
	// ダッシュは大きな音が出る（消音壁で囲まれていれば小さくなる）
	MakePlayerNoise(NoiseLocation, Loudness, DashSoundDamage, FLinearColor(1.f, 0.45f, 0.2f), 22.f, EKakurenboNoise::Dash);
	Sfx2D(this, EKakurenboSfx::Dash, 0.9f);

	// 足元に煙幕を投げる：しばらくの間、煙の中・向こう側は鬼から見えない（追いかけている鬼は見失う）
	if (const ACharacter* Player = GetPlayerCharacter())
	{
		const FVector Feet = Player->GetActorLocation() - FVector(0.f, 0.f, Player->GetSimpleCollisionHalfHeight());
		// SpawnActorDeferred: 生成 → 残る時間を設定 → FinishSpawning（ここで BeginPlay が走る）
		const FTransform At(Feet);
		if (ASmokeCloud* Smoke = GetWorld()->SpawnActorDeferred<ASmokeCloud>(SmokeClass ? SmokeClass.Get() : ASmokeCloud::StaticClass(), At, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn))
		{
			Smoke->Duration = GetSmokeDuration();
			Smoke->FinishSpawning(At);
		}
		Sfx3D(this, EKakurenboSfx::Smoke, Feet + FVector(0.f, 0.f, 60.f));
	}
}

float AKakurenboGameMode::MakePlayerNoise(const FVector& Location, float Loudness, double SoundDamage, const FLinearColor& RingColor, float RingThickness, EKakurenboNoise Kind)
{
	UKakurenboGridSubsystem* Grid = GetWorld()->GetSubsystem<UKakurenboGridSubsystem>();
	const ACharacter* Player = GetPlayerCharacter();
	TArray<FIntPoint> WallCells;
	float Damping = 0.f;
	if (Grid && Player && Grid->IsConfigured())
	{
		Damping = Grid->GetEnclosureNoiseDamping(Grid->WorldToCell(Player->GetActorLocation()), WallTypes, nullptr, &WallCells);
	}
	const float Scaled = Loudness * FMath::Clamp(1.f - Damping, 0.f, 1.f);
	EmitNoise(Location, Scaled, Kind);
	ShowNoiseRing(Location, Scaled, RingColor, RingThickness);

	// 音を消した消音壁は傷み、回数が尽きると壊れる（囲みに穴が開く）
	if (Damping > 0.f && SoundDamage > 0.0)
	{
		if (const int32 Broken = Grid->DamageSound(WallCells, SoundDamage))
		{
			if (AKakurenboGameState* State = GS())
			{
				State->LastRoundWallsDestroyed += Broken;
			}
			ShowPopup(TEXT("消音壁が壊れた！"), FLinearColor(0.85f, 0.8f, 0.65f));
		}
	}
	return Scaled;
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

int32 AKakurenboGameMode::GetPlayerQuietWallRemaining() const
{
	const UKakurenboGridSubsystem* Grid = GetWorld()->GetSubsystem<UKakurenboGridSubsystem>();
	const ACharacter* Player = GetPlayerCharacter();
	if (!Grid || !Player || !Grid->IsConfigured())
	{
		return -1;
	}
	TArray<FIntPoint> WallCells;
	Grid->GetEnclosureNoiseDamping(Grid->WorldToCell(Player->GetActorLocation()), WallTypes, nullptr, &WallCells);
	double Min = -1.0;
	for (const FIntPoint& Cell : WallCells)
	{
		const APlaceableBlock* Block = Grid->GetBlock(Cell, 0);
		if (Block && Block->IsSoundBreakable() && (Min < 0.0 || Block->SoundHP < Min))
		{
			Min = Block->SoundHP;
		}
	}
	return Min < 0.0 ? -1 : FMath::CeilToInt(Min);
}

TArray<FIntPoint> AKakurenboGameMode::GetOniGateCells() const
{
	return Arena ? Arena->GetOniGateCells() : TArray<FIntPoint>();
}

void AKakurenboGameMode::EmitNoise(const FVector& Location, float Loudness, EKakurenboNoise Kind)
{
	for (AOniCharacter* Oni : Onis)
	{
		if (Oni)
		{
			Oni->HearNoise(Location, Loudness, Kind);
		}
	}
}

float AKakurenboGameMode::GetStepNoiseMultiplier() const
{
	// 壁に囲まれた場所（空洞）にいると、消音壁でなくても足音はほとんど外に漏れない。消音壁の方が効くならそちら
	const UKakurenboGridSubsystem* Grid = GetWorld()->GetSubsystem<UKakurenboGridSubsystem>();
	const ACharacter* Player = GetPlayerCharacter();
	if (!Grid || !Player || !Grid->IsConfigured())
	{
		return 1.f;
	}
	const FIntPoint Cell = Grid->WorldToCell(Player->GetActorLocation());
	if (!Grid->IsWalkable(Cell) || Grid->IsInMainArea(Cell))
	{
		return 1.f; // 広い場所にいる（壁の上に乗っているときも聞こえる）
	}
	return FMath::Min(EnclosedStepMultiplier, GetPlayerNoiseMultiplier());
}

void AKakurenboGameMode::HandlePlayerStep(const FVector& FeetLocation, bool bSneaking)
{
	AKakurenboGameState* State = GS();
	if (!State || State->Phase != EKakurenboPhase::Hide)
	{
		return;
	}
	const float Multiplier = GetStepNoiseMultiplier();
	const float Loudness = StepNoiseLoudness * (bSneaking ? SneakStepMultiplier : 1.f) * Multiplier;
	State->StepsThisRound++;
	if (bOnisSpawnedThisRound)
	{
		EmitNoise(FeetLocation, Loudness, EKakurenboNoise::Step);
		if (bShowStepRing && Loudness > 0.05f)
		{
			// 足音の輪は控えめに（普通の鬼が聞こえるくらいの大きさ・細く暗い線）
			ShowNoiseRing(FeetLocation, Loudness * 0.6f, FLinearColor(0.35f, 0.5f, 0.8f), 3.f);
		}
	}
	Sfx2D(this, EKakurenboSfx::PlayerStep, 0.35f /* 連打の音の 60% */ * (bSneaking ? 0.4f : 1.f) * (Multiplier < 1.f ? 0.5f : 1.f), FMath::FRandRange(0.9f, 1.1f));
}

void AKakurenboGameMode::HandlePlayerClimb(const FVector& FeetLocation)
{
	AKakurenboGameState* State = GS();
	if (!State || State->Phase != EKakurenboPhase::Hide)
	{
		return;
	}
	// よじ登るとガサッと音が出る（足音として聞かれる。壁に囲まれていれば小さい）
	const float Loudness = ClimbNoiseLoudness * GetStepNoiseMultiplier();
	State->ClimbsThisRound++;
	if (bOnisSpawnedThisRound)
	{
		EmitNoise(FeetLocation, Loudness, EKakurenboNoise::Step);
		if (bShowStepRing)
		{
			ShowNoiseRing(FeetLocation, Loudness, FLinearColor(0.55f, 0.75f, 1.f), 6.f);
		}
	}
	Sfx2D(this, EKakurenboSfx::PlayerClimb, 0.45f); // 連打の音（0.6）の 75%
}

void AKakurenboGameMode::HandleOniSummon(AOniCharacter* Caller, const FVector& TargetLocation)
{
	AKakurenboGameState* State = GS();
	if (!State || State->Phase != EKakurenboPhase::Hide || !Caller)
	{
		return;
	}
	// 呼んだ鬼の周り SummonRadius 以内の鬼が、プレイヤーを見つけた場所へ集まってくる
	int32 Count = 0;
	for (AOniCharacter* Oni : Onis)
	{
		if (Oni && Oni != Caller && Oni->IsActive() && FVector::Dist2D(Oni->GetActorLocation(), Caller->GetActorLocation()) <= Caller->SummonRadius)
		{
			Oni->Summon(TargetLocation);
			++Count;
		}
	}
	LastSummonedCount = Count;
	State->SummonsThisRound++;
	UE_LOG(LogKakurenbo, Log, TEXT("Detector summoned %d onis"), Count);

	Sfx2D(this, EKakurenboSfx::Summon, 0.8f);
	if (UKakurenboFxSubsystem* F = Fx(this))
	{
		F->Ring(Caller->GetActorLocation(), 60.f, Caller->SummonRadius, 0.9f, FLinearColor(0.75f, 0.3f, 1.f), 18.f);
	}
	ShowPopup(Count > 0 ? FString::Printf(TEXT("見張りに見つかった…　鬼が %d 体あつまってくる"), Count) : FString(TEXT("見張りに見つかった…")), FLinearColor(0.8f, 0.5f, 1.f));
}

void AKakurenboGameMode::CollectTreasure(ATreasureActor* Treasure)
{
	AKakurenboGameState* State = GS();
	if (!State || State->Phase != EKakurenboPhase::Hide || !Treasure || !Treasures.Contains(Treasure))
	{
		return;
	}
	// M15：お宝はコインではなく宝石（宝石のお店で道具と交換する）。ユーザーの希望：「お宝を取ったときはコイン入手なしで」
	const int32 Gems = GetGemsPerTreasure();
	State->TreasuresCollectedThisRound++;
	State->Gems += Gems;
	State->GemsThisRound += Gems;
	UE_LOG(LogKakurenbo, Log, TEXT("Treasure collected (+%d gems)"), Gems);

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
	ShowPopup(FString::Printf(TEXT("宝石 +%d"), Gems), TreasureColor);

	// M15：そのラウンドのお宝を全部取ったら、ボーナスの宝石（AllTreasureBonusCoinRatio を 0 より大きくするとコインも）
	if (!State->bAllTreasuresThisRound && State->TreasuresThisRound > 0 && State->TreasuresCollectedThisRound >= State->TreasuresThisRound)
	{
		State->bAllTreasuresThisRound = true;
		const double BonusCoins = GetClearReward() * AllTreasureBonusCoinRatio;
		if (BonusCoins > 0.0)
		{
			State->AddCoins(BonusCoins, true);
		}
		State->TreasureBonusCoinsThisRound = BonusCoins;
		const int32 BonusGems = GetAllTreasureBonusGems();
		State->Gems += BonusGems;
		State->GemsThisRound += BonusGems;
		Sfx2D(this, EKakurenboSfx::Gem);
		FlashScreen(FLinearColor(0.4f, 0.9f, 1.f, 0.35f), 0.6f);
		ShowPopup(BonusCoins > 0.0 ? FString::Printf(TEXT("お宝コンプリート！ 宝石 +%d・コイン +%s"), BonusGems, *UKakurenboLibrary::FormatBigNumber(BonusCoins))
			: FString::Printf(TEXT("お宝コンプリート！ ボーナス 宝石 +%d"), BonusGems), FLinearColor(0.5f, 0.95f, 1.f));
		UE_LOG(LogKakurenbo, Log, TEXT("All treasures collected: bonus %d gems, %s coins"), BonusGems, *UKakurenboLibrary::FormatBigNumber(BonusCoins));
	}

	// 宝物鬼は取られた後もその場所を見回りに来る
	if (const UKakurenboGridSubsystem* Grid = GetWorld()->GetSubsystem<UKakurenboGridSubsystem>())
	{
		TakenTreasureCells.AddUnique(Grid->WorldToCell(Treasure->GetActorLocation()));
	}
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
		ShowPopup(FString::Printf(TEXT("鬼が%sにかかった"), *Def.DisplayName.ToString()), Def.Color);
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
	EmitNoise(Location, Trap->Def.NoiseLoudness, EKakurenboNoise::Decoy);
	Sfx3D(this, EKakurenboSfx::DecoyPing, Location + FVector(0.f, 0.f, 40.f));
	if (UKakurenboFxSubsystem* F = Fx(this))
	{
		// 音の届く範囲（標準の聞こえる距離 × 大きさ）まで輪を広げる
		F->Ring(Location, 40.f, GetOniHearingRadius() * Trap->Def.NoiseLoudness, 0.6f, Trap->Def.Color, 10.f);
	}
}

// ---------------------------------------------------------------- 宝石のお店の道具（M15）

void AKakurenboGameMode::HandleGadgetNoise(ATrapActor* Trap, float Loudness, EKakurenboNoise Kind)
{
	if (!Trap || GetPhase() != EKakurenboPhase::Hide || !bOnisSpawnedThisRound)
	{
		return;
	}
	// 身代わり人形はいま走っている場所から、迷い猫は猫のいる場所から
	const FVector Location = Trap->GetGadgetLocation();
	EmitNoise(Location, Loudness * GetMapNoiseScale(), Kind);
	ShowNoiseRing(Location, Loudness * GetMapNoiseScale(), Trap->Def.Color, 8.f);
}

void AKakurenboGameMode::HandleFakeHiderCaught(ATrapActor* Trap, AOniCharacter* Oni)
{
	AKakurenboGameState* State = GS();
	UKakurenboGridSubsystem* Grid = GetWorld()->GetSubsystem<UKakurenboGridSubsystem>();
	if (!State || !Grid || !Trap || State->Phase != EKakurenboPhase::Hide)
	{
		return;
	}
	State->TrapsTriggeredThisRound++;
	const FVector Location = Trap->GetGadgetLocation() + FVector(0.f, 0.f, 40.f);
	if (UKakurenboFxSubsystem* F = Fx(this))
	{
		FKakurenboBurstParams Params;
		Params.Color = FLinearColor(0.85f, 0.85f, 0.95f);
		Params.Count = 18;
		Params.Size = 14.f;
		Params.Speed = 260.f;
		Params.UpBias = 0.6f;
		Params.Lifetime = 0.9f;
		Params.bSpheres = true;
		F->Burst(Location, Params);
	}
	Sfx3D(this, EKakurenboSfx::Poof, Location);
	ShowPopup(FString::Printf(TEXT("%s：「にせもの！？」"), Oni ? *Oni->TypeDisplayName.ToString() : TEXT("鬼")), Trap->Def.Color);
	UE_LOG(LogKakurenbo, Log, TEXT("Fake hider at (%d,%d) caught by %s"), Trap->Cell.X, Trap->Cell.Y, Oni ? *Oni->GetName() : TEXT("none"));
	if (Oni)
	{
		Oni->ReactToFake();
	}
	Grid->ConsumeTrap(Trap); // 設計図は残るので、在庫があれば次の設置パートで置き直される
}

void AKakurenboGameMode::HandleOneWayBroken(ATrapActor* Trap)
{
	AKakurenboGameState* State = GS();
	UKakurenboGridSubsystem* Grid = GetWorld()->GetSubsystem<UKakurenboGridSubsystem>();
	if (!State || !Grid || !Trap || State->Phase != EKakurenboPhase::Hide)
	{
		return;
	}
	State->TrapsTriggeredThisRound++;
	const FVector Location = Trap->GetActorLocation() + FVector(0.f, 0.f, 100.f);
	if (UKakurenboFxSubsystem* F = Fx(this))
	{
		FKakurenboBurstParams Params;
		Params.Color = Trap->Def.Color;
		Params.Count = 12;
		Params.Size = 16.f;
		Params.Speed = 300.f;
		Params.Lifetime = 0.8f;
		F->Burst(Location, Params);
	}
	Sfx3D(this, EKakurenboSfx::BlockBreak, Location, 0.8f, 1.3f);
	ShowPopup(TEXT("のれんが破られた！"), Trap->Def.Color);
	Grid->ConsumeTrap(Trap);
}

double AKakurenboGameMode::GetOneWayMaxHP() const
{
	const TArray<FWallTypeDef> Walls = GetEffectiveWallTypes();
	return (Walls.Num() > 0 ? Walls[0].MaxHP : 3.0) * OneWayHPMultiplier;
}

void AKakurenboGameMode::HandleGateHit(ATrapActor* Trap, double Damage)
{
	if (!Trap || GetPhase() != EKakurenboPhase::Hide)
	{
		return;
	}
	const bool bBroken = Trap->DamageGate(Damage);
	const FVector Location = Trap->GetActorLocation() + FVector(0.f, 0.f, 150.f);
	if (UKakurenboFxSubsystem* F = Fx(this))
	{
		FKakurenboBurstParams Params;
		Params.Color = Trap->Def.Color;
		Params.Count = 5;
		Params.Size = 10.f;
		Params.Speed = 240.f;
		Params.Lifetime = 0.5f;
		F->Burst(Location, Params);
	}
	if (bBroken)
	{
		HandleOneWayBroken(Trap); // ここで消える
	}
	else
	{
		Sfx3D(this, EKakurenboSfx::BlockHit, Location, 0.8f, 1.2f);
	}
}

TArray<AHiderCharacter*> AKakurenboGameMode::GetFakeHiders() const
{
	TArray<AHiderCharacter*> Fakes;
	if (const UKakurenboGridSubsystem* Grid = GetWorld()->GetSubsystem<UKakurenboGridSubsystem>())
	{
		for (const ATrapActor* Trap : Grid->GetAllTraps())
		{
			if (Trap && Trap->GetFakeHider())
			{
				Fakes.Add(Trap->GetFakeHider());
			}
		}
	}
	return Fakes;
}

void AKakurenboGameMode::ConsumeRoundGadgets()
{
	UKakurenboGridSubsystem* Grid = GetWorld()->GetSubsystem<UKakurenboGridSubsystem>();
	AKakurenboGameState* State = GS();
	if (!Grid || !State)
	{
		return;
	}
	// 真っ暗ブロックは 1 ラウンドで壊れる（ユーザーの希望：1 プレイで壊れる代わりに館じゅうを暗くする）。ゴールドラッシュも同じ。
	// 迷い猫は鬼に当たった時点で壊れた扱い（HandleCatTaken）。頭の上の猫は、かくれんぼが終わると自分で消える
	for (ATrapActor* Trap : Grid->GetAllTraps())
	{
		if (Trap && (Trap->GetKind() == ETrapKind::Darkness || Trap->GetKind() == ETrapKind::GoldRush))
		{
			State->TrapsTriggeredThisRound++;
			Grid->ConsumeTrap(Trap);
		}
	}
}

void AKakurenboGameMode::SetMansionDark(bool bDark, float SightRadius)
{
	bMansionDark = bDark;
	DarkSightRadius = SightRadius;
	if (bDark && !DarknessVolume)
	{
		// 画面全体に効く（Unbound）ポストプロセス：露出を下げて真っ暗に、四隅をさらに暗く。鬼の懐中電灯は見える
		DarknessVolume = GetWorld()->SpawnActor<APostProcessVolume>();
		if (DarknessVolume)
		{
			DarknessVolume->bUnbound = true;
			DarknessVolume->Priority = 50.f;
			FPostProcessSettings& PP = DarknessVolume->Settings;
			PP.bOverride_AutoExposureBias = true;
			PP.AutoExposureBias = DarknessExposureBias;
			PP.bOverride_VignetteIntensity = true;
			PP.VignetteIntensity = 0.9f;
			PP.bOverride_ColorSaturation = true;
			PP.ColorSaturation = FVector4(0.6f, 0.6f, 0.75f, 1.f);
		}
	}
	if (DarknessVolume)
	{
		DarknessVolume->bEnabled = bDark;
		DarknessVolume->BlendWeight = bDark ? 1.f : 0.f;
	}
	// 真っ暗の間は、プレイヤーの懐中電灯で前を照らす（ユーザーの希望。鬼に見つかりやすくはならない）
	if (AHiderCharacter* Hider = Cast<AHiderCharacter>(GetPlayerCharacter()))
	{
		Hider->SetFlashlightOn(bDark);
	}
	if (bDark)
	{
		ShowPopup(TEXT("館が真っ暗になった…"), FLinearColor(0.6f, 0.55f, 0.85f));
	}
}

bool AKakurenboGameMode::CanUseDaruma() const
{
	const AKakurenboGameState* State = GS();
	const int32 Type = FindTrapTypeOfKind(ETrapKind::Daruma);
	return State && State->Phase == EKakurenboPhase::Hide && bOnisSpawnedThisRound && !IsDarumaActive()
		&& State->TrapStock.IsValidIndex(Type) && State->TrapStock[Type] > 0;
}

bool AKakurenboGameMode::UseDaruma()
{
	if (!CanUseDaruma())
	{
		return false;
	}
	AKakurenboGameState* State = GS();
	const int32 Type = FindTrapTypeOfKind(ETrapKind::Daruma);
	State->TrapStock[Type]--;
	DarumaTimeLeft = TrapTypes[Type].Duration > 0.f ? TrapTypes[Type].Duration : 3.f;
	// 使った瞬間に視界にいた鬼を、効いている間ずっと止める（ユーザーの指摘：「全然弱い。これで敵を巻くことができない」。
	// 前は見ている間だけ止まり、逃げようと背を向けると動き出した）
	DarumaFrozenOnis.Reset();
	for (AOniCharacter* Oni : Onis)
	{
		if (Oni && IsOniInPlayerView(Oni))
		{
			DarumaFrozenOnis.Add(Oni);
		}
	}
	Sfx2D(this, EKakurenboSfx::Daruma, 1.1f);
	ShowPopup(DarumaFrozenOnis.Num() > 0 ? FString::Printf(TEXT("だるまさんが ころんだ！　%d 体ストップ"), DarumaFrozenOnis.Num()) : FString(TEXT("だるまさんが ころんだ！　（見えている鬼がいない）")),
		TrapTypes[Type].Color);
	FlashScreen(FLinearColor(1.f, 0.3f, 0.25f, 0.25f), 0.4f);
	UE_LOG(LogKakurenbo, Log, TEXT("Daruma used (%.1f s, %d onis frozen, %d left)"), DarumaTimeLeft, DarumaFrozenOnis.Num(), State->TrapStock[Type]);
	return true;
}

void AKakurenboGameMode::TickDaruma(float DeltaSeconds)
{
	if (DarumaTimeLeft > 0.f)
	{
		DarumaTimeLeft = FMath::Max(0.f, DarumaTimeLeft - DeltaSeconds);
		if (DarumaTimeLeft <= 0.f)
		{
			DarumaFrozenOnis.Reset();
		}
	}
}

bool AKakurenboGameMode::IsOniDarumaFrozen(const AOniCharacter* Oni) const
{
	return Oni && IsDarumaActive() && DarumaFrozenOnis.Contains(TWeakObjectPtr<AOniCharacter>(const_cast<AOniCharacter*>(Oni)));
}

bool AKakurenboGameMode::IsOniInPlayerView(const AOniCharacter* Oni) const
{
	const APlayerController* PC = GetWorld()->GetFirstPlayerController();
	const APawn* Player = PC ? PC->GetPawn() : nullptr;
	if (!Oni || !Player)
	{
		return false;
	}
	// プレイヤー（の体）が向いている方（DarumaViewHalfAngle 度以内・上から見た向き）にいて、間に壁・家具が無い鬼。
	// （ユーザーの希望：「カメラが見ているではなく、プレイヤーが見ているときに」。カメラだけ鬼に向けても止まらない）
	const FVector Head = Player->GetActorLocation() + FVector(0.f, 0.f, 50.f);
	const FVector Forward = Player->GetActorForwardVector().GetSafeNormal2D();
	const FVector Body = Oni->GetActorLocation() + FVector(0.f, 0.f, 40.f);
	const FVector To = Body - Head;
	if (To.Size() > DarumaViewDistance || FVector::DotProduct(To.GetSafeNormal2D(), Forward) < FMath::Cos(FMath::DegreesToRadians(DarumaViewHalfAngle)))
	{
		return false;
	}
	FCollisionQueryParams Params(SCENE_QUERY_STAT(KakuDarumaSight), false, Player);
	Params.AddIgnoredActor(Oni);
	FHitResult Hit;
	// 自分の頭から鬼の体まで
	return !GetWorld()->LineTraceSingleByChannel(Hit, Head, Body, ECC_Visibility, Params);
}

void AKakurenboGameMode::HandleTrapDisarmed(ATrapActor* Trap, AOniCharacter* Oni)
{
	AKakurenboGameState* State = GS();
	UKakurenboGridSubsystem* Grid = GetWorld()->GetSubsystem<UKakurenboGridSubsystem>();
	if (!State || !Grid || !Trap || State->Phase != EKakurenboPhase::Hide)
	{
		return;
	}
	State->TrapsTriggeredThisRound++;
	const FVector Location = Trap->GetActorLocation() + FVector(0.f, 0.f, 20.f);
	if (UKakurenboFxSubsystem* F = Fx(this))
	{
		FKakurenboBurstParams Params;
		Params.Color = Trap->Def.Color;
		Params.Count = 10;
		Params.Size = 12.f;
		Params.Speed = 300.f;
		Params.Lifetime = 0.6f;
		F->Burst(Location, Params);
	}
	Sfx3D(this, EKakurenboSfx::DecoyBreak, Location, 1.f, 0.7f);
	ShowPopup(FString::Printf(TEXT("%s が %s を壊した！"), Oni ? *Oni->TypeDisplayName.ToString() : TEXT("鬼"), *Trap->Def.DisplayName.ToString()), Trap->Def.Color);
	UE_LOG(LogKakurenbo, Log, TEXT("Trap %s at (%d,%d) disarmed by %s"), *Trap->Def.DisplayName.ToString(), Trap->Cell.X, Trap->Cell.Y, Oni ? *Oni->GetName() : TEXT("none"));
	Grid->ConsumeTrap(Trap); // 設計図は残るので、次の設置パートで在庫から置き直される
}

void AKakurenboGameMode::HandleCatTaken(ATrapActor* Cat, AOniCharacter* Oni)
{
	UKakurenboGridSubsystem* Grid = GetWorld()->GetSubsystem<UKakurenboGridSubsystem>();
	AKakurenboGameState* State = GS();
	if (!State || !Grid || !Cat || State->Phase != EKakurenboPhase::Hide)
	{
		return;
	}
	State->TrapsTriggeredThisRound++;
	ShowPopup(FString::Printf(TEXT("%s が %s を連れていった！"), Oni ? *Oni->TypeDisplayName.ToString() : TEXT("鬼"), *Cat->Def.DisplayName.ToString()), Cat->Def.Color);
	UE_LOG(LogKakurenbo, Log, TEXT("Cat at (%d,%d) taken by %s"), Cat->Cell.X, Cat->Cell.Y, Oni ? *Oni->GetName() : TEXT("none"));
	Grid->ReleaseTrap(Cat); // 壊れた壁と同じ：設計図は残るので、在庫があれば次の設置パートで置き直される
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

	// 宝石の見た目（Fab の宝石。いろいろな形と色。無いパソコンでは金色の立方体）
	TArray<UStaticMesh*> GemMeshes;
	for (const TSoftObjectPtr<UStaticMesh>& Soft : TreasureMeshes)
	{
		if (UStaticMesh* Loaded = UKakurenboLibrary::LoadIfExists(Soft))
		{
			GemMeshes.Add(Loaded);
		}
	}
	static const FLinearColor GemColors[] = {
		FLinearColor(1.f, 0.72f, 0.08f),  // 金
		FLinearColor(1.f, 0.12f, 0.2f),   // ルビー
		FLinearColor(0.15f, 0.45f, 1.f),  // サファイア
		FLinearColor(0.15f, 1.f, 0.35f),  // エメラルド
		FLinearColor(0.75f, 0.3f, 1.f),   // アメジスト
	};
	for (const FIntPoint& Cell : Cells)
	{
		ATreasureActor* Treasure = GetWorld()->SpawnActorDeferred<ATreasureActor>(TreasureClass, FTransform(Grid->CellFloorCenter(Cell)));
		if (!Treasure)
		{
			continue;
		}
		Treasure->Value = 0.0; // M15：お宝はコインではなく宝石
		if (GemMeshes.Num() > 0)
		{
			const FLinearColor GemColor = GemColors[TreasureRandom.RandRange(0, UE_ARRAY_COUNT(GemColors) - 1)];
			Treasure->Color = GemColor;
			Treasure->LookMesh = GemMeshes[TreasureRandom.RandRange(0, GemMeshes.Num() - 1)];
			Treasure->LookMaterial = MakeSurfaceMaterial(TreasureSurface, GemColor);
			Treasure->LookSize = TreasureMeshSize;
		}
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
	TakenTreasureCells.Reset();
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
		if (GateCells.Num() == 0 || !Grid->IsWalkable(Cell))
		{
			float BestDistSq = TNumericLimits<float>::Max();
			for (int32 Y = 0; Y < Grid->GetSizeY(); ++Y)
			{
				for (int32 X = 0; X < Grid->GetSizeX(); ++X)
				{
					const FIntPoint C(X, Y);
					const float DistSq = FVector::DistSquared2D(Grid->CellFloorCenter(C), GateLocation);
					if (Grid->IsWalkable(C) && !Cells.Contains(C) && DistSq < BestDistSq)
					{
						BestDistSq = DistSq;
						Cell = C;
					}
				}
			}
		}
		Cells.Add(Cell);
	}

	// 慎重鬼は舞台を手分けする（担当の区画）。探知鬼は見張りの場所をランダムに決める（スタート位置・門・ほかの探知鬼から離す）
	const int32 NumCareful = Settings.OniTypes.FilterByPredicate([](EOniType T) { return T == EOniType::Careful; }).Num();
	int32 CarefulIndex = 0;
	TArray<FVector> GuardAvoid = { Hider->GetActorLocation(), GateLocation };

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
		Oni->Intuition = bOniIntuition ? TypeRow.Intuition : 0.f; // 自動テストでは既定で切る（うろうろの行き先がプレイヤーに寄って結果が変わらないように）
		Oni->WanderSpeed = OniWanderSpeedBase * TypeRow.SpeedScale + Settings.OniSpeedBonus;
		Oni->InvestigateSpeed = OniInvestigateSpeedBase * TypeRow.SpeedScale + Settings.OniSpeedBonus;
		Oni->ChaseSpeed = OniChaseSpeedBase * TypeRow.SpeedScale + Settings.OniSpeedBonus;
		// 聞こえる距離は館の大きさに合わせる（GetOniHearingRadius。小さい館ほど音が届かない）
		const float BaseHearing = GetOniHearingRadius();
		Oni->HearingRadius = BaseHearing * TypeRow.HearingScale;
		Oni->BaseHearingRadius = BaseHearing;
		Oni->DecoyHearingRadius = BaseHearing * TypeRow.DecoyHearingScale;
		Oni->StepHearingRadius = BaseHearing * TypeRow.StepHearingScale;
		if (Type == EOniType::Careful)
		{
			Oni->SetCarefulSector(CarefulIndex++, NumCareful);
		}
		if (Type == EOniType::Detector)
		{
			const TArray<FIntPoint> Guard = Grid->FindRandomFreeCells(1, GuardAvoid, DetectorGuardMinDistanceCells, TreasureRandom);
			if (Guard.Num() > 0)
			{
				Oni->SetGuardCell(Guard[0]);
				GuardAvoid.Add(Grid->CellFloorCenter(Guard[0]));
			}
		}
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
		Oni->OnSummon.AddUObject(this, &AKakurenboGameMode::HandleOniSummon);
		Oni->Activate(Hider);
		Onis.Add(Oni);

		UE_LOG(LogKakurenbo, Log, TEXT("Oni %s spawned at cell (%d,%d), damage %.2f, chase speed %.0f"),
			*UEnum::GetValueAsString(Type), Cell.X, Cell.Y, Oni->AttackDamage, Oni->ChaseSpeed);
	}

	// 身代わり人形とは体が重なれる（触れたら捕まえる判定は距離で見る）
	for (AHiderCharacter* Fake : GetFakeHiders())
	{
		RegisterFakeHiderCollision(Fake);
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

void AKakurenboGameMode::ConfigureOniLook(AOniCharacter* Oni, EOniType Type) const
{
	if (!Oni)
	{
		return;
	}
	Oni->ApplyTypeSettings(Type, GetOniTypeRow(Type));
	if (LoadedOniLook.Mesh)
	{
		FKakurenboOniLook Look = LoadedOniLook;
		Look.Material = LoadedOniMaterials.FindRef(Type);
		Oni->SetSkeletalAppearance(Look);
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
			// 縁の光の色を変える（宝物鬼を金色っぽく）。動的マテリアル（MID）は元のマテリアルの値を一部だけ変えたコピー
			if (Pair.Value.MeshTint.A > 0.f)
			{
				if (UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(Material, this))
				{
					MID->SetVectorParameterValue(TEXT("ReflectionColor"), Pair.Value.MeshTint);
					Material = MID;
				}
			}
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
	if (AHiderCharacter* Hider = Cast<AHiderCharacter>(GetPlayerCharacter()))
	{
		Hider->CancelClimb(); // よじ登っている途中でパートが変わったら、そこでやめる
	}
	UE_LOG(LogKakurenbo, Log, TEXT("Phase -> %s (Stage %d, Coins %s)"),
		*UEnum::GetValueAsString(NewPhase), State->Stage, *UKakurenboLibrary::FormatBigNumber(State->Coins));
	State->OnPhaseChanged.Broadcast(NewPhase);

	// 外周の高い壁はかくれんぼ中だけ（上から見下ろすパートで壁際のプレイヤーが隠れないように）
	if (Arena)
	{
		Arena->SetTallWallsVisible(NewPhase == EKakurenboPhase::Hide);
	}

	// BGM：かくれんぼ中は速くて少し怖い曲、それ以外はのんびりした曲
	if (UKakurenboSoundSubsystem* Sound = GetWorld()->GetSubsystem<UKakurenboSoundSubsystem>())
	{
		Sound->PlayMusic(NewPhase == EKakurenboPhase::Hide ? EKakurenboMusic::Hide : EKakurenboMusic::Calm);
	}

	// かくれんぼ以外のパートに入るたびに自動で保存する（かくれんぼの途中で終わっても、直前の状態から再開できる）。
	// タイトルでは保存しない（まだ遊んでいない状態でセーブを上書きしないように）
	if (NewPhase != EKakurenboPhase::Hide && NewPhase != EKakurenboPhase::Title)
	{
		SaveProgress();
	}
}

void AKakurenboGameMode::StartShopPhase()
{
	DespawnOnis();
	ClearTreasures();

	// ステージが進んで館のマップが変わるなら、ここで切り替える（置いていた壁・罠は在庫に戻る）
	const FName WantedMap = GetMapNameForStage(GS() ? GS()->Stage : 1);
	if (WantedMap != CurrentMapName)
	{
		SwitchToMap(WantedMap);
		ShowNotice(FText::Format(NSLOCTEXT("Kakurenbo", "NoticeNewMap", "新しい館「{0}」！ 置いていた壁と罠は在庫に戻しました"), GetCurrentMapDisplayName()), 7.f);
	}
	SetPhase(EKakurenboPhase::Shop);
}

int32 AKakurenboGameMode::ClearAllPlaced()
{
	AKakurenboGameState* State = GS();
	UKakurenboGridSubsystem* Grid = GetWorld()->GetSubsystem<UKakurenboGridSubsystem>();
	if (!State || !Grid || GetPhase() != EKakurenboPhase::Build)
	{
		return 0;
	}
	State->WallStock.SetNum(WallTypes.Num());
	State->TrapStock.SetNum(TrapTypes.Num());
	const TArray<int32> WallsBefore = State->WallStock;
	const TArray<int32> TrapsBefore = State->TrapStock;
	TArray<FIntPoint> Cells;
	for (int32 Y = 0; Y < Grid->GetSizeY(); ++Y)
	{
		for (int32 X = 0; X < Grid->GetSizeX(); ++X)
		{
			Cells.Add(FIntPoint(X, Y));
		}
	}
	Grid->ClearCells(Cells, State->WallStock, State->TrapStock);
	int32 Returned = 0;
	for (int32 i = 0; i < State->WallStock.Num(); ++i)
	{
		Returned += State->WallStock[i] - (WallsBefore.IsValidIndex(i) ? WallsBefore[i] : 0);
	}
	for (int32 i = 0; i < State->TrapStock.Num(); ++i)
	{
		Returned += State->TrapStock[i] - (TrapsBefore.IsValidIndex(i) ? TrapsBefore[i] : 0);
	}
	Sfx2D(this, EKakurenboSfx::PickUp, 1.f, 0.8f);
	ShowNotice(FText::Format(NSLOCTEXT("Kakurenbo", "NoticeClearAll", "置いた壁と罠をすべて回収しました（{0} 個を在庫に戻しました）"), Returned));
	UE_LOG(LogKakurenbo, Log, TEXT("Cleared all placed walls / traps (%d returned to stock)"), Returned);
	return Returned;
}

void AKakurenboGameMode::ReturnToShop()
{
	if (GetPhase() == EKakurenboPhase::Build)
	{
		StartShopPhase();
	}
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
	State->StepsThisRound = 0;
	State->ClimbsThisRound = 0;
	State->SummonsThisRound = 0;
	State->TreasuresCollectedThisRound = 0;
	State->TreasureCoinsThisRound = 0.0;
	State->GemsThisRound = 0;
	State->bAllTreasuresThisRound = false;
	State->TreasureBonusCoinsThisRound = 0.0;
	DarumaTimeLeft = 0.f;
	bGoldRushThisRound = false;
	State->TimeCoinsThisRound = 0.0;
	State->MashCoinsThisRound = 0.0;
	State->HideTimeLimit = GetHideDuration();
	State->HideTimeRemaining = State->HideTimeLimit;
	State->HideStartCountdown = HideStartDelay;
	bOnisSpawnedThisRound = false;
	LastCountdownSecond = 0;
	LastTimeTickSecond = 0;
	ApplyPrestigeToPlayer(); // 煙幕ダッシュ・よじ登る（転生のお店で解放）
	if (AHiderCharacter* Hider = Cast<AHiderCharacter>(GetPlayerCharacter()))
	{
		Hider->ResetDash();          // ラウンドの始めに煙幕ダッシュの回数を元に戻す
		Hider->SetSneaking(false);   // しゃがみも元に戻す（立って始める）
	}

	// お宝は最初から置いておく（鬼が来る前に取りに行ける）
	SpawnTreasures();

	// 真っ暗ブロックを置いていれば、このラウンドは館じゅうが真っ暗。ゴールドラッシュを置いていれば、連打のコインが 2 倍（何個置いても 2 倍）
	bool bDark = false;
	float DarkRadius = DarkSightRadius;
	if (const UKakurenboGridSubsystem* Grid = GetWorld()->GetSubsystem<UKakurenboGridSubsystem>())
	{
		for (const ATrapActor* Trap : Grid->GetAllTraps())
		{
			if (Trap && Trap->GetKind() == ETrapKind::Darkness)
			{
				bDark = true;
				DarkRadius = Trap->Def.Radius > 0.f ? Trap->Def.Radius : DarkRadius;
			}
			if (Trap && Trap->GetKind() == ETrapKind::GoldRush && !bGoldRushThisRound)
			{
				bGoldRushThisRound = true;
				ShowPopup(FString::Printf(TEXT("ゴールドラッシュ！ 連打のコイン ×%s"), *UKakurenboLibrary::FormatStatNumber(GoldRushMashMultiplier)), Trap->Def.Color);
				UE_LOG(LogKakurenbo, Log, TEXT("Gold rush this round"));
			}
		}
	}
	SetMansionDark(bDark, DarkRadius);
	// のれんの耐久（通常壁の 2 倍。補強・硬さを買った後の値）
	if (UKakurenboGridSubsystem* Grid = GetWorld()->GetSubsystem<UKakurenboGridSubsystem>())
	{
		for (ATrapActor* Trap : Grid->GetAllTraps())
		{
			if (Trap && Trap->GetKind() == ETrapKind::OneWay)
			{
				Trap->SetGateHP(GetOneWayMaxHP());
			}
		}
		Grid->NotifyChanged(); // 鬼の経路（のれんを壊すコスト）を作り直させる
	}
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
	// 真っ暗ブロックはこのラウンドで使い切る（設計図は残るので、在庫があれば次の設置パートで置き直される）
	ConsumeRoundGadgets();
	SetMansionDark(false, DarkSightRadius);
	DarumaTimeLeft = 0.f;
	bGoldRushThisRound = false; // 結果画面の「連打 +」は MashCoinsThisRound で、ゴールドラッシュの分も入っている

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
	case EKakurenboPhase::Title:  break; // タイトルはメニューで選ぶ
	}
}

// ---------------------------------------------------------------- 館のマップ

FName AKakurenboGameMode::GetMapNameForStage(int32 Stage) const
{
	if (bMapOverride)
	{
		return MapOverride;
	}
	Stage = FMath::Max(1, Stage);
	// 表の中：そのステージまでで最後に書いてあるマップ
	FName Map = NAME_None;
	for (int32 i = 0; i < FMath::Min(Stage, StageRows.Num()); ++i)
	{
		if (!StageRows[i].Map.IsNone())
		{
			Map = StageRows[i].Map;
		}
	}
	// 表より後：StagesPerMapAfterTable ステージごとに、Maps.csv の後ろの MapsCycledAfterTable 個（大きい館）を順に回る
	if (Stage > StageRows.Num() && MapOrder.Num() > 0 && StageRows.Num() > 0)
	{
		const int32 Steps = (Stage - StageRows.Num()) / FMath::Max(1, StagesPerMapAfterTable);
		const int32 Cycle = FMath::Clamp(MapsCycledAfterTable, 1, MapOrder.Num());
		const int32 First = MapOrder.Num() - Cycle;
		const int32 Last = FMath::Max(First, MapOrder.IndexOfByKey(Map));
		Map = MapOrder[First + (Last - First + Steps) % Cycle];
	}
	if (!MapRows.Contains(Map))
	{
		Map = MapOrder.Num() > 0 ? MapOrder[0] : NAME_None;
	}
	return Map;
}

FText AKakurenboGameMode::GetCurrentMapDisplayName() const
{
	const FKakurenboMapRow* Row = MapRows.Find(CurrentMapName);
	return Row ? Row->DisplayName : CurrentMapName.IsNone() ? FText::GetEmpty() : FText::FromName(CurrentMapName);
}

KakurenboMaps::FLayout AKakurenboGameMode::LoadMapLayout(const FKakurenboMapRow& Row, KakurenboMaps::FLayout* OutFloorLayer) const
{
	FString Text;
	if (!Row.LayoutFile.IsEmpty() && !FFileHelper::LoadFileToString(Text, *KakurenboBalance::GetDataFilePath(FString(TEXT("Maps")) / Row.LayoutFile)))
	{
		UE_LOG(LogKakurenbo, Warning, TEXT("Map layout '%s' could not be read. The map is empty."), *Row.LayoutFile);
	}
	TArray<FString> Problems;
	// 館の大きさは間取りの文字数・行数で決まる（間取りの無い舞台＝テスト用の家具の無い舞台は GridSizeX × GridSizeY）
	int32 SizeX = GridSizeX;
	int32 SizeY = GridSizeY;
	KakurenboMaps::MeasureLayout(Text, SizeX, SizeY);
	KakurenboMaps::FLayout Layout = KakurenboMaps::ParseLayout(Text, SizeX, SizeY, Problems);
	for (const FString& Problem : Problems)
	{
		UE_LOG(LogKakurenbo, Warning, TEXT("Map layout '%s': %s"), *Row.LayoutFile, *Problem);
	}
	if (OutFloorLayer)
	{
		*OutFloorLayer = KakurenboMaps::ParseFloorLayer(Text, SizeX, SizeY); // 部屋ごとの床の種類（無ければ全部マップの床）
	}
	return Layout;
}

void AKakurenboGameMode::ApplyCurrentMap()
{
	UKakurenboGridSubsystem* Grid = GetWorld()->GetSubsystem<UKakurenboGridSubsystem>();
	if (!Arena || !Grid || !Grid->IsConfigured())
	{
		return;
	}
	FKakurenboMapRow Row;
	if (const FKakurenboMapRow* Found = MapRows.Find(CurrentMapName))
	{
		Row = *Found;
	}
	KakurenboMaps::FLayout FloorLayer;
	const KakurenboMaps::FLayout Layout = LoadMapLayout(Row, &FloorLayer);

	// 大きさが変わるなら、床・外周の壁・門とグリッドを作り直す（置いてある壁・罠は呼ぶ前に片付けておく）
	if (Grid->GetSizeX() != Layout.SizeX || Grid->GetSizeY() != Layout.SizeY)
	{
		Arena->RebuildShell(Layout.SizeX, Layout.SizeY);
		Grid->Configure(Arena->GetGridOrigin(), Arena->GridSizeX, Arena->GridSizeY, Arena->CellSize, BlockHeight, MaxStackHeight);
		Grid->SetReservedCells(Arena->GetOniGateCells());
		if (AKakurenboGameState* State = GS())
		{
			Arena->SetTallWallsVisible(State->Phase == EKakurenboPhase::Hide);
		}
	}

	// 鬼の出入り口の前（と、そこから出られるよう 1 列手前まで）には家具を置かない
	TArray<FIntPoint> KeepClear;
	for (const FIntPoint& Gate : GetOniGateCells())
	{
		for (int32 DY = -1; DY <= 1; ++DY)
		{
			for (int32 DX = -1; DX <= 0; ++DX)
			{
				KeepClear.AddUnique(Gate + FIntPoint(DX, DY));
			}
		}
	}
	TArray<FIntPoint> Obstacles;
	// 床の種類ごとの見た目（市松模様の暗い方は少し暗く）。'w' はマップの床。テクスチャが無いパソコンでは色
	TMap<TCHAR, FKakurenboFloorLook> FloorLooks;
	{
		FKakurenboFloorLook Default;
		Default.Material = MakeSurfaceMaterial(Row.FloorSurface, Row.FloorTint);
		Default.MaterialAlt = MakeSurfaceMaterial(Row.FloorSurface, Row.FloorTint * FloorCheckerDarken);
		Default.Color = Row.FloorColor;
		FloorLooks.Add(KakurenboMaps::DefaultFloorChar, Default);
	}
	for (TCHAR& C : FloorLayer.Cells)
	{
		C = FChar::ToLower(C);
	}
	for (const TCHAR Kind : TSet<TCHAR>(FloorLayer.Cells))
	{
		const FKakurenboFloorKindRow* KindRow = FloorKindRows.Find(Kind);
		if (!KindRow || FloorLooks.Contains(Kind))
		{
			continue; // 表に無い文字はマップの床
		}
		FKakurenboFloorLook Look;
		const FName Surface = KindRow->Surface.IsNone() ? Row.FloorSurface : KindRow->Surface;
		const FLinearColor Tint = (KindRow->Surface.IsNone() ? Row.FloorTint : FLinearColor::White) * KindRow->Tint;
		Look.Material = MakeSurfaceMaterial(Surface, Tint);
		Look.MaterialAlt = MakeSurfaceMaterial(Surface, Tint * KindRow->CheckerDarken);
		Look.Color = KindRow->Color;
		FloorLooks.Add(Kind, Look);
	}
	// 壁（と腰壁）の見た目（Data/Surfaces.csv。テクスチャが無いパソコンでは色）
	UMaterialInstanceDynamic* Wall = MakeSurfaceMaterial(Row.WallSurface, Row.WallTint);
	UMaterialInstanceDynamic* Wainscot = MakeSurfaceMaterial(Row.WainscotSurface, Row.WainscotTint);
	// 壁の飾りは本物の館だけ（家具の無いテスト用の舞台には飾らない）
	Arena->ApplyMap(Row, Layout, FurnitureRows, KeepClear, Obstacles, FloorLayer, FloorLooks, Wall, Wainscot, Row.LayoutFile.IsEmpty() ? nullptr : &DecorRows);
	Grid->SetObstacles(Obstacles);
	UE_LOG(LogKakurenbo, Log, TEXT("Map -> %s (%d blocked cells, %d floor kinds)"), *CurrentMapName.ToString(), Obstacles.Num(), Arena->GetFloorKindCount());
}

void AKakurenboGameMode::SwitchToMap(FName NewMap)
{
	AKakurenboGameState* State = GS();
	UKakurenboGridSubsystem* Grid = GetWorld()->GetSubsystem<UKakurenboGridSubsystem>();
	if (!State || !Grid || !Grid->IsConfigured())
	{
		return;
	}
	State->WallStock.SetNum(WallTypes.Num());
	State->TrapStock.SetNum(TrapTypes.Num());

	// 今のマップの設計図をとっておき、置いてあった壁・罠は在庫に戻す
	TArray<FKakurenboSavedColumn> Columns;
	Grid->ExportLayout(Columns);
	int32 Returned = 0;
	for (FKakurenboSavedColumn& Column : Columns)
	{
		for (const int32 Type : Column.Live)
		{
			if (State->WallStock.IsValidIndex(Type))
			{
				State->WallStock[Type]++;
				++Returned;
			}
		}
		if (Column.bTrapLive && State->TrapStock.IsValidIndex(Column.TrapDesign))
		{
			State->TrapStock[Column.TrapDesign]++;
			++Returned;
		}
		Column.Live.Reset();
		Column.bTrapLive = false;
	}
	Columns.RemoveAll([](const FKakurenboSavedColumn& Column) { return Column.Design.Num() == 0 && Column.TrapDesign == INDEX_NONE; });
	if (!CurrentMapName.IsNone() && Columns.Num() > 0)
	{
		FKakurenboSavedLayout Stored;
		Stored.Columns = Columns;
		StoredMapLayouts.Add(CurrentMapName, Stored);
	}
	Grid->ClearAllBlocks();

	// 次のマップを作り、前に来たときの設計図があれば戻す（壁は設置パートで在庫から直る）
	const FName OldMap = CurrentMapName;
	CurrentMapName = NewMap;
	ApplyCurrentMap();
	if (const FKakurenboSavedLayout* Stored = StoredMapLayouts.Find(NewMap))
	{
		TArray<FKakurenboSavedColumn> StoredColumns = Stored->Columns;
		RefundColumnsOutsideGrid(StoredColumns);
		Grid->ImportLayout(StoredColumns, GetEffectiveWallTypes(), TrapTypes);
		StoredMapLayouts.Remove(NewMap);
		ClearLayoutOnObstacles();
	}
	EnsurePlayerOnFreeCell();
	UE_LOG(LogKakurenbo, Log, TEXT("Switched map %s -> %s (%d walls / traps returned to stock)"), *OldMap.ToString(), *NewMap.ToString(), Returned);
}

void AKakurenboGameMode::RefundColumnsOutsideGrid(TArray<FKakurenboSavedColumn>& Columns)
{
	// 今の館の外になったマスの壁・罠は在庫に戻し、設計図は捨てる
	AKakurenboGameState* State = GS();
	const UKakurenboGridSubsystem* Grid = GetWorld()->GetSubsystem<UKakurenboGridSubsystem>();
	if (!State || !Grid)
	{
		return;
	}
	State->WallStock.SetNum(WallTypes.Num());
	State->TrapStock.SetNum(TrapTypes.Num());
	Columns.RemoveAll([&](const FKakurenboSavedColumn& Column)
	{
		if (Grid->IsInside(Column.Cell))
		{
			return false;
		}
		for (const int32 Type : Column.Live)
		{
			if (State->WallStock.IsValidIndex(Type))
			{
				State->WallStock[Type]++;
			}
		}
		if (Column.bTrapLive && State->TrapStock.IsValidIndex(Column.TrapDesign))
		{
			State->TrapStock[Column.TrapDesign]++;
		}
		return true;
	});
}

void AKakurenboGameMode::ClearLayoutOnObstacles()
{
	AKakurenboGameState* State = GS();
	UKakurenboGridSubsystem* Grid = GetWorld()->GetSubsystem<UKakurenboGridSubsystem>();
	if (!State || !Grid)
	{
		return;
	}
	TArray<FIntPoint> Cells;
	for (int32 Y = 0; Y < Grid->GetSizeY(); ++Y)
	{
		for (int32 X = 0; X < Grid->GetSizeX(); ++X)
		{
			// 家具のマスと、鬼の出入り口の前（館の大きさが変わると門の場所も変わる）
			if (Grid->IsObstacle(FIntPoint(X, Y)) || Grid->IsReservedCell(FIntPoint(X, Y)))
			{
				Cells.Add(FIntPoint(X, Y));
			}
		}
	}
	State->WallStock.SetNum(WallTypes.Num());
	State->TrapStock.SetNum(TrapTypes.Num());
	if (const int32 Cleared = Grid->ClearCells(Cells, State->WallStock, State->TrapStock))
	{
		UE_LOG(LogKakurenbo, Log, TEXT("%d wall/trap cells overlapped furniture and were returned to stock"), Cleared);
	}
}

void AKakurenboGameMode::EnsurePlayerOnFreeCell()
{
	UKakurenboGridSubsystem* Grid = GetWorld()->GetSubsystem<UKakurenboGridSubsystem>();
	ACharacter* Player = GetPlayerCharacter();
	if (!Grid || !Player || !Grid->IsConfigured())
	{
		return;
	}
	const FIntPoint Here = Grid->WorldToCell(Player->GetActorLocation());
	const TArray<bool> MainArea = Grid->GetMainAreaMask();
	auto IsGood = [&](const FIntPoint& Cell)
	{
		return Grid->IsWalkable(Cell) && MainArea[Cell.Y * Grid->GetSizeX() + Cell.X] && !Grid->IsReservedCell(Cell);
	};
	if (Grid->IsInside(Here) && IsGood(Here))
	{
		return;
	}
	// 舞台の真ん中に一番近い、広い場所の空きマスへ
	const FIntPoint Center(Grid->GetSizeX() / 2, Grid->GetSizeY() / 2);
	FIntPoint Best = Here;
	int32 BestDistSq = MAX_int32;
	for (int32 Y = 0; Y < Grid->GetSizeY(); ++Y)
	{
		for (int32 X = 0; X < Grid->GetSizeX(); ++X)
		{
			const FIntPoint Cell(X, Y);
			const int32 DistSq = FMath::Square(X - Center.X) + FMath::Square(Y - Center.Y);
			if (IsGood(Cell) && DistSq < BestDistSq)
			{
				BestDistSq = DistSq;
				Best = Cell;
			}
		}
	}
	const FVector Floor = Grid->CellFloorCenter(Best);
	Player->SetActorLocation(FVector(Floor.X, Floor.Y, Floor.Z + Player->GetSimpleCollisionHalfHeight() + 2.f), false, nullptr, ETeleportType::TeleportPhysics);
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

double AKakurenboGameMode::GetPrestigeStartCoins(int32 PrestigeCount) const
{
	return KakurenboBalance::GetPrestigeStartCoins(PrestigeCount, PrestigeSettings);
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
	// 宝石の道具は宝石で買ったものなので、転生してもなくならない（置いてあった分は在庫に戻す）
	TArray<int32> GemStock;
	GemStock.Init(0, TrapTypes.Num());
	for (const int32 Type : GetTrapTypesInShop(true))
	{
		GemStock[Type] = (State->TrapStock.IsValidIndex(Type) ? State->TrapStock[Type] : 0) + Grid->GetLiveTrapCount(Type);
	}
	Grid->ClearLiveKeepDesign();
	// はじめのコインは転生した回数ぶん増える（強化ではなく自動）
	State->Coins = GetPrestigeStartCoins(State->PrestigeCount);
	State->Stage = 1;
	State->MashIncomeLevel = 0;
	State->TimeIncomeLevel = 0;
	State->WallReinforceLevel = 0;
	State->WallStock.Init(0, WallTypes.Num());
	State->TrapStock = GemStock;
	State->LastRepairedWalls = 0;
	State->LastUnrepairedWalls = 0;
	State->LastRefilledTraps = 0;
	State->LastUnrefilledTraps = 0;

	UE_LOG(LogKakurenbo, Log, TEXT("Prestige #%d: +%d points (%d to spend, %d in total)"),
		State->PrestigeCount, Gained, State->PrestigePoints, State->TotalPrestigePoints);
	Sfx2D(this, EKakurenboSfx::Prestige);
	FlashScreen(FLinearColor(0.7f, 0.5f, 1.f, 0.6f), 1.2f);
	ShowNotice(FText::Format(NSLOCTEXT("Kakurenbo", "NoticePrestige", "転生した。転生ポイント +{0}・はじめのコイン {1}（ポイントは「転生のお店」で）"),
		Gained, FText::FromString(UKakurenboLibrary::FormatBigNumber(State->Coins))), 6.f);

	// まずは転生のお店で、もらったポイントを使ってもらう（そのあと設置 → ステージ 1 のかくれんぼ）
	StartShopPhase();
	if (AKakurenboPlayerController* PC = Cast<AKakurenboPlayerController>(GetWorld()->GetFirstPlayerController()))
	{
		PC->ShopTab = EKakurenboShopTab::Prestige;
	}
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
	Save->WallReinforceLevel = State->WallReinforceLevel;
	Save->PrestigePoints = State->PrestigePoints;
	Save->TotalPrestigePoints = State->TotalPrestigePoints;
	Save->PrestigeCount = State->PrestigeCount;
	Save->PrestigeLevels = State->PrestigeLevels;
	Save->WallStock = State->WallStock;
	Save->TrapStock = State->TrapStock;
	Save->Gems = State->Gems;
	Grid->ExportLayout(Save->Columns);
	Save->CurrentMap = CurrentMapName;
	Save->MapLayouts = StoredMapLayouts;
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
	State->WallReinforceLevel = Save->SaveVersion >= 6 ? Save->WallReinforceLevel : 0;
	// 転生のお店の強化は壁を作り直す前に戻す（耐久の倍率に使う）
	State->PrestigePoints = Save->PrestigePoints;
	State->TotalPrestigePoints = Save->SaveVersion >= 4 ? Save->TotalPrestigePoints : Save->PrestigePoints;
	State->PrestigeCount = Save->PrestigeCount;
	State->PrestigeLevels = Save->PrestigeLevels;
	State->PrestigeLevels.SetNum(static_cast<int32>(EPrestigeUpgrade::Count));

	// 版 5 までの壁（木・石・鉄・消音壁）は、今の種類（通常壁・消音壁・クライミング壁）に付け替える
	TArray<int32> LoadedWallStock = Save->WallStock;
	TArray<FKakurenboSavedColumn> SavedColumns = Save->Columns;
	TMap<FName, FKakurenboSavedLayout> LoadedLayouts = Save->MapLayouts;
	if (Save->SaveVersion < 6)
	{
		TArray<int32> Unused;
		RemapLegacyWallTypes(LoadedWallStock, SavedColumns);
		for (TPair<FName, FKakurenboSavedLayout>& Pair : LoadedLayouts)
		{
			RemapLegacyWallTypes(Unused, Pair.Value.Columns);
		}
		UE_LOG(LogKakurenbo, Log, TEXT("Converted the walls of an old save (version %d) to the new wall types"), Save->SaveVersion);
	}
	State->WallStock = LoadedWallStock;
	State->WallStock.SetNum(WallTypes.Num()); // CSV で壁の種類が増減していても合わせる
	State->TrapStock = Save->TrapStock;
	State->TrapStock.SetNum(TrapTypes.Num());
	State->Gems = Save->SaveVersion >= 7 ? Save->Gems : 0;

	// マップ（版 4 までのセーブには無いので、ステージ 1 のマップ）を先に作ってから、壁・罠を置き直す
	Grid->ClearAllBlocks();
	StoredMapLayouts = LoadedLayouts;
	CurrentMapName = (!Save->CurrentMap.IsNone() && MapRows.Contains(Save->CurrentMap)) ? Save->CurrentMap : GetMapNameForStage(1);
	StoredMapLayouts.Remove(CurrentMapName);
	ApplyCurrentMap();
	RefundColumnsOutsideGrid(SavedColumns); // 館の大きさが変わった（古いセーブ）
	Grid->ImportLayout(SavedColumns, GetEffectiveWallTypes(), TrapTypes);
	ClearLayoutOnObstacles(); // 家具と重なった壁（古いセーブ・間取りを変えたとき）は在庫に戻す

	if (Save->bHasPlayerLocation)
	{
		if (ACharacter* Player = GetPlayerCharacter())
		{
			Player->SetActorLocation(Save->PlayerLocation, false, nullptr, ETeleportType::TeleportPhysics);
		}
	}
	EnsurePlayerOnFreeCell();
	ApplyPrestigeToPlayer();
	UE_LOG(LogKakurenbo, Log, TEXT("Loaded save '%s': stage %d, coins %s, %d wall columns"),
		*SaveSlotName, State->Stage, *UKakurenboLibrary::FormatBigNumber(State->Coins), Save->Columns.Num());
	return true;
}

int32 AKakurenboGameMode::RemapLegacyWallType(int32 OldType)
{
	// 版 5 まで: 0 木 / 1 石 / 2 鉄 / 3 消音壁 → 今: 0 通常壁 / 1 消音壁（クライミング壁は新しい）
	return OldType == 3 ? 1 : 0;
}

void AKakurenboGameMode::RemapLegacyWallTypes(TArray<int32>& Stock, TArray<FKakurenboSavedColumn>& Columns)
{
	TArray<int32> NewStock;
	NewStock.Init(0, 3);
	for (int32 i = 0; i < Stock.Num(); ++i)
	{
		NewStock[RemapLegacyWallType(i)] += Stock[i];
	}
	if (Stock.Num() > 0)
	{
		Stock = NewStock;
	}
	for (FKakurenboSavedColumn& Column : Columns)
	{
		for (int32& Type : Column.Design)
		{
			Type = RemapLegacyWallType(Type);
		}
		for (int32& Type : Column.Live)
		{
			Type = RemapLegacyWallType(Type);
		}
	}
}

void AKakurenboGameMode::ResetProgress(bool bShowNotice)
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
	StoredMapLayouts.Reset();
	CurrentMapName = NAME_None;

	State->Coins = 0.0;
	State->Stage = 1;
	State->MashIncomeLevel = 0;
	State->TimeIncomeLevel = 0;
	State->WallReinforceLevel = 0;
	State->PrestigePoints = 0;
	State->TotalPrestigePoints = 0;
	State->PrestigeCount = 0;
	State->PrestigeLevels.Init(0, static_cast<int32>(EPrestigeUpgrade::Count));
	State->WallStock.Init(0, WallTypes.Num());
	State->TrapStock.Init(0, TrapTypes.Num());
	State->Gems = 0;
	State->LastRepairedWalls = 0;
	State->LastUnrepairedWalls = 0;
	State->LastRefilledTraps = 0;
	State->LastUnrefilledTraps = 0;

	if (ACharacter* Player = GetPlayerCharacter())
	{
		Player->SetActorLocation(FVector(CellSize * 0.5f, CellSize * 0.5f, 120.f), false, nullptr, ETeleportType::TeleportPhysics);
	}
	SwitchToMap(GetMapNameForStage(1));
	UE_LOG(LogKakurenbo, Log, TEXT("Progress reset"));
	if (bShowNotice)
	{
		ShowNotice(NSLOCTEXT("Kakurenbo", "NoticeReset", "はじめから"), 3.f);
	}

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
