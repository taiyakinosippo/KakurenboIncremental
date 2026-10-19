// テストプレイの自動プレイヤー（KakuPlaytest コマンド）。
// 「このゲームを何も知らない初心者」のつもりで本物のステージ（館）を遊び続け、1 ラウンドごとの記録を残す。
//   - 入力は本物と同じ経路：かくれんぼは疑似キー（WASD・連打・しゃがむ・煙幕・よじ登る・G）、購入は数字キーと同じ BuyItem、
//     設置は GameMode の PlaceWall / PlaceTrap（設置パートのクリックと同じ関数）、次へは Enter
//   - 初心者が知っていること：ヒント・鬼のチュートリアルに書いてあること・聞こえる足音（近くの鬼）・見えるお宝の光
//   - 記録：Saved/Playtest/rounds.csv（1 ラウンド 1 行）・events.log・shots/*.png（決めた枚数まで）
// 起動は Tools/RunPlaytest.ps1（時刻まで、落ちたら起動し直して続きから遊ぶ）

#include "KakurenboPlayerController.h"

#include "Containers/Ticker.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerInput.h"
#include "GridPathfinder.h"
#include "HiderCharacter.h"
#include "InputKeyEventArgs.h"
#include "KakurenboGameMode.h"
#include "KakurenboGameState.h"
#include "KakurenboGridSubsystem.h"
#include "KakurenboSettings.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "OniCharacter.h"
#include "TreasureActor.h"
#include "UnrealClient.h"

/** 自動プレイヤーの状態（PlayerController が TSharedPtr で持つ） */
struct FKakurenboPlaytest
{
	enum class EMove : uint8 { Hide, GoTreasure, Return, Flee };

	FDateTime Until;
	FString Dir;
	int32 ShotBudget = 220;
	/** お宝好きの遊び方（鬼が少し離れていれば遠くのお宝も取りに行く。宝石の流れを見るため） */
	bool bGreedy = false;
	/** 前の転生でもらったポイント（同じポイントしかもらえないなら、もう少しねばる） */
	int32 LastPrestigeGain = 0;
	int32 ShotsTaken = 0;
	TSet<FString> ShotOnce;
	FTSTicker::FDelegateHandle Ticker;

	// パートが変わってからの時間（実時間）と、そのパートでやることを済ませたか
	EKakurenboPhase Phase = EKakurenboPhase::Title;
	bool bPhaseKnown = false;
	double PhaseTime = 0.0;
	int32 PhaseStep = 0;
	double Clock = 0.0;

	// 通算
	int32 Rounds = 0;
	int32 Clears = 0;
	int32 HighestStage = 1;
	int32 FailsAtStage = 0;
	int32 FailStage = 0;
	TMap<FName, FIntPoint> HomeByMap;
	/** 設置パートで決めた隠れ場所（このラウンド）と、そこで見つかった回数（"館:x,y"。2 回見つかったら別の場所を試す） */
	FIntPoint BuildHome = FIntPoint(-1, -1);
	TMap<FString, int32> HomeCatches;
	FString LastIntroShot;

	// このラウンド
	double RoundCoinsAtStart = 0.0;
	int32 RoundGemsAtStart = 0;
	FString RoundOnis;
	int32 RoundChases = 0;
	int32 RoundTreasureTrips = 0;
	int32 RoundDashes = 0;
	int32 RoundDaruma = 0;
	int32 RoundStuck = 0;
	/** お宝を取りに出た時刻（このラウンド）と、お宝を取りに出て見つかった回数（ステージごと。初心者も懲りる） */
	float LastTreasureTripAt = -100.f;
	TMap<int32, int32> TreasureDeaths;
	float FirstChaseAt = -1.f;
	FString CaughtBy;
	FString CaughtState;
	FString ShopLog;
	FString BuildLog;
	bool bRoundActive = false;

	// かくれんぼの動き
	EMove Move = EMove::Hide;
	FIntPoint Home = FIntPoint(-1, -1);
	TArray<FIntPoint> Path;
	int32 PathIndex = 0;
	TWeakObjectPtr<ATreasureActor> TargetTreasure;
	bool bForwardDown = false;
	double NextMash = 0.0;
	double NextThink = 0.0;
	double NotChasedSince = 0.0;
	FVector StuckCheckPos = FVector::ZeroVector;
	double StuckCheckTime = 0.0;
	int32 StuckInARow = 0;
	FRandomStream Rand = FRandomStream(1234);
};

namespace
{
	const TCHAR* OniTypeName(EOniType Type)
	{
		switch (Type)
		{
		case EOniType::Balanced: return TEXT("Std");
		case EOniType::Scout:    return TEXT("Speed");
		case EOniType::Breaker:  return TEXT("Power");
		case EOniType::Careful:  return TEXT("Careful");
		case EOniType::Treasure: return TEXT("Treasure");
		case EOniType::Detector: return TEXT("Detector");
		default:                 return TEXT("?");
		}
	}

	/** 4 方向に家具（と館の外）を通らずにたどった歩数。置いた壁は自分の物なので通れるものとして数える */
	TArray<int32> FloodFromCells(const UKakurenboGridSubsystem* Grid, const TArray<FIntPoint>& Starts, bool bWallsBlock)
	{
		TArray<int32> Dist;
		Dist.Init(-1, Grid->GetSizeX() * Grid->GetSizeY());
		TArray<FIntPoint> Queue;
		for (const FIntPoint& S : Starts)
		{
			if (Grid->IsInside(S))
			{
				Dist[S.Y * Grid->GetSizeX() + S.X] = 0;
				Queue.Add(S);
			}
		}
		for (int32 Head = 0; Head < Queue.Num(); ++Head)
		{
			const FIntPoint C = Queue[Head];
			static const FIntPoint Dirs[4] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } };
			for (const FIntPoint& D : Dirs)
			{
				const FIntPoint N = C + D;
				if (!Grid->IsInside(N) || Grid->IsObstacle(N) || (bWallsBlock && Grid->GetColumnHeight(N) > 0))
				{
					continue;
				}
				int32& ND = Dist[N.Y * Grid->GetSizeX() + N.X];
				if (ND < 0)
				{
					ND = Dist[C.Y * Grid->GetSizeX() + C.X] + 1;
					Queue.Add(N);
				}
			}
		}
		return Dist;
	}
}

// ---------------------------------------------------------------- 開始・共通

void AKakurenboPlayerController::KakuPlaytest(const FString& Options)
{
	Playtest = MakeShared<FKakurenboPlaytest>();
	FKakurenboPlaytest& P = *Playtest;

	// 終わる時刻（今日のその時刻。過ぎていれば明日）
	FString UntilText = TEXT("08:00");
	FParse::Value(*Options, TEXT("Until="), UntilText);
	int32 Hour = 8, Minute = 0;
	FString H, M;
	if (UntilText.Split(TEXT(":"), &H, &M))
	{
		Hour = FCString::Atoi(*H);
		Minute = FCString::Atoi(*M);
	}
	const FDateTime Now = FDateTime::Now();
	P.Until = FDateTime(Now.GetYear(), Now.GetMonth(), Now.GetDay(), Hour, Minute);
	if (P.Until < Now)
	{
		P.Until += FTimespan::FromDays(1.0);
	}
	FParse::Value(*Options, TEXT("Shots="), P.ShotBudget);
	FParse::Value(*Options, TEXT("LastGain="), P.LastPrestigeGain); // 起動し直したとき、前の転生でもらったポイントを渡す
	FString Persona;
	P.bGreedy = FParse::Value(*Options, TEXT("Persona="), Persona) && Persona.Equals(TEXT("Greedy"), ESearchCase::IgnoreCase);
	P.Dir = FPaths::ProjectSavedDir() / TEXT("Playtest");
	P.Rand.Initialize(static_cast<int32>(Now.GetTicks() & 0x7fffffff));
	// 撮った枚数は前回の起動の分も数える（落ちて起動し直しても増えすぎないように）
	TArray<FString> Existing;
	IFileManager::Get().FindFiles(Existing, *(P.Dir / TEXT("shots") / TEXT("*.png")), true, false);
	P.ShotsTaken = Existing.Num();

	// 本物のキーボード・マウスは使わない（寝ている間に動かす）。ヒントと鬼のチュートリアルは初心者と同じく出す
	bIgnoreRealInputForAutoTest = true;
	bShowTipsInAutoTest = true;
	bShowOniIntroInAutoTest = true;
	ApplyAutoTestInputIsolation();

	TWeakObjectPtr<AKakurenboPlayerController> WeakThis(this);
	P.Ticker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakThis](float Dt) -> bool
	{
		AKakurenboPlayerController* PC = WeakThis.Get();
		if (!PC || !PC->Playtest)
		{
			return false;
		}
		PC->TickPlaytest(FMath::Min(Dt, 0.25f));
		return true;
	}));
	PlaytestLog(FString::Printf(TEXT("start (until %s, shots %d/%d)"), *P.Until.ToString(), P.ShotsTaken, P.ShotBudget));
}

