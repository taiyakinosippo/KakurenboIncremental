// 自動テスト（KakuAutoTest コマンド）の実装。
// 実際にゲームを動かしながら操作を時間差で実行し、ログとスクリーンショットで結果を確認する。
// Tools/RunAutoTest.ps1 -Scenario <名前> から起動する。

#include "Camera/CameraComponent.h"
#include "GameFramework/PlayerInput.h"
#include "HiderCharacter.h"
#include "InputKeyEventArgs.h"
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
	// 視点を急に回した直後はモーションブラーで画像がぶれるので、少し待ってから撮る
	auto ShotLater = [this, Shot](const FString& Name)
	{
		FTimerHandle Handle;
		GetWorldTimerManager().SetTimer(Handle, FTimerDelegate::CreateLambda([Shot, Name] { Shot(Name); }), 0.3f, false);
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
	// 鬼に背を向ける（画面外の矢印表示の確認用）
	auto LookAwayFromOni = [this, GM]()
	{
		if (const AOniCharacter* Oni = GM->GetOni())
		{
			if (GetPawn())
			{
				FRotator Rot = (GetPawn()->GetActorLocation() - Oni->GetActorLocation()).Rotation();
				Rot.Pitch = -5.f;
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
	// 結果の判定をログに出す（RunAutoTest.ps1 が "CHECK" を拾って表示する）
	auto Check = [](const FString& What, bool bOk)
	{
		if (bOk)
		{
			UE_LOG(LogTemp, Display, TEXT("[AutoTest] CHECK OK: %s"), *What);
		}
		else
		{
			UE_LOG(LogTemp, Error, TEXT("[AutoTest] CHECK FAILED: %s"), *What);
		}
	};
	// 本物の入力と同じ経路（PlayerInput）に疑似的なキー・マウス入力を流す
	auto SimulateKey = [this](const FKey& Key, EInputEvent Event)
	{
		if (PlayerInput)
		{
			PlayerInput->InputKey(FInputKeyEventArgs::CreateSimulated(Key, Event, 1.f));
		}
	};
	auto SimulateMouse = [this](float DX, float DY)
	{
		if (PlayerInput)
		{
			PlayerInput->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::MouseX, IE_Axis, DX, 1));
			PlayerInput->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::MouseY, IE_Axis, DY, 1));
		}
	};

	TArray<FAutoTestStep> Steps;

	if (Scenario.Equals(TEXT("Camera"), ESearchCase::IgnoreCase))
	{
		// ---- 1. かくれんぼ（一人称）：マウスで視点が回るか ----
		Steps.Add({ 1.f, [=, this]
		{
			AHiderCharacter* Hider = GetHider();
			Check(TEXT("hide phase uses first-person camera"),
				Hider->GetViewMode() == EHiderViewMode::FirstPerson && Hider->FirstPersonCamera->IsActive() && !Hider->OverheadCamera->IsActive());
			Check(TEXT("cursor hidden in hide phase"), !bShowMouseCursor);
			const FRotator RotBefore = GetControlRotation();
			SimulateMouse(150.f, 60.f); // 右へ 150、上へ 60 カウント

			// 疑似入力は次のフレームの入力処理で反映される。タイマーは入力処理より後に動くので、次のフレームで確かめる
			GetWorldTimerManager().SetTimerForNextTick(FTimerDelegate::CreateLambda([=, this]
			{
				const FRotator Now = GetControlRotation();
				const float DYaw = FRotator::NormalizeAxis(Now.Yaw - RotBefore.Yaw);
				const float DPitch = FRotator::NormalizeAxis(Now.Pitch - RotBefore.Pitch);
				Check(FString::Printf(TEXT("mouse right turns view right (dYaw=%.1f, expected %.1f)"), DYaw, 150.f * MouseSensitivity),
					FMath::IsNearlyEqual(DYaw, 150.f * MouseSensitivity, 0.5f));
				Check(FString::Printf(TEXT("mouse up looks up (dPitch=%.1f, expected %.1f)"), DPitch, 60.f * MouseSensitivity),
					FMath::IsNearlyEqual(DPitch, 60.f * MouseSensitivity, 0.5f));
			}));
		} });
		Steps.Add({ 0.3f, [=] { Shot(TEXT("camera_01_hide_firstperson")); } });

		// ---- 1b. 左クリックで連打しても、画面（視野角・カメラ位置）が拡縮しない ----
		Steps.Add({ 0.3f, [=, this]
		{
			AHiderCharacter* Hider = GetHider();
			const float FovBefore = Hider->FirstPersonCamera->FieldOfView;
			const FVector CamBefore = Hider->FirstPersonCamera->GetRelativeLocation();
			const int32 MashBefore = GetWorld()->GetGameState<AKakurenboGameState>()->MashCountThisRound;
			SimulateKey(EKeys::LeftMouseButton, IE_Pressed);
			SimulateKey(EKeys::LeftMouseButton, IE_Released);

			// 以前の演出（視野角を広げる・視点を沈める）は 0.125 秒ほど続いたので、直後のフレームで確かめる
			GetWorldTimerManager().SetTimerForNextTick(FTimerDelegate::CreateLambda([=, this]
			{
				const AKakurenboGameState* S = GetWorld()->GetGameState<AKakurenboGameState>();
				Check(FString::Printf(TEXT("left click mashes (count %d -> %d)"), MashBefore, S->MashCountThisRound), S->MashCountThisRound == MashBefore + 1);
				Check(FString::Printf(TEXT("mash does not zoom the view (FOV %.2f -> %.2f)"), FovBefore, Hider->FirstPersonCamera->FieldOfView),
					FMath::IsNearlyEqual(FovBefore, Hider->FirstPersonCamera->FieldOfView, 0.01f));
				Check(TEXT("mash does not move the camera"), Hider->FirstPersonCamera->GetRelativeLocation().Equals(CamBefore, 0.01f));
				Shot(TEXT("camera_01b_mash_popup"));
			}));
		} });

		// ---- 2. 鬼の方向表示（背後なら画面の縁に矢印、正面なら頭上にマーカー） ----
		Steps.Add({ 3.f, [=] { LookAwayFromOni(); Log(TEXT("oni behind")); } });
		Steps.Add({ 0.3f, [=] { Shot(TEXT("camera_02_oni_arrow")); } });
		Steps.Add({ 0.5f, [=] { LookAtOni(); } });
		Steps.Add({ 0.3f, [=] { Shot(TEXT("camera_03_oni_marker")); } });

		// ---- 3. リザルト（俯瞰） ----
		Steps.Add({ 0.5f, [=, this] { KakuSkipTime(1000.f); } });
		Steps.Add({ 1.f, [=, this]
		{
			AHiderCharacter* Hider = GetHider();
			Check(TEXT("result phase uses overhead camera"),
				Hider->GetViewMode() == EHiderViewMode::Overhead && Hider->OverheadCamera->IsActive() && !Hider->FirstPersonCamera->IsActive());
			Shot(TEXT("camera_04_result_overhead"));
		} });

		// ---- 4. 購入（俯瞰）：Q で回転、ホイールでズーム ----
		TSharedRef<float> YawBefore = MakeShared<float>(0.f);
		TSharedRef<float> ZoomBefore = MakeShared<float>(0.f);
		Steps.Add({ 0.5f, [=, this]
		{
			KakuNext(); // → 購入
			KakuAddCoins(500.0);
			KakuBuy(3); KakuBuy(3); KakuBuy(3); // 木の壁
			*YawBefore = GetHider()->GetOverheadYaw();
			*ZoomBefore = GetHider()->OverheadDistance;
			SimulateKey(EKeys::Q, IE_Pressed);
			SimulateKey(EKeys::MouseScrollUp, IE_Pressed);
			SimulateKey(EKeys::MouseScrollUp, IE_Released);
		} });
		Steps.Add({ 0.5f, [=, this]
		{
			SimulateKey(EKeys::Q, IE_Released);
			const float DYaw = FRotator::NormalizeAxis(GetHider()->GetOverheadYaw() - *YawBefore);
			Check(FString::Printf(TEXT("Q rotates overhead camera (dYaw=%.1f)"), DYaw), DYaw < -20.f);
			Check(FString::Printf(TEXT("wheel up zooms in (%.0f -> %.0f)"), *ZoomBefore, GetHider()->OverheadDistance), GetHider()->OverheadDistance < *ZoomBefore);
			Shot(TEXT("camera_05_shop_overhead"));
		} });

		// ---- 5. 設置（俯瞰）：画面上の位置でマスを指せるか、WASD が画面基準か ----
		TSharedRef<FVector> PosBefore = MakeShared<FVector>();
		Steps.Add({ 0.5f, [=, this] { KakuNext(); } }); // → 設置
		Steps.Add({ 0.8f, [=, this]
		{
			AHiderCharacter* Hider = GetHider();
			UKakurenboGridSubsystem* Grid = GetWorld()->GetSubsystem<UKakurenboGridSubsystem>();
			Check(TEXT("build phase uses overhead camera"), Hider->GetViewMode() == EHiderViewMode::Overhead && Hider->OverheadCamera->IsActive());
			Check(TEXT("cursor visible in build phase"), bShowMouseCursor);

			// 自分の 3 マス前（画面の上方向）の床を、画面上の位置から指す
			const FVector Fwd = FRotationMatrix(FRotator(0.f, Hider->GetOverheadYaw(), 0.f)).GetUnitAxis(EAxis::X);
			const FIntPoint Target = Grid->WorldToCell(Hider->GetActorLocation() + Fwd * 300.f);
			FVector2D ScreenPos;
			const bool bProjected = ProjectWorldLocationToScreen(Grid->CellFloorCenter(Target), ScreenPos, true);
			UpdateBuildTargetAt(ScreenPos);
			Check(FString::Printf(TEXT("screen point on floor -> cell (%d,%d), expected (%d,%d)"), BuildTargetCell.X, BuildTargetCell.Y, Target.X, Target.Y),
				bProjected && bHasBuildTarget && BuildTargetCell == Target);
			GM->PlaceWall(BuildTargetCell, 0);

			// 置いたブロックの上面を指すと、同じマスに積める
			ProjectWorldLocationToScreen(Grid->CellToWorld(Target, 0) + FVector(0.f, 0.f, Grid->GetCellSize() * 0.5f), ScreenPos, true);
			UpdateBuildTargetAt(ScreenPos);
			Check(FString::Printf(TEXT("screen point on block top -> same cell (%d,%d)"), BuildTargetCell.X, BuildTargetCell.Y),
				bHasBuildTarget && BuildTargetCell == Target && PickUpTarget != nullptr);
			GM->PlaceWall(BuildTargetCell, 0);
			Check(FString::Printf(TEXT("stacked height = %d"), Grid->GetColumnHeight(Target)), Grid->GetColumnHeight(Target) == 2);

			// D キーで画面の右方向へ歩く
			*PosBefore = Hider->GetActorLocation();
			SimulateKey(EKeys::D, IE_Pressed);
		} });
		Steps.Add({ 0.6f, [=, this]
		{
			SimulateKey(EKeys::D, IE_Released);
			const FVector Delta = GetPawn()->GetActorLocation() - *PosBefore;
			const FVector RightDir = FRotationMatrix(FRotator(0.f, GetHider()->GetOverheadYaw(), 0.f)).GetUnitAxis(EAxis::Y);
			const float Moved = FVector::DotProduct(Delta, RightDir);
			Check(FString::Printf(TEXT("D moves toward screen right (%.0f cm)"), Moved), Moved > 100.f);
			Log(TEXT("build"));
			Shot(TEXT("camera_06_build_overhead"));
		} });

		// ---- 6. かくれんぼへ戻ると一人称。俯瞰カメラが向いていた方向を向いて始まる ----
		Steps.Add({ 0.5f, [=, this] { KakuNext(); } });
		Steps.Add({ 0.3f, [=, this]
		{
			AHiderCharacter* Hider = GetHider();
			Check(TEXT("back to first-person"), Hider->GetViewMode() == EHiderViewMode::FirstPerson && Hider->FirstPersonCamera->IsActive());
			const float Diff = FMath::Abs(FRotator::NormalizeAxis(GetControlRotation().Yaw - Hider->GetOverheadYaw()));
			Check(FString::Printf(TEXT("first-person starts facing the overhead direction (diff=%.1f)"), Diff), Diff < 1.f);
			Check(TEXT("cursor hidden again"), !bShowMouseCursor);
			Shot(TEXT("camera_07_hide_again"));
		} });
	}
	else if (Scenario.Equals(TEXT("Touch"), ESearchCase::IgnoreCase))
	{
		// 目の見えない鬼を音だけで呼び寄せ、ぶつかった時点で見つかることを確認する
		Steps.Add({ 3.5f, [=]
		{
			if (AOniCharacter* Oni = GM->GetOni())
			{
				Oni->SightRadius = 0.f;          // 視線では見つけられない
				Oni->CloseSenseRadius = 0.f;
				Oni->NoiseInaccuracyCells = 0.f; // 音のした場所を正確に聞き取る
				Oni->HearingRadius = 100000.f;   // どこにいても聞こえる
				Oni->bDrawDebug = true;
			}
			Log(TEXT("blind oni"));
		} });
		for (int32 i = 0; i < 100; ++i)
		{
			Steps.Add({ 0.2f, [=, this] { KakuMash(1); } });
			if (i % 10 == 9)
			{
				const FString Label = FString::Printf(TEXT("mashing %d"), i + 1);
				Steps.Add({ 0.f, [=] { Log(Label); } });
			}
		}
		Steps.Add({ 0.5f, [=, this]
		{
			const AKakurenboGameState* S = GetWorld()->GetGameState<AKakurenboGameState>();
			const AOniCharacter* Oni = GM->GetOni();
			const FName Reason = Oni ? Oni->GetFoundReason() : FName();
			Check(FString::Printf(TEXT("blind oni finds the hider by touching (phase=%s, reason=%s)"), *UEnum::GetValueAsString(S->Phase), *Reason.ToString()),
				S->Phase == EKakurenboPhase::Result && !S->bLastRoundCleared && Reason == FName(TEXT("touch")));
			Shot(TEXT("touch_01_found"));
		} });
	}
	else if (Scenario.Equals(TEXT("Oni"), ESearchCase::IgnoreCase))
	{
		// 鬼が出てきてから連打し続け、音に寄ってきて見つかるまでを確認する
		Steps.Add({ 4.f, [=] { EnableOniDebug(); LookAtOni(); Log(TEXT("oni spawned")); ShotLater(TEXT("oni_01_spawned")); } });
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
				Steps.Add({ 0.f, [=] { LookAtOni(); ShotLater(TEXT("oni_02_investigating")); } });
			}
		}
		for (int32 i = 0; i < 10; ++i)
		{
			Steps.Add({ 1.f, [=] { LookAtOni(); Log(TEXT("waiting")); } });
		}
		Steps.Add({ 0.f, [=]
		{
			LookAtOni();
			Log(TEXT("final"));
			ShotLater(TEXT("oni_03_final"));
			const AOniCharacter* Oni = GM->GetOni();
			const FName Reason = Oni ? Oni->GetFoundReason() : FName();
			Check(FString::Printf(TEXT("oni finds the mashing hider by sight (reason=%s)"), *Reason.ToString()), Reason == FName(TEXT("sight")));
		} });
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
			GetHider()->SetOverheadYaw(30.f);
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
					ShotLater(TEXT("build_03_oni_attacking"));
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
