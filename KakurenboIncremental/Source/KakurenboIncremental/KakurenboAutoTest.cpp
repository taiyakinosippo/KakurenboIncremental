// 自動テスト（KakuAutoTest コマンド）の実装。
// 実際にゲームを動かしながら操作を時間差で実行し、ログとスクリーンショットで結果を確認する。
// Tools/RunAutoTest.ps1 -Scenario <名前> から起動する。

#include "HiderCharacter.h"
#include "KakurenboGameMode.h"
#include "KakurenboGameState.h"
#include "KakurenboGridSubsystem.h"
#include "KakurenboPlayerController.h"
#include "Misc/Paths.h"
#include "OniCharacter.h"
#include "TimerManager.h"
#include "UnrealClient.h"

namespace
{
	struct FAutoTestStep
	{
		float Delay; // 前のステップからの待ち時間（秒）
		TFunction<void()> Run;
	};
}

void AKakurenboPlayerController::KakuAutoTest(const FString& Scenario)
{
	AKakurenboGameMode* GM = GetKakurenboGameMode();
	if (!GM)
	{
		return;
	}

	auto Shot = [](const FString& Name)
	{
		const FString Path = FPaths::ProjectSavedDir() / TEXT("AutoTest") / (Name + TEXT(".png"));
		FScreenshotRequest::RequestScreenshot(Path, true, false);
		UE_LOG(LogTemp, Display, TEXT("[AutoTest] screenshot %s"), *FPaths::GetCleanFilename(Path));
	};
	auto Log = [this, GM](const FString& Label)
	{
		const AKakurenboGameState* S = GetWorld()->GetGameState<AKakurenboGameState>();
		const UKakurenboGridSubsystem* Grid = GetWorld()->GetSubsystem<UKakurenboGridSubsystem>();
		if (!S)
		{
			return;
		}
		FString OniInfo = TEXT("none");
		if (const AOniCharacter* Oni = GM->GetOni())
		{
			const float Dist = GetPawn() ? FVector::Dist2D(GetPawn()->GetActorLocation(), Oni->GetActorLocation()) : -1.f;
			OniInfo = FString::Printf(TEXT("%s dist=%.0f"), *UEnum::GetValueAsString(Oni->GetOniState()), Dist);
		}
		UE_LOG(LogTemp, Display, TEXT("[AutoTest] %s: Phase=%s Stage=%d Coins=%.2f Earned=%.2f Mash=%d MashLv=%d TimeLv=%d Remaining=%.1f Blocks=%d Stock=%s Destroyed=%d Oni=%s"),
			*Label, *UEnum::GetValueAsString(S->Phase), S->Stage, S->Coins, S->CoinsEarnedThisRound,
			S->MashCountThisRound, S->MashIncomeLevel, S->TimeIncomeLevel, S->HideTimeRemaining,
			Grid ? Grid->GetBlockCount() : -1,
			*FString::JoinBy(S->WallStock, TEXT(","), [](int32 N) { return FString::FromInt(N); }),
			S->LastRoundWallsDestroyed, *OniInfo);
	};
	// カメラを鬼の方へ向ける（スクリーンショットに鬼が写るように）
	auto LookAtOni = [this, GM]()
	{
		if (const AOniCharacter* Oni = GM->GetOni())
		{
			if (GetPawn())
			{
				FRotator Rot = (Oni->GetActorLocation() - GetPawn()->GetActorLocation()).Rotation();
				Rot.Pitch = -15.f;
				SetControlRotation(Rot);
			}
		}
	};
	auto EnableOniDebug = [GM]()
	{
		if (AOniCharacter* Oni = GM->GetOni())
		{
			Oni->bDrawDebug = true;
		}
	};

	TArray<FAutoTestStep> Steps;

	if (Scenario.Equals(TEXT("Oni"), ESearchCase::IgnoreCase))
	{
		// 鬼が出てきてから連打し続け、音に寄ってきて見つかるまでを確認する
		Steps.Add({ 4.f, [=] { EnableOniDebug(); LookAtOni(); Log(TEXT("oni spawned")); Shot(TEXT("oni_01_spawned")); } });
		for (int32 i = 0; i < 60; ++i)
		{
			Steps.Add({ 0.2f, [=, this] { KakuMash(1); } });
			if (i % 10 == 9)
			{
				const FString Label = FString::Printf(TEXT("mashing %d"), i + 1);
				Steps.Add({ 0.f, [=] { LookAtOni(); Log(Label); } });
			}
			if (i == 20)
			{
				Steps.Add({ 0.f, [=] { LookAtOni(); Shot(TEXT("oni_02_investigating")); } });
			}
		}
		for (int32 i = 0; i < 10; ++i)
		{
			Steps.Add({ 1.f, [=] { LookAtOni(); Log(TEXT("waiting")); } });
		}
		Steps.Add({ 0.f, [=] { LookAtOni(); Log(TEXT("final")); Shot(TEXT("oni_03_final")); } });
	}
	else if (Scenario.Equals(TEXT("Build"), ESearchCase::IgnoreCase))
	{
		// 壁を買う → プレイヤーの周りを 2 段の壁で囲む → 鬼を呼んで壁を壊させる
		Steps.Add({ 1.f, [=, this] { KakuSkipTime(1000.f); } });
		Steps.Add({ 1.f, [=, this]
		{
			KakuNext(); // → 購入
			KakuAddCoins(2000.0);
			for (int32 i = 0; i < 9; ++i) { KakuBuy(3); } // 木の壁
			for (int32 i = 0; i < 8; ++i) { KakuBuy(4); } // 石の壁
			Log(TEXT("shop bought walls"));
			Shot(TEXT("build_01_shop"));
		} });
		Steps.Add({ 1.f, [=, this]
		{
			KakuNext(); // → 設置
			UKakurenboGridSubsystem* Grid = GetWorld()->GetSubsystem<UKakurenboGridSubsystem>();
			const FIntPoint Me = Grid->WorldToCell(GetPawn()->GetActorLocation());
			// マスの中心に立たせる（はみ出していると隣のマスに置けないため）
			GetPawn()->SetActorLocation(Grid->CellFloorCenter(Me) + FVector(0, 0, GetPawn()->GetSimpleCollisionHalfHeight() + 2.f));
			// 周囲 8 マスに 下段: 木 / 上段: 石
			for (int32 Level = 0; Level < 2; ++Level)
			{
				for (int32 DY = -1; DY <= 1; ++DY)
				{
					for (int32 DX = -1; DX <= 1; ++DX)
					{
						if (DX != 0 || DY != 0)
						{
							KakuPlaceWall(Me.X + DX, Me.Y + DY, Level == 0 ? 0 : 1);
						}
					}
				}
			}
			// 回収のテスト：1 つ置いて回収すると在庫が戻る
			KakuPlaceWall(Me.X + 3, Me.Y, 0);
			Log(TEXT("placed (+1 extra)"));
			GM->PickUpWall(Grid->GetTopBlock(FIntPoint(Me.X + 3, Me.Y)));
			Log(TEXT("picked up extra"));
			// 自分のマスには置けない
			KakuPlaceWall(Me.X, Me.Y, 0);
			Log(TEXT("tried own cell"));
			SetControlRotation(FRotator(-35.f, 30.f, 0.f));
		} });
		Steps.Add({ 0.5f, [=] { Shot(TEXT("build_02_placed")); } });
		Steps.Add({ 1.f, [=, this] { KakuNext(); Log(TEXT("hide start")); } }); // → かくれんぼ
		Steps.Add({ 3.5f, [=] { EnableOniDebug(); LookAtOni(); Log(TEXT("oni spawned")); } });

		TSharedRef<bool> bAttackShot = MakeShared<bool>(false);
		for (int32 i = 0; i < 150; ++i)
		{
			Steps.Add({ 0.2f, [=, this]
			{
				KakuMash(1);
				const AOniCharacter* Oni = GM->GetOni();
				if (Oni && Oni->GetOniState() == EOniState::Attack && !*bAttackShot)
				{
					*bAttackShot = true;
					LookAtOni();
					Log(TEXT("oni attacking"));
					Shot(TEXT("build_03_oni_attacking"));
				}
			} });
			if (i % 10 == 9)
			{
				const FString Label = FString::Printf(TEXT("t+%.0fs"), (i + 1) * 0.2f);
				Steps.Add({ 0.f, [=] { LookAtOni(); Log(Label); } });
			}
		}
		Steps.Add({ 0.5f, [=] { Log(TEXT("final")); Shot(TEXT("build_04_final")); } });
	}
	else
	{
		// 基本ループ: 連打 → 逃げ切り → 購入 → 設置 → 次のステージ
		Steps.Add({ 1.f, [=] { Log(TEXT("start")); Shot(TEXT("loop_01_hide_countdown")); } });
		Steps.Add({ 1.f, [=, this] { KakuMash(30); Log(TEXT("after mash 30")); Shot(TEXT("loop_02_hide_mashing")); } });
		Steps.Add({ 1.f, [=, this] { KakuSkipTime(1000.f); } });
		Steps.Add({ 1.f, [=] { Log(TEXT("result")); Shot(TEXT("loop_03_result")); } });
		Steps.Add({ 1.f, [=, this] { KakuNext(); KakuAddCoins(1000.0); KakuBuy(1); KakuBuy(2); KakuBuy(2); Log(TEXT("shop bought")); Shot(TEXT("loop_04_shop")); } });
		Steps.Add({ 1.f, [=, this] { KakuNext(); Log(TEXT("build")); Shot(TEXT("loop_05_build")); } });
		Steps.Add({ 1.f, [=, this] { KakuNext(); Log(TEXT("hide stage 2")); } });
	}

	Steps.Add({ 1.f, [this] { UE_LOG(LogTemp, Display, TEXT("[AutoTest] done")); ConsoleCommand(TEXT("quit")); } });

	UE_LOG(LogTemp, Display, TEXT("[AutoTest] scenario '%s' with %d steps"), *Scenario, Steps.Num());
	float Time = 0.f;
	for (const FAutoTestStep& Step : Steps)
	{
		Time += Step.Delay;
		FTimerHandle Handle;
		GetWorldTimerManager().SetTimer(Handle, FTimerDelegate::CreateLambda(Step.Run), FMath::Max(Time, 0.01f), false);
	}
}