void AKakurenboPlayerController::PlaytestLog(const FString& Line)
{
	if (!Playtest)
	{
		return;
	}
	const FString Stamped = FString::Printf(TEXT("%s %s\n"), *FDateTime::Now().ToString(TEXT("%H:%M:%S")), *Line);
	FFileHelper::SaveStringToFile(Stamped, *(Playtest->Dir / TEXT("events.log")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM, &IFileManager::Get(), FILEWRITE_Append);
	UE_LOG(LogTemp, Display, TEXT("[Playtest] %s"), *Line);
}

void AKakurenboPlayerController::PlaytestShot(const FString& Name)
{
	if (!Playtest || Playtest->ShotsTaken >= Playtest->ShotBudget)
	{
		return;
	}
	// ステージの写真は何周目か（転生した回数）を付ける。同じ名前の写真が前の起動で撮ってあれば撮らない（起動し直しでタイトル・ヒントが増えないように）
	const AKakurenboGameState* S = GetWorld() ? GetWorld()->GetGameState<AKakurenboGameState>() : nullptr;
	const FString FullName = (Name.StartsWith(TEXT("s")) && S) ? FString::Printf(TEXT("p%d_%s"), S->PrestigeCount, *Name) : Name;
	TArray<FString> Same;
	IFileManager::Get().FindFiles(Same, *(Playtest->Dir / TEXT("shots") / (TEXT("*_") + FullName + TEXT(".png"))), true, false);
	if (Same.Num() > 0)
	{
		return;
	}
	++Playtest->ShotsTaken;
	const FString Path = Playtest->Dir / TEXT("shots") / FString::Printf(TEXT("%04d_%s.png"), Playtest->ShotsTaken, *FullName);
	FScreenshotRequest::RequestScreenshot(Path, true, false);
	PlaytestLog(FString::Printf(TEXT("shot %s"), *FPaths::GetCleanFilename(Path)));
}

void AKakurenboPlayerController::PlaytestKey(EKakurenboAction Action, bool bDown)
{
	const UKakurenboSettings* S = Settings();
	if (!S || !PlayerInput)
	{
		return;
	}
	// 連打はマウスの左ボタンではなく 2 つ目のキー（F）で押す（画面のボタンを押してしまわないように）
	FKey Key = S->GetKey(Action, false);
	const FKey Second = S->GetKey(Action, true);
	if (Key.IsMouseButton() && Second.IsValid())
	{
		Key = Second;
	}
	if (Key.IsValid())
	{
		PlayerInput->InputKey(FInputKeyEventArgs::CreateSimulated(Key, bDown ? IE_Pressed : IE_Released, bDown ? 1.f : 0.f));
	}
}

void AKakurenboPlayerController::PlaytestTap(EKakurenboAction Action)
{
	PlaytestKey(Action, true);
	PlaytestKey(Action, false);
}

void AKakurenboPlayerController::TickPlaytest(float RealDeltaSeconds)
{
	FKakurenboPlaytest& P = *Playtest;
	AKakurenboGameMode* GM = GetKakurenboGameMode();
	const AKakurenboGameState* S = GetWorld() ? GetWorld()->GetGameState<AKakurenboGameState>() : nullptr;
	if (!GM || !S)
	{
		return;
	}
	P.Clock += RealDeltaSeconds;

	if (FDateTime::Now() >= P.Until)
	{
		PlaytestLog(FString::Printf(TEXT("deadline reached: rounds %d, clears %d, highest stage %d, prestige %d"), P.Rounds, P.Clears, P.HighestStage, S->PrestigeCount));
		FTSTicker::GetCoreTicker().RemoveTicker(P.Ticker);
		Playtest.Reset();
		ConsoleCommand(TEXT("quit"));
		return;
	}

	// 鬼のチュートリアル：読んで（写真を撮って）から次へ
	if (GetMenu() == EKakurenboMenu::OniIntro)
	{
		const FString Key = FString::Printf(TEXT("intro_%d"), OniIntroPages.IsValidIndex(OniIntroPage) ? static_cast<int32>(OniIntroPages[OniIntroPage]) : -1);
		if (P.LastIntroShot != Key)
		{
			P.LastIntroShot = Key;
			P.PhaseTime = P.Clock;
			if (OniIntroPages.IsValidIndex(OniIntroPage))
			{
				PlaytestShot(FString::Printf(TEXT("intro_%s"), OniTypeName(OniIntroPages[OniIntroPage])));
				PlaytestLog(FString::Printf(TEXT("oni tutorial: %s"), OniTypeName(OniIntroPages[OniIntroPage])));
			}
		}
		else if (P.Clock - P.PhaseTime > 3.0 && CanTurnOniIntroPage())
		{
			PlaytestTap(EKakurenboAction::Next);
			P.LastIntroShot.Reset();
		}
		return;
	}
	if (IsMenuOpen())
	{
		CloseAllMenus();
		return;
	}

	// はじめて出たヒントは写しておく（初心者に伝わる文か見るため）
	if (const FKakurenboTip* Tip = GetCurrentTip())
	{
		const FString Key = TEXT("tip_") + Tip->Id.ToString();
		if (!P.ShotOnce.Contains(Key) && Tip->Age > 0.6f)
		{
			P.ShotOnce.Add(Key);
			PlaytestShot(Key);
			PlaytestLog(FString::Printf(TEXT("tip %s: %s"), *Tip->Id.ToString(), *Tip->Text));
		}
	}

	if (!P.bPhaseKnown || P.Phase != S->Phase)
	{
		const EKakurenboPhase Old = P.Phase;
		P.Phase = S->Phase;
		P.bPhaseKnown = true;
		P.PhaseTime = P.Clock;
		P.PhaseStep = 0;
		if (P.bForwardDown)
		{
			PlaytestKey(EKakurenboAction::MoveForward, false);
			P.bForwardDown = false;
		}
		PlaytestLog(FString::Printf(TEXT("phase %s -> %s (stage %d, coins %.0f, gems %d)"), *UEnum::GetValueAsString(Old), *UEnum::GetValueAsString(S->Phase), S->Stage, S->Coins, S->Gems));
	}

	switch (S->Phase)
	{
	case EKakurenboPhase::Title:  PlaytestTitle(); break;
	case EKakurenboPhase::Shop:   PlaytestShop(); break;
	case EKakurenboPhase::Build:  PlaytestBuild(); break;
	case EKakurenboPhase::Hide:   PlaytestHide(RealDeltaSeconds); break;
	case EKakurenboPhase::Result: PlaytestResult(); break;
	default: break;
	}
}

// ---------------------------------------------------------------- タイトル

void AKakurenboPlayerController::PlaytestTitle()
{
	FKakurenboPlaytest& P = *Playtest;
	const AKakurenboGameMode* GM = GetKakurenboGameMode();
	if (P.PhaseStep == 0 && P.Clock - P.PhaseTime > 3.0)
	{
		PlaytestShot(TEXT("title"));
		P.PhaseStep = 1;
	}
	else if (P.PhaseStep == 1 && P.Clock - P.PhaseTime > 4.0)
	{
		P.PhaseStep = 2;
		// セーブがあれば続きから（落ちて起動し直したとき）、無ければはじめから。メニューの選択は Enter と同じ関数で
		ActivateTitleItem(GM && GM->HasContinueData() ? EKakurenboTitleItem::Continue : EKakurenboTitleItem::NewGame);
		PlaytestLog(GM && GM->HasContinueData() ? TEXT("title: continue") : TEXT("title: new game"));
	}
}

// ---------------------------------------------------------------- リザルト

void AKakurenboPlayerController::PlaytestResult()
{
	FKakurenboPlaytest& P = *Playtest;
	AKakurenboGameMode* GM = GetKakurenboGameMode();
	const AKakurenboGameState* S = GetWorld()->GetGameState<AKakurenboGameState>();
	if (P.PhaseStep == 0 && P.Clock - P.PhaseTime > 1.5)
	{
		P.PhaseStep = 1;
		if (P.bRoundActive)
		{
			P.bRoundActive = false;
			++P.Rounds;
			const bool bCleared = S->bLastRoundCleared;
			const int32 PlayedStage = bCleared ? S->Stage - 1 : S->Stage;
			P.Clears += bCleared ? 1 : 0;
			P.HighestStage = FMath::Max(P.HighestStage, S->Stage);
			if (bCleared)
			{
				P.FailsAtStage = 0;
			}
			else
			{
				P.FailsAtStage = P.FailStage == PlayedStage ? P.FailsAtStage + 1 : 1;
				if (S->HideTimeLimit - S->HideTimeRemaining - P.LastTreasureTripAt < 8.f)
				{
					P.TreasureDeaths.FindOrAdd(PlayedStage)++;
					PlaytestLog(FString::Printf(TEXT("caught on a treasure trip (%d times at stage %d)"), P.TreasureDeaths[PlayedStage], PlayedStage));
				}
				P.FailStage = PlayedStage;
				P.HomeCatches.FindOrAdd(FString::Printf(TEXT("%s:%d,%d"), *GM->GetCurrentMapName().ToString(), P.BuildHome.X, P.BuildHome.Y))++;
			}
			const float Survived = S->HideTimeLimit - S->HideTimeRemaining;
			const FString CsvPath = P.Dir / TEXT("rounds.csv");
			if (!IFileManager::Get().FileExists(*CsvPath))
			{
				FFileHelper::SaveStringToFile(TEXT("time,round,prestige,stage,map,onis,cleared,survived,limit,coins_start,earned,mash_coins,time_coins,clear_reward,mashes,treasures,treasures_total,gems_gained,gems,walls_destroyed,traps_triggered,summons,chases,first_chase,treasure_trips,dashes,daruma,stuck,caught_by,caught_state,shop,build,coins_after_round\n"),
					*CsvPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
			}
			const FString Row = FString::Printf(TEXT("%s,%d,%d,%d,%s,%s,%d,%.1f,%.0f,%.0f,%.0f,%.0f,%.0f,%.0f,%d,%d,%d,%d,%d,%d,%d,%d,%d,%.1f,%d,%d,%d,%d,%s,%s,\"%s\",\"%s\",%.0f\n"),
				*FDateTime::Now().ToString(TEXT("%H:%M:%S")), P.Rounds, S->PrestigeCount, PlayedStage, *GM->GetCurrentMapName().ToString(), *P.RoundOnis,
				bCleared ? 1 : 0, Survived, S->HideTimeLimit, P.RoundCoinsAtStart, S->CoinsEarnedThisRound, S->MashCoinsThisRound, S->TimeCoinsThisRound, S->LastClearReward,
				S->MashCountThisRound, S->TreasuresCollectedThisRound, S->TreasuresThisRound, S->Gems - P.RoundGemsAtStart, S->Gems,
				S->LastRoundWallsDestroyed, S->TrapsTriggeredThisRound, S->SummonsThisRound, P.RoundChases, P.FirstChaseAt, P.RoundTreasureTrips, P.RoundDashes, P.RoundDaruma, P.RoundStuck,
				bCleared ? TEXT("-") : *P.CaughtBy, bCleared ? TEXT("-") : *P.CaughtState, *P.ShopLog.Replace(TEXT("\""), TEXT("'")), *P.BuildLog.Replace(TEXT("\""), TEXT("'")), S->Coins);
			FFileHelper::SaveStringToFile(Row, *CsvPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM, &IFileManager::Get(), FILEWRITE_Append);
			PlaytestLog(FString::Printf(TEXT("ROUND %d stage %d %s: %s after %.1f/%.0fs, earned %.0f (mash %.0f time %.0f clear %.0f), treasure %d/%d, walls broken %d, chases %d, caught by %s"),
				P.Rounds, PlayedStage, *GM->GetCurrentMapName().ToString(), bCleared ? TEXT("CLEAR") : TEXT("caught"), Survived, S->HideTimeLimit,
				S->CoinsEarnedThisRound, S->MashCoinsThisRound, S->TimeCoinsThisRound, S->LastClearReward, S->TreasuresCollectedThisRound, S->TreasuresThisRound,
				S->LastRoundWallsDestroyed, P.RoundChases, *P.CaughtBy));
			const FString Key = FString::Printf(TEXT("result_%s_%d"), bCleared ? TEXT("clear") : TEXT("caught"), PlayedStage);
			if (!P.ShotOnce.Contains(Key))
			{
				P.ShotOnce.Add(Key);
				PlaytestShot(FString::Printf(TEXT("s%02d_result_%s"), PlayedStage, bCleared ? TEXT("clear") : TEXT("caught")));
			}
		}
	}
	else if (P.PhaseStep == 1 && P.Clock - P.PhaseTime > 3.0)
	{
		P.PhaseStep = 2;
		PlaytestTap(EKakurenboAction::Next);
	}
	else if (P.PhaseStep == 2 && P.Clock - P.PhaseTime > 8.0)
	{
		P.PhaseStep = 1; // 進まなければもう一度
		P.PhaseTime = P.Clock - 3.0;
	}
}

// ---------------------------------------------------------------- 購入

void AKakurenboPlayerController::PlaytestShop()
{
	FKakurenboPlaytest& P = *Playtest;
	AKakurenboGameMode* GM = GetKakurenboGameMode();
	AKakurenboGameState* S = GetWorld()->GetGameState<AKakurenboGameState>();
	if (P.PhaseStep == 0 && P.Clock - P.PhaseTime > 1.5)
	{
		P.PhaseStep = 1;
		P.ShopLog.Reset();
		const FString ShotKey = FString::Printf(TEXT("shop_%d_%d"), S->PrestigeCount, S->Stage);
		if (!P.ShotOnce.Contains(ShotKey) && (S->Stage <= 3 || S->Stage % 4 == 0))
		{
			P.ShotOnce.Add(ShotKey);
			PlaytestShot(FString::Printf(TEXT("s%02d_shop"), S->Stage));
		}

		// 転生：同じステージで何度も見つかる（＝行き詰まった）とき。初心者は転生のお知らせ・ボタンを見て押す
		const bool bWallStage = S->Stage == 8 || S->Stage == 13 || S->Stage == 18 || S->Stage == 23;
		const bool bAtLeastLast = GM->GetPrestigePointsOnReset() >= P.LastPrestigeGain;
		const bool bStuck = P.FailStage == S->Stage && P.FailsAtStage >= (!bAtLeastLast ? 10 : bWallStage ? 3 : 6);
		if (GM->CanPrestige() && bStuck)
		{
			const int32 Points = GM->GetPrestigePointsOnReset();
			RequestPrestige();
			RequestPrestige(); // P を 2 回（1 回目は確認）
			PlaytestLog(FString::Printf(TEXT("PRESTIGE at stage %d after %d fails (+%d pt, now %d prestiges, start coins %.0f)"), P.FailStage, P.FailsAtStage, Points, S->PrestigeCount, S->Coins));
			P.ShopLog += FString::Printf(TEXT("PRESTIGE(+%dpt) "), Points);
			P.LastPrestigeGain = Points;
			P.FailsAtStage = 0;
			P.FailStage = 0;
			P.HomeByMap.Reset();
		}

		// 転生のお店：使えるポイントを、逃げる道具 → 壁の硬さ → よじ登る → 稼ぎの順で
		{
			const EPrestigeUpgrade Priority[] = { EPrestigeUpgrade::SmokeDuration, EPrestigeUpgrade::WallHP, EPrestigeUpgrade::Climb, EPrestigeUpgrade::Income,
				EPrestigeUpgrade::SmokeCount, EPrestigeUpgrade::WallHP, EPrestigeUpgrade::Income, EPrestigeUpgrade::Treasure, EPrestigeUpgrade::SmokeDuration, EPrestigeUpgrade::QuietHP };
			for (int32 Guard = 0; Guard < 40; ++Guard)
			{
				const TArray<FShopItemView> Items = GM->GetPrestigeShopItems();
				bool bBought = false;
				for (EPrestigeUpgrade U : Priority)
				{
					const int32 Index = static_cast<int32>(U);
					if (Items.IsValidIndex(Index) && Items[Index].Cost > 0.0 && Items[Index].Cost <= S->PrestigePoints && GM->TryBuyPrestigeUpgrade(Index))
					{
						P.ShopLog += FString::Printf(TEXT("P:%s "), *UEnum::GetValueAsString(U).RightChop(18));
						bBought = true;
						break;
					}
				}
				if (!bBought)
				{
					break;
				}
			}
			GM->ApplyPrestigeToPlayer();
		}

		// コインのお店（数字キーと同じ BuyItem）
		ShopTab = EKakurenboShopTab::Coin;
		BuyAmount = EKakurenboBuyAmount::One;
		const int32 Refilled = GM->BuyAllMissing();
		if (Refilled > 0)
		{
			P.ShopLog += FString::Printf(TEXT("refill%d "), Refilled);
		}
		const int32 Stage = S->Stage;
		const int32 WallTarget = 8 + (Stage >= 4 ? 7 : 0) + (Stage >= 6 ? 30 : 0) + (Stage >= 10 ? 30 : 0);
		const int32 Sticky = GM->FindTrapTypeOfKind(ETrapKind::Sticky);
		const int32 Decoy = GM->FindTrapTypeOfKind(ETrapKind::Decoy);
		struct FWant { int32 ShopIndex; int32 Have; int32 Target; const TCHAR* Name; };
		for (int32 Guard = 0; Guard < 300; ++Guard)
		{
			const TArray<FShopItemView> Items = GM->GetShopItems();
			TArray<FWant> Wants;
			Wants.Add({ GM->GetShopIndexOfWall(0), GM->GetOwnedWallCount(0), WallTarget, TEXT("wall") });
			if (Stage >= 6 && GM->WallTypes.IsValidIndex(1))
			{
				Wants.Add({ GM->GetShopIndexOfWall(1), GM->GetOwnedWallCount(1), 2, TEXT("quiet") });
			}
			if (GM->IsClimbUnlocked() && GM->WallTypes.IsValidIndex(2))
			{
				Wants.Add({ GM->GetShopIndexOfWall(2), GM->GetOwnedWallCount(2), 2, TEXT("climbwall") });
			}
			if (Stage >= 4 && Sticky != INDEX_NONE)
			{
				Wants.Add({ GM->GetShopIndexOfTrap(Sticky), GM->GetOwnedTrapCount(Sticky), Stage >= 9 ? 2 : 1, TEXT("sticky") });
			}
			if (Stage >= 5 && Decoy != INDEX_NONE)
			{
				Wants.Add({ GM->GetShopIndexOfTrap(Decoy), GM->GetOwnedTrapCount(Decoy), 1, TEXT("decoy") });
			}
			// 足りない物の中で一番安い物
			int32 Pick = INDEX_NONE;
			const TCHAR* PickName = nullptr;
			for (const FWant& W : Wants)
			{
				if (W.Have < W.Target && Items.IsValidIndex(W.ShopIndex) && Items[W.ShopIndex].Cost <= S->Coins
					&& (Pick == INDEX_NONE || Items[W.ShopIndex].Cost < Items[Pick].Cost))
				{
					Pick = W.ShopIndex;
					PickName = W.Name;
				}
			}
			// 無ければ稼ぎの強化（連打・時間）と壁の補強のうち安い物
			if (Pick == INDEX_NONE)
			{
				const int32 Upgrades[] = { 0, 1, 2 };
				for (int32 U : Upgrades)
				{
					if (U == 2 && GM->GetOwnedWallCount(0) < 8)
					{
						continue;
					}
					if (Items.IsValidIndex(U) && Items[U].Cost <= S->Coins && (Pick == INDEX_NONE || Items[U].Cost < Items[Pick].Cost))
					{
						Pick = U;
						PickName = U == 0 ? TEXT("mashUp") : U == 1 ? TEXT("timeUp") : TEXT("reinforce");
					}
				}
			}
			if (Pick == INDEX_NONE)
			{
				break;
			}
			const double Before = S->Coins;
			BuyItem(Pick + 1);
			if (S->Coins >= Before)
			{
				break; // 買えなかった
			}
			P.ShopLog += FString::Printf(TEXT("%s(%.0f) "), PickName, Before - S->Coins);
		}

		// 宝石のお店：持っている数が少ない道具から（初心者はいろいろ試す）
		const ETrapKind GemKinds[] = { ETrapKind::GoldRush, ETrapKind::FakeHider, ETrapKind::Darkness, ETrapKind::Cat, ETrapKind::Daruma, ETrapKind::OneWay };
		for (int32 Guard = 0; Guard < 30; ++Guard)
		{
			const TArray<FShopItemView> Items = GM->GetGemShopItems();
			int32 Pick = INDEX_NONE;
			int32 PickOwned = 0;
			ETrapKind PickKind = ETrapKind::Sticky;
			for (ETrapKind Kind : GemKinds)
			{
				const int32 Trap = GM->FindTrapTypeOfKind(Kind);
				const int32 Index = Trap != INDEX_NONE ? GM->GetGemShopIndexOfTrap(Trap) : INDEX_NONE;
				if (Items.IsValidIndex(Index) && Items[Index].Cost <= S->Gems)
				{
					const int32 Owned = GM->GetOwnedTrapCount(Trap);
					if (Owned < 2 && (Pick == INDEX_NONE || Owned < PickOwned))
					{
						Pick = Index;
						PickOwned = Owned;
						PickKind = Kind;
					}
				}
			}
			if (Pick == INDEX_NONE || GM->TryBuyGemShopItem(Pick, 1) <= 0)
			{
				break;
			}
			P.ShopLog += FString::Printf(TEXT("G:%s "), *UEnum::GetValueAsString(PickKind).RightChop(11));
		}
		PlaytestLog(FString::Printf(TEXT("shop stage %d: %s| left %.0f coins, %d gems, mashLv %d timeLv %d reinforceLv %d walls %d"),
			S->Stage, *P.ShopLog, S->Coins, S->Gems, S->MashIncomeLevel, S->TimeIncomeLevel, S->WallReinforceLevel, GM->GetOwnedWallCount(0)));
	}
	else if (P.PhaseStep == 1 && P.Clock - P.PhaseTime > 3.0)
	{
		P.PhaseStep = 2;
		PlaytestTap(EKakurenboAction::Next);
	}
	else if (P.PhaseStep == 2 && P.Clock - P.PhaseTime > 8.0)
	{
		P.PhaseStep = 1;
		P.PhaseTime = P.Clock - 3.0;
	}
}

// ---------------------------------------------------------------- 設置

void AKakurenboPlayerController::PlaytestBuild()
{
	FKakurenboPlaytest& P = *Playtest;
	AKakurenboGameMode* GM = GetKakurenboGameMode();
	AKakurenboGameState* S = GetWorld()->GetGameState<AKakurenboGameState>();
	UKakurenboGridSubsystem* Grid = GetWorld()->GetSubsystem<UKakurenboGridSubsystem>();
	if (P.PhaseStep == 0 && P.Clock - P.PhaseTime > 1.2)
	{
		P.PhaseStep = 1;
		P.BuildLog.Reset();
		const FName Map = GM->GetCurrentMapName();
		const TArray<FIntPoint> Gate = GM->GetOniGateCells();
		const TArray<int32> GateDist = FloodFromCells(Grid, Gate, false);
		auto DistOf = [&](const FIntPoint& C) { return Grid->IsInside(C) ? GateDist[C.Y * Grid->GetSizeX() + C.X] : -1; };
		auto IsFloor = [&](const FIntPoint& C) { return Grid->IsInside(C) && !Grid->IsObstacle(C) && !Grid->IsReservedCell(C); };

		// 隠れ場所：門から遠く、家具・部屋の壁に囲まれた所（初心者は「奥の部屋の隅の家具のそば」に隠れる）
		FIntPoint Home(-1, -1);
		// 持っている壁（在庫＋置いてある分）。壁があれば、まわりを囲める広さのある所を選ぶ
		const int32 WallsOwned = GM->GetOwnedWallCount(0) + (GM->WallTypes.IsValidIndex(1) ? GM->GetOwnedWallCount(1) : 0);
		auto RingSize = [&](const FIntPoint& C)
		{
			int32 N = 0;
			for (int32 DY = -1; DY <= 1; ++DY)
			{
				for (int32 DX = -1; DX <= 1; ++DX)
				{
					N += (DX || DY) && IsFloor(FIntPoint(C.X + DX, C.Y + DY)) ? 1 : 0;
				}
			}
			return N - 1; // 入り口の 1 マスは空ける
		};
		// 入り口にできるマス：隠れ場所の隣で、その先が囲み（3×3）の外の床へ続いている
		static const FIntPoint Dirs4[4] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } };
		auto LeadsOut = [&](const FIntPoint& H, const FIntPoint& N)
		{
			if (!IsFloor(N))
			{
				return false;
			}
			for (const FIntPoint& D : Dirs4)
			{
				const FIntPoint M = N + D;
				if (IsFloor(M) && FMath::Max(FMath::Abs(M.X - H.X), FMath::Abs(M.Y - H.Y)) >= 2)
				{
					return true;
				}
			}
			return false;
		};
		auto CatchesNear = [&](const FIntPoint& C)
		{
			int32 N = 0;
			for (int32 DY = -1; DY <= 1; ++DY)
			{
				for (int32 DX = -1; DX <= 1; ++DX)
				{
					N += P.HomeCatches.FindRef(FString::Printf(TEXT("%s:%d,%d"), *Map.ToString(), C.X + DX, C.Y + DY));
				}
			}
			return N;
		};
		if (const FIntPoint* Known = P.HomeByMap.Find(Map))
		{
			// 前に決めた場所。壁が増えて囲めるようになったのに囲む所が無い（家具のすき間）・2 回見つかった場所なら選び直す
			if ((WallsOwned < 3 || RingSize(*Known) >= 3) && CatchesNear(*Known) < 2)
			{
				Home = *Known;
			}
		}
		if (!IsFloor(Home))
		{
			float Best = -1e9f;
			for (int32 Y = 0; Y < Grid->GetSizeY(); ++Y)
			{
				for (int32 X = 0; X < Grid->GetSizeX(); ++X)
				{
					const FIntPoint C(X, Y);
					if (!IsFloor(C) || DistOf(C) < 0)
					{
						continue;
					}
					int32 Cover = 0;
					int32 OpenSides = 0;
					for (int32 DY = -1; DY <= 1; ++DY)
					{
						for (int32 DX = -1; DX <= 1; ++DX)
						{
							const FIntPoint N(X + DX, Y + DY);
							if ((DX || DY) && (!Grid->IsInside(N) || Grid->IsObstacle(N)))
							{
								++Cover;
							}
						}
					}
					static const FIntPoint Dirs[4] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } };
					for (const FIntPoint& D : Dirs)
					{
						OpenSides += IsFloor(C + D) ? 1 : 0;
					}
					if (OpenSides == 0)
					{
						continue;
					}
					if (WallsOwned >= 3 && (RingSize(C) < 3 || RingSize(C) > WallsOwned))
					{
						continue;
					}
					bool bHasDoor = false;
					for (const FIntPoint& D : Dirs4)
					{
						bHasDoor |= LeadsOut(C, C + D);
					}
					if (!bHasDoor || CatchesNear(C) >= 2)
					{
						continue;
					}
					// 見通し：まわり 8 マス以内で、家具・部屋の壁にさえぎられずにここが見える床の数（少ないほど見つかりにくい）
					int32 Exposure = 0;
					for (int32 OY = -8; OY <= 8; ++OY)
					{
						for (int32 OX = -8; OX <= 8; ++OX)
						{
							const FIntPoint T(X + OX, Y + OY);
							if ((OX == 0 && OY == 0) || OX * OX + OY * OY > 64 || !IsFloor(T))
							{
								continue;
							}
							bool bClear = true;
							const int32 Steps = FMath::Max(FMath::Abs(OX), FMath::Abs(OY)) * 2;
							for (int32 i = 1; i < Steps && bClear; ++i)
							{
								const FIntPoint M(FMath::RoundToInt(X + OX * float(i) / Steps), FMath::RoundToInt(Y + OY * float(i) / Steps));
								bClear = !Grid->IsObstacle(M);
							}
							Exposure += bClear ? 1 : 0;
						}
					}
					const float Score = 0.6f * DistOf(C) + 2.f * Cover - 0.35f * Exposure + P.Rand.FRandRange(0.f, 3.f);
					if (Score > Best)
					{
						Best = Score;
						Home = C;
					}
				}
			}
			// 隠れ場所を変えたら、前の場所に置いた壁と罠は全部回収して置き直す（Delete を 2 回と同じ）
			if (P.HomeByMap.Contains(Map) || Grid->GetBlockCount() > 0)
			{
				const int32 Returned = GM->ClearAllPlaced();
				P.BuildLog += FString::Printf(TEXT("clearAll%d "), Returned);
			}
			P.HomeByMap.Add(Map, Home);
		}
		P.Home = Home;
		P.BuildHome = Home;
		if (!SetStartCell(Home))
		{
			PlaytestLog(FString::Printf(TEXT("build: could not move start to (%d,%d)"), Home.X, Home.Y));
		}

		// 隠れ場所を壁で囲む。入り口は門から遠い側に 1 マスだけ（ステージ 2 のヒントの「入り口の向き」）
		TArray<FIntPoint> Ring;
		// 壁が硬くなって（今の鬼で 4 発以上）きたら、入り口も閉じて完全に閉じこもる（壊しに来ても時間までもつ）
		const TArray<FWallTypeDef> EffWalls = GM->GetEffectiveWallTypes();
		const int32 WallHits = EffWalls.Num() > 0 ? GM->GetHitsToBreak(EffWalls[0].MaxHP) : 0;
		const bool bSealed = WallHits >= 4;
		FIntPoint Door(-1, -1);
		int32 DoorDist = -1;
		for (const FIntPoint& D : Dirs4)
		{
			const FIntPoint N = Home + D;
			if (LeadsOut(Home, N) && DistOf(N) > DoorDist)
			{
				DoorDist = DistOf(N);
				Door = N;
			}
		}
		for (int32 DY = -1; DY <= 1; ++DY)
		{
			for (int32 DX = -1; DX <= 1; ++DX)
			{
				const FIntPoint N(Home.X + DX, Home.Y + DY);
				if ((DX || DY) && (N != Door || bSealed) && IsFloor(N))
				{
					Ring.Add(N);
				}
			}
		}
		const int32 Tiers = S->Stage >= 10 ? 3 : S->Stage >= 4 ? 2 : 1;
		int32 Placed = 0;
		for (int32 Tier = 1; Tier <= Tiers; ++Tier)
		{
			for (const FIntPoint& C : Ring)
			{
				// まず消音壁・クライミング壁（持っていれば）を 1 段目に、残りは通常壁
				if (Grid->GetColumnHeight(C) >= Tier || Grid->GetDesignHeight(C) >= Tier)
				{
					continue;
				}
				bool bDone = false;
				for (int32 Type : { 1, 0, 2 })
				{
					if (Type == 2 && Tier != 1)
					{
						continue;
					}
					if (S->WallStock.IsValidIndex(Type) && S->WallStock[Type] > 0 && GM->CanPlaceWall(C, Type) && GM->PlaceWall(C, Type))
					{
						++Placed;
						bDone = true;
						break;
					}
				}
				if (!bDone && S->WallStock.IsValidIndex(0) && S->WallStock[0] == 0)
				{
					break;
				}
			}
		}
		P.BuildLog += FString::Printf(TEXT("home(%d,%d) ring%d door(%d,%d)%s hits%d placed%d "), Home.X, Home.Y, Ring.Num(), Door.X, Door.Y, bSealed ? TEXT(" SEALED") : TEXT(""), WallHits, Placed);

		// 壁が余ったら（後半は壁が安い）、2 マス外にもう 1 重の囲い（7×7）。外の入り口は内の入り口から遠い側（中をぐるっと回る通路になる）
		const int32 Spare = S->WallStock.IsValidIndex(0) ? S->WallStock[0] : 0;
		if (Spare >= 10)
		{
			auto Cheb = [&](const FIntPoint& A, const FIntPoint& B) { return FMath::Max(FMath::Abs(A.X - B.X), FMath::Abs(A.Y - B.Y)); };
			TArray<FIntPoint> Outer;
			FIntPoint OuterDoor(-1, -1);
			float OuterDoorScore = -1e9f;
			for (int32 DY = -3; DY <= 3; ++DY)
			{
				for (int32 DX = -3; DX <= 3; ++DX)
				{
					const FIntPoint C(Home.X + DX, Home.Y + DY);
					if (Cheb(C, Home) != 3 || !IsFloor(C))
					{
						continue;
					}
					Outer.Add(C);
					bool bOut = false;
					for (const FIntPoint& D : Dirs4)
					{
						bOut |= IsFloor(C + D) && Cheb(C + D, Home) >= 4;
					}
					const float Score = FVector2D(C - Door).Size() + 0.1f * DistOf(C);
					if (bOut && Score > OuterDoorScore)
					{
						OuterDoorScore = Score;
						OuterDoor = C;
					}
				}
			}
			int32 OuterPlaced = 0;
			if (OuterDoor.X >= 0)
			{
				Outer.Remove(OuterDoor);
				for (int32 Tier = 1; Tier <= FMath::Max(1, Tiers - 1); ++Tier)
				{
					for (const FIntPoint& C : Outer)
					{
						if (Grid->GetColumnHeight(C) < Tier && Grid->GetDesignHeight(C) < Tier && S->WallStock[0] > 0 && GM->CanPlaceWall(C, 0) && GM->PlaceWall(C, 0))
						{
							++OuterPlaced;
						}
					}
				}
			}
			P.BuildLog += FString::Printf(TEXT("outer%d door(%d,%d) placed%d "), Outer.Num(), OuterDoor.X, OuterDoor.Y, OuterPlaced);
		}

		// 罠と宝石の道具
		auto PlaceIfOwned = [&](ETrapKind Kind, const FIntPoint& Cell) -> bool
		{
			const int32 Trap = GM->FindTrapTypeOfKind(Kind);
			if (Trap == INDEX_NONE || !S->TrapStock.IsValidIndex(Trap) || S->TrapStock[Trap] <= 0 || !Grid->IsInside(Cell))
			{
				return false;
			}
			if (GM->CanPlaceTrap(Cell, Trap) && GM->PlaceTrap(Cell, Trap))
			{
				P.BuildLog += FString::Printf(TEXT("%s(%d,%d) "), *UEnum::GetValueAsString(Kind).RightChop(11), Cell.X, Cell.Y);
				return true;
			}
			return false;
		};
		// 入り口の外のマス（入り口から門の方へ）
		FIntPoint Outside(-1, -1);
		for (const FIntPoint& D : Dirs4)
		{
			const FIntPoint N = Door + D;
			if (N != Home && IsFloor(N) && Grid->GetColumnHeight(N) == 0 && (Outside.X < 0 || DistOf(N) < DistOf(Outside)))
			{
				Outside = N;
			}
		}
		// 一方通行ののれん：入り口に、外向き（鬼は入れない）
		if (PlaceIfOwned(ETrapKind::OneWay, Door))
		{
			const FIntPoint Out = Door - Home;
			const int32 Want = Out == FIntPoint(1, 0) ? 0 : Out == FIntPoint(0, 1) ? 1 : Out == FIntPoint(-1, 0) ? 2 : 3;
			for (int32 i = 0; i < 4 && Grid->GetTrapDir(Door) != Want; ++i)
			{
				GM->RotateTrap(Door);
			}
		}
		else
		{
			PlaceIfOwned(ETrapKind::Sticky, Door);
		}
		PlaceIfOwned(ETrapKind::Sticky, Outside);
		// 隠れ場所から離れた、門寄りの床（おとり・猫・身代わり人形）
		auto FindAway = [&](float MinCells, float MaxCells) -> FIntPoint
		{
			for (int32 Try = 0; Try < 400; ++Try)
			{
				const FIntPoint C(P.Rand.RandRange(0, Grid->GetSizeX() - 1), P.Rand.RandRange(0, Grid->GetSizeY() - 1));
				const float D = FVector2D(C - Home).Size();
				if (IsFloor(C) && Grid->GetColumnHeight(C) == 0 && !Grid->HasTrap(C) && DistOf(C) >= 0 && D >= MinCells && D <= MaxCells && DistOf(C) < DistOf(Home))
				{
					return C;
				}
			}
			return FIntPoint(-1, -1);
		};
		PlaceIfOwned(ETrapKind::Decoy, FindAway(7.f, 14.f));
		PlaceIfOwned(ETrapKind::Cat, FindAway(6.f, 12.f));
		PlaceIfOwned(ETrapKind::FakeHider, FindAway(4.f, 9.f));
		PlaceIfOwned(ETrapKind::Darkness, FindAway(2.f, 6.f));
		PlaceIfOwned(ETrapKind::GoldRush, FindAway(2.f, 6.f));

		const FString ShotKey = FString::Printf(TEXT("build_%s_%d"), *Map.ToString(), S->PrestigeCount);
		if (!P.ShotOnce.Contains(ShotKey) && (S->Stage <= 3 || S->Stage % 3 == 0))
		{
			P.ShotOnce.Add(ShotKey);
			PlaytestShot(FString::Printf(TEXT("s%02d_build"), S->Stage));
		}
		PlaytestLog(FString::Printf(TEXT("build stage %d %s: %s| missing %d, stock walls %s"), S->Stage, *Map.ToString(), *P.BuildLog, Grid->GetTotalMissing(),
			*FString::JoinBy(S->WallStock, TEXT("/"), [](int32 N) { return FString::FromInt(N); })));
	}
	else if (P.PhaseStep == 1 && P.Clock - P.PhaseTime > 3.0)
	{
		P.PhaseStep = 2;
		PlaytestTap(EKakurenboAction::Next);
	}
	else if (P.PhaseStep == 2 && P.Clock - P.PhaseTime > 8.0)
	{
		P.PhaseStep = 1;
		P.PhaseTime = P.Clock - 3.0;
	}
}

