#include "KakurenboGameMode.h"

#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerStart.h"
#include "HiderCharacter.h"
#include "KakurenboArena.h"
#include "DrawDebugHelpers.h"
#include "KakurenboGameState.h"
#include "KakurenboGridSubsystem.h"
#include "KakurenboHUD.h"
#include "OniCharacter.h"
#include "PlaceableBlock.h"
#include "KakurenboLibrary.h"
#include "KakurenboPlayerController.h"

DEFINE_LOG_CATEGORY_STATIC(LogKakurenbo, Log, All);

AKakurenboGameMode::AKakurenboGameMode()
{
	PrimaryActorTick.bCanEverTick = true;

	// このゲームで使うクラスを登録する（BP の派生クラスに差し替えてもよい）
	DefaultPawnClass = AHiderCharacter::StaticClass();
	PlayerControllerClass = AKakurenboPlayerController::StaticClass();
	GameStateClass = AKakurenboGameState::StaticClass();
	HUDClass = AKakurenboHUD::StaticClass();
	OniClass = AOniCharacter::StaticClass();

	// 壁の初期設定。鬼の攻撃力はステージごとに 1.6 倍になるので、
	// 木は常に一撃、石はステージ 4 から一撃、鉄はステージ 7 から一撃で壊れる
	auto AddWall = [this](const TCHAR* Name, double HP, double Cost, FLinearColor Color)
	{
		FWallTypeDef& Def = WallTypes.AddDefaulted_GetRef();
		Def.DisplayName = FText::FromString(Name);
		Def.MaxHP = HP;
		Def.Cost = Cost;
		Def.Color = Color;
	};
	AddWall(TEXT("木の壁"), 1.0, 10.0, FLinearColor(0.55f, 0.35f, 0.18f));
	AddWall(TEXT("石の壁"), 4.0, 120.0, FLinearColor(0.22f, 0.22f, 0.25f));
	AddWall(TEXT("鉄の壁"), 16.0, 1500.0, FLinearColor(0.25f, 0.35f, 0.55f));
}

AKakurenboGameState* AKakurenboGameMode::GS() const
{
	return GetGameState<AKakurenboGameState>();
}

void AKakurenboGameMode::BeginPlay()
{
	Super::BeginPlay();

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
	}

	// グリッド（ブロック配置・鬼の経路探索）を舞台に合わせて初期化
	if (UKakurenboGridSubsystem* Grid = GetWorld()->GetSubsystem<UKakurenboGridSubsystem>())
	{
		Grid->Configure(Arena->GetGridOrigin(), Arena->GridSizeX, Arena->GridSizeY, Arena->CellSize, MaxStackHeight);
	}

	// 最初のラウンドは「何もない空間で棒立ちのまま連打」から始まる
	StartHidePhase();
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
		if (State->HideStartCountdown <= 0.f)
		{
			SpawnOni();
		}
		return;
	}

	State->bOniActive = Oni != nullptr;
	if (Oni)
	{
		State->OniState = Oni->GetOniState();
	}

	// 時間収入（毎フレーム、経過時間ぶんだけ加算）
	State->AddCoins(GetTimeIncomePerSecond() * DeltaSeconds, true);

	State->HideTimeRemaining -= DeltaSeconds;
	if (State->HideTimeRemaining <= 0.f)
	{
		State->HideTimeRemaining = 0.f;
		EndHidePhase(true);
	}
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

double AKakurenboGameMode::GetClearReward() const
{
	const AKakurenboGameState* State = GS();
	return UKakurenboLibrary::ExpCurve(ClearRewardBase, ClearRewardGrowth, State ? State->Stage - 1 : 0);
}

float AKakurenboGameMode::GetHideDuration() const
{
	const AKakurenboGameState* State = GS();
	return HideDurationBase + HideDurationPerStage * ((State ? State->Stage : 1) - 1);
}

double AKakurenboGameMode::GetOniAttackDamage() const
{
	const AKakurenboGameState* State = GS();
	return UKakurenboLibrary::ExpCurve(OniAttackDamageBase, OniAttackDamageGrowth, State ? State->Stage - 1 : 0);
}

float AKakurenboGameMode::GetOniHearingRadius() const
{
	const AKakurenboGameState* State = GS();
	return OniHearingRadiusBase + OniHearingRadiusPerStage * ((State ? State->Stage : 1) - 1);
}

EKakurenboPhase AKakurenboGameMode::GetPhase() const
{
	const AKakurenboGameState* State = GS();
	return State ? State->Phase : EKakurenboPhase::Shop;
}

// ---------------------------------------------------------------- 購入

