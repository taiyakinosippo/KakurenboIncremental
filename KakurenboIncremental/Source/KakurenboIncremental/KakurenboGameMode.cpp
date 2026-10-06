#include "KakurenboGameMode.h"

#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerStart.h"
#include "HiderCharacter.h"
#include "KakurenboArena.h"
#include "KakurenboGameState.h"
#include "KakurenboHUD.h"
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

	// 開始前のカウントダウン
	if (State->HideStartCountdown > 0.f)
	{
		State->HideStartCountdown = FMath::Max(0.f, State->HideStartCountdown - DeltaSeconds);
		return;
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
	return Items;
}

bool AKakurenboGameMode::TryBuyShopItem(int32 Index)
{
	switch (Index)
	{
	case KakurenboShop::MashUpgrade: return TryBuyMashUpgrade();
	case KakurenboShop::TimeUpgrade: return TryBuyTimeUpgrade();
	default: return false;
	}
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