// ---------------------------------------------------------------- かくれんぼ

void AKakurenboPlayerController::PlaytestHide(float RealDeltaSeconds)
{
	FKakurenboPlaytest& P = *Playtest;
	AKakurenboGameMode* GM = GetKakurenboGameMode();
	AKakurenboGameState* S = GetWorld()->GetGameState<AKakurenboGameState>();
	UKakurenboGridSubsystem* Grid = GetWorld()->GetSubsystem<UKakurenboGridSubsystem>();
	AHiderCharacter* Hider = GetHider();
	if (!Hider || !Grid)
	{
		return;
	}
	const double Now = P.Clock;
	const FIntPoint MyCell = Grid->WorldToCell(Hider->GetActorLocation());

	// ラウンドの始め
	if (P.PhaseStep == 0)
	{
		P.PhaseStep = 1;
		P.bRoundActive = true;
		P.RoundCoinsAtStart = S->Coins;
		P.RoundGemsAtStart = S->Gems;
		P.RoundChases = P.RoundTreasureTrips = P.RoundDashes = P.RoundDaruma = P.RoundStuck = 0;
		P.FirstChaseAt = -1.f;
		P.CaughtBy = TEXT("-");
		P.CaughtState = TEXT("-");
		P.Move = FKakurenboPlaytest::EMove::Hide;
		P.Home = MyCell;
		P.Path.Reset();
		P.NextThink = Now + 1.0;
		P.RoundOnis.Reset();
		P.LastTreasureTripAt = -100.f;
	}
	if (P.RoundOnis.IsEmpty() && GM->GetOnis().Num() > 0)
	{
		for (const AOniCharacter* Oni : GM->GetOnis())
		{
			P.RoundOnis += FString(P.RoundOnis.IsEmpty() ? TEXT("") : TEXT("+")) + (Oni ? OniTypeName(Oni->OniType) : TEXT("?"));
		}
	}

	// 鬼の様子（初心者にわかるのは、足音が聞こえる近さの鬼と、追ってくる鬼）
	const AOniCharacter* Chaser = nullptr;
	const AOniCharacter* Nearest = nullptr;
	float NearestDist = 1e9f;
	for (const AOniCharacter* Oni : GM->GetOnis())
	{
		if (!Oni)
		{
			continue;
		}
		const float D = FVector::Dist2D(Oni->GetActorLocation(), Hider->GetActorLocation());
		if (D < NearestDist)
		{
			NearestDist = D;
			Nearest = Oni;
		}
		if (Oni->GetOniState() == EOniState::Chase && Oni->GetChaseTarget() == Hider && (!Chaser || D < FVector::Dist2D(Chaser->GetActorLocation(), Hider->GetActorLocation())))
		{
			Chaser = Oni;
		}
	}
	// 見つかった瞬間の記録（結果画面に移る前に、触れた鬼を覚えておく）
	if (Nearest && NearestDist < 350.f)
	{
		P.CaughtBy = OniTypeName(Nearest->OniType);
		P.CaughtState = UEnum::GetValueAsString(Nearest->GetOniState()).RightChop(11);
	}

	if (S->HideStartCountdown > 0.f || IsPaused())
	{
		return;
	}
	const float Elapsed = S->HideTimeLimit - S->HideTimeRemaining;

	auto SetSneak = [&](bool bWant)
	{
		const UKakurenboSettings* Set = Settings();
		if (Set && Set->bCrouchToggle ? bSneakToggled != bWant : Hider->IsSneaking() != bWant)
		{
			PlaytestTap(EKakurenboAction::Sneak);
		}
	};
	auto PathTo = [&](const FIntPoint& Goal) -> bool
	{
		P.Path.Reset();
		P.PathIndex = 0;
		if (Goal == MyCell)
		{
			return true;
		}
		const FKakurenboPathGrid Walk = Grid->BuildWalkGrid();
		return KakurenboPathfinding::FindPath(Walk, MyCell, Goal, P.Path) && P.Path.Num() > 0;
	};

	// 追われたら：煙幕（あれば）を投げて、見えない所へ逃げる
	if (Chaser)
	{
		P.NotChasedSince = Now;
		const bool bNewChase = P.Move != FKakurenboPlaytest::EMove::Flee;
		if (bNewChase || (P.PathIndex >= P.Path.Num() && Now >= P.NextThink))
		{
			P.Move = FKakurenboPlaytest::EMove::Flee;
			P.NextThink = Now + 0.8;
			if (bNewChase)
			{
				++P.RoundChases;
				if (P.FirstChaseAt < 0.f)
				{
					P.FirstChaseAt = Elapsed;
				}
				PlaytestLog(FString::Printf(TEXT("chased by %s at %.1fs (dist %.0f, smoke %d, daruma %s)"), OniTypeName(Chaser->OniType), Elapsed,
					FVector::Dist2D(Chaser->GetActorLocation(), Hider->GetActorLocation()), Hider->GetDashUsesLeft(), GM->CanUseDaruma() ? TEXT("yes") : TEXT("no")));
				if (!P.ShotOnce.Contains(TEXT("chase")) || P.Rand.FRand() < 0.04f)
				{
					P.ShotOnce.Add(TEXT("chase"));
					PlaytestShot(FString::Printf(TEXT("s%02d_chased_%s"), S->Stage, OniTypeName(Chaser->OniType)));
				}
			}
			SetSneak(false);
			// 逃げ先：近くの床のうち、鬼から遠くて鬼から見えない所
			const TArray<int32> MyDist = FloodFromCells(Grid, { MyCell }, true);
			FCollisionQueryParams Params;
			Params.AddIgnoredActor(Chaser);
			Params.AddIgnoredActor(Hider);
			const FVector Eye = Chaser->GetActorLocation() + FVector(0.f, 0.f, 60.f);
			FIntPoint Best = MyCell;
			float BestScore = -1e9f;
			for (int32 Y = 0; Y < Grid->GetSizeY(); ++Y)
			{
				for (int32 X = 0; X < Grid->GetSizeX(); ++X)
				{
					const int32 Steps = MyDist[Y * Grid->GetSizeX() + X];
					if (Steps <= 0 || Steps > 14 || Grid->IsReservedCell(FIntPoint(X, Y)))
					{
						continue;
					}
					const FVector Floor = Grid->CellFloorCenter(FIntPoint(X, Y));
					const float Away = FVector::Dist2D(Floor, Chaser->GetActorLocation());
					const bool bOutOfSight = GetWorld()->LineTraceTestByChannel(Eye, Floor + FVector(0.f, 0.f, 90.f), ECC_Visibility, Params);
					const float Score = Away + (bOutOfSight ? 700.f : 0.f) - Steps * 25.f;
					if (Score > BestScore)
					{
						BestScore = Score;
						Best = FIntPoint(X, Y);
					}
				}
			}
			PathTo(Best);
			// だるまさんがころんだ：追ってくる鬼の方を向いて使う
			if (bNewChase && GM->CanUseDaruma() && FVector::Dist2D(Chaser->GetActorLocation(), Hider->GetActorLocation()) < 1200.f)
			{
				const FRotator Face = (Chaser->GetActorLocation() - Hider->GetActorLocation()).Rotation();
				Hider->SetActorRotation(FRotator(0.f, Face.Yaw, 0.f));
				PlaytestTap(EKakurenboAction::UseItem);
				++P.RoundDaruma;
				PlaytestLog(TEXT("used daruma"));
			}
		}
		// 走り出したら煙幕
		if (GM->IsDashUnlocked() && Hider->GetDashUsesLeft() > 0 && P.bForwardDown && !Hider->IsDashing()
			&& FVector::Dist2D(Chaser->GetActorLocation(), Hider->GetActorLocation()) < 900.f)
		{
			PlaytestTap(EKakurenboAction::Dash);
			++P.RoundDashes;
			PlaytestLog(TEXT("smoke dash"));
		}
	}
	else if (P.Move == FKakurenboPlaytest::EMove::Flee && Now - P.NotChasedSince > 2.0)
	{
		// 見失ってもらえた：その場（近く）に隠れ直す
		P.Move = FKakurenboPlaytest::EMove::Hide;
		P.Home = MyCell;
		P.Path.Reset();
		PlaytestLog(FString::Printf(TEXT("lost the chaser at %.1fs, hiding at (%d,%d)"), Elapsed, MyCell.X, MyCell.Y));
	}
	else if (P.Move == FKakurenboPlaytest::EMove::Return && !Chaser)
	{
		if (P.Path.Num() == 0 || P.PathIndex >= P.Path.Num())
		{
			if (MyCell == P.Home || !PathTo(P.Home))
			{
				P.Move = FKakurenboPlaytest::EMove::Hide;
				P.Home = MyCell;
			}
		}
	}

	// お宝を取りに行くか（近くに鬼の足音がしない・時間が残っている・歩いて行ける近さ）
	if (!Chaser && P.Move == FKakurenboPlaytest::EMove::Hide && Now >= P.NextThink)
	{
		P.NextThink = Now + 2.0;
		// 懲りた分だけ慎重に：お宝で 1 回見つかったら鬼が離れてから・遅めに、3 回見つかったらそのステージではもう取りに行かない
		const int32 Burned = P.TreasureDeaths.FindRef(S->Stage);
		const int32 MaxBurn = P.bGreedy ? 6 : 3;
		const float SafeDist = P.bGreedy ? 800.f : 1100.f + 500.f * Burned;
		if (Burned < MaxBurn && Elapsed > 2.5f + (P.bGreedy ? 0.f : 6.f * Burned) && NearestDist > SafeDist && S->HideTimeRemaining > 10.f && GM->GetTreasures().Num() > 0)
		{
			const FKakurenboPathGrid Walk = Grid->BuildWalkGrid();
			ATreasureActor* BestT = nullptr;
			TArray<FIntPoint> BestPath;
			for (ATreasureActor* T : GM->GetTreasures())
			{
				TArray<FIntPoint> Path;
				if (T && KakurenboPathfinding::FindPath(Walk, MyCell, Grid->WorldToCell(T->GetActorLocation()), Path) && Path.Num() <= (P.bGreedy ? 32 : 16) && (!BestT || Path.Num() < BestPath.Num()))
				{
					BestT = T;
					BestPath = Path;
				}
			}
			if (BestT)
			{
				P.Move = FKakurenboPlaytest::EMove::GoTreasure;
				P.TargetTreasure = BestT;
				P.Path = BestPath;
				P.PathIndex = 0;
				++P.RoundTreasureTrips;
				P.LastTreasureTripAt = Elapsed;
				// 宝物鬼がいれば、しのび足で（チュートリアルの「対策」）
				bool bTreasureOni = false;
				for (const AOniCharacter* Oni : GM->GetOnis())
				{
					bTreasureOni |= Oni && Oni->OniType == EOniType::Treasure;
				}
				SetSneak(bTreasureOni);
				PlaytestLog(FString::Printf(TEXT("going for treasure (%d steps, %s)"), BestPath.Num(), bTreasureOni ? TEXT("sneaking") : TEXT("walking")));
			}
		}
	}
	if (P.Move == FKakurenboPlaytest::EMove::GoTreasure)
	{
		const bool bGone = !P.TargetTreasure.IsValid() || !GM->GetTreasures().Contains(P.TargetTreasure.Get());
		if (bGone || NearestDist < 700.f || P.PathIndex >= P.Path.Num())
		{
			if (!bGone)
			{
				PlaytestLog(FString::Printf(TEXT("gave up the treasure (oni %.0f away)"), NearestDist));
			}
			P.Move = FKakurenboPlaytest::EMove::Return;
			PathTo(P.Home);
		}
	}

	// 道に沿って歩く（カメラを行き先へ向けて W を押す。WASD はカメラ基準）
	const bool bWantMove = P.Path.IsValidIndex(P.PathIndex);
	if (bWantMove)
	{
		const FVector Target = Grid->CellFloorCenter(P.Path[P.PathIndex]);
		FVector To = Target - Hider->GetActorLocation();
		To.Z = 0.f;
		if (To.Size() < 45.f)
		{
			++P.PathIndex;
		}
		else
		{
			SetControlRotation(FRotator(-25.f, To.Rotation().Yaw, 0.f));
		}
		if (!P.bForwardDown)
		{
			PlaytestKey(EKakurenboAction::MoveForward, true);
			P.bForwardDown = true;
			P.StuckCheckPos = Hider->GetActorLocation();
			P.StuckCheckTime = Now;
		}
		// 引っかかったら：よじ登れるなら登る、無理なら道を選び直す
		if (Now - P.StuckCheckTime > 1.2)
		{
			if (FVector::Dist2D(Hider->GetActorLocation(), P.StuckCheckPos) < 40.f && !Hider->IsClimbing())
			{
				++P.RoundStuck;
				++P.StuckInARow;
				if (GM->IsClimbUnlocked() && P.StuckInARow == 1)
				{
					PlaytestTap(EKakurenboAction::Climb);
				}
				else if (P.StuckInARow >= 3)
				{
					PlaytestLog(FString::Printf(TEXT("stuck at (%d,%d) going to (%d,%d)"), MyCell.X, MyCell.Y, P.Path.Last().X, P.Path.Last().Y));
					P.Path.Reset();
					P.Move = FKakurenboPlaytest::EMove::Hide;
					P.Home = MyCell;
					P.StuckInARow = 0;
				}
				else
				{
					PathTo(P.Path.Last());
				}
			}
			else
			{
				P.StuckInARow = 0;
			}
			P.StuckCheckPos = Hider->GetActorLocation();
			P.StuckCheckTime = Now;
		}
	}
	else if (P.bForwardDown)
	{
		PlaytestKey(EKakurenboAction::MoveForward, false);
		P.bForwardDown = false;
	}

	// 隠れ場所では：しゃがんで、鬼の足音が近くなければ連打（ヒントの「連打の音で鬼が寄ってくる」を読んだので、近いときは止める）
	if (P.Move == FKakurenboPlaytest::EMove::Hide && !bWantMove)
	{
		SetSneak(true);
		const bool bQuietNow = NearestDist < 900.f;
		if (!bQuietNow && GM->CanMash() && Now >= P.NextMash)
		{
			PlaytestTap(EKakurenboAction::Mash);
			P.NextMash = Now + 0.14 + P.Rand.FRandRange(0.0, 0.08); // 1 秒に 5〜7 回
		}
		// ときどき周りを見回す（カメラ）
		if (P.Rand.FRand() < RealDeltaSeconds * 0.3f)
		{
			SetControlRotation(FRotator(-30.f, GetControlRotation().Yaw + P.Rand.FRandRange(-90.f, 90.f), 0.f));
		}
	}

	// 様子の写真（ステージごとに 1 枚、真ん中あたり）
	const FString ShotKey = FString::Printf(TEXT("hide_%d_%d"), S->PrestigeCount, S->Stage);
	if (Elapsed > S->HideTimeLimit * 0.4f && !P.ShotOnce.Contains(ShotKey))
	{
		P.ShotOnce.Add(ShotKey);
		PlaytestShot(FString::Printf(TEXT("s%02d_hide"), S->Stage));
	}
}