// 商品の並び: [0] 連打強化, [1] 時間収入強化, [2〜] 壁（M3 で追加）
namespace KakurenboShop
{
	constexpr int32 MashUpgrade = 0;
	constexpr int32 TimeUpgrade = 1;
	constexpr int32 NumUpgrades = 2;
}

TArray<FShopItemView> AKakurenboGameMode::GetShopItems() const
{
	TArray<FShopItemView> Items;
	const AKakurenboGameState* State = GS();
	if (!State)
	{
		return Items;
	}

	auto Fmt = [](double V) { return FText::FromString(UKakurenboLibrary::FormatBigNumber(V)); };

	{
		FShopItemView& Item = Items.AddDefaulted_GetRef();
		Item.DisplayName = NSLOCTEXT("Kakurenbo", "ShopMash", "連打コイン強化");
		Item.Cost = GetMashUpgradeCost();
		Item.Description = FText::Format(NSLOCTEXT("Kakurenbo", "ShopMashDesc", "1回 {0} → {1} コイン"),
			Fmt(GetMashIncome()), Fmt(UKakurenboLibrary::ExpCurve(MashIncomeBase, MashIncomeGrowth, State->MashIncomeLevel + 1)));
		Item.OwnedText = FText::Format(NSLOCTEXT("Kakurenbo", "ShopLevel", "Lv.{0}"), State->MashIncomeLevel);
	}
	{
		FShopItemView& Item = Items.AddDefaulted_GetRef();
		Item.DisplayName = NSLOCTEXT("Kakurenbo", "ShopTime", "時間コイン強化");
		Item.Cost = GetTimeUpgradeCost();
		Item.Description = FText::Format(NSLOCTEXT("Kakurenbo", "ShopTimeDesc", "毎秒 {0} → {1} コイン"),
			Fmt(GetTimeIncomePerSecond()), Fmt(UKakurenboLibrary::ExpCurve(TimeIncomeBase, TimeIncomeGrowth, State->TimeIncomeLevel + 1)));
		Item.OwnedText = FText::Format(NSLOCTEXT("Kakurenbo", "ShopLevel", "Lv.{0}"), State->TimeIncomeLevel);
	}
	for (int32 i = 0; i < WallTypes.Num(); ++i)
	{
		const FWallTypeDef& Def = WallTypes[i];
		FShopItemView& Item = Items.AddDefaulted_GetRef();
		Item.DisplayName = Def.DisplayName;
		Item.Cost = Def.Cost;
		// 今のステージの鬼に何発耐えるか
		const int32 Hits = FMath::Max(1, FMath::CeilToInt(Def.MaxHP / GetOniAttackDamage()));
		Item.Description = FText::Format(NSLOCTEXT("Kakurenbo", "ShopWallDesc", "耐久 {0}（今の鬼の攻撃 {1} 回で壊れる）"),
			Fmt(Def.MaxHP), Hits);
		Item.OwnedText = FText::Format(NSLOCTEXT("Kakurenbo", "ShopStock", "在庫 {0}"), State->WallStock.IsValidIndex(i) ? State->WallStock[i] : 0);
	}
	return Items;
}

bool AKakurenboGameMode::TryBuyShopItem(int32 Index)
{
	switch (Index)
	{
	case KakurenboShop::MashUpgrade: return TryBuyMashUpgrade();
	case KakurenboShop::TimeUpgrade: return TryBuyTimeUpgrade();
	default: return TryBuyWall(Index - KakurenboShop::NumUpgrades);
	}
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

	// プレイヤーと重なる場所には置けない（カプセルとブロックの箱が交わるかを調べる）
	if (const ACharacter* Pawn = Cast<ACharacter>(GetWorld()->GetFirstPlayerController() ? GetWorld()->GetFirstPlayerController()->GetPawn() : nullptr))
	{
		const float Half = Grid->GetCellSize() * 0.5f;
		const FVector BoxCenter = Grid->CellToWorld(Cell, Grid->GetColumnHeight(Cell));
		const FVector P = Pawn->GetActorLocation();
		const float Radius = Pawn->GetSimpleCollisionRadius();
		const float HalfHeight = Pawn->GetSimpleCollisionHalfHeight();

		const float DX = FMath::Max(0.f, FMath::Abs(P.X - BoxCenter.X) - Half);
		const float DY = FMath::Max(0.f, FMath::Abs(P.Y - BoxCenter.Y) - Half);
		const bool bOverlapXY = DX * DX + DY * DY < Radius * Radius;
		const bool bOverlapZ = FMath::Abs(P.Z - BoxCenter.Z) < HalfHeight + Half;
		if (bOverlapXY && bOverlapZ)
		{
			if (OutReason) *OutReason = NSLOCTEXT("Kakurenbo", "PlacePlayer", "自分と重なる場所には置けません");
			return false;
		}
	}
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
	if (!Grid->PlaceBlock(Cell, WallTypeIndex, Def.MaxHP, Def.Color))
	{
		return false;
	}
	GS()->WallStock[WallTypeIndex]--;
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
	if (!Grid->RemoveBlock(Block))
	{
		return false;
	}
	// 回収した壁は新品として在庫に戻る
	if (State->WallStock.IsValidIndex(TypeIndex))
	{
		State->WallStock[TypeIndex]++;
	}
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

	// 連打の音が鬼に届く
	if (Oni)
	{
		Oni->HearNoise(NoiseLocation);
	}
	if (bShowNoiseRing)
	{
		// 音の届く範囲を床に円で表示（開発用の描画機能を流用した仮の演出）
		const FVector Floor(NoiseLocation.X, NoiseLocation.Y, 3.f);
		DrawDebugCircle(GetWorld(), Floor, GetOniHearingRadius(), 64, FColor(255, 220, 80), false, 0.15f, 0, 4.f, FVector(1, 0, 0), FVector(0, 1, 0), false);
	}
}

