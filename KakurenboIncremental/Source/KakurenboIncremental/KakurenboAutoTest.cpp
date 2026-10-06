// 自動テスト（KakuAutoTest コマンド）の実装。
// 実際にゲームを動かしながら操作を時間差で実行し、ログとスクリーンショットで結果を確認する。
// Tools/RunAutoTest.ps1 -Scenario <名前> から起動する。
//
// 結果の判定は "[AutoTest] CHECK OK / CHECK FAILED" としてログに出す。
// 疑似入力（SimulateKey / SimulateMouse）の結果は、次のフレームの入力処理で反映されるので NextTick で確かめる。

#include "Camera/CameraComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerInput.h"
#include "HiderCharacter.h"
#include "InputKeyEventArgs.h"
#include "KakurenboGameMode.h"
#include "KakurenboGameState.h"
#include "KakurenboGridSubsystem.h"
#include "KakurenboOniBlackboard.h"
#include "KakurenboPlayerController.h"
#include "KakurenboSaveGame.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/Paths.h"
#include "OniCharacter.h"
#include "TimerManager.h"
#include "TreasureActor.h"
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

	// 実際のキーボード・マウス入力を無視する（テスト中に PC を触っても結果がぶれないように）
	bIgnoreRealInputForAutoTest = true;
	ApplyAutoTestInputIsolation();

	// ---------------------------------------------------------------- 共通の道具

	auto GS = [this]() { return GetWorld()->GetGameState<AKakurenboGameState>(); };
	auto Grid = [this]() { return GetWorld()->GetSubsystem<UKakurenboGridSubsystem>(); };

	auto Shot = [](const FString& Name)
	{
		const FString Path = FPaths::ProjectSavedDir() / TEXT("AutoTest") / (Name + TEXT(".png"));
		FScreenshotRequest::RequestScreenshot(Path, true, false);
		UE_LOG(LogTemp, Display, TEXT("[AutoTest] screenshot %s"), *FPaths::GetCleanFilename(Path));
	};
	// カメラを急に動かした直後は画像がぶれるので、少し待ってから撮る
	auto ShotLater = [this, Shot](const FString& Name)
	{
		FTimerHandle Handle;
		GetWorldTimerManager().SetTimer(Handle, FTimerDelegate::CreateLambda([Shot, Name] { Shot(Name); }), 0.3f, false);
	};
	// 次のフレーム（入力処理が終わった後）に実行する
	auto NextTick = [this](TFunction<void()> Fn)
	{
		GetWorldTimerManager().SetTimerForNextTick(FTimerDelegate::CreateLambda(MoveTemp(Fn)));
	};
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
	auto Log = [this, GM, GS, Grid](const FString& Label)
	{
		const AKakurenboGameState* S = GS();
		const UKakurenboGridSubsystem* G = Grid();
		if (!S)
		{
			return;
		}
		FString OniInfo;
		for (const AOniCharacter* Oni : GM->GetOnis())
		{
			if (Oni)
			{
				const float Dist = GetPawn() ? FVector::Dist2D(GetPawn()->GetActorLocation(), Oni->GetActorLocation()) : -1.f;
				OniInfo += FString::Printf(TEXT("[%s/%s %.0f]"), *UEnum::GetValueAsString(Oni->GetOniState()), *UEnum::GetValueAsString(Oni->GetIntent()), Dist);
			}
		}
		UE_LOG(LogTemp, Display, TEXT("[AutoTest] %s: Phase=%s Stage=%d Coins=%.2f Earned=%.2f Mash=%d Remaining=%.1f Blocks=%d Missing=%d Stock=%s Destroyed=%d Treasure=%d/%d Onis=%s"),
			*Label, *UEnum::GetValueAsString(S->Phase), S->Stage, S->Coins, S->CoinsEarnedThisRound, S->MashCountThisRound, S->HideTimeRemaining,
			G ? G->GetBlockCount() : -1, G ? G->GetTotalMissing() : -1,
			*FString::JoinBy(S->WallStock, TEXT(","), [](int32 N) { return FString::FromInt(N); }),
			S->LastRoundWallsDestroyed, S->TreasuresCollectedThisRound, S->TreasuresThisRound, *OniInfo);
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
	// プレイヤーを床の上の指定位置へ動かす
	auto TeleportPlayer = [this](const FVector& FloorLocation)
	{
		if (APawn* P = GetPawn())
		{
			P->SetActorLocation(FVector(FloorLocation.X, FloorLocation.Y, P->GetSimpleCollisionHalfHeight() + 2.f), false, nullptr, ETeleportType::TeleportPhysics);
		}
	};
	// 三人称カメラを鬼の方へ向ける（スクリーンショットに鬼が写るように）
	auto LookAtOni = [this, GM]()
	{
		const AOniCharacter* Nearest = nullptr;
		for (const AOniCharacter* Oni : GM->GetOnis())
		{
			if (Oni && GetPawn() && (!Nearest || FVector::Dist(Oni->GetActorLocation(), GetPawn()->GetActorLocation()) < FVector::Dist(Nearest->GetActorLocation(), GetPawn()->GetActorLocation())))
			{
				Nearest = Oni;
			}
		}
		if (Nearest)
		{
			FRotator Rot = (Nearest->GetActorLocation() - GetPawn()->GetActorLocation()).Rotation();
			Rot.Pitch = -25.f;
			SetControlRotation(Rot);
		}
	};
	auto EnableOniDebug = [GM]()
	{
		for (AOniCharacter* Oni : GM->GetOnis())
		{
			if (Oni)
			{
				Oni->bDrawDebug = true;
			}
		}
	};

	// テスト用：指定した 1 体だけを動かし、他の鬼は止めて遠くへ置く
	auto KeepOnlyOni = [this, GM, Grid](int32 Keep) -> AOniCharacter*
	{
		AOniCharacter* Kept = GM->GetOnis().IsValidIndex(Keep) ? GM->GetOnis()[Keep].Get() : nullptr;
		for (int32 i = 0; i < GM->GetOnis().Num(); ++i)
		{
			AOniCharacter* Oni = GM->GetOnis()[i];
			if (Oni && Oni != Kept)
			{
				Oni->Deactivate();
				Oni->SetActorLocation(FVector(0.f, 0.f, -5000.f)); // 舞台の下へ（ぶつからないように）
				Oni->GetCharacterMovement()->DisableMovement();
			}
		}
		return Kept;
	};
	// テスト用：設置パートを通さずにマスへ木の壁を置く
	auto PlaceTestWall = [GM, Grid](const FIntPoint& Cell)
	{
		const FWallTypeDef& Def = GM->WallTypes[0];
		Grid()->PlaceBlock(Cell, 0, Def.MaxHP, Def.Color);
	};
	// テスト用：このラウンドに出てくる鬼の種類を変える（鬼が出てくる前に呼ぶ）
	auto SetStageOniTypes = [GM](const TArray<EOniType>& Types)
	{
		if (GM->StageRows.Num() == 0)
		{
			GM->StageRows.AddDefaulted();
		}
		GM->StageRows[0].OniTypes = Types;
	};
	// テスト用：目も耳も使わず、指定どおりに動く鬼にする
	auto MakeBlindListener = [](AOniCharacter* Oni)
	{
		Oni->SightRadius = 0.f;
		Oni->CloseSenseRadius = 0.f;
		Oni->HearingRadius = 100000.f;
		Oni->NoiseInaccuracyCells = 0.f;
		Oni->InvestigateMaxDuration = 100.f;
		Oni->PocketInspectChance = 0.f;
		Oni->bDrawDebug = true;
	};
	auto FoundReasonOfAny = [GM]() -> FName
	{
		for (const AOniCharacter* Oni : GM->GetOnis())
		{
			if (Oni && !Oni->GetFoundReason().IsNone())
			{
				return Oni->GetFoundReason();
			}
		}
		return NAME_None;
	};

	TArray<FAutoTestStep> Steps;

	// ================================================================ Entrance / Closed
	if (Scenario.Equals(TEXT("Entrance"), ESearchCase::IgnoreCase) || Scenario.Equals(TEXT("Closed"), ESearchCase::IgnoreCase))
	{
		// プレイヤーの周り 8 マスを木の壁で囲む。Entrance は東側の 1 マスを開けて入り口にする
		const bool bWithEntrance = Scenario.Equals(TEXT("Entrance"), ESearchCase::IgnoreCase);
		const FString Prefix = bWithEntrance ? TEXT("entrance") : TEXT("closed");
		Steps.Add({ 0.3f, [=, this]
		{
			SetStageOniTypes({ EOniType::Balanced });
			const FIntPoint Me = Grid()->WorldToCell(GetPawn()->GetActorLocation());
			TeleportPlayer(Grid()->CellFloorCenter(Me));
			for (int32 DY = -1; DY <= 1; ++DY)
			{
				for (int32 DX = -1; DX <= 1; ++DX)
				{
					const bool bEntrance = bWithEntrance && DX == 1 && DY == 0;
					if ((DX != 0 || DY != 0) && !bEntrance)
					{
						PlaceTestWall(Me + FIntPoint(DX, DY));
					}
				}
			}
			Check(FString::Printf(TEXT("%s: player is %s (pockets=%d)"), *Prefix, bWithEntrance ? TEXT("not enclosed") : TEXT("enclosed"), Grid()->FindEnclosedPockets().Num()),
				Grid()->FindEnclosedPockets().Num() == (bWithEntrance ? 0 : 1));
		} });
		Steps.Add({ 3.3f, [=]
		{
			if (AOniCharacter* Oni = KeepOnlyOni(0))
			{
				MakeBlindListener(Oni); // 目を使わず、連打の音だけで近づかせる
			}
			Log(TEXT("oni ready"));
		} });
		for (int32 i = 0; i < 80; ++i)
		{
			Steps.Add({ 0.25f, [=, this] { KakuMash(1); } });
			if (i % 20 == 19)
			{
				const FString Label = FString::Printf(TEXT("t+%.0fs"), (i + 1) * 0.25f);
				Steps.Add({ 0.f, [=] { Log(Label); } });
			}
		}
		Steps.Add({ 0.5f, [=, this]
		{
			const int32 Destroyed = GS()->LastRoundWallsDestroyed;
			Check(FString::Printf(TEXT("%s: oni reached the player (phase=%s, reason=%s)"), *Prefix, *UEnum::GetValueAsString(GS()->Phase), *FoundReasonOfAny().ToString()),
				GS()->Phase == EKakurenboPhase::Result && FoundReasonOfAny() == FName(TEXT("touch")));
			if (bWithEntrance)
			{
				Check(FString::Printf(TEXT("entrance: came in through the entrance without breaking walls (destroyed=%d)"), Destroyed), Destroyed == 0);
			}
			else
			{
				Check(FString::Printf(TEXT("closed: no other route, so it broke in (destroyed=%d)"), Destroyed), Destroyed >= 1);
			}
			Shot(Prefix + TEXT("_01_result"));
		} });
	}
	// ================================================================ Pocket
	else if (Scenario.Equals(TEXT("Pocket"), ESearchCase::IgnoreCase))
	{
		// 角に斜めに壁を置いて空洞を作り、鬼が壊して調べに行くか
		Steps.Add({ 0.3f, [=, this]
		{
			SetStageOniTypes({ EOniType::Balanced });
			for (int32 k = 0; k <= 3; ++k)
			{
				PlaceTestWall(FIntPoint(k, 3 - k));
			}
			Check(FString::Printf(TEXT("diagonal walls make a hollow corner (pockets=%d)"), Grid()->FindEnclosedPockets().Num()), Grid()->FindEnclosedPockets().Num() == 1);
		} });
		Steps.Add({ 3.3f, [=, this]
		{
			if (AOniCharacter* Oni = KeepOnlyOni(0))
			{
				Oni->SightRadius = 0.f;
				Oni->CloseSenseRadius = 0.f;
				Oni->HearingRadius = 0.f;
				Oni->PocketInspectChance = 1.f; // 空洞があれば必ず調べに行く
				Oni->bDrawDebug = true;
			}
			// プレイヤーは空洞から遠い反対側の隅へ
			TeleportPlayer(Grid()->CellFloorCenter(FIntPoint(Grid()->GetSizeX() - 2, Grid()->GetSizeY() - 2)));
			GetHider()->SetOverheadYaw(225.f);
		} });
		for (int32 i = 0; i < 25; ++i)
		{
			Steps.Add({ 1.f, [=]
			{
				const AOniCharacter* Oni = GM->GetOnis().Num() > 0 ? GM->GetOnis()[0].Get() : nullptr;
				if (Oni && Oni->GetOniState() == EOniState::Attack)
				{
					LookAtOni();
				}
				Log(TEXT("waiting"));
			} });
		}
		Steps.Add({ 0.5f, [=, this]
		{
			const int32 Destroyed = GS()->LastRoundWallsDestroyed;
			Check(FString::Printf(TEXT("oni broke into the hollow corner (destroyed=%d, pockets left=%d)"), Destroyed, Grid()->FindEnclosedPockets().Num()),
				Destroyed >= 1 && Grid()->FindEnclosedPockets().Num() == 0);
			Shot(TEXT("pocket_01_after"));
		} });
	}
	// ================================================================ Spin
	else if (Scenario.Equals(TEXT("Spin"), ESearchCase::IgnoreCase))
	{
		// プレイヤーが鬼の真上（少しずれた位置）に浮いているとき、鬼がくるくる回らないか
		struct FSpinRecord
		{
			float LastYaw = 0.f;
			float TotalTurn = 0.f;
			FVector StartLocation = FVector::ZeroVector;
		};
		TSharedRef<FSpinRecord> Record = MakeShared<FSpinRecord>();
		Steps.Add({ 0.3f, [=] { SetStageOniTypes({ EOniType::Balanced }); } });
		Steps.Add({ 3.3f, [=, this]
		{
			AOniCharacter* Oni = KeepOnlyOni(0);
			UCharacterMovementComponent* Move = GetHider()->GetCharacterMovement();
			Move->SetMovementMode(MOVE_Flying);
			Move->StopMovementImmediately();
			GetPawn()->SetActorLocation(Oni->GetActorLocation() + FVector(30.f, 0.f, 250.f), false, nullptr, ETeleportType::TeleportPhysics);
		} });
		Steps.Add({ 0.5f, [=, this]
		{
			AOniCharacter* Oni = GM->GetOnis()[0];
			Check(FString::Printf(TEXT("oni chases the player above it (intent=%s)"), *UEnum::GetValueAsString(Oni->GetIntent())), Oni->GetIntent() == EOniState::Chase);
			Record->LastYaw = Oni->GetActorRotation().Yaw;
			Record->StartLocation = Oni->GetActorLocation();
		} });
		for (int32 i = 0; i < 30; ++i)
		{
			Steps.Add({ 0.05f, [=]
			{
				AOniCharacter* Oni = GM->GetOnis()[0];
				const float Yaw = Oni->GetActorRotation().Yaw;
				Record->TotalTurn += FMath::Abs(FRotator::NormalizeAxis(Yaw - Record->LastYaw));
				Record->LastYaw = Yaw;
			} });
		}
		Steps.Add({ 0.1f, [=, this]
		{
			AOniCharacter* Oni = GM->GetOnis()[0];
			const float Moved = FVector::Dist2D(Oni->GetActorLocation(), Record->StartLocation);
			Check(FString::Printf(TEXT("oni does not spin under the player (turned %.0f deg in 1.5s, moved %.0f cm)"), Record->TotalTurn, Moved),
				Record->TotalTurn < 90.f && Moved < 60.f);
			Check(TEXT("hovering player is not caught"), GS()->Phase == EKakurenboPhase::Hide);
		} });
	}
	// ================================================================ Breaker
	else if (Scenario.Equals(TEXT("Breaker"), ESearchCase::IgnoreCase))
	{
		// パワー鬼：音には反応せず、見えた壁を壊しに行く。範囲攻撃・高い攻撃力
		Steps.Add({ 0.3f, [=] { SetStageOniTypes({ EOniType::Breaker }); } });
		Steps.Add({ 3.3f, [=, this]
		{
			AOniCharacter* Oni = KeepOnlyOni(0);
			Check(FString::Printf(TEXT("breaker spawned (type=%s, damage=%.2f, chase=%.0f)"), *UEnum::GetValueAsString(Oni->OniType), Oni->AttackDamage, Oni->ChaseSpeed),
				Oni->OniType == EOniType::Breaker && Oni->AttackDamage > 2.f && Oni->ChaseSpeed < 840.f);
			Oni->SightRadius = 1200.f;
			Oni->bDrawDebug = true;
			// パワー鬼の正面 4〜5 マス先に壁を 2 個置く（プレイヤーは見えない遠くへ）
			const FVector Fwd = Oni->GetActorForwardVector().GetSafeNormal2D();
			PlaceTestWall(Grid()->WorldToCell(Oni->GetActorLocation() + Fwd * 400.f));
			PlaceTestWall(Grid()->WorldToCell(Oni->GetActorLocation() + Fwd * 550.f + FVector(0.f, 100.f, 0.f)));
			TeleportPlayer(Grid()->CellFloorCenter(Grid()->FindSpreadFreeCells({ Oni->GetActorLocation() }, 1)[0]));
			KakuMash(1);
			NextTick([=]
			{
				Check(FString::Printf(TEXT("breaker ignores the mash noise (intent=%s)"), *UEnum::GetValueAsString(Oni->GetIntent())), Oni->GetIntent() != EOniState::Investigate);
			});
		} });
		for (int32 i = 0; i < 15; ++i)
		{
			Steps.Add({ 1.f, [=] { Log(TEXT("waiting")); } });
		}
		Steps.Add({ 0.1f, [=, this]
		{
			Check(FString::Printf(TEXT("breaker went and broke the walls it saw (destroyed=%d)"), GS()->LastRoundWallsDestroyed), GS()->LastRoundWallsDestroyed >= 2);
			Shot(TEXT("breaker_01_after"));
		} });
	}
	// ================================================================ Careful
	else if (Scenario.Equals(TEXT("Careful"), ESearchCase::IgnoreCase))
	{
		// 慎重鬼 2 体：調べた場所を共有し、互いに離れた場所を調べる
		Steps.Add({ 0.3f, [=] { SetStageOniTypes({ EOniType::Careful, EOniType::Careful }); } });
		Steps.Add({ 3.3f, [=, this]
		{
			Check(FString::Printf(TEXT("two careful onis (%d)"), GM->GetOnis().Num()), GM->GetOnis().Num() == 2 && GM->GetOnis()[0]->OniType == EOniType::Careful);
			Check(TEXT("careful breaks one wall at a time"), GM->GetOnis()[0]->bSingleTargetAttack);
			for (AOniCharacter* Oni : GM->GetOnis())
			{
				Oni->SightRadius = 0.f; // プレイヤーを追いかけず、調べる動きだけを見る
				Oni->CloseSenseRadius = 0.f;
				Oni->bDrawDebug = true;
			}
			// プレイヤーは隅で動かない（見つからないように遠くへ）
			TeleportPlayer(Grid()->CellFloorCenter(FIntPoint(1, 1)));
		} });
		TSharedRef<int32> MaxTargetGapViolations = MakeShared<int32>(0);
		for (int32 i = 0; i < 16; ++i)
		{
			Steps.Add({ 0.5f, [=, this]
			{
				// 2 体が同じ場所（3 マス以内）に向かっていないか
				const AOniCharacter* A = GM->GetOnis()[0];
				const AOniCharacter* B = GM->GetOnis()[1];
				const FIntPoint CA = Grid()->WorldToCell(A->GetActorLocation());
				const FIntPoint CB = Grid()->WorldToCell(B->GetActorLocation());
				if (FMath::Max(FMath::Abs(CA.X - CB.X), FMath::Abs(CA.Y - CB.Y)) <= 1)
				{
					++*MaxTargetGapViolations;
				}
			} });
		}
		Steps.Add({ 0.1f, [=, this]
		{
			const UKakurenboOniBlackboard* BB = GetWorld()->GetSubsystem<UKakurenboOniBlackboard>();
			const int32 Checked = BB ? BB->GetCheckedCount(GetWorld()->GetTimeSeconds(), 1000.f) : 0;
			Check(FString::Printf(TEXT("careful onis share the cells they checked (%d cells)"), Checked), Checked >= 30);
			Check(FString::Printf(TEXT("careful onis search separate places (times they were side by side: %d / 16)"), *MaxTargetGapViolations), *MaxTargetGapViolations <= 3);
			Shot(TEXT("careful_01"));
		} });
	}
	// ================================================================ Camera
	else if (Scenario.Equals(TEXT("Camera"), ESearchCase::IgnoreCase))
	{
		// ---- 1. かくれんぼ（三人称）：マウスでカメラが回るか ----
		Steps.Add({ 1.f, [=, this]
		{
			AHiderCharacter* Hider = GetHider();
			Check(TEXT("hide phase uses third-person camera"), Hider->GetViewMode() == EHiderViewMode::ThirdPerson && Hider->Camera->IsActive());
			Check(TEXT("cursor hidden in hide phase"), !bShowMouseCursor);
			const FRotator RotBefore = GetControlRotation();
			SimulateMouse(150.f, 60.f); // 右へ 150、上へ 60 カウント
			NextTick([=, this]
			{
				const FRotator Now = GetControlRotation();
				const float DYaw = FRotator::NormalizeAxis(Now.Yaw - RotBefore.Yaw);
				const float DPitch = FRotator::NormalizeAxis(Now.Pitch - RotBefore.Pitch);
				Check(FString::Printf(TEXT("mouse right turns camera right (dYaw=%.1f, expected %.1f)"), DYaw, 150.f * MouseSensitivity),
					FMath::IsNearlyEqual(DYaw, 150.f * MouseSensitivity, 0.5f));
				Check(FString::Printf(TEXT("mouse up raises camera view (dPitch=%.1f, expected %.1f)"), DPitch, 60.f * MouseSensitivity),
					FMath::IsNearlyEqual(DPitch, 60.f * MouseSensitivity, 0.5f));
			});
		} });
		Steps.Add({ 0.4f, [=, this]
		{
			// カメラはプレイヤーの後ろ上方にある
			AHiderCharacter* Hider = GetHider();
			const FVector CamLoc = Hider->Camera->GetComponentLocation();
			const FVector ToPlayer = Hider->GetActorLocation() - CamLoc;
			Check(FString::Printf(TEXT("camera is behind and above the player (dist=%.0f, height=%.0f)"), ToPlayer.Size(), -ToPlayer.Z),
				ToPlayer.Size() > 200.f && -ToPlayer.Z > 50.f);
			Shot(TEXT("camera_01_hide_thirdperson"));
		} });

		// ---- 2. かくれんぼ中も移動・ジャンプ・連打できる ----
		TSharedRef<FVector> PosBefore = MakeShared<FVector>();
		Steps.Add({ 0.2f, [=, this]
		{
			*PosBefore = GetPawn()->GetActorLocation();
			SimulateKey(EKeys::W, IE_Pressed);
		} });
		Steps.Add({ 0.6f, [=, this]
		{
			SimulateKey(EKeys::W, IE_Released);
			const FVector Forward = FRotationMatrix(FRotator(0.f, GetControlRotation().Yaw, 0.f)).GetUnitAxis(EAxis::X);
			const float Moved = FVector::DotProduct(GetPawn()->GetActorLocation() - *PosBefore, Forward);
			Check(FString::Printf(TEXT("W moves toward the camera's forward in hide phase (%.0f cm)"), Moved), Moved > 100.f);
		} });
		Steps.Add({ 0.3f, [=, this]
		{
			SimulateKey(EKeys::SpaceBar, IE_Pressed);
			SimulateKey(EKeys::SpaceBar, IE_Released);
			const int32 MashBefore = GS()->MashCountThisRound;
			SimulateKey(EKeys::LeftMouseButton, IE_Pressed);
			SimulateKey(EKeys::LeftMouseButton, IE_Released);
			NextTick([=, this]
			{
				const float VelZ = GetHider()->GetCharacterMovement()->Velocity.Z;
				Check(FString::Printf(TEXT("Space jumps in hide phase (vz=%.0f)"), VelZ), VelZ > 100.f);
				Check(FString::Printf(TEXT("left click mashes (count %d -> %d)"), MashBefore, GS()->MashCountThisRound), GS()->MashCountThisRound == MashBefore + 1);
			});
		} });

		// ---- 2b. 舞台の端でも、三人称カメラは外周の壁の外へ出ない ----
		Steps.Add({ 0.6f, [=, this]
		{
			UKakurenboGridSubsystem* G = Grid();
			TeleportPlayer(G->CellFloorCenter(FIntPoint(0, G->GetSizeY() / 2))); // 西の端
			SetControlRotation(FRotator(-20.f, 0.f, 0.f)); // 東を向く ＝ カメラは西（外周の外側）へ行こうとする
		} });
		Steps.Add({ 0.6f, [=, this]
		{
			UKakurenboGridSubsystem* G = Grid();
			const float ArenaMinX = G->CellFloorCenter(FIntPoint(0, 0)).X - G->GetCellSize() * 0.5f;
			const FVector CamLoc = GetHider()->Camera->GetComponentLocation();
			Check(FString::Printf(TEXT("third-person camera stays inside the arena at the edge (camX=%.0f, arena minX=%.0f)"), CamLoc.X, ArenaMinX), CamLoc.X > ArenaMinX);
			Shot(TEXT("camera_01b_edge"));
		} });

		// ---- 3. リザルト（俯瞰） ----
		Steps.Add({ 1.f, [=, this] { KakuSkipTime(1000.f); } });
		Steps.Add({ 1.f, [=, this]
		{
			Check(TEXT("result phase uses overhead camera"), GetHider()->GetViewMode() == EHiderViewMode::Overhead);
			Shot(TEXT("camera_02_result_overhead"));
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
		} });

		// ---- 5. 設置（俯瞰）：画面上の位置でマスを指せるか、WASD が画面基準か ----
		Steps.Add({ 0.5f, [=, this] { KakuNext(); } }); // → 設置
		Steps.Add({ 0.8f, [=, this]
		{
			AHiderCharacter* Hider = GetHider();
			UKakurenboGridSubsystem* G = Grid();
			Check(TEXT("build phase uses overhead camera"), Hider->GetViewMode() == EHiderViewMode::Overhead);
			Check(TEXT("cursor visible in build phase"), bShowMouseCursor);

			// 自分の 3 マス前（画面の上方向）の床を、画面上の位置から指す
			const FVector Fwd = FRotationMatrix(FRotator(0.f, Hider->GetOverheadYaw(), 0.f)).GetUnitAxis(EAxis::X);
			const FIntPoint Target = G->WorldToCell(Hider->GetActorLocation() + Fwd * 300.f);
			FVector2D ScreenPos;
			const bool bProjected = ProjectWorldLocationToScreen(G->CellFloorCenter(Target), ScreenPos, true);
			UpdateBuildTargetAt(ScreenPos);
			Check(FString::Printf(TEXT("screen point on floor -> cell (%d,%d), expected (%d,%d)"), BuildTargetCell.X, BuildTargetCell.Y, Target.X, Target.Y),
				bProjected && bHasBuildTarget && BuildTargetCell == Target);
			GM->PlaceWall(BuildTargetCell, 0);

			// 置いたブロックの上面を指すと、同じマスに積める
			ProjectWorldLocationToScreen(G->CellToWorld(Target, 0) + FVector(0.f, 0.f, G->GetBlockHeight() * 0.5f), ScreenPos, true);
			UpdateBuildTargetAt(ScreenPos);
			Check(FString::Printf(TEXT("screen point on block top -> same cell (%d,%d)"), BuildTargetCell.X, BuildTargetCell.Y),
				bHasBuildTarget && BuildTargetCell == Target && PickUpTarget != nullptr);
			GM->PlaceWall(BuildTargetCell, 0);
			Check(FString::Printf(TEXT("stacked height = %d"), G->GetColumnHeight(Target)), G->GetColumnHeight(Target) == 2);

			// ブロック 1 段はプレイヤーと同じ高さ
			const float BlockH = G->GetBlockHeight();
			const float PlayerH = Hider->GetSimpleCollisionHalfHeight() * 2.f;
			Check(FString::Printf(TEXT("one block is as tall as the player (%.0f / %.0f)"), BlockH, PlayerH), FMath::IsNearlyEqual(BlockH, PlayerH, 1.f));

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
			Check(FString::Printf(TEXT("D moves toward screen right in build phase (%.0f cm)"), Moved), Moved > 100.f);
			Shot(TEXT("camera_03_build_overhead"));
		} });

		// ---- 6. かくれんぼへ戻ると三人称。俯瞰カメラが向いていた方向を向いて始まる ----
		Steps.Add({ 0.5f, [=, this] { KakuNext(); } });
		Steps.Add({ 0.3f, [=, this]
		{
			AHiderCharacter* Hider = GetHider();
			Check(TEXT("back to third-person"), Hider->GetViewMode() == EHiderViewMode::ThirdPerson);
			const float Diff = FMath::Abs(FRotator::NormalizeAxis(GetControlRotation().Yaw - Hider->GetOverheadYaw()));
			Check(FString::Printf(TEXT("third-person starts facing the overhead direction (diff=%.1f)"), Diff), Diff < 1.f);
			Check(TEXT("cursor hidden again"), !bShowMouseCursor);
			Shot(TEXT("camera_04_hide_again"));
		} });
	}
	// ================================================================ Senses
	else if (Scenario.Equals(TEXT("Senses"), ESearchCase::IgnoreCase))
	{
		// 鬼 A だけを使う（鬼 B は止めて隅へ）
		auto OniA = [GM]() -> AOniCharacter* { return GM->GetOnis().Num() > 0 ? GM->GetOnis()[0].Get() : nullptr; };

		Steps.Add({ 3.5f, [=, this]
		{
			Check(FString::Printf(TEXT("onis spawned as the stage table says (%d / %d)"), GM->GetOnis().Num(), GM->GetNumOnis()), GM->GetOnis().Num() == GM->GetNumOnis() && GM->GetNumOnis() >= 2);
			Check(FString::Printf(TEXT("oni chase speed is about twice the player's (%.0f / %.0f)"), OniA()->ChaseSpeed, GetHider()->GetCharacterMovement()->MaxWalkSpeed),
				OniA()->ChaseSpeed >= GetHider()->GetCharacterMovement()->MaxWalkSpeed * 1.9f);
			AOniCharacter* A = OniA();
			if (GM->GetOnis().Num() > 1)
			{
				// 鬼 B は止めて、鬼 A からもプレイヤーからも遠い隅へ
				AOniCharacter* B = GM->GetOnis()[1];
				B->Deactivate();
				const FIntPoint Far = Grid()->FindSpreadFreeCells({ A->GetActorLocation(), GetPawn()->GetActorLocation() }, 1)[0];
				B->SetActorLocation(Grid()->CellFloorCenter(Far) + FVector(0, 0, 100));
			}
			EnableOniDebug();
			// 鬼 A の正面 5m に立つ
			TeleportPlayer(A->GetActorLocation() + A->GetActorForwardVector() * 500.f);
			Log(TEXT("player in front of oni A"));
		} });
		Steps.Add({ 0.4f, [=, this]
		{
			AOniCharacter* A = OniA();
			Check(FString::Printf(TEXT("seen -> oni chases (intent=%s)"), *UEnum::GetValueAsString(A->GetIntent())), A->GetIntent() == EOniState::Chase);
			Check(TEXT("being seen does not end the round"), GS()->Phase == EKakurenboPhase::Hide);
			LookAtOni();
			ShotLater(TEXT("senses_01_chase"));
			// 鬼 A から一番遠いマスへ逃げる（視界の外）
			TeleportPlayer(Grid()->CellFloorCenter(Grid()->FindSpreadFreeCells({ A->GetActorLocation() }, 1)[0]));
		} });
		Steps.Add({ 2.6f, [=, this]
		{
			AOniCharacter* A = OniA();
			Check(FString::Printf(TEXT("lost sight for 2s -> back to wander (intent=%s)"), *UEnum::GetValueAsString(A->GetIntent())), A->GetIntent() == EOniState::Wander);
			Check(TEXT("still hiding"), GS()->Phase == EKakurenboPhase::Hide);
			Log(TEXT("after losing sight"));

			// 追跡の時間切れ：見えたまま追わせて、0.6 秒で諦めるか（鬼は速いので、追いつかれない距離から始める）
			// うろうろ中は向きが変わるので、ここからは視界を全方向にし、舞台の中心へ向かう方向に立つ（舞台の外に出ないように）
			A->ChaseMaxDuration = 0.6f;
			A->SightHalfAngle = 180.f;
			const FVector ToCenter = (Grid()->CellFloorCenter(FIntPoint(Grid()->GetSizeX() / 2, Grid()->GetSizeY() / 2)) - A->GetActorLocation()).GetSafeNormal2D();
			TeleportPlayer(A->GetActorLocation() + ToCenter * 850.f);
		} });
		Steps.Add({ 0.3f, [=, this]
		{
			Check(FString::Printf(TEXT("seen again -> chases (intent=%s)"), *UEnum::GetValueAsString(OniA()->GetIntent())), OniA()->GetIntent() == EOniState::Chase);
		} });
		Steps.Add({ 0.5f, [=, this]
		{
			AOniCharacter* A = OniA();
			Check(FString::Printf(TEXT("chase time limit -> back to wander (intent=%s)"), *UEnum::GetValueAsString(A->GetIntent())), A->GetIntent() == EOniState::Wander);
			Check(TEXT("still hiding after the chase"), GS()->Phase == EKakurenboPhase::Hide);
		} });
		Steps.Add({ 0.3f, [=, this]
		{
			// 時間切れで諦めた直後は、見えていてもすぐには追いかけ直さない
			AOniCharacter* A = OniA();
			Check(FString::Printf(TEXT("after the time limit, does not re-chase right away (intent=%s, in sight=%d)"), *UEnum::GetValueAsString(A->GetIntent()), A->IsTargetInSight() ? 1 : 0),
				A->GetIntent() == EOniState::Wander);
			// 遠くへ逃げてから、音の調査の時間切れを確かめる
			TeleportPlayer(Grid()->CellFloorCenter(Grid()->FindSpreadFreeCells({ A->GetActorLocation() }, 1)[0]));
			A->InvestigateMaxDuration = 1.5f;
			A->HearingRadius = 100000.f;
			A->NoiseInaccuracyCells = 0.f;
		} });
		Steps.Add({ 0.2f, [=, this]
		{
			KakuMash(1);
			NextTick([=]
			{
				Check(FString::Printf(TEXT("heard the mash -> investigates (intent=%s)"), *UEnum::GetValueAsString(OniA()->GetIntent())), OniA()->GetIntent() == EOniState::Investigate);
			});
		} });
		Steps.Add({ 2.0f, [=, this]
		{
			Check(FString::Printf(TEXT("investigate time limit -> back to wander (intent=%s)"), *UEnum::GetValueAsString(OniA()->GetIntent())), OniA()->GetIntent() == EOniState::Wander);
			Check(TEXT("still hiding at the end"), GS()->Phase == EKakurenboPhase::Hide);
			Log(TEXT("final"));
		} });
	}
	// ================================================================ Touch
	else if (Scenario.Equals(TEXT("Touch"), ESearchCase::IgnoreCase))
	{
		// 目の見えない鬼を音だけで呼び寄せ、ぶつかった時点で見つかることを確認する
		Steps.Add({ 3.5f, [=]
		{
			for (AOniCharacter* Oni : GM->GetOnis())
			{
				Oni->SightRadius = 0.f;          // 視線では追いかけない
				Oni->CloseSenseRadius = 0.f;
				Oni->NoiseInaccuracyCells = 0.f; // 音のした場所を正確に聞き取る
				Oni->HearingRadius = 100000.f;   // どこにいても聞こえる
				Oni->InvestigateMaxDuration = 100.f;
				Oni->bDrawDebug = true;
			}
			Log(TEXT("blind onis"));
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
			FName Reason;
			for (const AOniCharacter* Oni : GM->GetOnis())
			{
				if (Oni && !Oni->GetFoundReason().IsNone())
				{
					Reason = Oni->GetFoundReason();
				}
			}
			Check(FString::Printf(TEXT("oni finds the hider by touching (phase=%s, reason=%s)"), *UEnum::GetValueAsString(GS()->Phase), *Reason.ToString()),
				GS()->Phase == EKakurenboPhase::Result && !GS()->bLastRoundCleared && Reason == FName(TEXT("touch")));
			Shot(TEXT("touch_01_found"));
		} });
	}
	// ================================================================ Treasure
	else if (Scenario.Equals(TEXT("Treasure"), ESearchCase::IgnoreCase))
	{
		TSharedRef<TArray<FIntPoint>> FirstRoundCells = MakeShared<TArray<FIntPoint>>();
		Steps.Add({ 0.5f, [=, this]
		{
			const TArray<TObjectPtr<ATreasureActor>>& Treasures = GM->GetTreasures();
			Check(FString::Printf(TEXT("treasures spawned at hide start (%d)"), Treasures.Num()), Treasures.Num() == GM->GetNumTreasures());
			for (const ATreasureActor* T : Treasures)
			{
				const FIntPoint Cell = Grid()->WorldToCell(T->GetActorLocation());
				FirstRoundCells->Add(Cell);
				const float DistCells = FVector::Dist2D(T->GetActorLocation(), GetPawn()->GetActorLocation()) / Grid()->GetCellSize();
				Check(FString::Printf(TEXT("treasure at free cell (%d,%d), %.1f cells from player"), Cell.X, Cell.Y, DistCells),
					Grid()->GetColumnHeight(Cell) == 0 && DistCells >= GM->TreasureMinDistanceCells - 0.01f);
			}
			ShotLater(TEXT("treasure_01_spawned"));
		} });
		// お宝に 1 個ずつ触れていく。「触れる → 確認 → 次へ」の順番を守るため、確認が終わってから次へ進む
		TSharedRef<TFunction<void(int32)>> CollectNext = MakeShared<TFunction<void(int32)>>();
		*CollectNext = [=, this](int32 Remaining)
		{
			if (Remaining <= 0 || GM->GetTreasures().Num() == 0)
			{
				return;
			}
			const ATreasureActor* T = GM->GetTreasures()[0];
			const double Value = T->Value;
			const double CoinsBefore = GS()->Coins;
			const int32 CountBefore = GS()->TreasuresCollectedThisRound;
			TeleportPlayer(T->GetActorLocation());
			FTimerHandle Handle;
			GetWorldTimerManager().SetTimer(Handle, FTimerDelegate::CreateLambda([=, this]
			{
				const double Gained = GS()->Coins - CoinsBefore;
				Check(FString::Printf(TEXT("touching a treasure collects it (count %d -> %d, +%.1f coins, value %.1f)"), CountBefore, GS()->TreasuresCollectedThisRound, Gained, Value),
					GS()->TreasuresCollectedThisRound == CountBefore + 1 && Gained >= Value - 0.01);
				(*CollectNext)(Remaining - 1);
			}), 0.25f, false);
		};
		Steps.Add({ 0.4f, [=] { (*CollectNext)(3); } });
		Steps.Add({ 1.5f, [=, this]
		{
			Check(FString::Printf(TEXT("all treasures collected (%d/%d)"), GS()->TreasuresCollectedThisRound, GS()->TreasuresThisRound),
				GS()->TreasuresCollectedThisRound == GS()->TreasuresThisRound && GM->GetTreasures().Num() == 0);
			Log(TEXT("collected"));
			KakuSkipTime(1000.f);
		} });
		Steps.Add({ 1.f, [=, this]
		{
			Check(TEXT("round ended"), GS()->Phase == EKakurenboPhase::Result);
			Shot(TEXT("treasure_02_result"));
			KakuNext(); // → 購入
		} });
		Steps.Add({ 0.5f, [=, this] { KakuNext(); } }); // → 設置
		Steps.Add({ 0.5f, [=, this] { KakuNext(); } }); // → かくれんぼ（ステージ 2）
		Steps.Add({ 0.5f, [=, this]
		{
			TArray<FIntPoint> Cells;
			for (const ATreasureActor* T : GM->GetTreasures())
			{
				Cells.Add(Grid()->WorldToCell(T->GetActorLocation()));
			}
			int32 SameCount = 0;
			for (const FIntPoint& C : Cells)
			{
				SameCount += FirstRoundCells->Contains(C) ? 1 : 0;
			}
			Check(FString::Printf(TEXT("new treasures appear at new random places (%d spawned, %d same as last round)"), Cells.Num(), SameCount),
				Cells.Num() == GM->GetNumTreasures() && SameCount < Cells.Num());
			Check(FString::Printf(TEXT("treasure value grows with stage (%.1f)"), GM->GetTreasureValue()), GM->GetTreasureValue() > GM->GetStageSettingsFor(1).ClearReward * GM->TreasureRewardRatio);
		} });
	}
	// ================================================================ Build
	else if (Scenario.Equals(TEXT("Build"), ESearchCase::IgnoreCase))
	{
		// 壁で囲む → 鬼に壊させる → 次の設置パートで在庫から自動修復されるか
		TSharedRef<int32> Destroyed = MakeShared<int32>(0);
		TSharedRef<int32> StockBeforeRepair = MakeShared<int32>(0);
		Steps.Add({ 1.f, [=, this] { KakuSkipTime(1000.f); } });
		Steps.Add({ 1.f, [=, this]
		{
			KakuNext(); // → 購入
			KakuAddCoins(3000.0);
			for (int32 i = 0; i < 16; ++i) { KakuBuy(3); } // 木の壁（8 個で囲み、8 個は修復用）
			Log(TEXT("shop bought walls"));
		} });
		Steps.Add({ 1.f, [=, this]
		{
			KakuNext(); // → 設置
			UKakurenboGridSubsystem* G = Grid();
			const FIntPoint Me = G->WorldToCell(GetPawn()->GetActorLocation());
			TeleportPlayer(G->CellFloorCenter(Me)); // マスの中心に立つ（はみ出すと隣に置けない）
			for (int32 DY = -1; DY <= 1; ++DY)
			{
				for (int32 DX = -1; DX <= 1; ++DX)
				{
					if (DX != 0 || DY != 0)
					{
						KakuPlaceWall(Me.X + DX, Me.Y + DY, 0);
					}
				}
			}
			Check(FString::Printf(TEXT("ring of 8 walls placed (blocks=%d)"), G->GetBlockCount()), G->GetBlockCount() == 8 && G->GetTotalMissing() == 0);
			GetHider()->SetOverheadYaw(30.f);
		} });
		Steps.Add({ 0.6f, [=] { Shot(TEXT("build_01_placed")); } });
		Steps.Add({ 0.5f, [=, this] { KakuNext(); Log(TEXT("hide start")); } }); // → かくれんぼ
		Steps.Add({ 3.5f, [=] { EnableOniDebug(); Log(TEXT("onis spawned")); } });

		TSharedRef<bool> bAttackShot = MakeShared<bool>(false);
		for (int32 i = 0; i < 140; ++i)
		{
			Steps.Add({ 0.25f, [=, this]
			{
				KakuMash(1);
				for (const AOniCharacter* Oni : GM->GetOnis())
				{
					if (Oni && Oni->GetOniState() == EOniState::Attack && !*bAttackShot)
					{
						*bAttackShot = true;
						LookAtOni();
						Log(TEXT("oni attacking"));
						ShotLater(TEXT("build_02_oni_attacking"));
					}
				}
			} });
			if (i % 20 == 19)
			{
				const FString Label = FString::Printf(TEXT("t+%.0fs"), (i + 1) * 0.25f);
				Steps.Add({ 0.f, [=] { Log(Label); } });
			}
		}
		Steps.Add({ 0.5f, [=, this]
		{
			UKakurenboGridSubsystem* G = Grid();
			*Destroyed = GS()->LastRoundWallsDestroyed;
			Log(TEXT("round over"));
			Check(TEXT("round is over"), GS()->Phase == EKakurenboPhase::Result);
			Check(FString::Printf(TEXT("onis broke walls (%d) and the layout remembers them (missing=%d)"), *Destroyed, G->GetTotalMissing()),
				*Destroyed > 0 && G->GetTotalMissing() == *Destroyed && G->GetBlockCount() == 8 - *Destroyed);
			Shot(TEXT("build_03_result"));
			KakuNext(); // → 購入
		} });
		Steps.Add({ 0.5f, [=, this]
		{
			*StockBeforeRepair = GS()->WallStock[0];
			KakuNext(); // → 設置（ここで自動修復）
		} });
		Steps.Add({ 0.5f, [=, this]
		{
			UKakurenboGridSubsystem* G = Grid();
			Log(TEXT("after auto repair"));
			Check(FString::Printf(TEXT("broken walls auto-repaired from stock (repaired=%d, destroyed=%d)"), GS()->LastRepairedWalls, *Destroyed),
				GS()->LastRepairedWalls == *Destroyed && G->GetBlockCount() == 8 && G->GetTotalMissing() == 0);
			Check(FString::Printf(TEXT("repair used stock (%d -> %d)"), *StockBeforeRepair, GS()->WallStock[0]), GS()->WallStock[0] == *StockBeforeRepair - *Destroyed);
			Shot(TEXT("build_04_repaired"));
		} });
	}
	// ================================================================ SaveRun1 / SaveRun2（再起動をまたぐセーブ。Tools/RunSaveRestartTest.ps1 から使う）
	else if (Scenario.Equals(TEXT("SaveRun1"), ESearchCase::IgnoreCase))
	{
		// 1 回目の起動：遊んで、保存された状態で終了する
		Steps.Add({ 0.5f, [=, this]
		{
			Check(FString::Printf(TEXT("1st launch starts fresh (stage %d, blocks %d)"), GS()->Stage, Grid()->GetBlockCount()),
				GS()->Stage == 1 && GS()->Phase == EKakurenboPhase::Hide && Grid()->GetBlockCount() == 0);
			KakuSkipTime(1000.f);
		} });
		Steps.Add({ 1.f, [=, this]
		{
			KakuNext(); // → 購入
			KakuAddCoins(500.0);
			KakuBuy(3); KakuBuy(3); // 木の壁 ×2
		} });
		Steps.Add({ 0.5f, [=, this]
		{
			KakuNext(); // → 設置
			const FIntPoint Me = Grid()->WorldToCell(GetPawn()->GetActorLocation());
			KakuPlaceWall(Me.X + 2, Me.Y, 0);
			KakuPlaceWall(Me.X - 2, Me.Y, 0);
		} });
		Steps.Add({ 0.5f, [=, this]
		{
			KakuNext(); // → かくれんぼ（開始時に保存される）
			Log(TEXT("saved for restart"));
			Check(TEXT("saved before quitting"), UGameplayStatics::DoesSaveGameExist(GM->SaveSlotName, 0));
		} });
	}
	else if (Scenario.Equals(TEXT("SaveRun2"), ESearchCase::IgnoreCase))
	{
		// 2 回目の起動：続きから（設置パート）始まっているか
		Steps.Add({ 0.5f, [=, this]
		{
			const AKakurenboGameState* S = GS();
			Log(TEXT("2nd launch"));
			Check(FString::Printf(TEXT("2nd launch resumes in the build phase (phase=%s)"), *UEnum::GetValueAsString(S->Phase)), S->Phase == EKakurenboPhase::Build);
			Check(FString::Printf(TEXT("stage and coins carried over (stage %d, coins %.2f, expected 2 / ~580)"), S->Stage, S->Coins),
				S->Stage == 2 && FMath::Abs(S->Coins - 580.0) < 1.0);
			Check(FString::Printf(TEXT("placed walls carried over (blocks %d, wood stock %d)"), Grid()->GetBlockCount(), S->WallStock.IsValidIndex(0) ? S->WallStock[0] : -1),
				Grid()->GetBlockCount() == 2 && S->WallStock.IsValidIndex(0) && S->WallStock[0] == 0);
			Check(TEXT("resume notice is shown"), !S->NoticeText.IsEmpty() && GetWorld()->GetTimeSeconds() < S->NoticeUntilTime);
			Shot(TEXT("saverun2_resumed"));
		} });
	}
	// ================================================================ Save
	else if (Scenario.Equals(TEXT("Save"), ESearchCase::IgnoreCase))
	{
		// テスト用のスロットに保存 → メモリ上の状態を消す → 読み込み、で元に戻るか（ユーザーのセーブには触らない）
		struct FSnapshot
		{
			double Coins = 0.0;
			int32 Stage = 0;
			int32 MashLevel = 0;
			TArray<int32> Stock;
			TArray<FKakurenboSavedColumn> Layout;
			FVector PlayerLocation = FVector::ZeroVector;
		};
		TSharedRef<FSnapshot> Expected = MakeShared<FSnapshot>();

		Steps.Add({ 0.5f, [=, this]
		{
			GM->SaveSlotName = TEXT("KakuAutoTest");
			GM->bSaveEnabled = true;
			UGameplayStatics::DeleteGameInSlot(GM->SaveSlotName, 0);
			KakuSkipTime(1000.f);
		} });
		Steps.Add({ 1.f, [=, this]
		{
			KakuNext(); // → 購入
			KakuAddCoins(1000.0);
			KakuBuy(1);                         // 連打強化
			KakuBuy(3); KakuBuy(3); KakuBuy(3); // 木の壁 ×3
			KakuBuy(4);                         // 石の壁 ×1
		} });
		Steps.Add({ 0.5f, [=, this]
		{
			KakuNext(); // → 設置
			UKakurenboGridSubsystem* G = Grid();
			const FIntPoint Me = G->WorldToCell(GetPawn()->GetActorLocation());
			KakuPlaceWall(Me.X + 2, Me.Y, 0);
			KakuPlaceWall(Me.X + 2, Me.Y, 1); // 積む
			KakuPlaceWall(Me.X - 2, Me.Y + 1, 0);
		} });
		Steps.Add({ 0.5f, [=, this]
		{
			KakuNext(); // → かくれんぼ（開始時に保存される）
			const AKakurenboGameState* S = GS();
			Expected->Coins = S->Coins;
			Expected->Stage = S->Stage;
			Expected->MashLevel = S->MashIncomeLevel;
			Expected->Stock = S->WallStock;
			Grid()->ExportLayout(Expected->Layout);
			Expected->PlayerLocation = GetPawn()->GetActorLocation();
			Check(TEXT("save file exists after starting the round"), UGameplayStatics::DoesSaveGameExist(GM->SaveSlotName, 0));
			Log(TEXT("saved"));
		} });
		Steps.Add({ 0.5f, [=, this]
		{
			// 「再起動」の代わりに、メモリ上の状態を消してから読み込む
			AKakurenboGameState* S = GS();
			Grid()->ClearAllBlocks();
			S->Coins = 0.0;
			S->Stage = 1;
			S->MashIncomeLevel = 0;
			S->WallStock.Init(0, S->WallStock.Num());
			TeleportPlayer(FVector::ZeroVector);

			const bool bLoaded = GM->LoadProgress();
			TArray<FKakurenboSavedColumn> Layout;
			Grid()->ExportLayout(Layout);
			bool bSameLayout = Layout.Num() == Expected->Layout.Num();
			for (int32 i = 0; bSameLayout && i < Layout.Num(); ++i)
			{
				bSameLayout = Layout[i].Cell == Expected->Layout[i].Cell && Layout[i].Design == Expected->Layout[i].Design && Layout[i].Live == Expected->Layout[i].Live;
			}
			Check(TEXT("save loads"), bLoaded);
			Check(FString::Printf(TEXT("coins / stage / upgrade restored (%.1f, %d, Lv%d)"), S->Coins, S->Stage, S->MashIncomeLevel),
				FMath::IsNearlyEqual(S->Coins, Expected->Coins, 0.01) && S->Stage == Expected->Stage && S->MashIncomeLevel == Expected->MashLevel);
			Check(FString::Printf(TEXT("wall stock restored (%s)"), *FString::JoinBy(S->WallStock, TEXT(","), [](int32 N) { return FString::FromInt(N); })), S->WallStock == Expected->Stock);
			Check(FString::Printf(TEXT("wall layout restored (%d columns, %d blocks)"), Layout.Num(), Grid()->GetBlockCount()), bSameLayout && Grid()->GetBlockCount() == 3);
			Check(FString::Printf(TEXT("player position restored (%.0f cm off)"), FVector::Dist(GetPawn()->GetActorLocation(), Expected->PlayerLocation)),
				FVector::Dist(GetPawn()->GetActorLocation(), Expected->PlayerLocation) < 5.f);
			Log(TEXT("loaded"));
		} });
		Steps.Add({ 0.5f, [=, this]
		{
			// 最初からやり直す
			GM->ResetProgress();
			const AKakurenboGameState* S = GS();
			Check(FString::Printf(TEXT("reset -> stage 1, no coins, no walls (stage %d, coins %.1f, blocks %d)"), S->Stage, S->Coins, Grid()->GetBlockCount()),
				S->Stage == 1 && S->Coins < 0.01 && Grid()->GetBlockCount() == 0 && S->Phase == EKakurenboPhase::Hide);
			UGameplayStatics::DeleteGameInSlot(GM->SaveSlotName, 0);
			GM->bSaveEnabled = false;
			Shot(TEXT("save_01_after_reset"));
		} });
	}
	// ================================================================ Loop
	else
	{
		// 基本ループ: 連打 → 逃げ切り → 購入 → 設置 → 次のステージ
		Steps.Add({ 1.f, [=, this]
		{
			Log(TEXT("start"));
			Check(FString::Printf(TEXT("treasures spawned (%d)"), GM->GetTreasures().Num()), GM->GetTreasures().Num() == GM->GetNumTreasures());
			Shot(TEXT("loop_01_hide_countdown"));
		} });
		Steps.Add({ 1.f, [=, this] { KakuMash(30); Log(TEXT("after mash 30")); Shot(TEXT("loop_02_hide_mashing")); } });
		Steps.Add({ 1.f, [=, this] { KakuSkipTime(1000.f); } });
		Steps.Add({ 1.f, [=, this]
		{
			Log(TEXT("result"));
			Check(TEXT("escaped -> stage 2"), GS()->Phase == EKakurenboPhase::Result && GS()->bLastRoundCleared && GS()->Stage == 2);
			Shot(TEXT("loop_03_result"));
		} });
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
