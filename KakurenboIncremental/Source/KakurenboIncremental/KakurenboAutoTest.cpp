// 自動テスト（KakuAutoTest コマンド）の実装。
// 実際にゲームを動かしながら操作を時間差で実行し、ログとスクリーンショットで結果を確認する。
// Tools/RunAutoTest.ps1 -Scenario <名前> から起動する。
//
// 結果の判定は "[AutoTest] CHECK OK / CHECK FAILED" としてログに出す。
// 疑似入力（SimulateKey / SimulateMouse）の結果は、次のフレームの入力処理で反映されるので NextTick で確かめる。

#include "Camera/CameraComponent.h"
#include "Animation/AnimSequence.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerInput.h"
#include "HiderCharacter.h"
#include "InputKeyEventArgs.h"
#include "KakurenboFx.h"
#include "KakurenboGameMode.h"
#include "KakurenboGameState.h"
#include "KakurenboArena.h"
#include "KakurenboGridSubsystem.h"
#include "KakurenboHUD.h"
#include "KakurenboLibrary.h"
#include "KakurenboOniBlackboard.h"
#include "KakurenboPlayerController.h"
#include "KakurenboSaveGame.h"
#include "KakurenboSoundSubsystem.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "OniCharacter.h"
#include "PlaceableBlock.h"
#include "TimerManager.h"
#include "TrapActor.h"
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

	// 館の家具があると、テストで壁を置くマスや鬼の通り道が塞がることがあるので、家具の無い舞台で行う。
	// マップそのものを確かめるシナリオ（と再起動をまたぐセーブ）だけは本物のマップを使う
	const bool bUsesMaps = Scenario.Equals(TEXT("Maps"), ESearchCase::IgnoreCase) || Scenario.Equals(TEXT("Mood"), ESearchCase::IgnoreCase)
		|| Scenario.Equals(TEXT("SaveRun1"), ESearchCase::IgnoreCase) || Scenario.Equals(TEXT("SaveRun2"), ESearchCase::IgnoreCase);
	if (!bUsesMaps)
	{
		GM->bMapOverride = true;
		GM->MapOverride = NAME_None;
		GM->SwitchToMap(NAME_None);
	}

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
	// HUD のボタンをクリックする（本物と同じ入力の経路。カーソル位置はテスト用で、本物のマウスは動かさない）。
	// ボタンは HUD が描いた次のフレームから押せる。結果は NextTick で確かめる
	auto ClickUI = [this](EKakurenboUIAction Action, int32 Index) -> bool
	{
		FVector2D Center;
		const AKakurenboHUD* HUD = GetHUD<AKakurenboHUD>();
		if (!HUD || !HUD->GetButtonCenter(Action, Index, Center) || !PlayerInput)
		{
			return false;
		}
		bUseTestCursor = true;
		TestCursorPosition = Center;
		PlayerInput->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::LeftMouseButton, IE_Pressed, 1.f));
		PlayerInput->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::LeftMouseButton, IE_Released, 1.f));
		return true;
	};
	// 画面上の点をクリックする（設置パートで床を指して置く）
	auto ClickAt = [this](const FVector2D& ScreenPx)
	{
		bUseTestCursor = true;
		TestCursorPosition = ScreenPx;
		PlayerInput->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::LeftMouseButton, IE_Pressed, 1.f));
		PlayerInput->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::LeftMouseButton, IE_Released, 1.f));
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
	// テスト用：出てくる鬼の種類を変える（鬼が出てくる前に呼ぶ。どのステージでも同じにする）
	auto SetStageOniTypes = [GM](const TArray<EOniType>& Types)
	{
		if (GM->StageRows.Num() == 0)
		{
			GM->StageRows.AddDefaulted();
		}
		for (FKakurenboStageRow& Row : GM->StageRows)
		{
			Row.OniTypes = Types;
		}
	};
	// 商品を番号キーと同じ経路（KakuBuy）で買う。商品の番号は並びが変わってもよいように GameMode に聞く
	auto BuyWall = [this, GM](int32 WallType, int32 Count)
	{
		for (int32 i = 0; i < Count; ++i)
		{
			KakuBuy(GM->GetShopIndexOfWall(WallType) + 1);
		}
	};
	auto BuyTrap = [this, GM](int32 TrapType, int32 Count)
	{
		for (int32 i = 0; i < Count; ++i)
		{
			KakuBuy(GM->GetShopIndexOfTrap(TrapType) + 1);
		}
	};
	auto Sound = [this]() { return GetWorld()->GetSubsystem<UKakurenboSoundSubsystem>(); };
	auto FxSys = [this]() { return GetWorld()->GetSubsystem<UKakurenboFxSubsystem>(); };
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
			// 鬼は門（舞台の端）から出てくるので、真上に浮かぶプレイヤーが外周の壁に重ならないよう、少し内側へ移す
			Oni->SetActorLocation(Grid()->CellFloorCenter(FIntPoint(Grid()->GetSizeX() / 2 - 4, Grid()->GetSizeY() / 2)) + FVector(0.f, 0.f, 100.f), false, nullptr, ETeleportType::TeleportPhysics);
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
	// ================================================================ Perch（家具・壁の上に乗ったプレイヤーへ鬼が飛び乗る）
	else if (Scenario.Equals(TEXT("Perch"), ESearchCase::IgnoreCase))
	{
		// 1 回目: 3×3 マス・高さ 120cm の家具（壊せない）の真ん中に立つ → 縁からは届かないので、飛び乗って捕まえる
		// 2 回目: 木の壁 1 段の上に立つ → 壁を壊さずに飛び乗って捕まえる
		const FIntPoint Furniture(6, 12);
		const FIntPoint Pillar(8, 5);
		auto ReadyOni = [=, this](const FIntPoint& OniCell) -> AOniCharacter*
		{
			AOniCharacter* Oni = KeepOnlyOni(0);
			if (Oni)
			{
				Oni->SightRadius = 3000.f;
				Oni->SightHalfAngle = 180.f;
				Oni->SetActorLocation(Grid()->CellFloorCenter(OniCell) + FVector(0.f, 0.f, 100.f), false, nullptr, ETeleportType::TeleportPhysics);
			}
			return Oni;
		};
		auto TheOni = [GM]() -> AOniCharacter* { return GM->GetOnis().Num() > 0 ? GM->GetOnis()[0].Get() : nullptr; };

		Steps.Add({ 0.3f, [=, this]
		{
			SetStageOniTypes({ EOniType::Balanced });
			// 家具の代わり：当たり判定のある箱を置き、そのマスを通れない障害物にする（館の家具と同じ扱い）
			UKakurenboGridSubsystem* G = Grid();
			TArray<FIntPoint> Cells;
			for (int32 DY = -1; DY <= 1; ++DY)
			{
				for (int32 DX = -1; DX <= 1; ++DX)
				{
					Cells.Add(Furniture + FIntPoint(DX, DY));
				}
			}
			G->SetObstacles(Cells);
			const FVector Center = G->CellFloorCenter(Furniture);
			AStaticMeshActor* Box = GetWorld()->SpawnActor<AStaticMeshActor>(Center + FVector(0.f, 0.f, 60.f), FRotator::ZeroRotator);
			Box->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
			Box->GetStaticMeshComponent()->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
			Box->GetStaticMeshComponent()->SetCollisionProfileName(TEXT("BlockAll"));
			Box->SetActorScale3D(FVector(G->GetCellSize() * 3.f / 100.f, G->GetCellSize() * 3.f / 100.f, 1.2f));
			// 家具の真ん中の上に立つ
			GetPawn()->SetActorLocation(Center + FVector(0.f, 0.f, 120.f + GetPawn()->GetSimpleCollisionHalfHeight() + 2.f), false, nullptr, ETeleportType::TeleportPhysics);
		} });
		Steps.Add({ 3.2f, [=, this]
		{
			AOniCharacter* Oni = ReadyOni(Furniture + FIntPoint(5, 0));
			Check(TEXT("an oni came out"), Oni != nullptr);
			const float Feet = GetPawn()->GetActorLocation().Z - GetPawn()->GetSimpleCollisionHalfHeight() - Grid()->CellFloorCenter(Furniture).Z;
			Check(FString::Printf(TEXT("player stands on the furniture (feet %.0f cm above the floor)"), Feet), Feet > 100.f);
			LookAtOni();
		} });
		Steps.Add({ 1.2f, [=] { ShotLater(TEXT("perch_01_leap")); } });
		Steps.Add({ 4.5f, [=, this]
		{
			const AOniCharacter* Oni = TheOni();
			Check(FString::Printf(TEXT("oni leaps onto the furniture and catches the player (phase=%s, reason=%s, leaps=%d)"),
				*UEnum::GetValueAsString(GS()->Phase), *FoundReasonOfAny().ToString(), Oni ? Oni->GetLeapCount() : -1),
				GS()->Phase == EKakurenboPhase::Result && FoundReasonOfAny() == FName(TEXT("touch")) && Oni && Oni->GetLeapCount() >= 1);
			KakuNext(); // → 購入
		} });
		Steps.Add({ 0.5f, [=, this] { KakuNext(); } }); // → 設置
		Steps.Add({ 0.5f, [=, this]
		{
			PlaceTestWall(Pillar);
			KakuNext(); // → かくれんぼ
			const FVector Top = Grid()->CellFloorCenter(Pillar) + FVector(0.f, 0.f, Grid()->GetBlockHeight());
			GetPawn()->SetActorLocation(Top + FVector(0.f, 0.f, GetPawn()->GetSimpleCollisionHalfHeight() + 2.f), false, nullptr, ETeleportType::TeleportPhysics);
		} });
		Steps.Add({ 3.2f, [=, this]
		{
			AOniCharacter* Oni = ReadyOni(Pillar + FIntPoint(5, 2));
			Check(TEXT("an oni came out (2nd round)"), Oni != nullptr);
		} });
		Steps.Add({ 5.5f, [=, this]
		{
			const AOniCharacter* Oni = TheOni();
			Check(FString::Printf(TEXT("oni leaps onto the wall instead of breaking it (phase=%s, reason=%s, leaps=%d, walls broken=%d)"),
				*UEnum::GetValueAsString(GS()->Phase), *FoundReasonOfAny().ToString(), Oni ? Oni->GetLeapCount() : -1, GS()->LastRoundWallsDestroyed),
				GS()->Phase == EKakurenboPhase::Result && FoundReasonOfAny() == FName(TEXT("touch")) && Oni && Oni->GetLeapCount() >= 1 && GS()->LastRoundWallsDestroyed == 0);
			Shot(TEXT("perch_02_caught_on_wall"));
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
		// 2 体とも同じ門から出てくるので、離れるまで少し待ってから数える
		Steps.Add({ 1.5f, [=] { Log(TEXT("onis left the gate")); } });
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
			// ジャンプは最初はできない（転生のお店で解放する）
			SimulateKey(EKeys::SpaceBar, IE_Pressed);
			SimulateKey(EKeys::SpaceBar, IE_Released);
			const int32 MashBefore = GS()->MashCountThisRound;
			SimulateKey(EKeys::LeftMouseButton, IE_Pressed);
			SimulateKey(EKeys::LeftMouseButton, IE_Released);
			NextTick([=, this]
			{
				const float VelZ = GetHider()->GetCharacterMovement()->Velocity.Z;
				Check(FString::Printf(TEXT("Space does not jump before it is unlocked (vz=%.0f)"), VelZ), VelZ < 50.f && !GM->IsJumpUnlocked());
				Check(FString::Printf(TEXT("left click mashes (count %d -> %d)"), MashBefore, GS()->MashCountThisRound), GS()->MashCountThisRound == MashBefore + 1);
			});
		} });
		Steps.Add({ 0.3f, [=, this]
		{
			GS()->PrestigeLevels[static_cast<int32>(EPrestigeUpgrade::Jump)] = 1; // 転生のお店で買ったことにする
			GM->ApplyPrestigeToPlayer();
			SimulateKey(EKeys::SpaceBar, IE_Pressed);
			SimulateKey(EKeys::SpaceBar, IE_Released);
			NextTick([=, this]
			{
				const float VelZ = GetHider()->GetCharacterMovement()->Velocity.Z;
				Check(FString::Printf(TEXT("after unlocking, Space jumps in hide phase (vz=%.0f)"), VelZ), VelZ > 100.f);
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
			BuyWall(0, 3); // 木の壁
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

		// ---- 5. 設置（真上から）：プレイヤーは見えない、画面上の位置でマスを指せるか、WASD でカメラが画面基準に動くか ----
		TSharedRef<FVector> FocusBefore = MakeShared<FVector>();
		Steps.Add({ 0.5f, [=, this] { KakuNext(); } }); // → 設置
		Steps.Add({ 0.8f, [=, this]
		{
			AHiderCharacter* Hider = GetHider();
			UKakurenboGridSubsystem* G = Grid();
			Check(TEXT("build phase uses the top-down camera"), Hider->GetViewMode() == EHiderViewMode::TopDown);
			Check(FString::Printf(TEXT("build camera looks straight down (pitch %.1f)"), Hider->Camera->GetComponentRotation().Pitch), Hider->Camera->GetComponentRotation().Pitch < -80.f);
			Check(TEXT("player is hidden in build phase"), Hider->IsHidden());
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

			// D キーでカメラが画面の右方向へ動く（プレイヤーは動かない）
			*FocusBefore = Hider->GetTopDownFocus();
			*PosBefore = Hider->GetActorLocation();
			SimulateKey(EKeys::D, IE_Pressed);
		} });
		Steps.Add({ 0.6f, [=, this]
		{
			SimulateKey(EKeys::D, IE_Released);
			const FVector RightDir = FRotationMatrix(FRotator(0.f, GetHider()->GetOverheadYaw(), 0.f)).GetUnitAxis(EAxis::Y);
			const float Moved = FVector::DotProduct(GetHider()->GetTopDownFocus() - *FocusBefore, RightDir);
			Check(FString::Printf(TEXT("D moves the camera toward screen right in build phase (%.0f cm)"), Moved), Moved > 100.f);
			Check(TEXT("the player does not move with WASD in build phase"), FVector::Dist2D(GetPawn()->GetActorLocation(), *PosBefore) < 1.f);
		} });
		Steps.Add({ 0.8f, [=, this]
		{
			// T キー：カーソルの指すマスへスタート位置を動かす（テスト用のカーソル位置を使う。本物のマウスは動かさない）。
			// カメラは少し遅れて付いてくるので、止まってから画面上の位置を求める
			UKakurenboGridSubsystem* G = Grid();
			// 前の手順で積んだ壁（画面の上方向 3 マス先）が視線を遮らないよう、反対側（画面の下方向）のマスを選ぶ
			const FVector Back = -FRotationMatrix(FRotator(0.f, GetHider()->GetOverheadYaw(), 0.f)).GetUnitAxis(EAxis::X);
			const FIntPoint Raw = G->WorldToCell(GetPawn()->GetActorLocation() + Back * 300.f);
			const FIntPoint NewStart(FMath::Clamp(Raw.X, 1, G->GetSizeX() - 4), FMath::Clamp(Raw.Y, 1, G->GetSizeY() - 2));
			UE_LOG(LogTemp, Display, TEXT("[AutoTest] T target (%d,%d), player at (%d,%d), height %d"), NewStart.X, NewStart.Y,
				G->WorldToCell(GetPawn()->GetActorLocation()).X, G->WorldToCell(GetPawn()->GetActorLocation()).Y, G->GetColumnHeight(NewStart));
			FVector2D ScreenPos;
			ProjectWorldLocationToScreen(G->CellFloorCenter(NewStart), ScreenPos, true);
			bUseTestCursor = true;
			TestCursorPosition = ScreenPos;
			SimulateKey(EKeys::T, IE_Pressed);
			SimulateKey(EKeys::T, IE_Released);
			NextTick([=, this]
			{
				const FIntPoint Now = Grid()->WorldToCell(GetPawn()->GetActorLocation());
				Check(FString::Printf(TEXT("T moves the start position to the cursor cell (%d,%d), expected (%d,%d)"), Now.X, Now.Y, NewStart.X, NewStart.Y), Now == NewStart);
				Check(TEXT("cannot start on the oni gate"), !GM->MovePlayerStart(GM->GetOniGateCells()[0]));
				bUseTestCursor = false;
			});
			ShotLater(TEXT("camera_03_build_topdown"));
		} });
		// Z で斜めに傾ける（積んだ壁の高さが見える）→ X で真上に戻す
		TSharedRef<float> PitchBefore = MakeShared<float>(0.f);
		Steps.Add({ 0.6f, [=, this]
		{
			*PitchBefore = GetHider()->TopDownPitch;
			SimulateKey(EKeys::Z, IE_Pressed);
		} });
		Steps.Add({ 0.6f, [=, this]
		{
			SimulateKey(EKeys::Z, IE_Released);
			Check(FString::Printf(TEXT("Z tilts the build camera (pitch %.1f -> %.1f)"), *PitchBefore, GetHider()->TopDownPitch),
				*PitchBefore < -85.f && GetHider()->TopDownPitch > -60.f);
			ShotLater(TEXT("camera_03b_build_tilted"));
		} });
		Steps.Add({ 0.6f, [=, this] { SimulateKey(EKeys::X, IE_Pressed); } });
		Steps.Add({ 1.2f, [=, this]
		{
			SimulateKey(EKeys::X, IE_Released);
			Check(FString::Printf(TEXT("X brings it back to straight down (pitch %.1f)"), GetHider()->TopDownPitch), GetHider()->TopDownPitch < -89.f);
		} });

		// ---- 6. かくれんぼへ戻ると三人称。俯瞰カメラが向いていた方向を向いて始まる ----
		Steps.Add({ 0.5f, [=, this] { KakuNext(); } });
		Steps.Add({ 0.3f, [=, this]
		{
			AHiderCharacter* Hider = GetHider();
			Check(TEXT("back to third-person"), Hider->GetViewMode() == EHiderViewMode::ThirdPerson);
			Check(TEXT("player is visible again"), !Hider->IsHidden());
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
			Check(FString::Printf(TEXT("onis spawned as the stage table says (%d / %d)"), GM->GetOnis().Num(), GM->GetNumOnis()), GM->GetOnis().Num() == GM->GetNumOnis() && GM->GetNumOnis() >= 1);
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
			BuyWall(0, 16); // 木の壁（8 個で囲み、8 個は修復用）
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
	// ================================================================ Trap
	else if (Scenario.Equals(TEXT("Trap"), ESearchCase::IgnoreCase))
	{
		// 罠を買う → 置く → おとりに鬼が呼ばれて壊す・トリモチで鬼が止まる（その間は捕まらない）→ 次の設置パートで在庫から置き直す
		struct FTrapTest
		{
			FIntPoint Me, StickyCell, DecoyCell, WallCell;
		};
		TSharedRef<FTrapTest> T = MakeShared<FTrapTest>();
		constexpr int32 Sticky = 0;
		constexpr int32 Decoy = 1;
		auto TheOni = [GM]() -> AOniCharacter* { return GM->GetOnis().Num() > 0 ? GM->GetOnis()[0].Get() : nullptr; };

		Steps.Add({ 0.5f, [=, this] { KakuSkipTime(1000.f); } });
		Steps.Add({ 1.f, [=, this]
		{
			KakuNext(); // → 購入
			KakuAddCoins(5000.0);
			const double FirstPrice = GM->GetTrapCost(Sticky);
			BuyTrap(Sticky, 2);
			BuyTrap(Decoy, 1);
			BuyWall(0, 1);
			const AKakurenboGameState* S = GS();
			Check(FString::Printf(TEXT("traps bought into stock (sticky %d, decoy %d)"), S->TrapStock[Sticky], S->TrapStock[Decoy]), S->TrapStock[Sticky] == 2 && S->TrapStock[Decoy] == 1);
			const double Expected = FirstPrice * FMath::Pow(GM->TrapTypes[Sticky].CostGrowth, 2.0);
			Check(FString::Printf(TEXT("trap price grows with the number owned (%.1f -> %.1f, expected %.1f)"), FirstPrice, GM->GetTrapCost(Sticky), Expected),
				FMath::IsNearlyEqual(GM->GetTrapCost(Sticky), Expected, 0.01));
			Log(TEXT("bought traps"));
		} });
		Steps.Add({ 0.5f, [=, this]
		{
			KakuNext(); // → 設置
			SetStageOniTypes({ EOniType::Balanced }); // 鬼は 1 体
			UKakurenboGridSubsystem* G = Grid();
			T->Me = G->WorldToCell(GetPawn()->GetActorLocation());
			TeleportPlayer(G->CellFloorCenter(T->Me));
			T->StickyCell = T->Me + FIntPoint(3, 0);
			T->DecoyCell = FIntPoint(1, 1); // 隅
			T->WallCell = T->Me + FIntPoint(0, 3);
			KakuPlaceWall(T->WallCell.X, T->WallCell.Y, 0);
			KakuPlaceTrap(T->StickyCell.X, T->StickyCell.Y, Sticky);
			KakuPlaceTrap(T->DecoyCell.X, T->DecoyCell.Y, Decoy);
			const AKakurenboGameState* S = GS();
			Check(FString::Printf(TEXT("traps placed from stock (stock %d,%d)"), S->TrapStock[Sticky], S->TrapStock[Decoy]),
				S->TrapStock[Sticky] == 1 && S->TrapStock[Decoy] == 0 && G->GetTrap(T->StickyCell) && G->GetTrap(T->DecoyCell));
			Check(TEXT("a wall cannot go on a trap"), !G->CanPlaceBlock(T->StickyCell));
			Check(TEXT("a trap cannot go on a wall or another trap"), !G->CanPlaceTrap(T->WallCell) && !G->CanPlaceTrap(T->StickyCell));
			Check(TEXT("traps do not block the onis' path"), G->BuildWalkGrid().IsFree(T->StickyCell));
			GetHider()->SetOverheadYaw(30.f);
			ShotLater(TEXT("trap_01_build"));
		} });
		Steps.Add({ 1.f, [=, this] { KakuNext(); Log(TEXT("hide start")); } }); // → かくれんぼ（3 秒後に鬼が出てくる）

		// 鬼が出てきた直後に、目を使わず音だけで動く鬼にして、おとりの近くへ移す（おとりは鬼が出てきて 0.5 秒後に最初の音を出す）
		Steps.Add({ 3.15f, [=, this]
		{
			AOniCharacter* Oni = KeepOnlyOni(0);
			Check(TEXT("an oni came out"), Oni != nullptr);
			if (!Oni)
			{
				return;
			}
			MakeBlindListener(Oni);
			Oni->SetActorLocation(Grid()->CellFloorCenter(T->DecoyCell + FIntPoint(5, 5)) + FVector(0.f, 0.f, 100.f), false, nullptr, ETeleportType::TeleportPhysics);
			Log(TEXT("oni moved near the decoy"));
		} });
		Steps.Add({ 0.7f, [=, this]
		{
			const ATrapActor* DecoyActor = Grid()->GetTrap(T->DecoyCell);
			Check(FString::Printf(TEXT("decoy makes noise (pings %d)"), DecoyActor ? DecoyActor->GetPingCount() : -1), DecoyActor && DecoyActor->GetPingCount() >= 1);
			Check(FString::Printf(TEXT("oni goes to the decoy (intent=%s)"), *UEnum::GetValueAsString(TheOni()->GetIntent())),
				TheOni()->GetIntent() == EOniState::Investigate || !Grid()->GetTrap(T->DecoyCell));
			Shot(TEXT("trap_02_decoy"));
		} });
		Steps.Add({ 3.f, [=, this]
		{
			UKakurenboGridSubsystem* G = Grid();
			Check(FString::Printf(TEXT("oni broke the decoy (live=%d, design=%d, triggered=%d)"), G->GetTrap(T->DecoyCell) ? 1 : 0, G->GetTrapDesign(T->DecoyCell), GS()->TrapsTriggeredThisRound),
				!G->GetTrap(T->DecoyCell) && G->GetTrapDesign(T->DecoyCell) == Decoy && GS()->TrapsTriggeredThisRound == 1);
			// トリモチ：鬼をトリモチの上へ
			TheOni()->SetActorLocation(G->CellFloorCenter(T->StickyCell) + FVector(0.f, 0.f, 100.f), false, nullptr, ETeleportType::TeleportPhysics);
		} });
		Steps.Add({ 0.3f, [=, this]
		{
			UKakurenboGridSubsystem* G = Grid();
			AOniCharacter* Oni = TheOni();
			Check(FString::Printf(TEXT("sticky trap stuns the oni (intent=%s)"), *UEnum::GetValueAsString(Oni->GetIntent())), Oni->GetIntent() == EOniState::Stunned);
			Check(TEXT("sticky trap is used up but stays in the layout"), !G->GetTrap(T->StickyCell) && G->GetTrapDesign(T->StickyCell) == Sticky && GS()->TrapsTriggeredThisRound == 2);
			Check(TEXT("stun stars are shown"), Oni->StunStars.Num() > 0 && !Oni->StunStars[0]->bHiddenInGame);
			// 動けない鬼に触れても捕まらない（体が重ならない「触れた」距離。普段ならこれで見つかる）
			TeleportPlayer(Oni->GetActorLocation() + FVector(75.f, 0.f, 0.f));
			LookAtOni();
			ShotLater(TEXT("trap_03_stunned"));
		} });
		Steps.Add({ 0.5f, [=, this]
		{
			Check(FString::Printf(TEXT("touching a stunned oni is safe (phase=%s)"), *UEnum::GetValueAsString(GS()->Phase)), GS()->Phase == EKakurenboPhase::Hide);
			// 抜け出したときに捕まらないよう遠くへ
			TeleportPlayer(Grid()->CellFloorCenter(Grid()->FindSpreadFreeCells({ TheOni()->GetActorLocation() }, 1)[0]));
		} });
		Steps.Add({ 3.6f, [=, this]
		{
			AOniCharacter* Oni = TheOni();
			Check(FString::Printf(TEXT("oni recovers after the stun (intent=%s)"), *UEnum::GetValueAsString(Oni->GetIntent())), Oni->GetIntent() != EOniState::Stunned);
			Check(TEXT("right after recovering, the oni cannot be stunned again"), !Oni->CanBeStunned());
		} });
		Steps.Add({ 3.2f, [=, this]
		{
			AOniCharacter* Oni = TheOni();
			Check(TEXT("a few seconds later it can be stunned again"), Oni->CanBeStunned());
			Oni->Deactivate();
			KakuSkipTime(1000.f);
		} });
		Steps.Add({ 1.f, [=, this]
		{
			Check(FString::Printf(TEXT("result counts the used traps (%d)"), GS()->TrapsTriggeredThisRound), GS()->Phase == EKakurenboPhase::Result && GS()->TrapsTriggeredThisRound == 2);
			KakuNext(); // → 購入
		} });
		Steps.Add({ 0.5f, [=, this] { KakuNext(); } }); // → 設置（ここで罠を置き直す）
		Steps.Add({ 0.5f, [=, this]
		{
			UKakurenboGridSubsystem* G = Grid();
			const AKakurenboGameState* S = GS();
			Check(FString::Printf(TEXT("used traps are refilled from stock (refilled %d, missing %d, sticky stock %d)"), S->LastRefilledTraps, S->LastUnrefilledTraps, S->TrapStock[Sticky]),
				S->LastRefilledTraps == 1 && S->LastUnrefilledTraps == 1 && S->TrapStock[Sticky] == 0 && G->GetTrap(T->StickyCell) && !G->GetTrap(T->DecoyCell));
			GetHider()->SetOverheadYaw(30.f);
			ShotLater(TEXT("trap_04_refilled"));
		} });
		Steps.Add({ 0.6f, [=, this]
		{
			// 回収：残っている罠は在庫に戻る。消えた罠の設計図は消えるだけ
			UKakurenboGridSubsystem* G = Grid();
			const bool bPicked1 = GM->PickUpTrap(T->StickyCell);
			const bool bPicked2 = GM->PickUpTrap(T->DecoyCell);
			const AKakurenboGameState* S = GS();
			Check(FString::Printf(TEXT("pick up traps (stock %d,%d)"), S->TrapStock[Sticky], S->TrapStock[Decoy]),
				bPicked1 && bPicked2 && S->TrapStock[Sticky] == 1 && S->TrapStock[Decoy] == 0 && !G->HasTrap(T->StickyCell) && !G->HasTrap(T->DecoyCell));
		} });
	}
	// ================================================================ Shop
	else if (Scenario.Equals(TEXT("Shop"), ESearchCase::IgnoreCase))
	{
		// 商品の並び・壊れた壁と使った罠の数・罠の値段の上がり方・購入の音
		TSharedRef<FIntPoint> WallCell = MakeShared<FIntPoint>(0, 0);
		TSharedRef<FIntPoint> TrapCell = MakeShared<FIntPoint>(0, 0);
		Steps.Add({ 0.5f, [=, this] { KakuSkipTime(1000.f); } });
		Steps.Add({ 1.f, [=, this]
		{
			KakuNext(); // → 購入
			KakuAddCoins(100000.0);
			const TArray<FShopItemView> Items = GM->GetShopItems();
			Check(FString::Printf(TEXT("shop lists upgrades, walls and traps within the number keys (%d items)"), Items.Num()),
				Items.Num() == 2 + GM->WallTypes.Num() + GM->TrapTypes.Num() && Items.Num() <= 9);
			Check(TEXT("no wall reinforcement: item 3 is the first wall, then walls, then traps"),
				GM->GetShopIndexOfWall(0) == 2
				&& Items[GM->GetShopIndexOfWall(0)].DisplayName.EqualTo(GM->WallTypes[0].DisplayName)
				&& Items[GM->GetShopIndexOfTrap(0)].DisplayName.EqualTo(GM->TrapTypes[0].DisplayName));
			const int32 BuyBefore = Sound()->GetPlayCount(EKakurenboSfx::Buy);
			const double WoodPrice = GM->GetWallCost(0);
			BuyWall(0, 3);
			BuyTrap(0, 1);
			Check(TEXT("buying plays the purchase sound"), Sound()->GetPlayCount(EKakurenboSfx::Buy) > BuyBefore);
			const double Expected = WoodPrice * FMath::Pow(GM->WallTypes[0].CostGrowth, 3.0);
			Check(FString::Printf(TEXT("wall price grows with the number owned (%.2f -> %.2f, expected %.2f)"), WoodPrice, GM->GetWallCost(0), Expected),
				GM->WallTypes[0].CostGrowth > 1.0 && FMath::IsNearlyEqual(GM->GetWallCost(0), Expected, 0.001));
			const int32 Quiet = GM->WallTypes.IndexOfByPredicate([](const FWallTypeDef& Def) { return Def.NoiseDamping > 0.f; });
			Check(FString::Printf(TEXT("soundproof wall prices grow like traps (x%.2f, sticky x%.2f)"), GM->WallTypes[Quiet].CostGrowth, GM->TrapTypes[0].CostGrowth),
				Quiet != INDEX_NONE && GM->WallTypes[Quiet].CostGrowth >= GM->TrapTypes[0].CostGrowth && GM->WallTypes[0].CostGrowth < GM->TrapTypes[0].CostGrowth);
			Shot(TEXT("shop_01_items"));
		} });
		Steps.Add({ 0.5f, [=, this]
		{
			KakuNext(); // → 設置
			const FIntPoint Me = Grid()->WorldToCell(GetPawn()->GetActorLocation());
			*WallCell = Me + FIntPoint(2, 0);
			*TrapCell = Me + FIntPoint(0, 2);
			KakuPlaceWall(WallCell->X, WallCell->Y, 0);
			KakuPlaceWall(WallCell->X, WallCell->Y, 0); // 2 段
			KakuPlaceWall(WallCell->X - 4, WallCell->Y, 0);
			KakuPlaceTrap(TrapCell->X, TrapCell->Y, 0);
			Check(TEXT("walls and a trap are placed"), Grid()->GetColumnHeight(*WallCell) == 2 && Grid()->GetTrap(*TrapCell) != nullptr);
		} });
		Steps.Add({ 0.5f, [=, this] { KakuNext(); } }); // → かくれんぼ
		Steps.Add({ 0.5f, [=, this]
		{
			// 鬼に壊された・踏まれた代わりに、2 段の壁を壊してトリモチを使ったことにする
			Grid()->DamageBlocksInRadius(Grid()->CellToWorld(*WallCell, 0), 200.f, 1000.0); // 2 段とも（離れた 1 個は残す）
			Grid()->ConsumeTrap(Grid()->GetTrap(*TrapCell));
			KakuSkipTime(1000.f);
		} });
		Steps.Add({ 1.f, [=, this]
		{
			KakuNext(); // → 購入
			const int32 WoodIndex = GM->GetShopIndexOfWall(0);
			const int32 StickyIndex = GM->GetShopIndexOfTrap(0);
			TArray<FShopItemView> Items = GM->GetShopItems();
			Check(FString::Printf(TEXT("shop shows broken walls by type (wood missing %d, text '%s')"), Grid()->GetMissingWallCount(0), *Items[WoodIndex].RepairText.ToString()),
				Grid()->GetMissingWallCount(0) == 2 && Items[WoodIndex].RepairText.ToString().Contains(TEXT("2")) && Items[WoodIndex].bNeedsMoreForRepair);
			Check(FString::Printf(TEXT("shop shows used traps by type ('%s')"), *Items[StickyIndex].RepairText.ToString()),
				Grid()->GetMissingTrapCount(0) == 1 && !Items[StickyIndex].RepairText.IsEmpty() && Items[StickyIndex].bNeedsMoreForRepair);
			Check(TEXT("types that are not broken show nothing"), Items[GM->GetShopIndexOfWall(1)].RepairText.IsEmpty() && Items[GM->GetShopIndexOfTrap(1)].RepairText.IsEmpty());
			int32 PlanCount = 0;
			double PlanCost = 0.0;
			GM->GetRefillPlan(PlanCount, PlanCost);
			Check(FString::Printf(TEXT("refill plan: 3 items (%d, %.1f coins)"), PlanCount, PlanCost), PlanCount == 3 && PlanCost > 0.0);
			Shot(TEXT("shop_02_broken_counts"));

			// まとめて補充（R）は値段の高いものから：トリモチの分しかコインが無ければ、トリモチだけ買う
			GS()->Coins = GM->GetTrapCost(0) + 5.0;
			SimulateKey(EKeys::R, IE_Pressed);
			SimulateKey(EKeys::R, IE_Released);
			NextTick([=, this]
			{
				Check(FString::Printf(TEXT("refill buys the most expensive first (sticky stock %d, wood stock %d)"), GS()->TrapStock[0], GS()->WallStock[0]),
					GS()->TrapStock[0] == 1 && GS()->WallStock[0] == 0);
				// コインがあれば全部
				KakuAddCoins(100000.0);
				Check(TEXT("the refill button is clickable"), ClickUI(EKakurenboUIAction::RefillAll, 0));
				NextTick([=, this]
				{
					const TArray<FShopItemView> After = GM->GetShopItems();
					Check(FString::Printf(TEXT("refill buys everything that is short (wood stock %d, sticky stock %d)"), GS()->WallStock[0], GS()->TrapStock[0]),
						GS()->WallStock[0] == 2 && GS()->TrapStock[0] == 1);
					Check(TEXT("after refilling, the shop says the stock can repair them"),
						!After[WoodIndex].bNeedsMoreForRepair && !After[WoodIndex].RepairText.IsEmpty() && !After[StickyIndex].bNeedsMoreForRepair);
					bUseTestCursor = false;
				});
			});
		} });
		Steps.Add({ 0.5f, [=, this]
		{
			const double TrapPrice = GM->GetTrapCost(0);
			BuyTrap(0, 1);
			Check(FString::Printf(TEXT("trap price goes up after buying (%.1f -> %.1f)"), TrapPrice, GM->GetTrapCost(0)),
				FMath::IsNearlyEqual(GM->GetTrapCost(0), TrapPrice * GM->TrapTypes[0].CostGrowth));
			// コインが足りないと買えず、失敗の音
			GS()->Coins = 0.0;
			const int32 FailBefore = Sound()->GetPlayCount(EKakurenboSfx::BuyFail);
			const int32 LevelBefore = GS()->MashIncomeLevel;
			KakuBuy(1);
			Check(TEXT("not enough coins -> nothing bought, fail sound"), GS()->MashIncomeLevel == LevelBefore && Sound()->GetPlayCount(EKakurenboSfx::BuyFail) == FailBefore + 1);
			Shot(TEXT("shop_03_repairable"));
		} });
		Steps.Add({ 0.5f, [=, this]
		{
			KakuNext(); // → 設置（ここで自動で直る）
			Check(FString::Printf(TEXT("broken walls and used traps are repaired from the stock (walls %d, traps %d)"), GS()->LastRepairedWalls, GS()->LastRefilledTraps),
				GS()->LastRepairedWalls == 2 && GS()->LastRefilledTraps == 1 && Grid()->GetTotalMissing() == 0 && Grid()->GetMissingTrapCount() == 0);
		} });
		// 設置パートもマウスで：下の欄をクリックして罠を選び、床をクリックして置く
		TSharedRef<FIntPoint> ClickCell = MakeShared<FIntPoint>(0, 0);
		Steps.Add({ 1.0f, [=, this]
		{
			const int32 TrapSlot = GM->WallTypes.Num(); // 壁の後ろに罠が並ぶ
			Check(TEXT("the trap slot is clickable"), ClickUI(EKakurenboUIAction::BuildSlot, TrapSlot));
			NextTick([=, this] { Check(FString::Printf(TEXT("clicking a slot selects it (slot %d)"), SelectedBuildSlot), SelectedBuildSlot == TrapSlot && IsTrapSlotSelected()); });
		} });
		Steps.Add({ 0.3f, [=, this]
		{
			const FIntPoint Me = Grid()->WorldToCell(GetPawn()->GetActorLocation());
			*ClickCell = Me + FIntPoint(-2, -2);
			FVector2D ScreenPos;
			ProjectWorldLocationToScreen(Grid()->CellFloorCenter(*ClickCell), ScreenPos, true);
			const int32 StockBefore = GS()->TrapStock[0];
			ClickAt(ScreenPos);
			NextTick([=, this]
			{
				Check(FString::Printf(TEXT("clicking the floor places the trap (cell (%d,%d), stock %d -> %d)"), ClickCell->X, ClickCell->Y, StockBefore, GS()->TrapStock[0]),
					Grid()->GetTrap(*ClickCell) != nullptr && GS()->TrapStock[0] == StockBefore - 1);
				ShotLater(TEXT("shop_04_build_click"));
			});
		} });
		Steps.Add({ 0.6f, [=, this]
		{
			// 「かくれんぼ開始」ボタン
			Check(TEXT("the start button is clickable"), ClickUI(EKakurenboUIAction::NextPhase, 0));
			NextTick([=, this]
			{
				Check(FString::Printf(TEXT("the start button starts the round (%s)"), *UEnum::GetValueAsString(GS()->Phase)), GS()->Phase == EKakurenboPhase::Hide);
				bUseTestCursor = false;
			});
		} });
	}
	// ================================================================ Fx（演出と効果音）
	else if (Scenario.Equals(TEXT("Fx"), ESearchCase::IgnoreCase))
	{
		// 効果音は -NoSound でも「鳴らした回数」を数えるので、きっかけが正しいかを確かめられる。
		// RunAutoTest.ps1 -Sound を付けると実際に音を出して、再生が始まったかも確かめる
		auto HUD = [this]() { return GetHUD<AKakurenboHUD>(); };
		TSharedRef<int32> Count = MakeShared<int32>(0);
		TSharedRef<FIntPoint> WallCell = MakeShared<FIntPoint>(0, 0);
		auto TheOni = [GM]() -> AOniCharacter* { return GM->GetOnis().Num() > 0 ? GM->GetOnis()[0].Get() : nullptr; };

		Steps.Add({ 3.5f, [=, this]
		{
			Check(FString::Printf(TEXT("countdown beeps 3, 2, 1 (%d)"), Sound()->GetPlayCount(EKakurenboSfx::CountdownBeep)), Sound()->GetPlayCount(EKakurenboSfx::CountdownBeep) == 3);
			Check(TEXT("start sound when the onis come out"), Sound()->GetPlayCount(EKakurenboSfx::RoundStart) == 1);
			// 鬼は 1 体だけ、しばらく何も見えず聞こえないようにしておく
			if (AOniCharacter* Oni = KeepOnlyOni(0))
			{
				Oni->SightRadius = 0.f;
				Oni->CloseSenseRadius = 0.f;
				Oni->HearingRadius = 0.f;
				Oni->PocketInspectChance = 0.f;
			}
		} });
		Steps.Add({ 0.3f, [=, this]
		{
			const int32 MashBefore = Sound()->GetPlayCount(EKakurenboSfx::Mash);
			const int32 RingBefore = FxSys()->GetRingCount();
			KakuMash(1);
			Check(TEXT("mash: sound and noise ring"), Sound()->GetPlayCount(EKakurenboSfx::Mash) == MashBefore + 1 && FxSys()->GetRingCount() == RingBefore + 1);
			SetControlRotation(FRotator(-55.f, 0.f, 0.f));
			Shot(TEXT("fx_01_noise_ring"));
		} });
		Steps.Add({ 0.5f, [=, this]
		{
			*Count = FxSys()->GetBurstCount();
			if (GM->GetTreasures().Num() > 0)
			{
				TeleportPlayer(GM->GetTreasures()[0]->GetActorLocation());
			}
		} });
		Steps.Add({ 0.25f, [=, this]
		{
			Check(FString::Printf(TEXT("treasure: sparkles, sound and popup (bursts %d -> %d, popups %d)"), *Count, FxSys()->GetBurstCount(), HUD() ? HUD()->GetPopupCount() : -1),
				FxSys()->GetBurstCount() > *Count && Sound()->GetPlayCount(EKakurenboSfx::Treasure) == 1 && HUD() && HUD()->GetPopupCount() > 0);
			Shot(TEXT("fx_02_treasure"));
		} });
		Steps.Add({ 0.5f, [=, this]
		{
			// 壁を 1 個置いて壊す（積むと、落ちてきた上の段に破片が隠れて写らない）
			const FIntPoint Me = Grid()->WorldToCell(GetPawn()->GetActorLocation());
			*WallCell = FIntPoint(FMath::Clamp(Me.X + 3, 0, Grid()->GetSizeX() - 1), Me.Y);
			if (*WallCell == Me)
			{
				*WallCell = FIntPoint(Me.X - 3, Me.Y);
			}
			TeleportPlayer(Grid()->CellFloorCenter(Me));
			PlaceTestWall(*WallCell);
			// 壁が自分の体に隠れないよう、少し横から見る
			SetControlRotation(FRotator(-20.f, (Grid()->CellFloorCenter(*WallCell) - GetPawn()->GetActorLocation()).Rotation().Yaw + 30.f, 0.f));
		} });
		Steps.Add({ 0.4f, [=, this]
		{
			*Count = FxSys()->GetBurstCount();
			const int32 BreakBefore = Sound()->GetPlayCount(EKakurenboSfx::BlockBreak);
			const int32 Broken = Grid()->DamageBlocksInRadius(Grid()->CellToWorld(*WallCell, 0), 100.f, 100.0);
			Check(FString::Printf(TEXT("wall break: debris and crash sound (broken %d, bursts %d -> %d)"), Broken, *Count, FxSys()->GetBurstCount()),
				Broken == 1 && FxSys()->GetBurstCount() == *Count + 1 && Sound()->GetPlayCount(EKakurenboSfx::BlockBreak) == BreakBefore + 1);
			FTimerHandle Handle;
			GetWorldTimerManager().SetTimer(Handle, FTimerDelegate::CreateLambda([Shot] { Shot(TEXT("fx_03_wall_break")); }), 0.1f, false);
		} });
		Steps.Add({ 0.5f, [=, this]
		{
			PlaceTestWall(*WallCell);
			const int32 HitBefore = Sound()->GetPlayCount(EKakurenboSfx::BlockHit);
			const int32 Broken = Grid()->DamageBlocksInRadius(Grid()->CellToWorld(*WallCell, 0), 100.f, 0.5);
			Check(TEXT("wall hit (not broken): hit sound"), Broken == 0 && Grid()->GetColumnHeight(*WallCell) == 1 && Sound()->GetPlayCount(EKakurenboSfx::BlockHit) == HitBefore + 1);
		} });
		Steps.Add({ 0.3f, [=, this]
		{
			// 見つかって追いかけられる：「！」の音
			AOniCharacter* Oni = TheOni();
			*Count = Sound()->GetPlayCount(EKakurenboSfx::Alert);
			Oni->SightRadius = 2000.f;
			Oni->SightHalfAngle = 180.f;
			const FVector ToCenter = (Grid()->CellFloorCenter(FIntPoint(Grid()->GetSizeX() / 2, Grid()->GetSizeY() / 2)) - Oni->GetActorLocation()).GetSafeNormal2D();
			TeleportPlayer(Oni->GetActorLocation() + ToCenter * 500.f);
		} });
		Steps.Add({ 0.3f, [=, this]
		{
			Check(FString::Printf(TEXT("chase start: alert sound (intent=%s)"), *UEnum::GetValueAsString(TheOni()->GetIntent())),
				Sound()->GetPlayCount(EKakurenboSfx::Alert) == *Count + 1);
			// ぶつかって見つかる
			TeleportPlayer(TheOni()->GetActorLocation() + FVector(40.f, 0.f, 0.f));
		} });
		Steps.Add({ 0.2f, [=, this]
		{
			Check(FString::Printf(TEXT("caught: sound and red flash (flash %.2f)"), HUD() ? HUD()->GetFlashAlpha() : -1.f),
				GS()->Phase == EKakurenboPhase::Result && !GS()->bLastRoundCleared && Sound()->GetPlayCount(EKakurenboSfx::Caught) == 1 && HUD() && HUD()->GetFlashAlpha() > 0.f);
			Shot(TEXT("fx_04_caught"));
		} });
		Steps.Add({ 1.f, [=, this] { KakuNext(); } });   // → 購入
		Steps.Add({ 0.5f, [=, this] { KakuNext(); } });  // → 設置
		Steps.Add({ 0.5f, [=, this] { KakuNext(); } });  // → かくれんぼ
		Steps.Add({ 0.5f, [=, this]
		{
			// 残り 5.5 秒にする → 秒読みの音
			*Count = Sound()->GetPlayCount(EKakurenboSfx::TimeTick);
			GS()->HideStartCountdown = 0.f;
			GS()->HideTimeRemaining = 5.5f;
		} });
		Steps.Add({ 1.2f, [=, this]
		{
			Check(FString::Printf(TEXT("last seconds tick (%d)"), Sound()->GetPlayCount(EKakurenboSfx::TimeTick) - *Count), Sound()->GetPlayCount(EKakurenboSfx::TimeTick) > *Count);
			for (AOniCharacter* Oni : GM->GetOnis())
			{
				if (Oni)
				{
					Oni->Deactivate(); // 逃げ切りの確認の邪魔をしないように
				}
			}
			KakuSkipTime(1000.f);
		} });
		Steps.Add({ 0.2f, [=, this]
		{
			Check(FString::Printf(TEXT("clear: fanfare and gold flash (flash %.2f)"), HUD() ? HUD()->GetFlashAlpha() : -1.f),
				GS()->Phase == EKakurenboPhase::Result && GS()->bLastRoundCleared && Sound()->GetPlayCount(EKakurenboSfx::Clear) == 1 && HUD() && HUD()->GetFlashAlpha() > 0.f);
			Shot(TEXT("fx_05_clear"));
			if (GEngine && GEngine->UseSound())
			{
				Check(FString::Printf(TEXT("sounds really started playing (%d)"), Sound()->GetStartedCount()), Sound()->GetStartedCount() > 10);
			}
			else
			{
				UE_LOG(LogTemp, Display, TEXT("[AutoTest] sound is off (-NoSound): only the sound triggers were checked"));
			}
		} });
	}
	// ================================================================ Gate（鬼の出入り口）
	else if (Scenario.Equals(TEXT("Gate"), ESearchCase::IgnoreCase))
	{
		// 鬼は必ず赤い門の前のマスから出てくる。そこには壁も罠も置けない
		Steps.Add({ 0.3f, [=, this]
		{
			SetStageOniTypes({ EOniType::Balanced, EOniType::Scout, EOniType::Breaker, EOniType::Careful, EOniType::Careful, EOniType::Balanced });
			const TArray<FIntPoint> Gate = GM->GetOniGateCells();
			Check(FString::Printf(TEXT("the gate has 6 spawn cells, all reserved in the grid (%d / %d)"), Gate.Num(), Grid()->GetReservedCells().Num()),
				Gate.Num() == 6 && Grid()->GetReservedCells().Num() == 6 && Grid()->IsReservedCell(Gate[0]));
			FText Reason;
			Check(FString::Printf(TEXT("cannot put a wall in front of the gate (%s)"), *Reason.ToString()), !Grid()->CanPlaceBlock(Gate[0], &Reason));
			Check(TEXT("cannot put a trap in front of the gate"), !Grid()->CanPlaceTrap(Gate[1]));
			// 門の方を見る
			SetControlRotation(FRotator(-15.f, (GM->GetArena()->GetOniGateLocation() - GetPawn()->GetActorLocation()).Rotation().Yaw, 0.f));
			ShotLater(TEXT("gate_01_countdown"));
		} });
		Steps.Add({ 3.f, [=, this]
		{
			const TArray<FIntPoint> Gate = GM->GetOniGateCells();
			TSet<FIntPoint> Used;
			bool bAllAtGate = GM->GetOnis().Num() == 6;
			for (const AOniCharacter* Oni : GM->GetOnis())
			{
				const FIntPoint Cell = Grid()->WorldToCell(Oni->GetActorLocation());
				bAllAtGate &= Gate.Contains(Cell);
				Used.Add(Cell);
			}
			Check(FString::Printf(TEXT("all 6 onis come out of the gate cells, one per cell (%d onis, %d cells)"), GM->GetOnis().Num(), Used.Num()), bAllAtGate && Used.Num() == 6);

			// 鬼の見た目：アセット（Fab の Cute Creature）がこのパソコンにあればそのモデル、無ければ円柱
			const bool bAssetHere = !GM->OniSkeletalMesh.IsNull() && FPackageName::DoesPackageExist(GM->OniSkeletalMesh.ToSoftObjectPath().GetLongPackageName());
			bool bAllMatch = true;
			bool bAllAnimated = true;
			for (const AOniCharacter* Oni : GM->GetOnis())
			{
				bAllMatch &= Oni->UsesSkeletalMesh() == bAssetHere;
				bAllAnimated &= !bAssetHere || Oni->GetPlayingAnimation() != nullptr;
			}
			Check(FString::Printf(TEXT("oni look matches the project (character model in project=%d)"), bAssetHere ? 1 : 0), bAllMatch);
			Check(TEXT("character model onis play an animation"), bAllAnimated);
			Log(TEXT("onis out"));
			Shot(TEXT("gate_02_onis_out"));
		} });
		// 見つからないように遠くの隅へ（撮ってから動かす）
		Steps.Add({ 0.2f, [=, this] { TeleportPlayer(Grid()->CellFloorCenter(FIntPoint(1, 1))); } });
		Steps.Add({ 3.8f, [=, this]
		{
			// 6 体が門の前で押し合って詰まらず、散らばっていく
			int32 StillAtGate = 0;
			for (const AOniCharacter* Oni : GM->GetOnis())
			{
				StillAtGate += GM->GetOniGateCells().Contains(Grid()->WorldToCell(Oni->GetActorLocation())) ? 1 : 0;
			}
			Log(TEXT("4s later"));
			Check(FString::Printf(TEXT("onis leave the gate without getting stuck (%d of 6 still there)"), StillAtGate), StillAtGate <= 2 || GS()->Phase != EKakurenboPhase::Hide);
			KakuSkipTime(1000.f);
		} });
		Steps.Add({ 1.f, [=, this] { KakuNext(); } }); // → 購入
		Steps.Add({ 0.5f, [=, this] { KakuNext(); } }); // → 設置
		Steps.Add({ 1.f, [=, this]
		{
			Check(TEXT("cannot start the round in front of the gate"), !GM->MovePlayerStart(GM->GetOniGateCells()[0]));
			Shot(TEXT("gate_03_build_topdown"));
		} });
	}
	// ================================================================ GateWalled（出入り口の前を壁で囲まれても、鬼は壊して出てくる）
	else if (Scenario.Equals(TEXT("GateWalled"), ESearchCase::IgnoreCase))
	{
		Steps.Add({ 0.3f, [=, this]
		{
			SetStageOniTypes({ EOniType::Balanced, EOniType::Scout });
			// 門の前の 3×2 マスを、外周の壁と一緒に木の壁で囲む（コの字）
			const TArray<FIntPoint> Gate = GM->GetOniGateCells();
			int32 MinX = MAX_int32, MinY = MAX_int32, MaxY = MIN_int32;
			for (const FIntPoint& C : Gate)
			{
				MinX = FMath::Min(MinX, C.X);
				MinY = FMath::Min(MinY, C.Y);
				MaxY = FMath::Max(MaxY, C.Y);
			}
			for (int32 Y = MinY - 1; Y <= MaxY + 1; ++Y)
			{
				PlaceTestWall(FIntPoint(MinX - 1, Y));
			}
			for (int32 X = MinX; X < Grid()->GetSizeX(); ++X)
			{
				PlaceTestWall(FIntPoint(X, MinY - 1));
				PlaceTestWall(FIntPoint(X, MaxY + 1));
			}
			Check(FString::Printf(TEXT("the gate area is walled in (in main area: %d)"), Grid()->IsInMainArea(Gate[0]) ? 1 : 0), !Grid()->IsInMainArea(Gate[0]));
			TeleportPlayer(Grid()->CellFloorCenter(FIntPoint(1, 1)));
			GetHider()->SetOverheadYaw(0.f);
		} });
		Steps.Add({ 3.f, [=]
		{
			for (AOniCharacter* Oni : GM->GetOnis())
			{
				Oni->SightRadius = 0.f; // プレイヤーにも音にも反応しない（自分で出てくるかを見る）
				Oni->CloseSenseRadius = 0.f;
				Oni->HearingRadius = 0.f;
				Oni->DecoyHearingRadius = 0.f;
				Oni->bDrawDebug = true;
			}
		} });
		for (int32 i = 0; i < 12; ++i)
		{
			Steps.Add({ 1.f, [=] { Log(TEXT("waiting")); } });
		}
		Steps.Add({ 0.1f, [=, this]
		{
			int32 Out = 0;
			for (const AOniCharacter* Oni : GM->GetOnis())
			{
				Out += Grid()->IsInMainArea(Grid()->WorldToCell(Oni->GetActorLocation())) ? 1 : 0;
			}
			Check(FString::Printf(TEXT("onis break out of the walled-in gate (%d of %d out, walls destroyed %d)"), Out, GM->GetOnis().Num(), GS()->LastRoundWallsDestroyed),
				Out == GM->GetOnis().Num() && GS()->LastRoundWallsDestroyed >= 1);
			Shot(TEXT("gatewalled_01_after"));
		} });
	}
	// ================================================================ Matchup（鬼の種類と罠の相性）
	else if (Scenario.Equals(TEXT("Matchup"), ESearchCase::IgnoreCase))
	{
		// スピード鬼：おとりにだまされない・トリモチで長く止まる
		// パワー鬼：おとりに遠くから寄っていく・トリモチはすぐ抜け出し、直後に踏んだトリモチは壊す
		constexpr int32 Sticky = 0;
		constexpr int32 Decoy = 1;
		const FIntPoint DecoyCell(6, 6), StickyA(14, 4), StickyB(14, 8), StickyC(14, 16);
		auto Scout = [GM]() { return GM->GetOnis()[0].Get(); };
		auto Breaker = [GM]() { return GM->GetOnis()[1].Get(); };
		auto Put = [this, GM](AOniCharacter* Oni, const FIntPoint& Cell)
		{
			Oni->SetActorLocation(GetWorld()->GetSubsystem<UKakurenboGridSubsystem>()->CellFloorCenter(Cell) + FVector(0.f, 0.f, 100.f), false, nullptr, ETeleportType::TeleportPhysics);
		};
		Steps.Add({ 0.3f, [=] { SetStageOniTypes({ EOniType::Scout, EOniType::Breaker }); } });
		Steps.Add({ 3.f, [=, this]
		{
			Check(TEXT("a speed oni and a power oni"), GM->GetOnis().Num() == 2 && Scout()->OniType == EOniType::Scout && Breaker()->OniType == EOniType::Breaker);
			for (AOniCharacter* Oni : GM->GetOnis())
			{
				Oni->SightRadius = 0.f; // プレイヤーには反応させない（おとりの音は聞く）
				Oni->CloseSenseRadius = 0.f;
				Oni->HearingRadius = 0.f;
				Oni->PocketInspectChance = 0.f;
				Oni->InvestigateMaxDuration = 100.f;
				Oni->NoiseInaccuracyCells = 0.f;
			}
			Check(FString::Printf(TEXT("speed oni is not fooled by decoys, power oni hears them from far (decoy hearing %.0f / %.0f)"), Scout()->DecoyHearingRadius, Breaker()->DecoyHearingRadius),
				Scout()->DecoyHearingRadius == 0.f && Breaker()->DecoyHearingRadius > GM->GetOniHearingRadius());
			Grid()->PlaceTrap(DecoyCell, Decoy, GM->TrapTypes[Decoy]);
			Grid()->PlaceTrap(StickyA, Sticky, GM->TrapTypes[Sticky]);
			Grid()->PlaceTrap(StickyB, Sticky, GM->TrapTypes[Sticky]);
			Grid()->PlaceTrap(StickyC, Sticky, GM->TrapTypes[Sticky]);
			TeleportPlayer(Grid()->CellFloorCenter(FIntPoint(1, 22)));
			Put(Scout(), DecoyCell + FIntPoint(7, 0));
			Put(Breaker(), DecoyCell + FIntPoint(0, 7));
		} });
		Steps.Add({ 1.2f, [=, this]
		{
			Check(FString::Printf(TEXT("decoy: the power oni goes to it, the speed oni ignores it (power %s, speed %s)"),
				*UEnum::GetValueAsString(Breaker()->GetIntent()), *UEnum::GetValueAsString(Scout()->GetIntent())),
				Breaker()->GetIntent() == EOniState::Investigate && Scout()->GetIntent() != EOniState::Investigate);
			// トリモチ：スピード鬼を乗せる
			Put(Scout(), StickyA);
		} });
		Steps.Add({ 0.3f, [=, this]
		{
			Check(TEXT("speed oni is stuck on the sticky trap"), Scout()->GetIntent() == EOniState::Stunned);
			Put(Breaker(), StickyB);
		} });
		Steps.Add({ 0.3f, [=, this] { Check(TEXT("power oni is stuck too"), Breaker()->GetIntent() == EOniState::Stunned); } });
		Steps.Add({ 1.8f, [=, this]
		{
			// パワー鬼は 4 秒 × 0.4 = 1.6 秒で抜け出す。かからない間にトリモチを踏むと壊す
			Check(FString::Printf(TEXT("power oni gets out quickly (intent %s, can disarm %d)"), *UEnum::GetValueAsString(Breaker()->GetIntent()), Breaker()->CanDisarmTraps() ? 1 : 0),
				Breaker()->GetIntent() != EOniState::Stunned && Breaker()->CanDisarmTraps());
			Put(Breaker(), StickyC);
		} });
		Steps.Add({ 0.3f, [=, this]
		{
			Check(FString::Printf(TEXT("power oni destroys a sticky trap it steps on right after (trap left %d, design %d, intent %s)"),
				Grid()->GetTrap(StickyC) ? 1 : 0, Grid()->GetTrapDesign(StickyC), *UEnum::GetValueAsString(Breaker()->GetIntent())),
				!Grid()->GetTrap(StickyC) && Grid()->GetTrapDesign(StickyC) == Sticky && Breaker()->GetIntent() != EOniState::Stunned);
			Shot(TEXT("matchup_01"));
		} });
		Steps.Add({ 2.6f, [=, this]
		{
			// スピード鬼は 4 秒 × 1.5 = 6 秒動けない（ここで乗せてから 5.3 秒）
			Check(FString::Printf(TEXT("speed oni is stuck longer (still %s after 5.3s)"), *UEnum::GetValueAsString(Scout()->GetIntent())), Scout()->GetIntent() == EOniState::Stunned);
		} });
		Steps.Add({ 1.2f, [=, this]
		{
			Check(FString::Printf(TEXT("speed oni gets out after 6s (%s)"), *UEnum::GetValueAsString(Scout()->GetIntent())), Scout()->GetIntent() != EOniState::Stunned);
		} });
	}
	// ================================================================ CarefulShare（慎重鬼は仲間が調べている建物に行かない）
	else if (Scenario.Equals(TEXT("CarefulShare"), ESearchCase::IgnoreCase))
	{
		// ドーナツ形の壁（3×3 の輪。真ん中が空洞）を 2 つ。慎重鬼 2 体が、それぞれ別の建物を調べに行くか
		const FIntPoint DonutA(17, 6), DonutB(17, 18);
		struct FShare { int32 Same = 0; int32 Split = 0; };
		TSharedRef<FShare> Share = MakeShared<FShare>();
		Steps.Add({ 0.3f, [=, this]
		{
			SetStageOniTypes({ EOniType::Careful, EOniType::Careful });
			for (const FIntPoint& Center : { DonutA, DonutB })
			{
				for (int32 DY = -1; DY <= 1; ++DY)
				{
					for (int32 DX = -1; DX <= 1; ++DX)
					{
						if (DX != 0 || DY != 0)
						{
							PlaceTestWall(Center + FIntPoint(DX, DY));
						}
					}
				}
			}
			Check(FString::Printf(TEXT("two donut-shaped buildings (pockets %d)"), Grid()->FindEnclosedPockets().Num()), Grid()->FindEnclosedPockets().Num() == 2);
			Check(TEXT("a building = the ring and its hollow"), Grid()->GetStructureAround(DonutA).Num() == 9 && Grid()->GetStructureAround(DonutA + FIntPoint(1, 0)).Contains(DonutA));
			TeleportPlayer(Grid()->CellFloorCenter(FIntPoint(1, 12)));
		} });
		Steps.Add({ 3.f, [=, this]
		{
			for (AOniCharacter* Oni : GM->GetOnis())
			{
				Oni->SightRadius = 0.f;
				Oni->CloseSenseRadius = 0.f;
				Oni->HearingRadius = 0.f;
				Oni->DecoyHearingRadius = 0.f;
				Oni->StepHearingRadius = 0.f;
				Oni->bDrawDebug = true;
			}
			// 慎重鬼は広い場所を見終わってから空洞へ行くので、広い場所は「もう調べた」ことにしておく
			if (UKakurenboOniBlackboard* BB = GetWorld()->GetSubsystem<UKakurenboOniBlackboard>())
			{
				for (int32 Y = 0; Y < Grid()->GetSizeY(); ++Y)
				{
					for (int32 X = 0; X < Grid()->GetSizeX(); ++X)
					{
						BB->MarkChecked(FIntPoint(X, Y), GetWorld()->GetTimeSeconds() + 1000.f);
					}
				}
			}
		} });
		auto WhichDonut = [=](const AOniCharacter* Oni) -> int32
		{
			const EOniState Intent = Oni->GetIntent();
			if (Intent != EOniState::Inspect)
			{
				return 0;
			}
			const FIntPoint C = Oni->GetInspectCell();
			auto Near = [&C](const FIntPoint& D) { return FMath::Abs(C.X - D.X) <= 1 && FMath::Abs(C.Y - D.Y) <= 1; };
			return Near(DonutA) ? 1 : Near(DonutB) ? 2 : 0;
		};
		for (int32 i = 0; i < 24; ++i)
		{
			Steps.Add({ 0.25f, [=]
			{
				const int32 A = WhichDonut(GM->GetOnis()[0]);
				const int32 B = WhichDonut(GM->GetOnis()[1]);
				if (A != 0 && A == B)
				{
					++Share->Same;
				}
				else if (A != 0 && B != 0)
				{
					++Share->Split;
				}
			} });
		}
		Steps.Add({ 0.1f, [=, this]
		{
			Check(FString::Printf(TEXT("careful onis never target the same building (same %d, split %d of 24 checks)"), Share->Same, Share->Split), Share->Same == 0 && Share->Split >= 4);
			Shot(TEXT("carefulshare_01"));
		} });
	}
	// ================================================================ Look（鬼の見た目を近くで見る）
	else if (Scenario.Equals(TEXT("Look"), ESearchCase::IgnoreCase))
	{
		// 4 種類の鬼をプレイヤーの前に横一列に並べ、こちらを向かせて撮る（大きさ・向き・色の確認）
		Steps.Add({ 0.3f, [=] { SetStageOniTypes({ EOniType::Balanced, EOniType::Scout, EOniType::Breaker, EOniType::Careful, EOniType::Treasure, EOniType::Detector }); } });
		Steps.Add({ 3.3f, [=, this]
		{
			const FIntPoint Me = Grid()->WorldToCell(GetPawn()->GetActorLocation());
			TeleportPlayer(Grid()->CellFloorCenter(Me));
			for (int32 i = 0; i < GM->GetOnis().Num(); ++i)
			{
				AOniCharacter* Oni = GM->GetOnis()[i];
				Oni->Deactivate(); // 止めて待機のアニメーションにする
				const FVector Location = Grid()->CellFloorCenter(Me + FIntPoint(5, i * 2 - 5)) + FVector(0.f, 0.f, 100.f);
				Oni->SetActorLocation(Location, false, nullptr, ETeleportType::TeleportPhysics);
				Oni->SetActorRotation(FRotator(0.f, 180.f, 0.f)); // プレイヤーの方（-X）を向く
			}
			SetControlRotation(FRotator(-12.f, 0.f, 0.f));
		} });
		Steps.Add({ 3.0f, [=, this]
		{
			Shot(TEXT("look_01_lineup"));
			// 1 体だけ動かして、走るアニメーションを見る
			if (AOniCharacter* Oni = GM->GetOnis()[1])
			{
				Oni->Activate(GetHider());
				Oni->SightRadius = 0.f;
				Oni->CloseSenseRadius = 0.f;
				Oni->HearingRadius = 0.f;
				Oni->DebugGoTo(Grid()->WorldToCell(Oni->GetActorLocation()) + FIntPoint(0, 8));
			}
		} });
		Steps.Add({ 0.6f, [=, this]
		{
			const AOniCharacter* Oni = GM->GetOnis()[1];
			Check(FString::Printf(TEXT("a moving oni plays a different animation from a standing one (%s / %s)"),
				Oni->GetPlayingAnimation() ? *Oni->GetPlayingAnimation()->GetName() : TEXT("none"),
				GM->GetOnis()[0]->GetPlayingAnimation() ? *GM->GetOnis()[0]->GetPlayingAnimation()->GetName() : TEXT("none")),
				!Oni->UsesSkeletalMesh() || Oni->GetPlayingAnimation() != GM->GetOnis()[0]->GetPlayingAnimation());
			Shot(TEXT("look_02_running"));
		} });
	}
	// ================================================================ Crowd（鬼どうしのすれ違い）
	else if (Scenario.Equals(TEXT("Crowd"), ESearchCase::IgnoreCase))
	{
		// 幅 1 マスの通路の両端から、2 体の鬼を反対側へ向かわせる。体がぶつかって押し合うと、どちらも通れなくなる
		const FIntPoint WestEnd(5, 5), EastEnd(13, 5);
		Steps.Add({ 0.3f, [=, this]
		{
			SetStageOniTypes({ EOniType::Balanced, EOniType::Balanced });
			for (int32 X = 4; X <= 14; ++X)
			{
				PlaceTestWall(FIntPoint(X, 4));
				PlaceTestWall(FIntPoint(X, 6));
			}
			TeleportPlayer(Grid()->CellFloorCenter(FIntPoint(18, 20)));
		} });
		Steps.Add({ 3.3f, [=, this]
		{
			Check(TEXT("two onis"), GM->GetOnis().Num() == 2);
			const FIntPoint Starts[2] = { WestEnd, EastEnd };
			for (int32 i = 0; i < 2; ++i)
			{
				AOniCharacter* Oni = GM->GetOnis()[i];
				Oni->SightRadius = 0.f; // プレイヤーには反応させない
				Oni->CloseSenseRadius = 0.f;
				Oni->HearingRadius = 0.f;
				Oni->PocketInspectChance = 0.f;
				Oni->InvestigateMaxDuration = 100.f;
				Oni->SearchDuration = 10.f; // 着いたらその場で見回し続ける（うろうろで離れないように）
				Oni->bDrawDebug = true;
				Oni->SetActorLocation(Grid()->CellFloorCenter(Starts[i]) + FVector(0.f, 0.f, 100.f), false, nullptr, ETeleportType::TeleportPhysics);
			}
		} });
		Steps.Add({ 0.2f, [=, this]
		{
			// 西の鬼は東の端へ、東の鬼は西の端へ（通路の中で正面からすれ違う）
			GM->GetOnis()[0]->DebugGoTo(EastEnd);
			GM->GetOnis()[1]->DebugGoTo(WestEnd);
			SetControlRotation(FRotator(-55.f, (Grid()->CellFloorCenter(FIntPoint(9, 5)) - GetPawn()->GetActorLocation()).Rotation().Yaw, 0.f));
		} });
		Steps.Add({ 0.6f, [=] { Log(TEXT("passing")); Shot(TEXT("crowd_01_passing")); } });
		Steps.Add({ 2.0f, [=, this]
		{
			const FIntPoint A = Grid()->WorldToCell(GM->GetOnis()[0]->GetActorLocation());
			const FIntPoint B = Grid()->WorldToCell(GM->GetOnis()[1]->GetActorLocation());
			Log(TEXT("after"));
			Check(FString::Printf(TEXT("the onis passed each other in the corridor (west oni at (%d,%d), east oni at (%d,%d))"), A.X, A.Y, B.X, B.Y),
				FMath::Abs(A.X - EastEnd.X) <= 1 && FMath::Abs(B.X - WestEnd.X) <= 1);
			Check(FString::Printf(TEXT("no walls were broken to get past (%d)"), GS()->LastRoundWallsDestroyed), GS()->LastRoundWallsDestroyed == 0);
		} });
	}
	// ================================================================ Quiet（消音壁）
	else if (Scenario.Equals(TEXT("Quiet"), ESearchCase::IgnoreCase))
	{
		// 消音壁で囲むと、連打の音が鬼に届く距離が短くなる（囲まれていないと効かない）
		constexpr int32 Wood = 0;
		const int32 Quiet = GM->WallTypes.IndexOfByPredicate([](const FWallTypeDef& Def) { return Def.NoiseDamping > 0.f; });
		TSharedRef<FIntPoint> Me = MakeShared<FIntPoint>(0, 0);
		auto Ring = [](const FIntPoint& C)
		{
			TArray<FIntPoint> Cells;
			for (int32 DY = -1; DY <= 1; ++DY)
			{
				for (int32 DX = -1; DX <= 1; ++DX)
				{
					if (DX != 0 || DY != 0)
					{
						Cells.Add(C + FIntPoint(DX, DY));
					}
				}
			}
			return Cells;
		};
		auto PlaceType = [GM, Grid](const FIntPoint& Cell, int32 Type)
		{
			const TArray<FWallTypeDef> Types = GM->GetEffectiveWallTypes();
			Grid()->PlaceBlock(Cell, Type, Types[Type].MaxHP, Types[Type].Color, Types[Type].SoundHP);
		};
		// 鬼をプレイヤーから 5m 離れた所に置いて、連打する。聞こえたら音のした方へ向かう
		auto MashNearOni = [=, this](bool bExpectHeard, const FString& What)
		{
			AOniCharacter* Oni = GM->GetOnis()[0];
			Oni->SetActorLocation(Grid()->CellFloorCenter(*Me + FIntPoint(-5, 0)) + FVector(0.f, 0.f, 100.f), false, nullptr, ETeleportType::TeleportPhysics);
			KakuMash(1);
			NextTick([=]
			{
				const bool bHeard = Oni->GetIntent() == EOniState::Investigate;
				Check(FString::Printf(TEXT("%s (multiplier %.2f, heard=%d)"), *What, GM->GetPlayerNoiseMultiplier(), bHeard ? 1 : 0), bHeard == bExpectHeard);
			});
		};

		Steps.Add({ 0.3f, [=, this]
		{
			Check(FString::Printf(TEXT("there is a soundproof wall type (index %d)"), Quiet), Quiet != INDEX_NONE);
			SetStageOniTypes({ EOniType::Balanced });
			*Me = Grid()->WorldToCell(GetPawn()->GetActorLocation());
			TeleportPlayer(Grid()->CellFloorCenter(*Me));
			Check(TEXT("not enclosed: full volume"), FMath::IsNearlyEqual(GM->GetPlayerNoiseMultiplier(), 1.f));
			for (const FIntPoint& Cell : Ring(*Me))
			{
				PlaceType(Cell, Quiet);
			}
			const float Expected = 1.f - GM->WallTypes[Quiet].NoiseDamping;
			Check(FString::Printf(TEXT("enclosed by 8 soundproof walls (walls %d, multiplier %.2f, expected %.2f)"), GM->GetPlayerEnclosureWallCount(), GM->GetPlayerNoiseMultiplier(), Expected),
				GM->GetPlayerEnclosureWallCount() == 8 && FMath::IsNearlyEqual(GM->GetPlayerNoiseMultiplier(), Expected, 0.001f));
			const int32 FullSoundHP = FMath::RoundToInt(GM->GetEffectiveWallTypes()[Quiet].SoundHP);
			Check(FString::Printf(TEXT("soundproof walls can mute %d sounds (HUD shows %d)"), FullSoundHP, GM->GetPlayerQuietWallRemaining()),
				FullSoundHP > 0 && GM->GetPlayerQuietWallRemaining() == FullSoundHP);
			SetControlRotation(FRotator(-50.f, 30.f, 0.f));
			ShotLater(TEXT("quiet_01_enclosed"));
		} });
		Steps.Add({ 3.f, [=, this]
		{
			if (AOniCharacter* Oni = KeepOnlyOni(0))
			{
				MakeBlindListener(Oni);
				Oni->HearingRadius = 1000.f; // 連打がそのまま 10m まで届く鬼。消音壁 80% なら 2m
			}
		} });
		Steps.Add({ 0.3f, [=] { MashNearOni(false, TEXT("soundproof walls: the oni 5m away does not hear the mash")); } });
		Steps.Add({ 0.3f, [=, this]
		{
			const int32 FullSoundHP = FMath::RoundToInt(GM->GetEffectiveWallTypes()[Quiet].SoundHP);
			Check(FString::Printf(TEXT("muting a mash wears the soundproof walls (%d -> %d)"), FullSoundHP, GM->GetPlayerQuietWallRemaining()),
				GM->GetPlayerQuietWallRemaining() == FullSoundHP - FMath::RoundToInt(GM->MashSoundDamage));
		} });
		Steps.Add({ 0.3f, [=, this]
		{
			// 半分（縦横の 4 個）を木の壁にすると、効き目も半分
			for (const FIntPoint& D : { FIntPoint(1, 0), FIntPoint(-1, 0), FIntPoint(0, 1), FIntPoint(0, -1) })
			{
				Grid()->PickUpBlock(Grid()->GetTopBlock(*Me + D));
				PlaceType(*Me + D, Wood);
			}
			const float Expected = 1.f - GM->WallTypes[Quiet].NoiseDamping * 0.5f;
			Check(FString::Printf(TEXT("half soundproof: half the effect (multiplier %.2f, expected %.2f)"), GM->GetPlayerNoiseMultiplier(), Expected),
				FMath::IsNearlyEqual(GM->GetPlayerNoiseMultiplier(), Expected, 0.001f));
		} });
		Steps.Add({ 0.3f, [=] { MashNearOni(true, TEXT("half soundproof: the oni 5m away hears the mash")); } });
		Steps.Add({ 0.1f, [=, this]
		{
			// 音を消せる回数が尽きると壊れる（残りの 4 個（斜め）を、あと 2 回で壊れる状態にして 2 回連打）
			GM->GetOnis()[0]->Deactivate(); // 寄ってきて壁を壊さないように
			for (const FIntPoint& D : { FIntPoint(1, 1), FIntPoint(-1, 1), FIntPoint(1, -1), FIntPoint(-1, -1) })
			{
				if (APlaceableBlock* Block = Grid()->GetBlock(*Me + D, 0))
				{
					Block->SoundHP = 2.0;
				}
			}
			const int32 DestroyedBefore = GS()->LastRoundWallsDestroyed;
			KakuMash(1);
			const bool bStillThere = Grid()->GetColumnHeight(*Me + FIntPoint(1, 1)) == 1;
			KakuMash(1);
			int32 Gone = 0;
			for (const FIntPoint& D : { FIntPoint(1, 1), FIntPoint(-1, 1), FIntPoint(1, -1), FIntPoint(-1, -1) })
			{
				Gone += Grid()->GetColumnHeight(*Me + D) == 0 ? 1 : 0;
			}
			Check(FString::Printf(TEXT("soundproof walls break when their count runs out (after 1: still there=%d, after 2: %d of 4 broken, destroyed +%d)"),
				bStillThere ? 1 : 0, Gone, GS()->LastRoundWallsDestroyed - DestroyedBefore),
				bStillThere && Gone == 4 && GS()->LastRoundWallsDestroyed - DestroyedBefore == 4);
			Check(TEXT("broken soundproof walls stay in the layout (repaired from stock later)"), Grid()->GetMissingWallCount(Quiet) == 4);
			Check(FString::Printf(TEXT("only wood walls left: no muting (multiplier %.2f, HUD %d)"), GM->GetPlayerNoiseMultiplier(), GM->GetPlayerQuietWallRemaining()),
				FMath::IsNearlyEqual(GM->GetPlayerNoiseMultiplier(), 1.f) && GM->GetPlayerQuietWallRemaining() == -1);
			ShotLater(TEXT("quiet_02_broken"));
		} });
		Steps.Add({ 0.6f, [=, this]
		{
			// 1 か所開けると囲まれていないので効かない
			Grid()->PickUpBlock(Grid()->GetTopBlock(*Me + FIntPoint(1, 0)));
			Check(FString::Printf(TEXT("opened: not enclosed, full volume (multiplier %.2f)"), GM->GetPlayerNoiseMultiplier()), FMath::IsNearlyEqual(GM->GetPlayerNoiseMultiplier(), 1.f) && GM->GetPlayerEnclosureWallCount() == 0);
			KakuSkipTime(1000.f);
		} });
	}
	// ================================================================ Dash
	else if (Scenario.Equals(TEXT("Dash"), ESearchCase::IgnoreCase))
	{
		// Shift でダッシュ：しばらく速くなり、大きな音が出る（連打より遠くまで届く）。クールタイムがある
		auto TheOni = [GM]() -> AOniCharacter* { return GM->GetOnis().Num() > 0 ? GM->GetOnis()[0].Get() : nullptr; };
		Steps.Add({ 0.3f, [=, this]
		{
			SetStageOniTypes({ EOniType::Balanced });
			TeleportPlayer(Grid()->CellFloorCenter(FIntPoint(4, Grid()->GetSizeY() / 2)));
			SetControlRotation(FRotator(-20.f, 90.f, 0.f)); // 南北の向き（W で鬼のいる東とは直角の方向へ走る）
		} });
		Steps.Add({ 3.f, [=, this]
		{
			AOniCharacter* Oni = KeepOnlyOni(0);
			MakeBlindListener(Oni);
			Oni->HearingRadius = 1000.f; // 連打は 10m、ダッシュ（×1.5）は 15m まで届く
			Oni->SetActorLocation(GetPawn()->GetActorLocation() + FVector(1300.f, 0.f, 20.f), false, nullptr, ETeleportType::TeleportPhysics);
			KakuMash(1);
			// ダッシュは最初は使えない
			const int32 SoundBefore = Sound()->GetPlayCount(EKakurenboSfx::Dash);
			SimulateKey(EKeys::LeftShift, IE_Pressed);
			SimulateKey(EKeys::LeftShift, IE_Released);
			NextTick([=, this]
			{
				Check(FString::Printf(TEXT("a mash does not reach the oni 13m away (intent=%s)"), *UEnum::GetValueAsString(Oni->GetIntent())), Oni->GetIntent() != EOniState::Investigate);
				Check(TEXT("Shift does nothing before the dash is unlocked"), !GetHider()->IsDashing() && Sound()->GetPlayCount(EKakurenboSfx::Dash) == SoundBefore);
				// 転生のお店でダッシュの速さ Lv1 を買ったことにする（クールタイムは Lv0 のまま）
				GS()->PrestigeLevels[static_cast<int32>(EPrestigeUpgrade::DashSpeed)] = 1;
				GM->ApplyPrestigeToPlayer();
			});
		} });
		Steps.Add({ 0.3f, [=, this]
		{
			const int32 SoundBefore = Sound()->GetPlayCount(EKakurenboSfx::Dash);
			SimulateKey(EKeys::LeftShift, IE_Pressed);
			SimulateKey(EKeys::LeftShift, IE_Released);
			SimulateKey(EKeys::W, IE_Pressed);
			NextTick([=, this]
			{
				AHiderCharacter* Hider = GetHider();
				const float Expected = 420.f * static_cast<float>(GM->GetPrestigeValue(EPrestigeUpgrade::DashSpeed, 1));
				Check(FString::Printf(TEXT("after unlocking, Shift starts a dash (speed %.0f, expected %.0f)"), Hider->GetCharacterMovement()->MaxWalkSpeed, Expected),
					Hider->IsDashing() && FMath::IsNearlyEqual(Hider->GetCharacterMovement()->MaxWalkSpeed, Expected, 1.f));
				Check(TEXT("dash makes the dash sound"), Sound()->GetPlayCount(EKakurenboSfx::Dash) == SoundBefore + 1);
				Check(FString::Printf(TEXT("the loud dash reaches the oni 13m away (intent=%s)"), *UEnum::GetValueAsString(TheOni()->GetIntent())), TheOni()->GetIntent() == EOniState::Investigate);
			});
		} });
		Steps.Add({ 0.5f, [=, this]
		{
			const float Speed = GetHider()->GetVelocity().Size2D();
			Check(FString::Printf(TEXT("running faster while dashing (%.0f cm/s)"), Speed), Speed > 520.f);
			Shot(TEXT("dash_01_dashing"));
			// クールタイム中はもう一度押してもダッシュしない
			const int32 SoundBefore = Sound()->GetPlayCount(EKakurenboSfx::Dash);
			SimulateKey(EKeys::LeftShift, IE_Pressed);
			SimulateKey(EKeys::LeftShift, IE_Released);
			NextTick([=, this]
			{
				Check(FString::Printf(TEXT("cooldown: no second dash right away (cooldown %.1fs)"), GetHider()->GetDashCooldownRemaining()),
					Sound()->GetPlayCount(EKakurenboSfx::Dash) == SoundBefore && GetHider()->GetDashCooldownRemaining() > 0.f);
			});
		} });
		Steps.Add({ 1.2f, [=, this]
		{
			SimulateKey(EKeys::W, IE_Released);
			AHiderCharacter* Hider = GetHider();
			Check(FString::Printf(TEXT("the dash ends and the speed goes back (%.0f)"), Hider->GetCharacterMovement()->MaxWalkSpeed),
				!Hider->IsDashing() && FMath::IsNearlyEqual(Hider->GetCharacterMovement()->MaxWalkSpeed, 420.f, 1.f));
			Shot(TEXT("dash_02_cooldown"));
			TheOni()->Deactivate();
		} });
		Steps.Add({ 6.5f, [=, this]
		{
			Check(FString::Printf(TEXT("after the cooldown (%.1fs), can dash again (%.1fs left)"), GetHider()->DashCooldown, GetHider()->GetDashCooldownRemaining()),
				GetHider()->GetDashCooldownRemaining() <= 0.f && FMath::IsNearlyEqual(GetHider()->DashCooldown, static_cast<float>(GM->GetPrestigeValue(EPrestigeUpgrade::DashCooldown, 0))));
		} });
	}
	// ================================================================ Prestige（転生）
	else if (Scenario.Equals(TEXT("Prestige"), ESearchCase::IgnoreCase))
	{
		// 転生：P を 2 回 → 転生ポイントで壁が硬くなる。コインなどは最初から、設計図は残って在庫を買えば直る
		Steps.Add({ 0.5f, [=, this] { KakuSkipTime(1000.f); } });
		Steps.Add({ 1.f, [=, this]
		{
			KakuNext(); // → 購入
			KakuAddCoins(5000.0);
			KakuBuy(1);
			BuyWall(0, 3);
		} });
		Steps.Add({ 0.5f, [=, this]
		{
			KakuNext(); // → 設置
			const FIntPoint Me = Grid()->WorldToCell(GetPawn()->GetActorLocation());
			KakuPlaceWall(Me.X + 2, Me.Y, 0);
			KakuPlaceWall(Me.X + 2, Me.Y + 1, 0);
			KakuPlaceWall(Me.X + 2, Me.Y + 2, 0);
			Check(TEXT("3 walls placed"), Grid()->GetBlockCount() == 3);
		} });
		Steps.Add({ 0.5f, [=, this] { KakuNext(); } }); // → かくれんぼ
		Steps.Add({ 0.5f, [=, this] { KakuSkipTime(1000.f); } });
		Steps.Add({ 1.f, [=, this]
		{
			KakuNext(); // → 購入
			// まだ転生できないステージでは P を押しても何も起きない
			GS()->Stage = GM->PrestigeSettings.MinStage - 1;
			SimulateKey(EKeys::P, IE_Pressed);
			SimulateKey(EKeys::P, IE_Released);
			NextTick([=, this]
			{
				Check(FString::Printf(TEXT("cannot prestige before stage %d"), GM->PrestigeSettings.MinStage), !GM->CanPrestige() && !IsPrestigeConfirmPending());
				GS()->Stage = GM->PrestigeSettings.MinStage + 1; // 転生ポイント 2
			});
		} });
		Steps.Add({ 0.3f, [=, this]
		{
			Check(FString::Printf(TEXT("can prestige for %d points"), GM->GetPrestigePointsOnReset()), GM->CanPrestige() && GM->GetPrestigePointsOnReset() == 2);
			SimulateKey(EKeys::P, IE_Pressed);
			SimulateKey(EKeys::P, IE_Released);
			NextTick([=, this]
			{
				Check(TEXT("the first P only asks to confirm"), IsPrestigeConfirmPending() && GS()->PrestigeCount == 0);
			});
			ShotLater(TEXT("prestige_01_confirm"));
		} });
		Steps.Add({ 0.6f, [=, this]
		{
			SimulateKey(EKeys::P, IE_Pressed);
			SimulateKey(EKeys::P, IE_Released);
			NextTick([=, this]
			{
				const AKakurenboGameState* S = GS();
				Check(FString::Printf(TEXT("the second P prestiges and gives points to spend (count %d, points %d, total %d)"), S->PrestigeCount, S->PrestigePoints, S->TotalPrestigePoints),
					S->PrestigeCount == 1 && S->PrestigePoints == 2 && S->TotalPrestigePoints == 2);
				Check(FString::Printf(TEXT("prestige alone does not make walls harder (x%.2f)"), GM->GetWallHPMultiplier()), FMath::IsNearlyEqual(GM->GetWallHPMultiplier(), 1.0));
				Check(FString::Printf(TEXT("everything else starts over (stage %d, coins %.1f, mash Lv%d, wood stock %d)"), S->Stage, S->Coins, S->MashIncomeLevel, S->WallStock[0]),
					S->Stage == 1 && S->Coins < 0.01 && S->MashIncomeLevel == 0 && S->WallStock[0] == 0);
				Check(FString::Printf(TEXT("after prestige, the prestige shop opens first (phase %s, prestige tab %d)"), *UEnum::GetValueAsString(S->Phase), bPrestigeShopTab ? 1 : 0),
					S->Phase == EKakurenboPhase::Shop && bPrestigeShopTab);
				Check(FString::Printf(TEXT("placed walls are gone but the layout is kept (blocks %d, missing %d)"), Grid()->GetBlockCount(), Grid()->GetTotalMissing()),
					Grid()->GetBlockCount() == 0 && Grid()->GetTotalMissing() == 3);
				Check(TEXT("prestige sound"), Sound()->GetPlayCount(EKakurenboSfx::Prestige) == 1);
			});
		} });
		Steps.Add({ 0.8f, [=] { Shot(TEXT("prestige_02_restart")); } });

		// 転生のお店：マウスでタブを切り替えて、商品をクリックで買う
		auto Click = ClickUI;
		Steps.Add({ 0.4f, [=, this]
		{
			Check(TEXT("the coin tab is clickable"), Click(EKakurenboUIAction::ShopTab, 0));
			NextTick([=, this]
			{
				Check(TEXT("clicking the coin tab opens the coin shop"), !bPrestigeShopTab);
				Click(EKakurenboUIAction::ShopTab, 1);
				NextTick([=, this] { Check(TEXT("clicking the prestige tab opens the prestige shop"), bPrestigeShopTab); });
			});
		} });
		Steps.Add({ 0.4f, [=, this]
		{
			const TArray<FShopItemView> Items = GM->GetPrestigeShopItems();
			Check(FString::Printf(TEXT("the prestige shop lists every permanent upgrade (%d)"), Items.Num()), Items.Num() == static_cast<int32>(EPrestigeUpgrade::Count) && Items[0].bPrestigeItem);
			Check(TEXT("dash and jump are locked at first"), !GM->IsDashUnlocked() && !GM->IsJumpUnlocked() && GetHider()->JumpMaxCount == 0 && !GetHider()->bDashUnlocked);
			Check(TEXT("click the wall hardness upgrade"), Click(EKakurenboUIAction::ShopItem, static_cast<int32>(EPrestigeUpgrade::WallHP)));
			NextTick([=, this]
			{
				Check(FString::Printf(TEXT("clicking buys it with points (Lv%d, %d pt left, walls x%.2f)"), GM->GetPrestigeLevel(EPrestigeUpgrade::WallHP), GS()->PrestigePoints, GM->GetWallHPMultiplier()),
					GM->GetPrestigeLevel(EPrestigeUpgrade::WallHP) == 1 && GS()->PrestigePoints == 1 && FMath::IsNearlyEqual(GM->GetWallHPMultiplier(), GM->GetPrestigeValue(EPrestigeUpgrade::WallHP, 1)));
				Shot(TEXT("prestige_03_prestige_shop"));
			});
		} });
		Steps.Add({ 0.5f, [=, this]
		{
			// 足りないと買えない（ジャンプは 2 pt）
			Click(EKakurenboUIAction::ShopItem, static_cast<int32>(EPrestigeUpgrade::Jump));
			NextTick([=, this]
			{
				Check(FString::Printf(TEXT("not enough points -> not bought (jump Lv%d, %d pt)"), GM->GetPrestigeLevel(EPrestigeUpgrade::Jump), GS()->PrestigePoints),
					GM->GetPrestigeLevel(EPrestigeUpgrade::Jump) == 0 && GS()->PrestigePoints == 1);
				// ほかの強化も確かめる（ポイントを足して、番号で買う）
				GS()->PrestigePoints += 20;
				const double TreasureBefore = GM->GetTreasureValue();
				KakuBuyPrestige(static_cast<int32>(EPrestigeUpgrade::Treasure) + 1);
				KakuBuyPrestige(static_cast<int32>(EPrestigeUpgrade::DashSpeed) + 1);
				KakuBuyPrestige(static_cast<int32>(EPrestigeUpgrade::DashCooldown) + 1);
				KakuBuyPrestige(static_cast<int32>(EPrestigeUpgrade::Jump) + 1);
				const int32 PointsAfter = GS()->PrestigePoints;
				KakuBuyPrestige(static_cast<int32>(EPrestigeUpgrade::Jump) + 1); // 最大レベルなのでもう買えない
				AHiderCharacter* Hider = GetHider();
				Check(FString::Printf(TEXT("treasure upgrade raises the treasure value (%.1f -> %.1f)"), TreasureBefore, GM->GetTreasureValue()),
					FMath::IsNearlyEqual(GM->GetTreasureValue(), TreasureBefore * GM->GetPrestigeValue(EPrestigeUpgrade::Treasure, 1) / GM->GetPrestigeValue(EPrestigeUpgrade::Treasure, 0), 0.01));
				Check(FString::Printf(TEXT("dash unlocked with its speed and cooldown (x%.2f, %.1fs)"), Hider->DashSpeedMultiplier, Hider->DashCooldown),
					Hider->bDashUnlocked && FMath::IsNearlyEqual(Hider->DashSpeedMultiplier, static_cast<float>(GM->GetPrestigeValue(EPrestigeUpgrade::DashSpeed, 1)), 0.001f)
					&& FMath::IsNearlyEqual(Hider->DashCooldown, static_cast<float>(GM->GetPrestigeValue(EPrestigeUpgrade::DashCooldown, 1)), 0.001f));
				Check(TEXT("jump unlocked"), Hider->JumpMaxCount == 1 && GM->IsJumpUnlocked());
				Check(TEXT("jump is maxed: cannot buy it again"), GS()->PrestigePoints == PointsAfter && GM->GetPrestigeLevel(EPrestigeUpgrade::Jump) == 1 && GM->GetPrestigeShopItems()[static_cast<int32>(EPrestigeUpgrade::Jump)].bMaxed);
			});
		} });
		Steps.Add({ 0.5f, [=, this]
		{
			Check(TEXT("click the coin shop tab"), Click(EKakurenboUIAction::ShopTab, 0));
			NextTick([=, this]
			{
				// コインのお店：クリックで木の壁を買う
				KakuAddCoins(100.0);
				Check(TEXT("back on the coin tab"), !bPrestigeShopTab && Click(EKakurenboUIAction::ShopItem, GM->GetShopIndexOfWall(0)));
				NextTick([=, this]
				{
					const TArray<FShopItemView> Items = GM->GetShopItems();
					Check(FString::Printf(TEXT("clicking a coin shop item buys it (wood stock %d)"), GS()->WallStock[0]), GS()->WallStock[0] == 1);
					Check(FString::Printf(TEXT("the shop shows the harder wall ('%s')"), *Items[GM->GetShopIndexOfWall(0)].Description.ToString()),
						Items[GM->GetShopIndexOfWall(0)].Description.ToString().Contains(UKakurenboLibrary::FormatStatNumber(GM->WallTypes[0].MaxHP * GM->GetWallHPMultiplier())));
				});
			});
		} });
		Steps.Add({ 0.5f, [=, this]
		{
			// 「設置パートへ」ボタン（設計図どおりに 1 個だけ直る）
			Check(TEXT("click the next button"), Click(EKakurenboUIAction::NextPhase, 0));
		} });
		Steps.Add({ 0.5f, [=, this]
		{
			double MaxHP = 0.0;
			for (int32 Y = 0; Y < Grid()->GetSizeY(); ++Y)
			{
				for (int32 X = 0; X < Grid()->GetSizeX(); ++X)
				{
					if (const APlaceableBlock* Block = Grid()->GetTopBlock(FIntPoint(X, Y)))
					{
						MaxHP = Block->MaxHP;
					}
				}
			}
			Check(FString::Printf(TEXT("the next button goes to the build phase (%s)"), *UEnum::GetValueAsString(GS()->Phase)), GS()->Phase == EKakurenboPhase::Build);
			Check(FString::Printf(TEXT("the kept layout is repaired from new stock with harder walls (repaired %d, HP %.2f)"), GS()->LastRepairedWalls, MaxHP),
				GS()->LastRepairedWalls == 1 && Grid()->GetTotalMissing() == 2 && FMath::IsNearlyEqual(MaxHP, GM->WallTypes[0].MaxHP * GM->GetWallHPMultiplier(), 0.001));
			bUseTestCursor = false;
		} });
		Steps.Add({ 0.6f, [=] { Shot(TEXT("prestige_04_build")); } });
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
			BuyWall(0, 2); // 木の壁 ×2
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
			int32 PrestigePoints = 0;
			TArray<int32> Stock;
			TArray<int32> TrapStock;
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
			KakuBuy(1);     // 連打強化
			// 転生して、転生のお店で壁の硬さ Lv2（耐久 ×1.5^2）を買い、3 pt 残っていることにする
			GS()->PrestigePoints = 3;
			GS()->TotalPrestigePoints = 6;
			GS()->PrestigeCount = 1;
			GS()->PrestigeLevels[static_cast<int32>(EPrestigeUpgrade::WallHP)] = 2;
			BuyWall(0, 3);  // 木の壁 ×3
			BuyWall(1, 1);  // 石の壁 ×1
			BuyTrap(0, 2);  // トリモチ ×2
		} });
		Steps.Add({ 0.5f, [=, this]
		{
			KakuNext(); // → 設置
			UKakurenboGridSubsystem* G = Grid();
			const FIntPoint Me = G->WorldToCell(GetPawn()->GetActorLocation());
			KakuPlaceWall(Me.X + 2, Me.Y, 0);
			KakuPlaceWall(Me.X + 2, Me.Y, 1); // 積む
			KakuPlaceWall(Me.X - 2, Me.Y + 1, 0);
			KakuPlaceTrap(Me.X, Me.Y + 2, 0);
		} });
		Steps.Add({ 0.5f, [=, this]
		{
			KakuNext(); // → かくれんぼ（開始時に保存される）
			const AKakurenboGameState* S = GS();
			Expected->Coins = S->Coins;
			Expected->Stage = S->Stage;
			Expected->MashLevel = S->MashIncomeLevel;
			Expected->PrestigePoints = S->PrestigePoints;
			Expected->Stock = S->WallStock;
			Expected->TrapStock = S->TrapStock;
			Grid()->ExportLayout(Expected->Layout);
			Expected->PlayerLocation = GetPawn()->GetActorLocation();
			Check(TEXT("save file exists after starting the round"), UGameplayStatics::DoesSaveGameExist(GM->SaveSlotName, 0));
			Check(FString::Printf(TEXT("test setup: 3 prestige points, 1 trap placed, 1 in stock (%d pt, traps %d, stock %d)"), S->PrestigePoints, Grid()->GetAllTraps().Num(), S->TrapStock.IsValidIndex(0) ? S->TrapStock[0] : -1),
				S->PrestigePoints == 3 && Grid()->GetAllTraps().Num() == 1 && S->TrapStock.IsValidIndex(0) && S->TrapStock[0] == 1);
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
			S->PrestigePoints = 0;
			S->TotalPrestigePoints = 0;
			S->PrestigeCount = 0;
			S->PrestigeLevels.Init(0, S->PrestigeLevels.Num());
			S->WallStock.Init(0, S->WallStock.Num());
			S->TrapStock.Init(0, S->TrapStock.Num());
			TeleportPlayer(FVector::ZeroVector);

			const bool bLoaded = GM->LoadProgress();
			TArray<FKakurenboSavedColumn> Layout;
			Grid()->ExportLayout(Layout);
			bool bSameLayout = Layout.Num() == Expected->Layout.Num();
			for (int32 i = 0; bSameLayout && i < Layout.Num(); ++i)
			{
				bSameLayout = Layout[i].Cell == Expected->Layout[i].Cell && Layout[i].Design == Expected->Layout[i].Design && Layout[i].Live == Expected->Layout[i].Live
					&& Layout[i].TrapDesign == Expected->Layout[i].TrapDesign && Layout[i].bTrapLive == Expected->Layout[i].bTrapLive;
			}
			// 転生の倍率をかけた耐久で作り直されているか（木の壁 1 × 1.5^2）
			double MinHP = TNumericLimits<double>::Max();
			for (int32 Y = 0; Y < Grid()->GetSizeY(); ++Y)
			{
				for (int32 X = 0; X < Grid()->GetSizeX(); ++X)
				{
					if (const APlaceableBlock* Block = Grid()->GetTopBlock(FIntPoint(X, Y)))
					{
						MinHP = FMath::Min(MinHP, Block->MaxHP);
					}
				}
			}
			Check(TEXT("save loads"), bLoaded);
			Check(FString::Printf(TEXT("coins / stage / upgrades / prestige restored (%.1f, %d, mash Lv%d, %d pt, %d times)"), S->Coins, S->Stage, S->MashIncomeLevel, S->PrestigePoints, S->PrestigeCount),
				FMath::IsNearlyEqual(S->Coins, Expected->Coins, 0.01) && S->Stage == Expected->Stage && S->MashIncomeLevel == Expected->MashLevel
				&& S->PrestigePoints == Expected->PrestigePoints && S->PrestigeCount == 1 && S->TotalPrestigePoints == 6
				&& GM->GetPrestigeLevel(EPrestigeUpgrade::WallHP) == 2);
			Check(FString::Printf(TEXT("wall stock restored (%s)"), *FString::JoinBy(S->WallStock, TEXT(","), [](int32 N) { return FString::FromInt(N); })), S->WallStock == Expected->Stock);
			Check(FString::Printf(TEXT("trap stock restored (%s)"), *FString::JoinBy(S->TrapStock, TEXT(","), [](int32 N) { return FString::FromInt(N); })), S->TrapStock == Expected->TrapStock);
			Check(FString::Printf(TEXT("wall and trap layout restored (%d columns, %d blocks, %d traps)"), Layout.Num(), Grid()->GetBlockCount(), Grid()->GetAllTraps().Num()),
				bSameLayout && Grid()->GetBlockCount() == 3 && Grid()->GetAllTraps().Num() == 1);
			Check(FString::Printf(TEXT("restored walls keep the prestige HP (min %.2f, expected %.2f)"), MinHP, GM->WallTypes[0].MaxHP * GM->GetPrestigeValue(EPrestigeUpgrade::WallHP, 2)),
				FMath::IsNearlyEqual(MinHP, GM->WallTypes[0].MaxHP * GM->GetPrestigeValue(EPrestigeUpgrade::WallHP, 2), 0.001));
			Check(FString::Printf(TEXT("player position restored (%.0f cm off)"), FVector::Dist(GetPawn()->GetActorLocation(), Expected->PlayerLocation)),
				FVector::Dist(GetPawn()->GetActorLocation(), Expected->PlayerLocation) < 5.f);
			Log(TEXT("loaded"));
		} });
		Steps.Add({ 0.5f, [=, this]
		{
			// 最初からやり直す
			GM->ResetProgress();
			const AKakurenboGameState* S = GS();
			Check(FString::Printf(TEXT("reset -> stage 1, no coins, no walls, no traps (stage %d, coins %.1f, blocks %d, traps %d)"), S->Stage, S->Coins, Grid()->GetBlockCount(), Grid()->GetAllTraps().Num()),
				S->Stage == 1 && S->Coins < 0.01 && Grid()->GetBlockCount() == 0 && Grid()->GetAllTraps().Num() == 0 && S->PrestigePoints == 0 && GM->GetPrestigeLevel(EPrestigeUpgrade::WallHP) == 0 && S->Phase == EKakurenboPhase::Hide);
			UGameplayStatics::DeleteGameInSlot(GM->SaveSlotName, 0);
			GM->bSaveEnabled = false;
			Shot(TEXT("save_01_after_reset"));
		} });
	}
	// ================================================================ BackToShop（設置パートから購入パートへ戻る）
	else if (Scenario.Equals(TEXT("BackToShop"), ESearchCase::IgnoreCase))
	{
		Steps.Add({ 0.5f, [=, this] { KakuSkipTime(1000.f); } });
		Steps.Add({ 1.f, [=, this]
		{
			KakuNext(); // → 購入
			KakuAddCoins(500.0);
			BuyWall(0, 2);
		} });
		Steps.Add({ 0.5f, [=, this]
		{
			KakuNext(); // → 設置
			const FIntPoint Me = Grid()->WorldToCell(GetPawn()->GetActorLocation());
			KakuPlaceWall(Me.X + 2, Me.Y, 0);
			Check(FString::Printf(TEXT("in the build phase with 1 wall placed (phase %s, blocks %d)"), *UEnum::GetValueAsString(GS()->Phase), Grid()->GetBlockCount()),
				GS()->Phase == EKakurenboPhase::Build && Grid()->GetBlockCount() == 1);
		} });
		Steps.Add({ 0.6f, [=, this]
		{
			Shot(TEXT("backtoshop_01_build"));
			Check(TEXT("the back-to-shop button is clickable"), ClickUI(EKakurenboUIAction::BackToShop, 0));
			NextTick([=, this]
			{
				Check(FString::Printf(TEXT("clicking it returns to the shop, the placed wall stays (phase %s, blocks %d, wood stock %d)"),
					*UEnum::GetValueAsString(GS()->Phase), Grid()->GetBlockCount(), GS()->WallStock[0]),
					GS()->Phase == EKakurenboPhase::Shop && Grid()->GetBlockCount() == 1 && GS()->WallStock[0] == 1);
			});
		} });
		Steps.Add({ 0.6f, [=, this]
		{
			Shot(TEXT("backtoshop_02_shop"));
			KakuNext(); // → もう一度設置
		} });
		Steps.Add({ 0.6f, [=, this]
		{
			Check(TEXT("back in the build phase"), GS()->Phase == EKakurenboPhase::Build);
			SimulateKey(EKeys::B, IE_Pressed);
			NextTick([=, this]
			{
				Check(FString::Printf(TEXT("the B key also returns to the shop (phase %s)"), *UEnum::GetValueAsString(GS()->Phase)), GS()->Phase == EKakurenboPhase::Shop);
				SimulateKey(EKeys::B, IE_Released);
			});
		} });
	}
	// ================================================================ Steps（足音・ジャンプの音・しのび足）
	else if (Scenario.Equals(TEXT("Steps"), ESearchCase::IgnoreCase))
	{
		struct FStepRecord { int32 Steps = 0; int32 Sounds = 0; int32 JumpSounds = 0; };
		TSharedRef<FStepRecord> Before = MakeShared<FStepRecord>();
		auto Listener = [GM](int32 Index) { return GM->GetOnis()[Index].Get(); };
		// 1) 広い場所を歩くと、足音が鬼に聞こえる（鬼は止めておき、音を聞いて向かおうとしたかだけ見る）
		Steps.Add({ 0.3f, [=] { SetStageOniTypes({ EOniType::Balanced, EOniType::Balanced }); } });
		Steps.Add({ 3.3f, [=, this]
		{
			for (int32 i = 0; i < 2; ++i)
			{
				AOniCharacter* Oni = Listener(i);
				Oni->SightRadius = 0.f;
				Oni->CloseSenseRadius = 0.f;
				Oni->HearingRadius = 0.f;
				Oni->DecoyHearingRadius = 0.f;
				Oni->PocketInspectChance = 0.f;
				Oni->NoiseInaccuracyCells = 0.f;
				Oni->InvestigateMaxDuration = 100.f;
				Oni->StepHearingRadius = 1200.f; // 足音（0.5）は 6m まで聞こえる
				Oni->GetCharacterMovement()->DisableMovement();
			}
			Listener(0)->SetActorLocation(Grid()->CellFloorCenter(FIntPoint(5, 12)) + FVector(0.f, 0.f, 100.f), false, nullptr, ETeleportType::TeleportPhysics);
			Listener(1)->SetActorLocation(Grid()->CellFloorCenter(FIntPoint(16, 19)) + FVector(0.f, 0.f, 100.f), false, nullptr, ETeleportType::TeleportPhysics);
			TeleportPlayer(Grid()->CellFloorCenter(FIntPoint(8, 12)));
			SetControlRotation(FRotator(-20.f, 0.f, 0.f)); // +X（鬼から離れる向き）へ歩く
			Before->Steps = GS()->StepsThisRound;
			Before->Sounds = Sound()->GetPlayCount(EKakurenboSfx::PlayerStep);
			SimulateKey(EKeys::W, IE_Pressed);
		} });
		Steps.Add({ 0.7f, [=, this]
		{
			SimulateKey(EKeys::W, IE_Released);
			Check(FString::Printf(TEXT("walking makes footsteps (%d steps, %d sounds)"), GS()->StepsThisRound - Before->Steps, Sound()->GetPlayCount(EKakurenboSfx::PlayerStep) - Before->Sounds),
				GS()->StepsThisRound - Before->Steps >= 2 && Sound()->GetPlayCount(EKakurenboSfx::PlayerStep) - Before->Sounds >= 2);
			Check(FString::Printf(TEXT("an oni nearby heard the footsteps (intent %s)"), *UEnum::GetValueAsString(Listener(0)->GetIntent())), Listener(0)->GetIntent() == EOniState::Investigate);
			Check(TEXT("open area: footsteps at full loudness"), FMath::IsNearlyEqual(GM->GetStepNoiseMultiplier(), 1.f));
		} });
		// 2) 壁に囲まれた中（消音壁でなくても）では足音が 80% 小さく、5m 先の鬼には聞こえない
		Steps.Add({ 0.3f, [=, this]
		{
			for (int32 X = 14; X <= 18; ++X)
			{
				for (int32 Y = 10; Y <= 14; ++Y)
				{
					if (X == 14 || X == 18 || Y == 10 || Y == 14)
					{
						PlaceTestWall(FIntPoint(X, Y));
					}
				}
			}
			TeleportPlayer(Grid()->CellFloorCenter(FIntPoint(15, 12)));
		} });
		Steps.Add({ 0.3f, [=, this]
		{
			Check(FString::Printf(TEXT("enclosed by plain walls: footsteps x%.2f (expected 0.2)"), GM->GetStepNoiseMultiplier()), FMath::IsNearlyEqual(GM->GetStepNoiseMultiplier(), 0.2f, 0.01f));
			Before->Steps = GS()->StepsThisRound;
			SimulateKey(EKeys::W, IE_Pressed);
		} });
		Steps.Add({ 0.45f, [=, this]
		{
			SimulateKey(EKeys::W, IE_Released);
			Check(FString::Printf(TEXT("walked inside the walls (%d steps), the oni 5m away did not hear (intent %s)"), GS()->StepsThisRound - Before->Steps, *UEnum::GetValueAsString(Listener(1)->GetIntent())),
				GS()->StepsThisRound - Before->Steps >= 1 && Listener(1)->GetIntent() != EOniState::Investigate);
			Shot(TEXT("steps_01_enclosed"));
		} });
		// 3) しのび足（Ctrl）：ゆっくり歩く
		Steps.Add({ 0.3f, [=, this] { bTestSneak = true; } });
		Steps.Add({ 0.2f, [=, this]
		{
			Check(FString::Printf(TEXT("sneaking walks slowly (speed %.0f)"), GetHider()->GetCharacterMovement()->MaxWalkSpeed),
				GetHider()->IsSneaking() && GetHider()->GetCharacterMovement()->MaxWalkSpeed < 300.f);
			bTestSneak = false;
		} });
		// 4) ジャンプ（転生のお店で解放）で音が出る
		Steps.Add({ 0.3f, [=, this]
		{
			Check(TEXT("sneaking stops when Ctrl is released"), !GetHider()->IsSneaking());
			GS()->PrestigeLevels[static_cast<int32>(EPrestigeUpgrade::Jump)] = 1;
			GM->ApplyPrestigeToPlayer();
			Before->JumpSounds = Sound()->GetPlayCount(EKakurenboSfx::PlayerJump);
			SimulateKey(EKeys::SpaceBar, IE_Pressed);
		} });
		Steps.Add({ 0.4f, [=, this]
		{
			SimulateKey(EKeys::SpaceBar, IE_Released);
			Check(FString::Printf(TEXT("jumping makes a sound (%d)"), Sound()->GetPlayCount(EKakurenboSfx::PlayerJump) - Before->JumpSounds),
				Sound()->GetPlayCount(EKakurenboSfx::PlayerJump) - Before->JumpSounds >= 1);
		} });
	}
	// ================================================================ TreasureOni（宝物鬼）
	else if (Scenario.Equals(TEXT("TreasureOni"), ESearchCase::IgnoreCase))
	{
		struct FPatrol { int32 Samples = 0; int32 Near = 0; int32 Patrolling = 0; };
		TSharedRef<FPatrol> Patrol = MakeShared<FPatrol>();
		Steps.Add({ 0.3f, [=] { SetStageOniTypes({ EOniType::Treasure }); } });
		Steps.Add({ 3.3f, [=, this]
		{
			AOniCharacter* Oni = GM->GetOnis()[0];
			Check(FString::Printf(TEXT("treasure oni: deaf to mashing, sensitive to footsteps (hearing %.0f / steps %.0f / base %.0f)"), Oni->HearingRadius, Oni->StepHearingRadius, GM->GetOniHearingRadius()),
				Oni->OniType == EOniType::Treasure && Oni->HearingRadius < GM->GetOniHearingRadius() * 0.5f && Oni->StepHearingRadius > GM->GetOniHearingRadius() * 1.5f);
			Check(FString::Printf(TEXT("treasure oni is weak at breaking walls (damage %.2f) and does not inspect hollows"), Oni->AttackDamage),
				Oni->AttackDamage < GM->GetOniAttackDamage() && Oni->PocketInspectChance <= 0.f);
			Oni->SightRadius = 0.f;
			Oni->CloseSenseRadius = 0.f;
			Oni->HearingRadius = 0.f;
			Oni->StepHearingRadius = 0.f;
			Oni->bDrawDebug = true;
			TeleportPlayer(Grid()->CellFloorCenter(FIntPoint(1, 1)));
		} });
		for (int32 i = 0; i < 28; ++i)
		{
			Steps.Add({ 0.5f, [=]
			{
				const AOniCharacter* Oni = GM->GetOnis()[0];
				const ATreasureActor* Treasure = Oni->GetPatrolTreasure();
				if (i >= 12) // 門からお宝まで歩く時間を除いて数える
				{
					++Patrol->Samples;
					Patrol->Patrolling += Treasure ? 1 : 0;
					Patrol->Near += (Treasure && FVector::Dist2D(Oni->GetActorLocation(), Treasure->GetActorLocation()) <= 380.f) ? 1 : 0;
				}
			} });
		}
		Steps.Add({ 0.1f, [=, this]
		{
			Check(FString::Printf(TEXT("treasure oni circles around treasures (patrolling %d, near %d of %d samples)"), Patrol->Patrolling, Patrol->Near, Patrol->Samples),
				Patrol->Patrolling == Patrol->Samples && Patrol->Near * 10 >= Patrol->Samples * 6);
			LookAtOni();
		} });
		Steps.Add({ 0.4f, [=, this] { Shot(TEXT("treasureoni_01")); } });
	}
	// ================================================================ Detector（探知鬼）
	else if (Scenario.Equals(TEXT("Detector"), ESearchCase::IgnoreCase))
	{
		struct FGuard { FVector PostLocation = FVector::ZeroVector; };
		TSharedRef<FGuard> Guard = MakeShared<FGuard>();
		auto Detector = [GM]() { return GM->GetOnis()[0].Get(); };
		Steps.Add({ 0.3f, [=] { SetStageOniTypes({ EOniType::Detector, EOniType::Balanced, EOniType::Balanced }); } });
		Steps.Add({ 3.3f, [=, this]
		{
			Check(FString::Printf(TEXT("a detector with a big summon radius (%.0f) and a guard post"), Detector()->SummonRadius),
				Detector()->OniType == EOniType::Detector && Detector()->SummonRadius >= 2000.f);
			for (AOniCharacter* Oni : GM->GetOnis())
			{
				Oni->SightRadius = 0.f; // まだ誰もプレイヤーを見ない
				Oni->CloseSenseRadius = 0.f;
				Oni->HearingRadius = 0.f;
				Oni->StepHearingRadius = 0.f;
				Oni->DecoyHearingRadius = 0.f;
				Oni->PocketInspectChance = 0.f;
			}
			TeleportPlayer(Grid()->CellFloorCenter(FIntPoint(1, 1)));
		} });
		Steps.Add({ 9.f, [=, this]
		{
			Check(FString::Printf(TEXT("the detector reached its post (%d,%d)"), Detector()->GetGuardCell().X, Detector()->GetGuardCell().Y), Detector()->IsAtGuardPost());
			Guard->PostLocation = Detector()->GetActorLocation();
		} });
		Steps.Add({ 3.f, [=, this]
		{
			const float Moved = FVector::Dist2D(Detector()->GetActorLocation(), Guard->PostLocation);
			Check(FString::Printf(TEXT("the detector stays at its post (moved %.0f cm in 3s)"), Moved), Moved < 30.f);
			// 他の鬼を探知鬼の近くへ。プレイヤーを探知鬼の正面 4 マス先へ出して、見つけさせる
			AOniCharacter* Watcher = Detector();
			const FIntPoint Post = Grid()->WorldToCell(Watcher->GetActorLocation());
			for (int32 i = 1; i < GM->GetOnis().Num(); ++i)
			{
				const FIntPoint Near(FMath::Clamp(Post.X + (i == 1 ? -6 : 6), 1, Grid()->GetSizeX() - 2), FMath::Clamp(Post.Y + 5, 1, Grid()->GetSizeY() - 2));
				GM->GetOnis()[i]->SetActorLocation(Grid()->CellFloorCenter(Near) + FVector(0.f, 0.f, 100.f), false, nullptr, ETeleportType::TeleportPhysics);
			}
			Watcher->SightRadius = 1500.f;
			const FVector Fwd = Watcher->GetActorForwardVector().GetSafeNormal2D();
			FIntPoint Spot = Post;
			for (int32 Dist = 4; Dist >= 2; --Dist)
			{
				const FIntPoint C = Grid()->WorldToCell(Watcher->GetActorLocation() + Fwd * Dist * 100.f);
				if (Grid()->IsWalkable(C))
				{
					Spot = C;
					break;
				}
			}
			TeleportPlayer(Grid()->CellFloorCenter(Spot));
		} });
		Steps.Add({ 0.5f, [=, this]
		{
			int32 Coming = 0;
			for (int32 i = 1; i < GM->GetOnis().Num(); ++i)
			{
				Coming += GM->GetOnis()[i]->GetIntent() == EOniState::Investigate ? 1 : 0;
			}
			Check(FString::Printf(TEXT("the detector saw the player and called the others (summoned %d, coming %d, summons %d)"), GM->GetLastSummonedCount(), Coming, GS()->SummonsThisRound),
				GS()->SummonsThisRound >= 1 && GM->GetLastSummonedCount() == 2 && Coming == 2);
			Check(TEXT("the detector itself does not chase"), Detector()->GetIntent() != EOniState::Chase);
			Shot(TEXT("detector_01_summon"));
		} });
	}
	// ================================================================ CarefulSweep（慎重鬼：広い場所から手分けして調べ、囲まれた場所と細い道は後回し）
	else if (Scenario.Equals(TEXT("CarefulSweep"), ESearchCase::IgnoreCase))
	{
		struct FSweep { int32 Samples = 0; int32 EarlyInspect = 0; int32 Inspect = 0; int32 SameInspect = 0; int32 Split = 0; int32 InCorridor = 0; };
		TSharedRef<FSweep> Sweep = MakeShared<FSweep>();
		const FIntPoint Donut(5, 5);
		Steps.Add({ 0.3f, [=, this]
		{
			SetStageOniTypes({ EOniType::Careful, EOniType::Careful });
			GS()->HideTimeRemaining = GS()->HideTimeLimit = 300.f; // 調べ終わるまで時間切れにしない
			// 中が空洞のドーナツ形の壁と、幅 1 マスの細い道（Y=19 の X 3〜10）
			for (int32 DY = -1; DY <= 1; ++DY)
			{
				for (int32 DX = -1; DX <= 1; ++DX)
				{
					if (DX != 0 || DY != 0)
					{
						PlaceTestWall(Donut + FIntPoint(DX, DY));
					}
				}
			}
			for (int32 X = 3; X <= 10; ++X)
			{
				PlaceTestWall(FIntPoint(X, 18));
				PlaceTestWall(FIntPoint(X, 20));
			}
			TeleportPlayer(Grid()->CellFloorCenter(FIntPoint(22, 1)));
		} });
		Steps.Add({ 3.3f, [=]
		{
			for (AOniCharacter* Oni : GM->GetOnis())
			{
				Oni->SightRadius = 0.f;
				Oni->CloseSenseRadius = 0.f;
				Oni->HearingRadius = 0.f;
				Oni->StepHearingRadius = 0.f;
				Oni->bDrawDebug = true;
			}
		} });
		for (int32 i = 0; i < 100; ++i)
		{
			Steps.Add({ 0.5f, [=, this]
			{
				if (GM->GetOnis().Num() < 2)
				{
					return;
				}
				const AOniCharacter* A = GM->GetOnis()[0];
				const AOniCharacter* B = GM->GetOnis()[1];
				const bool bA = A->GetIntent() == EOniState::Inspect || A->GetOniState() == EOniState::Attack;
				const bool bB = B->GetIntent() == EOniState::Inspect || B->GetOniState() == EOniState::Attack;
				++Sweep->Samples;
				if (i < 16)
				{
					Sweep->EarlyInspect += (bA || bB) ? 1 : 0; // 最初の 8 秒は広い場所を見ているはず
				}
				Sweep->Inspect += (bA || bB) ? 1 : 0;
				Sweep->SameInspect += (bA && bB) ? 1 : 0; // 空洞は 1 つなので、2 体同時に向かうのはだめ
				const FIntPoint CA = Grid()->WorldToCell(A->GetActorLocation());
				const FIntPoint CB = Grid()->WorldToCell(B->GetActorLocation());
				if (i >= 6)
				{
					Sweep->Split += ((CA.Y < Grid()->GetSizeY() / 2) != (CB.Y < Grid()->GetSizeY() / 2)) ? 1 : 0;
				}
				for (const FIntPoint& C : { CA, CB })
				{
					Sweep->InCorridor += (C.Y == 19 && C.X >= 3 && C.X <= 10) ? 1 : 0;
				}
				if (i % 20 == 19)
				{
					Log(FString::Printf(TEXT("t=%.0fs inspect %d same %d split %d corridor %d"), (i + 1) * 0.5f, Sweep->Inspect, Sweep->SameInspect, Sweep->Split, Sweep->InCorridor));
				}
			} });
		}
		Steps.Add({ 0.1f, [=, this]
		{
			Check(FString::Printf(TEXT("careful onis first look over the open area, not the hollow (inspect samples in the first 8s: %d)"), Sweep->EarlyInspect), Sweep->EarlyInspect == 0);
			Check(FString::Printf(TEXT("after the open area, they inspect the hollow (%d samples)"), Sweep->Inspect), Sweep->Inspect >= 1);
			Check(FString::Printf(TEXT("never both on the same hollow (%d)"), Sweep->SameInspect), Sweep->SameInspect == 0);
			Check(FString::Printf(TEXT("they split the stage (different halves %d of %d)"), Sweep->Split, Sweep->Samples - 6), Sweep->Split * 10 >= (Sweep->Samples - 6) * 5);
			Check(FString::Printf(TEXT("they rarely walk the narrow passage (%d of %d oni-samples)"), Sweep->InCorridor, Sweep->Samples * 2), Sweep->InCorridor * 10 <= Sweep->Samples * 2);
			Shot(TEXT("carefulsweep_01"));
		} });
	}
	// ================================================================ Maps（館のマップ）
	else if (Scenario.Equals(TEXT("Maps"), ESearchCase::IgnoreCase))
	{
		struct FMapState { int32 WoodBefore = 0; };
		TSharedRef<FMapState> MapState = MakeShared<FMapState>();
		Steps.Add({ 0.5f, [=, this]
		{
			Check(FString::Printf(TEXT("maps are loaded (%d) and stage 1 uses %s"), GM->MapOrder.Num(), *GM->GetCurrentMapName().ToString()),
				GM->MapOrder.Num() >= 5 && GM->GetCurrentMapName() == GM->GetMapNameForStage(1));
			Check(FString::Printf(TEXT("stages pick maps: 1 %s / 4 %s / 7 %s / 10 %s / 13 %s / 16 %s"), *GM->GetMapNameForStage(1).ToString(), *GM->GetMapNameForStage(4).ToString(),
				*GM->GetMapNameForStage(7).ToString(), *GM->GetMapNameForStage(10).ToString(), *GM->GetMapNameForStage(13).ToString(), *GM->GetMapNameForStage(16).ToString()),
				GM->GetMapNameForStage(1) == FName(TEXT("Hall")) && GM->GetMapNameForStage(4) == FName(TEXT("Library")) && GM->GetMapNameForStage(7) == FName(TEXT("Rooms"))
				&& GM->GetMapNameForStage(10) == FName(TEXT("Storeroom")) && GM->GetMapNameForStage(13) == FName(TEXT("Gallery")) && GM->GetMapNameForStage(16) == FName(TEXT("Hall")));
			KakuSkipTime(1000.f);
		} });
		Steps.Add({ 1.f, [=, this] { KakuNext(); KakuAddCoins(500.0); BuyWall(0, 2); } });
		Steps.Add({ 0.5f, [=, this]
		{
			KakuNext(); // → 設置（真上から）
			GM->bMapOverride = true;
			const FIntPoint Me = Grid()->WorldToCell(GetPawn()->GetActorLocation());
			KakuPlaceWall(Me.X + 2, Me.Y, 0);
			KakuPlaceWall(Me.X - 2, Me.Y, 0);
			MapState->WoodBefore = GS()->WallStock[0];
			Check(FString::Printf(TEXT("2 walls placed in the first map (blocks %d)"), Grid()->GetBlockCount()), Grid()->GetBlockCount() == 2);
		} });
		for (int32 m = 0; m < 5; ++m)
		{
			Steps.Add({ 0.8f, [=, this]
			{
				if (!GM->MapOrder.IsValidIndex(m))
				{
					return;
				}
				const FName Name = GM->MapOrder[m];
				GM->MapOverride = Name;
				GM->SwitchToMap(Name);
				UKakurenboGridSubsystem* G = Grid();
				// 鬼の出入り口から、プレイヤーのいる場所まで歩いて行けるか（家具で閉じ込められていないか）
				FKakurenboPathGrid Walk = G->BuildWalkGrid();
				TArray<FIntPoint> Path;
				const FIntPoint Gate = GM->GetOniGateCells()[0];
				const FIntPoint Me = G->WorldToCell(GetPawn()->GetActorLocation());
				const bool bReach = KakurenboPathfinding::FindPath(Walk, Gate, Me, Path);
				bool bGateFree = true;
				for (const FIntPoint& C : GM->GetOniGateCells())
				{
					bGateFree &= !G->IsObstacle(C);
				}
				const int32 MainCells = G->GetMainAreaMask().FilterByPredicate([](bool b) { return b; }).Num();
				const AKakurenboArena* Arena = GM->GetArena();
				Check(FString::Printf(TEXT("map %s: %d furniture pieces (%d meshes), %d blocked cells, open %d, gate free %d, reachable %d, player cell walkable %d"),
					*Name.ToString(), Arena->GetFurniturePieceCount(), Arena->GetFurnitureMeshCount(), G->GetObstacleCount(), MainCells, bGateFree, bReach, G->IsWalkable(Me)),
					Arena->GetFurniturePieceCount() > 5 && G->GetObstacleCount() > 10 && MainCells >= 300 && bGateFree && bReach && G->IsWalkable(Me));
				const bool bHasFab = FPackageName::DoesPackageExist(TEXT("/Game/Stylized_Library/Meshes/SM_Bookcase_01"));
				Check(FString::Printf(TEXT("map %s: furniture uses the Fab meshes when they are in the project (fab %d, meshes %d)"), *Name.ToString(), bHasFab, Arena->GetFurnitureMeshCount()),
					!bHasFab || Arena->GetFurnitureMeshCount() > 0);
				if (m == 1)
				{
					// 2 つ目のマップに移ったとき：前のマップの壁は在庫に戻っている
					Check(FString::Printf(TEXT("walls of the previous map went back to stock (wood %d -> %d, blocks %d)"), MapState->WoodBefore, GS()->WallStock[0], G->GetBlockCount()),
						GS()->WallStock[0] == MapState->WoodBefore + 2 && G->GetBlockCount() == 0);
					// 家具のマスには置けない
					for (int32 Y = 0; Y < G->GetSizeY(); ++Y)
					{
						for (int32 X = 0; X < G->GetSizeX(); ++X)
						{
							if (G->IsObstacle(FIntPoint(X, Y)))
							{
								FText Reason;
								Check(FString::Printf(TEXT("cannot place a wall on furniture (%s)"), *Reason.ToString()), !GM->CanPlaceWall(FIntPoint(X, Y), 0, &Reason) && !Reason.IsEmpty());
								Y = G->GetSizeY();
								break;
							}
						}
					}
				}
			} });
			Steps.Add({ 0.8f, [=, this] { Shot(FString::Printf(TEXT("maps_%02d_%s"), m + 1, GM->MapOrder.IsValidIndex(m) ? *GM->MapOrder[m].ToString() : TEXT("none"))); } });
		}
		Steps.Add({ 0.5f, [=, this]
		{
			// 最初のマップに戻ると、設計図が戻る（壁は在庫から設置パートで直る）
			GM->MapOverride = GM->MapOrder[0];
			GM->SwitchToMap(GM->MapOrder[0]);
			Check(FString::Printf(TEXT("the first map's design comes back (missing %d)"), Grid()->GetTotalMissing()), Grid()->GetTotalMissing() == 2);
			KakuShop();
		} });
		Steps.Add({ 0.5f, [=, this]
		{
			KakuNext(); // → 設置（在庫から直る）
		} });
		Steps.Add({ 0.5f, [=, this]
		{
			Check(FString::Printf(TEXT("walls rebuilt from stock (blocks %d, repaired %d)"), Grid()->GetBlockCount(), GS()->LastRepairedWalls), Grid()->GetBlockCount() == 2 && GS()->LastRepairedWalls == 2);
		} });
	}
	// ================================================================ Mood（館の見た目を三人称で撮る）
	else if (Scenario.Equals(TEXT("Mood"), ESearchCase::IgnoreCase))
	{
		Steps.Add({ 0.3f, [=] { SetStageOniTypes({ EOniType::Balanced, EOniType::Treasure, EOniType::Detector }); } });
		Steps.Add({ 1.0f, [=, this] { SetControlRotation(FRotator(-15.f, 180.f, 0.f)); } });
		Steps.Add({ 1.0f, [=, this] { Shot(TEXT("mood_01_hall_west")); } });
		Steps.Add({ 1.5f, [=, this]
		{
			// 鬼は止めて、プレイヤーの前（門の方）に並べる
			const FIntPoint Me = Grid()->WorldToCell(GetPawn()->GetActorLocation());
			for (int32 i = 0; i < GM->GetOnis().Num(); ++i)
			{
				AOniCharacter* Oni = GM->GetOnis()[i];
				Oni->Deactivate();
				Oni->SetActorLocation(Grid()->CellFloorCenter(Me + FIntPoint(4 + i, i * 2 - 2)) + FVector(0.f, 0.f, 100.f), false, nullptr, ETeleportType::TeleportPhysics);
				Oni->SetActorRotation(FRotator(0.f, 180.f, 0.f));
			}
			SetControlRotation(FRotator(-15.f, 0.f, 0.f)); // 門の方
		} });
		Steps.Add({ 1.5f, [=, this] { Shot(TEXT("mood_02_hall_gate")); SetControlRotation(FRotator(-35.f, 90.f, 0.f)); } });
		Steps.Add({ 1.0f, [=, this] { Shot(TEXT("mood_03_hall_north")); } });
		Steps.Add({ 0.3f, [=, this]
		{
			// 書庫の中から
			GM->bMapOverride = true;
			GM->MapOverride = TEXT("Library");
			GM->SwitchToMap(TEXT("Library"));
			TeleportPlayer(Grid()->CellFloorCenter(FIntPoint(3, 4)));
			SetControlRotation(FRotator(-15.f, 0.f, 0.f));
		} });
		Steps.Add({ 1.5f, [=, this] { Shot(TEXT("mood_04_library")); } });
	}
	// ================================================================ Sounds（鬼の足音・お宝のキラキラ・BGM）
	else if (Scenario.Equals(TEXT("Sounds"), ESearchCase::IgnoreCase))
	{
		struct FSoundCount { int32 OniSteps = 0; int32 Sparkles = 0; };
		TSharedRef<FSoundCount> Counts = MakeShared<FSoundCount>();
		Steps.Add({ 0.5f, [=, this]
		{
			SetStageOniTypes({ EOniType::Balanced });
			Check(FString::Printf(TEXT("hide phase plays the hide BGM (%s)"), *UEnum::GetValueAsString(Sound()->GetCurrentMusic())), Sound()->GetCurrentMusic() == EKakurenboMusic::Hide);
			Counts->Sparkles = Sound()->GetPlayCount(EKakurenboSfx::TreasureSparkle);
		} });
		Steps.Add({ 3.f, [=, this]
		{
			Check(FString::Printf(TEXT("treasures sparkle (%d sounds in 3s)"), Sound()->GetPlayCount(EKakurenboSfx::TreasureSparkle) - Counts->Sparkles),
				Sound()->GetPlayCount(EKakurenboSfx::TreasureSparkle) - Counts->Sparkles >= GM->GetTreasures().Num());
			if (AOniCharacter* Oni = KeepOnlyOni(0))
			{
				Oni->SightRadius = 0.f;
				Oni->CloseSenseRadius = 0.f;
				Oni->HearingRadius = 0.f;
				Oni->StepHearingRadius = 0.f;
				Oni->DebugGoTo(FIntPoint(4, 4));
			}
			TeleportPlayer(Grid()->CellFloorCenter(FIntPoint(2, 20)));
			Counts->OniSteps = Sound()->GetPlayCount(EKakurenboSfx::OniStep);
		} });
		Steps.Add({ 2.f, [=, this]
		{
			const AOniCharacter* Oni = GM->GetOnis()[0];
			Check(FString::Printf(TEXT("a walking oni stomps (%d step sounds in 2s, oni counted %d)"), Sound()->GetPlayCount(EKakurenboSfx::OniStep) - Counts->OniSteps, Oni->GetStepSoundCount()),
				Sound()->GetPlayCount(EKakurenboSfx::OniStep) - Counts->OniSteps >= 4 && Oni->GetStepSoundCount() >= 4);
			KakuSkipTime(1000.f);
		} });
		Steps.Add({ 1.f, [=, this]
		{
			Check(FString::Printf(TEXT("result plays the calm BGM (%s)"), *UEnum::GetValueAsString(Sound()->GetCurrentMusic())), Sound()->GetCurrentMusic() == EKakurenboMusic::Calm);
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