// ---------------------------------------------------------------- 鬼

void AKakurenboGameMode::SpawnOni()
{
	DespawnOni();

	UKakurenboGridSubsystem* Grid = GetWorld()->GetSubsystem<UKakurenboGridSubsystem>();
	AHiderCharacter* Hider = Cast<AHiderCharacter>(GetWorld()->GetFirstPlayerController() ? GetWorld()->GetFirstPlayerController()->GetPawn() : nullptr);
	if (!Grid || !Hider || !OniClass)
	{
		return;
	}

	// プレイヤーからいちばん遠い空きマスに出現
	const FIntPoint Cell = Grid->FindFarthestFreeCell(Hider->GetActorLocation());
	const FVector Location = Grid->CellFloorCenter(Cell) + FVector(0.f, 0.f, 100.f);
	const FRotator Facing(0.f, (Hider->GetActorLocation() - Location).Rotation().Yaw, 0.f);

	Oni = GetWorld()->SpawnActorDeferred<AOniCharacter>(OniClass, FTransform(Facing, Location), nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn);
	if (!Oni)
	{
		return;
	}

	const int32 Stage = GS()->Stage;
	Oni->WanderSpeed = OniWanderSpeedBase + OniSpeedPerStage * (Stage - 1);
	Oni->InvestigateSpeed = OniInvestigateSpeedBase + OniSpeedPerStage * (Stage - 1);
	Oni->SightRadius = OniSightRadius;
	Oni->SightHalfAngle = OniSightHalfAngle;
	Oni->HearingRadius = GetOniHearingRadius();
	Oni->AttackDamage = GetOniAttackDamage();
	Oni->bDrawDebug = bDebugOni;
	Oni->FinishSpawning(FTransform(Facing, Location));

	// 鬼からの通知を受け取る（C# の event += に相当）
	Oni->OnFoundHider.AddUObject(this, &AKakurenboGameMode::HandleOniFoundHider);
	Oni->OnDestroyedWalls.AddUObject(this, &AKakurenboGameMode::HandleOniDestroyedWalls);
	Oni->Activate(Hider);

	UE_LOG(LogKakurenbo, Log, TEXT("Oni spawned at cell (%d,%d), damage %.2f"), Cell.X, Cell.Y, Oni->AttackDamage);
}

void AKakurenboGameMode::DespawnOni()
{
	if (Oni)
	{
		Oni->Destroy();
		Oni = nullptr;
	}
	if (AKakurenboGameState* State = GS())
	{
		State->bOniActive = false;
	}
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
}

void AKakurenboGameMode::StartShopPhase()
{
	DespawnOni();
	SetPhase(EKakurenboPhase::Shop);
}

void AKakurenboGameMode::StartBuildPhase()
{
	SetPhase(EKakurenboPhase::Build);
}

void AKakurenboGameMode::StartHidePhase()
{
	AKakurenboGameState* State = GS();
	if (!State)
	{
		return;
	}
	State->CoinsEarnedThisRound = 0.0;
	State->MashCountThisRound = 0;
	State->LastRoundWallsDestroyed = 0;
	State->HideTimeLimit = GetHideDuration();
	State->HideTimeRemaining = State->HideTimeLimit;
	State->HideStartCountdown = HideStartDelay;
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
	}
	// 見つかった場合もペナルティなし（稼いだコインはそのまま）

	// 見つかったときは鬼の姿を少しの間見せたいので、リザルト画面を抜けるときに消す
	if (bCleared)
	{
		DespawnOni();
	}
	else if (Oni)
	{
		Oni->SetActorTickEnabled(false);
	}

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
