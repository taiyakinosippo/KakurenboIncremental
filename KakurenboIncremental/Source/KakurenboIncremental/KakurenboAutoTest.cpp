// 自動テスト（KakuAutoTest コマンド）の実装。
// 実際にゲームを動かしながら操作を時間差で実行し、ログとスクリーンショットで結果を確認する。
// Tools/RunAutoTest.ps1 -Scenario <名前> から起動する。
//
// 結果の判定は "[AutoTest] CHECK OK / CHECK FAILED" としてログに出す。
// 疑似入力（SimulateKey / SimulateMouse）の結果は、次のフレームの入力処理で反映されるので NextTick で確かめる。

#include "Camera/CameraComponent.h"
#include "DrawDebugHelpers.h"
#include "Misc/FileHelper.h"
#include "Components/PointLightComponent.h"
#include "Engine/PointLight.h"
#include "Components/CapsuleComponent.h"
#include "Components/PoseableMeshComponent.h"
#include "Animation/AnimSequence.h"
#include "Components/StaticMeshComponent.h"
#include "Containers/Ticker.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/OverlapResult.h"
#include "EngineUtils.h"
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
#include "KakurenboOniStudio.h"
#include "KakurenboPlayerController.h"
#include "KakurenboSaveGame.h"
#include "KakurenboSettings.h"
#include "KakurenboSoundSubsystem.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "OniCharacter.h"
#include "FakeHiderCharacter.h"
#include "GridPathfinder.h"
#include "PlaceableBlock.h"
#include "SmokeCloud.h"
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

	// タイトルを抜けて始める（セーブがあればつづきから、無ければはじめから）。タイトルそのものを確かめるシナリオだけはタイトルのまま
	const bool bTitleScenario = Scenario.Equals(TEXT("Title"), ESearchCase::IgnoreCase);
	if (!bTitleScenario)
	{
		CloseAllMenus();
		GM->LeaveTitleForTest();
	}

	// 館の家具があると、テストで壁を置くマスや鬼の通り道が塞がることがあるので、家具の無い舞台で行う。
	// マップそのものを確かめるシナリオ（と再起動をまたぐセーブ）だけは本物のマップを使う
	const bool bUsesMaps = Scenario.Equals(TEXT("Maps"), ESearchCase::IgnoreCase) || Scenario.Equals(TEXT("Mood"), ESearchCase::IgnoreCase)
		|| Scenario.Equals(TEXT("SaveRun1"), ESearchCase::IgnoreCase) || Scenario.Equals(TEXT("SaveRun2"), ESearchCase::IgnoreCase)
		|| Scenario.Equals(TEXT("CrouchWalk"), ESearchCase::IgnoreCase) || Scenario.Equals(TEXT("CameraWall"), ESearchCase::IgnoreCase)
		|| Scenario.Equals(TEXT("CarefulFind"), ESearchCase::IgnoreCase) || Scenario.Equals(TEXT("ShelfGap"), ESearchCase::IgnoreCase)
		|| Scenario.Equals(TEXT("BuildRay"), ESearchCase::IgnoreCase) || bTitleScenario;
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
	// 実時間で待ってから実行する（一時停止中はワールドのタイマーが止まるので、メニューのテストはこちらで進める）
	auto RealDelay = [](float Seconds, TFunction<void()> Fn)
	{
		TSharedRef<float> Left = MakeShared<float>(Seconds);
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Left, Fn = MoveTemp(Fn)](float Dt) mutable -> bool
		{
			*Left -= Dt;
			if (*Left > 0.f)
			{
				return true;
			}
			Fn();
			return false;
		}));
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
		// 2 個目の壁は 15 秒前後で壊れることが多いので、少し余裕を持って待つ（制限時間 30 秒の中で）
		for (int32 i = 0; i < 20; ++i)
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
			// ジャンプは無い。よじ登るのは最初はできない（転生のお店で解放する）
			SimulateKey(EKeys::SpaceBar, IE_Pressed);
			SimulateKey(EKeys::SpaceBar, IE_Released);
			const int32 MashBefore = GS()->MashCountThisRound;
			SimulateKey(EKeys::LeftMouseButton, IE_Pressed);
			SimulateKey(EKeys::LeftMouseButton, IE_Released);
			NextTick([=, this]
			{
				const float VelZ = GetHider()->GetCharacterMovement()->Velocity.Z;
				Check(FString::Printf(TEXT("Space does not jump or climb before climbing is unlocked (vz=%.0f, climbing %d)"), VelZ, GetHider()->IsClimbing()),
					VelZ < 50.f && !GetHider()->IsClimbing() && !GM->IsClimbUnlocked());
				Check(FString::Printf(TEXT("left click mashes (count %d -> %d)"), MashBefore, GS()->MashCountThisRound), GS()->MashCountThisRound == MashBefore + 1);
			});
		} });
		Steps.Add({ 0.3f, [=, this]
		{
			GS()->PrestigeLevels[static_cast<int32>(EPrestigeUpgrade::Climb)] = 1; // 転生のお店で買ったことにする
			GM->ApplyPrestigeToPlayer();
			SimulateKey(EKeys::SpaceBar, IE_Pressed);
			SimulateKey(EKeys::SpaceBar, IE_Released);
			NextTick([=, this]
			{
				const float VelZ = GetHider()->GetCharacterMovement()->Velocity.Z;
				Check(FString::Printf(TEXT("after unlocking, Space still does not jump in the open (vz=%.0f, climbing %d)"), VelZ, GetHider()->IsClimbing()),
					VelZ < 50.f && !GetHider()->IsClimbing());
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
			Check(FString::Printf(TEXT("a treasure gives gems, not coins (%d gems)"), GM->GetGemsPerTreasure()), GM->GetGemsPerTreasure() >= 1);
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
				Items.Num() == 3 + GM->WallTypes.Num() + GM->GetTrapTypesInShop(false).Num() && Items.Num() <= 9);
			Check(TEXT("item 3 is the wall reinforcement, then walls, then traps"),
				GM->GetShopIndexOfWall(0) == 3 && Items[2].DisplayName.EqualTo(GM->WallReinforceName)
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
			Check(FString::Printf(TEXT("soundproof wall prices grow much faster than normal walls, traps faster still (wall x%.2f, quiet x%.2f, sticky x%.2f)"),
				GM->WallTypes[0].CostGrowth, Quiet != INDEX_NONE ? GM->WallTypes[Quiet].CostGrowth : 0.0, GM->TrapTypes[0].CostGrowth),
				Quiet != INDEX_NONE && GM->WallTypes[Quiet].CostGrowth >= 1.4 && GM->WallTypes[0].CostGrowth < GM->WallTypes[Quiet].CostGrowth
				&& GM->TrapTypes[0].CostGrowth >= GM->WallTypes[Quiet].CostGrowth);
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
			// 追いかけてくる鬼は、どの種類でも目が赤く光る：全員をその場で追いかけさせる（動けないようにして、捕まらないように）
			for (int32 i = 0; i < GM->GetOnis().Num(); ++i)
			{
				AOniCharacter* Each = GM->GetOnis()[i];
				Each->SetActorLocation(Grid()->CellFloorCenter(Grid()->WorldToCell(GetPawn()->GetActorLocation()) + FIntPoint(4, i * 2 - 5)) + FVector(0.f, 0.f, 100.f), false, nullptr, ETeleportType::TeleportPhysics);
				Each->SetActorRotation(FRotator(0.f, 180.f, 0.f));
				Each->Activate(GetHider());
				Each->SightRadius = 3000.f;
				Each->SightHalfAngle = 180.f;
				Each->HearingRadius = 0.f;
				Each->WanderSpeed = Each->InvestigateSpeed = Each->ChaseSpeed = 1.f;
			}
		} });
		Steps.Add({ 1.0f, [=, this]
		{
			int32 Chasing = 0, Glowing = 0, Eyes = 0;
			for (const AOniCharacter* Oni : GM->GetOnis())
			{
				Chasing += Oni->GetIntent() == EOniState::Chase ? 1 : 0;
				Glowing += Oni->GetEyeGlowAmount() > 0.6f ? 1 : 0;
				Eyes += Oni->HasGlowingEyes() ? 1 : 0;
			}
			// 探知鬼は追いかけない（見つけたら仲間を呼ぶ）が、見つけている間は同じように目が光る
			Check(FString::Printf(TEXT("every oni that found the player glows red eyes (chasing %d, glowing %d, with eyes %d of %d)"), Chasing, Glowing, Eyes, GM->GetOnis().Num()),
				Chasing == GM->GetOnis().Num() - 1 && Glowing == GM->GetOnis().Num() && Eyes == GM->GetOnis().Num());
			GetHider()->ThirdPersonDistance = 300.f;
			SetControlRotation(FRotator(-8.f, 0.f, 0.f));
		} });
		Steps.Add({ 0.8f, [=, this] { Shot(TEXT("look_03_red_eyes")); } });
		Steps.Add({ 0.1f, [=, this]
		{
			// 見失って（追いかけるのをやめて）しばらくすると、元の目に戻る
			for (AOniCharacter* Oni : GM->GetOnis())
			{
				Oni->Deactivate();
			}
		} });
		Steps.Add({ 1.2f, [=, this]
		{
			int32 Glowing = 0;
			for (const AOniCharacter* Oni : GM->GetOnis())
			{
				Glowing += Oni->GetEyeGlowAmount() > 0.2f ? 1 : 0;
			}
			Check(FString::Printf(TEXT("eyes go back to normal when they stop chasing (%d still glowing)"), Glowing), Glowing == 0);
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
				Oni->HearingRadius = 1000.f / GM->MashNoiseLoudness; // 連打がそのまま 10m まで届く鬼。消音壁 80% なら 2m
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
		// Shift で煙幕ダッシュ：足元に煙幕を投げて、しばらく速くなる。大きな音が出る（連打より遠くまで届く）。
		// 1 ラウンドに使える回数が決まっている（クールタイムは無い）。煙の向こうは鬼から見えない
		auto TheOni = [GM]() -> AOniCharacter* { return GM->GetOnis().Num() > 0 ? GM->GetOnis()[0].Get() : nullptr; };
		auto SmokeCount = [this]()
		{
			int32 Count = 0;
			for (TActorIterator<ASmokeCloud> It(GetWorld()); It; ++It)
			{
				++Count;
			}
			return Count;
		};
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
				// 煙幕が残る時間は「煙幕ダッシュ」のレベルで 1 秒〜7 秒
				Check(FString::Printf(TEXT("smoke lasts 1s at Lv1 and 7s at most (Lv1 %.1f, Lv4 %.1f, Lv9 %.1f)"), GM->GetSmokeDuration(1), GM->GetSmokeDuration(4), GM->GetSmokeDuration(9)),
					FMath::IsNearlyEqual(GM->GetSmokeDuration(1), 1.f) && FMath::IsNearlyEqual(GM->GetSmokeDuration(4), 7.f) && GM->GetSmokeDuration(9) <= 7.f
					&& GM->GetSmokeDuration(2) > GM->GetSmokeDuration(1));
				// 転生のお店で煙幕ダッシュを買ったことにする（煙幕の数は Lv0 = 1 ラウンドに 1 回）。
				// 煙がすぐ消えると後の確認がしにくいので、残る時間は一番長い Lv4（7 秒）にする
				GS()->PrestigeLevels[static_cast<int32>(EPrestigeUpgrade::SmokeDuration)] = 4;
				GM->ApplyPrestigeToPlayer();
				Check(FString::Printf(TEXT("unlocking gives this round's uses (%d left, %d per round)"), GetHider()->GetDashUsesLeft(), GM->GetSmokeUsesPerRound()),
					GetHider()->GetDashUsesLeft() == GM->GetSmokeUsesPerRound() && GM->GetSmokeUsesPerRound() == 1);
			});
		} });
		Steps.Add({ 0.3f, [=, this]
		{
			const int32 SoundBefore = Sound()->GetPlayCount(EKakurenboSfx::Dash);
			const int32 SmokeSoundBefore = Sound()->GetPlayCount(EKakurenboSfx::Smoke);
			SimulateKey(EKeys::LeftShift, IE_Pressed);
			SimulateKey(EKeys::LeftShift, IE_Released);
			SimulateKey(EKeys::W, IE_Pressed);
			NextTick([=, this]
			{
				AHiderCharacter* Hider = GetHider();
				const float Expected = 420.f * Hider->DashSpeedMultiplier;
				Check(FString::Printf(TEXT("after unlocking, Shift starts a dash (speed %.0f, expected %.0f)"), Hider->GetCharacterMovement()->MaxWalkSpeed, Expected),
					Hider->IsDashing() && FMath::IsNearlyEqual(Hider->GetCharacterMovement()->MaxWalkSpeed, Expected, 1.f) && Expected > 500.f);
				Check(TEXT("dash makes the dash sound"), Sound()->GetPlayCount(EKakurenboSfx::Dash) == SoundBefore + 1);
				float CloudDuration = -1.f;
				for (TActorIterator<ASmokeCloud> It(GetWorld()); It; ++It)
				{
					CloudDuration = It->Duration;
				}
				Check(FString::Printf(TEXT("a smoke cloud is thrown at the feet (%d clouds, smoke sound %d, lasts %.1fs)"), SmokeCount(), Sound()->GetPlayCount(EKakurenboSfx::Smoke) - SmokeSoundBefore, CloudDuration),
					SmokeCount() == 1 && Sound()->GetPlayCount(EKakurenboSfx::Smoke) == SmokeSoundBefore + 1 && FMath::IsNearlyEqual(CloudDuration, GM->GetSmokeDuration()));
				Check(FString::Printf(TEXT("one use is spent (%d left)"), Hider->GetDashUsesLeft()), Hider->GetDashUsesLeft() == 0);
				Check(FString::Printf(TEXT("the loud dash reaches the oni 13m away (intent=%s)"), *UEnum::GetValueAsString(TheOni()->GetIntent())), TheOni()->GetIntent() == EOniState::Investigate);
			});
		} });
		Steps.Add({ 0.5f, [=, this]
		{
			const float Speed = GetHider()->GetVelocity().Size2D();
			Check(FString::Printf(TEXT("running faster while dashing (%.0f cm/s)"), Speed), Speed > 520.f);
			// 前へ走ると煙はカメラとプレイヤーの間に残る。それでも見える（以前は隠していたので、煙が出ていないように見えた）
			int32 Visible = 0;
			bool bTranslucent = false;
			for (TActorIterator<ASmokeCloud> It(GetWorld()); It; ++It)
			{
				Visible += It->GetVisiblePuffCount();
				bTranslucent |= It->IsTranslucent();
			}
			const bool bHasSmokeMaterial = FPackageName::DoesPackageExist(TEXT("/Game/Kakurenbo/Materials/M_KakuSmoke"));
			Check(FString::Printf(TEXT("the smoke stays visible behind the running player (%d puffs visible, translucent %d)"), Visible, bTranslucent ? 1 : 0),
				Visible >= 6 && (!bHasSmokeMaterial || bTranslucent));
			Shot(TEXT("dash_01_dashing"));
		} });
		Steps.Add({ 1.2f, [=, this]
		{
			SimulateKey(EKeys::W, IE_Released);
			AHiderCharacter* Hider = GetHider();
			Check(FString::Printf(TEXT("the dash ends and the speed goes back (%.0f)"), Hider->GetCharacterMovement()->MaxWalkSpeed),
				!Hider->IsDashing() && FMath::IsNearlyEqual(Hider->GetCharacterMovement()->MaxWalkSpeed, 420.f, 1.f));
			// 回数を使い切ったら、もう一度押してもダッシュしない（クールタイムで戻ったりしない）
			const int32 SoundBefore = Sound()->GetPlayCount(EKakurenboSfx::Dash);
			SimulateKey(EKeys::LeftShift, IE_Pressed);
			SimulateKey(EKeys::LeftShift, IE_Released);
			NextTick([=, this]
			{
				Check(TEXT("no uses left: Shift does nothing"), !GetHider()->IsDashing() && Sound()->GetPlayCount(EKakurenboSfx::Dash) == SoundBefore && SmokeCount() == 1);
			});
			// 振り返って煙幕を撮る
			SetControlRotation(FRotator(-15.f, -90.f, 0.f));
			ShotLater(TEXT("dash_02_smoke"));
		} });

		// 煙幕の向こうは見えない：追いかけてくる鬼の前で煙幕ダッシュすると見失う
		Steps.Add({ 1.0f, [=, this]
		{
			// 1 回目の煙幕から離れた場所で。鬼はプレイヤーの後ろ 6m、プレイヤーは鬼に背を向ける
			const FIntPoint Me(Grid()->GetSizeX() - 6, 5);
			TeleportPlayer(Grid()->CellFloorCenter(Me));
			SetControlRotation(FRotator(-20.f, -90.f, 0.f));
			AOniCharacter* Oni = TheOni();
			Oni->SightRadius = 3000.f;
			Oni->SightHalfAngle = 180.f;
			Oni->CloseSenseRadius = 150.f;
			Oni->HearingRadius = 0.f;
			Oni->SetActorLocation(Grid()->CellFloorCenter(Me + FIntPoint(0, 6)) + FVector(0.f, 0.f, 100.f), false, nullptr, ETeleportType::TeleportPhysics);
			// 「煙幕の数」を 1 つ上げたことにする（ラウンドの途中でも、増えた分は使える）
			GS()->PrestigeLevels[static_cast<int32>(EPrestigeUpgrade::SmokeCount)] = 1;
			GM->ApplyPrestigeToPlayer();
			Check(FString::Printf(TEXT("buying one more smoke adds a use (%d left)"), GetHider()->GetDashUsesLeft()), GetHider()->GetDashUsesLeft() == 1);
		} });
		Steps.Add({ 0.4f, [=, this]
		{
			Check(FString::Printf(TEXT("the oni behind sees the player and chases (intent=%s)"), *UEnum::GetValueAsString(TheOni()->GetIntent())),
				TheOni()->GetIntent() == EOniState::Chase && TheOni()->IsTargetInSight());
			SimulateKey(EKeys::LeftShift, IE_Pressed);
			SimulateKey(EKeys::LeftShift, IE_Released);
			SimulateKey(EKeys::W, IE_Pressed);
		} });
		Steps.Add({ 0.4f, [=, this]
		{
			Check(FString::Printf(TEXT("smoke between them: the oni loses sight (in sight=%d, clouds=%d)"), TheOni()->IsTargetInSight() ? 1 : 0, SmokeCount()),
				!TheOni()->IsTargetInSight() && SmokeCount() >= 1);
			Check(TEXT("still hiding"), GS()->Phase == EKakurenboPhase::Hide);
			ShotLater(TEXT("dash_03_smoke_blocks"));
		} });
		Steps.Add({ 1.2f, [=, this]
		{
			SimulateKey(EKeys::W, IE_Released);
			TheOni()->Deactivate();
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
				Check(FString::Printf(TEXT("everything else starts over (stage %d, mash Lv%d, wood stock %d)"), S->Stage, S->MashIncomeLevel, S->WallStock[0]),
					S->Stage == 1 && S->MashIncomeLevel == 0 && S->WallStock[0] == 0);
				// はじめのコインは転生した回数ぶん増える（強化ではなく自動）
				Check(FString::Printf(TEXT("after the 1st prestige you start with %.0f coins (expected %.0f = 1 x %.0f)"), S->Coins, GM->GetPrestigeStartCoins(1), GM->PrestigeSettings.StartCoinsPerPrestige),
					S->Coins > 0.0 && FMath::IsNearlyEqual(S->Coins, GM->GetPrestigeStartCoins(1)) && FMath::IsNearlyEqual(GM->GetPrestigeStartCoins(3), GM->GetPrestigeStartCoins(1) * 3.0));
				Check(FString::Printf(TEXT("after prestige, the prestige shop opens first (phase %s, prestige tab %d)"), *UEnum::GetValueAsString(S->Phase), IsPrestigeShopTab() ? 1 : 0),
					S->Phase == EKakurenboPhase::Shop && IsPrestigeShopTab());
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
				Check(TEXT("clicking the coin tab opens the coin shop"), !IsPrestigeShopTab());
				Click(EKakurenboUIAction::ShopTab, 1);
				NextTick([=, this] { Check(TEXT("clicking the prestige tab opens the prestige shop"), IsPrestigeShopTab()); });
			});
		} });
		Steps.Add({ 0.4f, [=, this]
		{
			const TArray<FShopItemView> Items = GM->GetPrestigeShopItems();
			Check(FString::Printf(TEXT("the prestige shop lists every permanent upgrade (%d)"), Items.Num()), Items.Num() == static_cast<int32>(EPrestigeUpgrade::Count) && Items[0].bPrestigeItem);
			Check(TEXT("dash and climbing are locked at first"), !GM->IsDashUnlocked() && !GM->IsClimbUnlocked() && GetHider()->JumpMaxCount == 0 && !GetHider()->bDashUnlocked && !GetHider()->bClimbUnlocked);
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
			Click(EKakurenboUIAction::ShopItem, static_cast<int32>(EPrestigeUpgrade::Climb));
			NextTick([=, this]
			{
				Check(FString::Printf(TEXT("not enough points -> not bought (climb Lv%d, %d pt)"), GM->GetPrestigeLevel(EPrestigeUpgrade::Climb), GS()->PrestigePoints),
					GM->GetPrestigeLevel(EPrestigeUpgrade::Climb) == 0 && GS()->PrestigePoints == 1);
				// ほかの強化も確かめる（ポイントを足して、番号で買う）
				GS()->PrestigePoints += 20;
				const int32 TreasureBefore = GM->GetGemsPerTreasure();
				KakuBuyPrestige(static_cast<int32>(EPrestigeUpgrade::Treasure) + 1);
				KakuBuyPrestige(static_cast<int32>(EPrestigeUpgrade::SmokeDuration) + 1);
				KakuBuyPrestige(static_cast<int32>(EPrestigeUpgrade::SmokeCount) + 1);
				KakuBuyPrestige(static_cast<int32>(EPrestigeUpgrade::Climb) + 1);
				const int32 PointsAfter = GS()->PrestigePoints;
				KakuBuyPrestige(static_cast<int32>(EPrestigeUpgrade::Climb) + 1); // 最大レベルなのでもう買えない
				AHiderCharacter* Hider = GetHider();
				Check(FString::Printf(TEXT("treasure upgrade gives one more gem per treasure (%d -> %d)"), TreasureBefore, GM->GetGemsPerTreasure()), GM->GetGemsPerTreasure() == TreasureBefore + 1);
				Check(FString::Printf(TEXT("smoke dash unlocked with its smoke time and uses (%.1fs, %d per round)"), GM->GetSmokeDuration(), Hider->DashUsesPerRound),
					Hider->bDashUnlocked && FMath::IsNearlyEqual(GM->GetSmokeDuration(), GM->MinSmokeDuration)
					&& Hider->DashUsesPerRound == GM->GetSmokeUsesPerRound(1) && GM->GetSmokeUsesPerRound(1) == GM->GetSmokeUsesPerRound(0) + 1);
				Check(TEXT("climbing unlocked (and still no jumping)"), Hider->bClimbUnlocked && Hider->JumpMaxCount == 0 && GM->IsClimbUnlocked());
				Check(TEXT("climbing is maxed: cannot buy it again"), GS()->PrestigePoints == PointsAfter && GM->GetPrestigeLevel(EPrestigeUpgrade::Climb) == 1 && GM->GetPrestigeShopItems()[static_cast<int32>(EPrestigeUpgrade::Climb)].bMaxed);
			});
		} });
		Steps.Add({ 0.5f, [=, this]
		{
			Check(TEXT("click the coin shop tab"), Click(EKakurenboUIAction::ShopTab, 0));
			NextTick([=, this]
			{
				// コインのお店：クリックで木の壁を買う
				KakuAddCoins(100.0);
				Check(TEXT("back on the coin tab"), !IsPrestigeShopTab() && Click(EKakurenboUIAction::ShopItem, GM->GetShopIndexOfWall(0)));
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
			// 家具の無い、置けるマスを 2 つ探して置く（館には家具がある）
			int32 Placed = 0;
			for (int32 Y = 1; Y < Grid()->GetSizeY() - 1 && Placed < 2; ++Y)
			{
				for (int32 X = 1; X < Grid()->GetSizeX() - 4 && Placed < 2; ++X)
				{
					if (GM->CanPlaceWall(FIntPoint(X, Y), 0))
					{
						KakuPlaceWall(X, Y, 0);
						++Placed;
						X += 3;
					}
				}
			}
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
			// 1 回目：ステージ 1 の逃げ切り報酬 ＋ 500 − 通常壁 2 個（1 個目と 2 個目の値段）
			const double ExpectedCoins = GM->GetStageSettingsFor(1).ClearReward + 500.0 - GM->WallTypes[0].Cost * (1.0 + GM->WallTypes[0].CostGrowth);
			Check(FString::Printf(TEXT("stage and coins carried over (stage %d, coins %.2f, expected 2 / %.2f)"), S->Stage, S->Coins, ExpectedCoins),
				S->Stage == 2 && FMath::Abs(S->Coins - ExpectedCoins) < 1.0);
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
		// 全部回収ボタン：1 回目は確認だけ、2 回目で置いた壁が全部在庫に戻り、設計図も消える
		Steps.Add({ 0.6f, [=, this] { KakuNext(); } });
		Steps.Add({ 0.6f, [=, this]
		{
			Check(TEXT("the clear-all button is clickable"), ClickUI(EKakurenboUIAction::ClearAll, 0));
			NextTick([=, this]
			{
				Check(FString::Printf(TEXT("the first click only asks to confirm (blocks %d, pending %d)"), Grid()->GetBlockCount(), IsClearAllConfirmPending()),
					Grid()->GetBlockCount() == 1 && IsClearAllConfirmPending());
				Shot(TEXT("backtoshop_03_clear_confirm"));
			});
		} });
		Steps.Add({ 0.4f, [=, this]
		{
			ClickUI(EKakurenboUIAction::ClearAll, 0);
			NextTick([=, this]
			{
				Check(FString::Printf(TEXT("the second click picks up everything (blocks %d, design %d, wood stock %d)"), Grid()->GetBlockCount(), Grid()->GetTotalMissing(), GS()->WallStock[0]),
					Grid()->GetBlockCount() == 0 && Grid()->GetTotalMissing() == 0 && GS()->WallStock[0] == 2);
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
			// しのび足はしゃがむ：体の高さが半分になり、鬼が狙う頭の点も低い家具（テーブル 80cm）より下になる
			TArray<FVector> Points;
			GetHider()->GetSightTargetPoints(Points);
			const float Floor = GetHider()->GetActorLocation().Z - GetHider()->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
			Check(FString::Printf(TEXT("sneaking crouches (crouched %d, height %.0f, head point %.0f cm above the floor)"), GetHider()->bIsCrouched,
				GetHider()->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() * 2.f, Points.Num() > 0 ? Points[0].Z - Floor : -1.f),
				GetHider()->bIsCrouched && Points.Num() > 0 && Points[0].Z - Floor < 80.f);
			Shot(TEXT("steps_crouch"));
			bTestSneak = false;
		} });
		// 4) よじ登る（転生のお店で解放）と音が出る：目の前に壁を置いて、そちらを向いてよじ登る
		Steps.Add({ 0.3f, [=, this]
		{
			Check(FString::Printf(TEXT("sneaking stops when Ctrl is released and the player stands up (crouched %d)"), GetHider()->bIsCrouched), !GetHider()->IsSneaking() && !GetHider()->bIsCrouched);
			GS()->PrestigeLevels[static_cast<int32>(EPrestigeUpgrade::Climb)] = 1;
			GM->ApplyPrestigeToPlayer();
			const FIntPoint Me = Grid()->WorldToCell(GetPawn()->GetActorLocation());
			TeleportPlayer(Grid()->CellFloorCenter(Me));
			for (const FIntPoint Dir : { FIntPoint(-1, 0), FIntPoint(0, 1), FIntPoint(0, -1), FIntPoint(1, 0) })
			{
				if (Grid()->IsWalkable(Me + Dir) && !Grid()->IsReservedCell(Me + Dir))
				{
					PlaceTestWall(Me + Dir);
					GetPawn()->SetActorRotation(FRotator(0.f, FVector(Dir.X, Dir.Y, 0.f).Rotation().Yaw, 0.f));
					break;
				}
			}
		} });
		Steps.Add({ 0.3f, [=, this]
		{
			Before->JumpSounds = Sound()->GetPlayCount(EKakurenboSfx::PlayerClimb);
			SimulateKey(EKeys::SpaceBar, IE_Pressed);
		} });
		Steps.Add({ 0.4f, [=, this]
		{
			SimulateKey(EKeys::SpaceBar, IE_Released);
			Check(FString::Printf(TEXT("climbing makes a sound (%d, climbs %d)"), Sound()->GetPlayCount(EKakurenboSfx::PlayerClimb) - Before->JumpSounds, GetHider()->GetClimbCount()),
				Sound()->GetPlayCount(EKakurenboSfx::PlayerClimb) - Before->JumpSounds >= 1 && GetHider()->GetClimbCount() >= 1);
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
			Check(FString::Printf(TEXT("treasure oni: ignores mashing and decoys, very sensitive to footsteps (hearing %.0f / decoy %.0f / steps %.0f / base %.0f)"), Oni->HearingRadius, Oni->DecoyHearingRadius, Oni->StepHearingRadius, GM->GetOniHearingRadius()),
				Oni->OniType == EOniType::Treasure && Oni->HearingRadius <= 0.f && Oni->DecoyHearingRadius <= 0.f && Oni->StepHearingRadius >= GM->GetOniHearingRadius() * 2.5f);
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
			Steps.Add({ 0.5f, [=, this]
			{
				const AOniCharacter* Oni = GM->GetOnis()[0];
				const bool bPatrolling = Oni->IsPatrollingTreasure();
				if (i >= 12) // 門からお宝まで歩く時間を除いて数える
				{
					++Patrol->Samples;
					Patrol->Patrolling += bPatrolling ? 1 : 0;
					Patrol->Near += (bPatrolling && FVector::Dist2D(Oni->GetActorLocation(), Grid()->CellFloorCenter(Oni->GetPatrolCenter())) <= 380.f) ? 1 : 0;
				}
			} });
		}
		Steps.Add({ 0.1f, [=, this]
		{
			Check(FString::Printf(TEXT("treasure oni circles around treasures (patrolling %d, near %d of %d samples)"), Patrol->Patrolling, Patrol->Near, Patrol->Samples),
				Patrol->Patrolling == Patrol->Samples && Patrol->Near * 10 >= Patrol->Samples * 6);
			LookAtOni();
		} });
		Steps.Add({ 0.4f, [=, this]
		{
			Shot(TEXT("treasureoni_01"));
			// お宝を全部取る → 宝物鬼は取られたお宝のあった場所を見回り続ける
			const TArray<TObjectPtr<ATreasureActor>> All = GM->GetTreasures();
			for (ATreasureActor* Treasure : All)
			{
				GM->CollectTreasure(Treasure);
			}
			Patrol->Samples = 0;
			Patrol->Patrolling = 0;
			Patrol->Near = 0;
		} });
		for (int32 i = 0; i < 20; ++i)
		{
			Steps.Add({ 0.5f, [=, this]
			{
				const AOniCharacter* Oni = GM->GetOnis()[0];
				const bool bPatrolling = Oni->IsPatrollingTreasure() && GM->GetTakenTreasureCells().Contains(Oni->GetPatrolCenter());
				if (i >= 4)
				{
					++Patrol->Samples;
					Patrol->Patrolling += bPatrolling ? 1 : 0;
					Patrol->Near += (bPatrolling && FVector::Dist2D(Oni->GetActorLocation(), Grid()->CellFloorCenter(Oni->GetPatrolCenter())) <= 380.f) ? 1 : 0;
				}
			} });
		}
		Steps.Add({ 0.1f, [=, this]
		{
			Check(FString::Printf(TEXT("after all treasures are taken, the treasure oni keeps patrolling where they were (taken %d, patrolling %d, near %d of %d)"),
				GM->GetTakenTreasureCells().Num(), Patrol->Patrolling, Patrol->Near, Patrol->Samples),
				GM->GetTreasures().Num() == 0 && GM->GetTakenTreasureCells().Num() >= 2 && Patrol->Patrolling == Patrol->Samples && Patrol->Near * 10 >= Patrol->Samples * 5);
		} });
	}
	// ================================================================ TreasureBreak（宝物鬼は、お宝への最短経路を塞ぐ壁を壊して進む）
	else if (Scenario.Equals(TEXT("TreasureBreak"), ESearchCase::IgnoreCase))
	{
		const FIntPoint Center(8, 12);
		Steps.Add({ 0.3f, [=] { SetStageOniTypes({ EOniType::Treasure }); } });
		Steps.Add({ 3.3f, [=, this]
		{
			TeleportPlayer(Grid()->CellFloorCenter(FIntPoint(2, 2)));
			// お宝を全部、壁の箱（7×7 の外周）の中へ。箱には入り口が無い
			const TArray<TObjectPtr<ATreasureActor>> All = GM->GetTreasures();
			for (int32 i = 0; i < All.Num(); ++i)
			{
				All[i]->SetActorLocation(Grid()->CellFloorCenter(Center + FIntPoint(i % 2, i / 2)));
			}
			for (int32 D = -3; D <= 3; ++D)
			{
				for (const FIntPoint& Cell : { Center + FIntPoint(D, -3), Center + FIntPoint(D, 3), Center + FIntPoint(-3, D), Center + FIntPoint(3, D) })
				{
					if (Grid()->GetColumnHeight(Cell) == 0)
					{
						Grid()->PlaceBlock(Cell, 0, 0.1, GM->WallTypes[0].Color);
					}
				}
			}
			if (AOniCharacter* Oni = KeepOnlyOni(0))
			{
				Oni->SightRadius = 0.f;
				Oni->CloseSenseRadius = 0.f;
				Oni->HearingRadius = 0.f;
				Oni->StepHearingRadius = 0.f;
			}
			Check(FString::Printf(TEXT("the treasures are walled in (pockets %d)"), Grid()->FindEnclosedPockets().Num()), Grid()->FindEnclosedPockets().Num() >= 1);
		} });
		Steps.Add({ 18.f, [=, this]
		{
			const AOniCharacter* Oni = GM->GetOnis()[0];
			const float Dist = FVector::Dist2D(Oni->GetActorLocation(), Grid()->CellFloorCenter(Center));
			Check(FString::Printf(TEXT("the treasure oni broke through the walls to the treasures (walls destroyed %d, distance %.0f cm)"), GS()->LastRoundWallsDestroyed, Dist),
				GS()->LastRoundWallsDestroyed >= 1 && Dist < 350.f);
			LookAtOni();
		} });
		Steps.Add({ 0.5f, [=, this] { Shot(TEXT("treasurebreak_01")); } });
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
				// 呼ばれた場所へ向かっている（そのままプレイヤーが見えて追いかけ始めていてもよい）
				const EOniState Intent = GM->GetOnis()[i]->GetIntent();
				Coming += (Intent == EOniState::Investigate || Intent == EOniState::Chase) ? 1 : 0;
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
		struct FMapState { int32 WoodBefore = 0; FName FirstMap; bool bChecked = false; };
		TSharedRef<FMapState> MapState = MakeShared<FMapState>();
		Steps.Add({ 0.5f, [=, this]
		{
			Check(FString::Printf(TEXT("maps are loaded (%d) and stage 1 uses %s (%dx%d)"), GM->MapOrder.Num(), *GM->GetCurrentMapName().ToString(), Grid()->GetSizeX(), Grid()->GetSizeY()),
				GM->MapOrder.Num() >= 20 && GM->GetCurrentMapName() == GM->GetMapNameForStage(1) && Grid()->GetSizeX() == 16 && Grid()->GetSizeY() == 16);
			// M12：ステージごとに違うマップ（大きさもステージごとに大きくなる）。表より後は一番大きい館 3 つを 1 ステージずつ回る
			Check(FString::Printf(TEXT("stages pick maps: 1 %s / 4 %s / 10 %s / 25 %s / 26 %s / 27 %s / 28 %s / 29 %s"), *GM->GetMapNameForStage(1).ToString(), *GM->GetMapNameForStage(4).ToString(),
				*GM->GetMapNameForStage(10).ToString(), *GM->GetMapNameForStage(25).ToString(), *GM->GetMapNameForStage(26).ToString(), *GM->GetMapNameForStage(27).ToString(),
				*GM->GetMapNameForStage(28).ToString(), *GM->GetMapNameForStage(29).ToString()),
				GM->GetMapNameForStage(1) == FName(TEXT("Parlor")) && GM->GetMapNameForStage(4) == FName(TEXT("Library")) && GM->GetMapNameForStage(10) == FName(TEXT("Hall"))
				&& GM->GetMapNameForStage(25) == FName(TEXT("Crown")) && GM->GetMapNameForStage(26) == FName(TEXT("Vault")) && GM->GetMapNameForStage(27) == FName(TEXT("Cloister"))
				&& GM->GetMapNameForStage(28) == FName(TEXT("Crown")) && GM->GetMapNameForStage(29) == FName(TEXT("Vault")));
			KakuSkipTime(1000.f);
		} });
		Steps.Add({ 1.f, [=, this] { KakuNext(); KakuAddCoins(500.0); BuyWall(0, 2); } });
		Steps.Add({ 0.5f, [=, this]
		{
			KakuNext(); // → 設置（真上から）
			GM->bMapOverride = true;
			// 家具の無い、置けるマスを 2 つ探して置く（最初の館は家具が多い）
			int32 Placed = 0;
			for (int32 Y = 1; Y < Grid()->GetSizeY() - 1 && Placed < 2; ++Y)
			{
				for (int32 X = 1; X < Grid()->GetSizeX() - 4 && Placed < 2; ++X)
				{
					if (GM->CanPlaceWall(FIntPoint(X, Y), 0) && !Grid()->IsObstacle(FIntPoint(X + 1, Y)))
					{
						KakuPlaceWall(X, Y, 0);
						++Placed;
						X += 3;
					}
				}
			}
			MapState->WoodBefore = GS()->WallStock[0];
			MapState->FirstMap = GM->GetCurrentMapName(); // ステージ 2 の館（ステージ 1 を逃げ切ったので変わっている）
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
					Arena->GetFurniturePieceCount() > 5 && G->GetObstacleCount() > 10 && MainCells * 2 >= G->GetSizeX() * G->GetSizeY() && bGateFree && bReach && G->IsWalkable(Me));
					// 館の大きさは間取りのとおり（グリッドと舞台の両方）。床・壁は Fab の木の模様（あれば）
					const bool bHasWood = FPackageName::DoesPackageExist(TEXT("/Game/Kakurenbo/Wood/Wood01_Base_Color"));
					// 家具のメッシュはマスからはみ出さない（はみ出すと、空いて見えるマスに壁が置けない）
					Check(FString::Printf(TEXT("map %s: furniture stays inside its cells (max overhang %.1f cm)"), *Name.ToString(), Arena->GetMaxFurnitureOverhang()), Arena->GetMaxFurnitureOverhang() < 8.f);
					// 2×2 のテーブルなどが、マスの境目にまたがって真ん中にぽつんと置かれていない（マスごとに 1 個ずつ）
					Check(FString::Printf(TEXT("map %s: no furniture mesh sits on a cell line without filling the cells (%d)"), *Name.ToString(), Arena->GetFurnitureStraddleCount()),
						Arena->GetFurnitureStraddleCount() == 0);
					Check(FString::Printf(TEXT("map %s is %dx%d (arena %dx%d), wood floor %d, wood walls %d"), *Name.ToString(), G->GetSizeX(), G->GetSizeY(), Arena->GridSizeX, Arena->GridSizeY,
						Arena->IsFloorUsingMaterial(), Arena->IsWallUsingPanels()),
						G->GetSizeX() == Arena->GridSizeX && G->GetSizeY() == Arena->GridSizeY && (!bHasWood || (Arena->IsFloorUsingMaterial() && Arena->IsWallUsingPanels())));
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
		Steps.Add({ 0.3f, [=, this]
		{
			// 全部の館で：家具はマスからはみ出さず、マスの境目にまたがって真ん中にぽつんと置かれていない
			FString Problems;
			for (const FName& Name : GM->MapOrder)
			{
				GM->MapOverride = Name;
				GM->SwitchToMap(Name);
				const AKakurenboArena* Arena = GM->GetArena();
				if (Arena->GetFurnitureStraddleCount() > 0 || Arena->GetMaxFurnitureOverhang() >= 8.f)
				{
					Problems += FString::Printf(TEXT(" %s(straddle %d, overhang %.0f)"), *Name.ToString(), Arena->GetFurnitureStraddleCount(), Arena->GetMaxFurnitureOverhang());
				}
			}
			Check(FString::Printf(TEXT("all %d maps keep every furniture mesh in its own cells:%s"), GM->MapOrder.Num(), Problems.IsEmpty() ? TEXT(" ok") : *Problems), Problems.IsEmpty());
			// M14：音の届く距離は館の大きさに合わせる（小さい館ほど小さい。連打の音が館じゅうに届かないように）
			GM->MapOverride = TEXT("Crown");
			GM->SwitchToMap(TEXT("Crown"));
			const float CrownScale = GM->GetMapNoiseScale();
			GM->MapOverride = TEXT("Parlor");
			GM->SwitchToMap(TEXT("Parlor"));
			const float ParlorScale = GM->GetMapNoiseScale();
			Check(FString::Printf(TEXT("noise reaches less far in small mansions (Parlor x%.2f, Crown x%.2f, hearing %.0f)"), ParlorScale, CrownScale, GM->GetOniHearingRadius()),
				FMath::IsNearlyEqual(ParlorScale, GM->MapNoiseScaleSmall, 0.01f) && FMath::IsNearlyEqual(CrownScale, 1.f, 0.01f)
				&& FMath::IsNearlyEqual(GM->GetOniHearingRadius(), GM->GetStageSettings().OniHearingRadius * ParlorScale, 1.f));
			// 2×2 のテーブルがある館（書斎）を上から撮る
			GM->MapOverride = TEXT("Study");
			GM->SwitchToMap(TEXT("Study"));
		} });
		Steps.Add({ 1.0f, [=, this] { Shot(TEXT("maps_98_study_tables")); } });
		Steps.Add({ 0.5f, [=, this]
		{
			// 最初のマップに戻ると、設計図が戻る（壁は在庫から設置パートで直る）
			// 部屋の壁のある館：部屋の壁は外周と同じ高さまである（上に乗れない・隣の部屋が見えない）
			GM->MapOverride = TEXT("Rooms");
			GM->SwitchToMap(TEXT("Rooms"));
			Check(FString::Printf(TEXT("room walls go up to the ceiling (%d tall parts, overhang %.1f cm)"), GM->GetArena()->GetTallRoomWallCount(), GM->GetArena()->GetMaxFurnitureOverhang()),
				GM->GetArena()->GetTallRoomWallCount() > 0 && GM->GetArena()->GetMaxFurnitureOverhang() < 8.f);
			GM->MapOverride = MapState->FirstMap;
			GM->SwitchToMap(MapState->FirstMap);
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
			// 置いた木の壁が床（木）と見分けられるか、近くから斜めに撮る
			for (int32 Y = 0; Y < Grid()->GetSizeY(); ++Y)
			{
				for (int32 X = 0; X < Grid()->GetSizeX(); ++X)
				{
					if (Grid()->GetColumnHeight(FIntPoint(X, Y)) > 0)
					{
						GetHider()->SetTopDownFocus(Grid()->CellFloorCenter(FIntPoint(X, Y)));
						Y = Grid()->GetSizeY();
						break;
					}
				}
			}
			GetHider()->TopDownDistance = 900.f;
			GetHider()->AddTopDownTilt(45.f);
		} });
		Steps.Add({ 1.5f, [=, this]
		{
			Shot(TEXT("maps_99_walls"));
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
		// M14：一番大きな館（王の間）を部屋ごとに三人称で撮る。部屋の隅から、その部屋らしい家具（小文字は床の種類）の方を向く
		Steps.Add({ 0.3f, [=, this]
		{
			GM->MapOverride = TEXT("Crown");
			GM->SwitchToMap(TEXT("Crown"));
			GS()->HideTimeRemaining = 600.f; // 撮り終わるまでかくれんぼを終わらせない
			const AKakurenboArena* Arena = GM->GetArena();
			Check(FString::Printf(TEXT("the mansion has floor kinds (%d), door frames (%d), windows (%d), paintings (%d) and wall lights (%d)"), Arena->GetFloorKindCount(),
				Arena->GetDecorCount(EKakurenboDecorKind::DoorFrame) + Arena->GetDecorCount(EKakurenboDecorKind::DoorFrameWide), Arena->GetDecorCount(EKakurenboDecorKind::Window),
				Arena->GetDecorCount(EKakurenboDecorKind::Painting), Arena->GetDecorCount(EKakurenboDecorKind::Sconce)),
				Arena->GetFloorKindCount() >= 4 && Arena->GetDecorCount(EKakurenboDecorKind::Window) > 0 && Arena->GetDecorCount(EKakurenboDecorKind::Painting) > 0
				&& Arena->GetDecorCount(EKakurenboDecorKind::Sconce) > 0);
			// M14：出入り口の上には壁がある（枠だけが天井まで抜けた隙間に立って見えない）。家具の当たり判定はメッシュ 1 個ずつ
			Check(FString::Printf(TEXT("doorways have a wall above them (%d headers, %d door frames), furniture collision follows each mesh (%d boxes, %d meshes)"), Arena->GetDoorHeaderCount(),
				Arena->GetDecorCount(EKakurenboDecorKind::DoorFrame) + Arena->GetDecorCount(EKakurenboDecorKind::DoorFrameWide), Arena->GetFurnitureCollisionBoxCount(), Arena->GetFurnitureMeshCount()),
				Arena->GetDoorHeaderCount() >= 10 && Arena->GetFurnitureCollisionBoxCount() >= Arena->GetFurnitureMeshCount());
		} });
		struct FRoomShot { TCHAR Key; const TCHAR* Name; };
		static const FRoomShot RoomShots[] = { { TEXT('m'), TEXT("entrance") }, { TEXT('p'), TEXT("corridor") }, { TEXT('D'), TEXT("bedroom") }, { TEXT('I'), TEXT("dining") },
			{ TEXT('V'), TEXT("kitchen") }, { TEXT('Y'), TEXT("bath") }, { TEXT('B'), TEXT("library") }, { TEXT('J'), TEXT("lounge") }, { TEXT('3'), TEXT("great_hall") } };
		for (int32 r = 0; r < UE_ARRAY_COUNT(RoomShots); ++r)
		{
			const FRoomShot Room = RoomShots[r];
			Steps.Add({ 0.5f, [=, this]
			{
				const AKakurenboArena* Arena = GM->GetArena();
				UKakurenboGridSubsystem* G = Grid();
				const bool bFloorKind = FChar::IsLower(Room.Key);
				auto Matches = [&](const FIntPoint& C) { return bFloorKind ? (Arena->GetFloorKind(C) == Room.Key && Arena->GetLayoutChar(C) != KakurenboMaps::WallChar) : Arena->GetLayoutChar(C) == Room.Key; };
				// 見る物：床の種類なら、その種類のマスの真ん中。家具なら最初に見つかった 1 つ（同じ文字のつながったマス）
				TArray<FIntPoint> Target;
				for (int32 Y = 0; Y < G->GetSizeY() && (bFloorKind || Target.Num() == 0); ++Y)
				{
					for (int32 X = 0; X < G->GetSizeX(); ++X)
					{
						if (Matches(FIntPoint(X, Y)))
						{
							Target.Add(FIntPoint(X, Y));
							if (!bFloorKind)
							{
								break;
							}
						}
					}
				}
				if (Target.Num() == 0)
				{
					Check(FString::Printf(TEXT("room '%c' (%s) exists in Crown"), Room.Key, Room.Name), false);
					return;
				}
				for (int32 i = 0; !bFloorKind && i < Target.Num(); ++i)
				{
					for (const FIntPoint& D : { FIntPoint(1, 0), FIntPoint(-1, 0), FIntPoint(0, 1), FIntPoint(0, -1) })
					{
						if (Matches(Target[i] + D) && !Target.Contains(Target[i] + D))
						{
							Target.Add(Target[i] + D);
						}
					}
				}
				FVector2D Sum(0.0, 0.0);
				for (const FIntPoint& C : Target)
				{
					Sum += FVector2D(C.X, C.Y);
				}
				const FVector2D Mid = Sum / Target.Num();
				// 立つ場所：見る物から歩いて行ける、同じ床の種類のマス（部屋の中）のうち、見る物から遠い所（最大 6 マス）
				TMap<FIntPoint, int32> Dist;
				TArray<FIntPoint> Queue;
				for (const FIntPoint& C : Target)
				{
					for (const FIntPoint& D : { FIntPoint(1, 0), FIntPoint(-1, 0), FIntPoint(0, 1), FIntPoint(0, -1) })
					{
						const FIntPoint N = C + D;
						if (G->IsWalkable(N) && !G->IsObstacle(N) && !Dist.Contains(N) && (bFloorKind || Arena->GetFloorKind(N) == Arena->GetFloorKind(C)))
						{
							Dist.Add(N, 1);
							Queue.Add(N);
						}
					}
				}
				const TCHAR RoomFloor = Arena->GetFloorKind(Target[0]); // 家具の下の床＝その部屋の床の種類
				FIntPoint Spot = Queue.Num() > 0 ? Queue[0] : Target[0];
				double BestScore = -1.0;
				for (int32 q = 0; q < Queue.Num(); ++q)
				{
					const FIntPoint C = Queue[q];
					const int32 Dc = Dist[C];
					const double Score = FVector2D::Distance(FVector2D(C.X, C.Y), Mid);
					if (Score > BestScore && Dc <= 6)
					{
						BestScore = Score;
						Spot = C;
					}
					for (const FIntPoint& D : { FIntPoint(1, 0), FIntPoint(-1, 0), FIntPoint(0, 1), FIntPoint(0, -1) })
					{
						const FIntPoint N = C + D;
						if (Dc < 6 && G->IsWalkable(N) && !G->IsObstacle(N) && !Dist.Contains(N) && (bFloorKind || Arena->GetFloorKind(N) == RoomFloor))
						{
							Dist.Add(N, Dc + 1);
							Queue.Add(N);
						}
					}
				}
				const FVector From = G->CellFloorCenter(Spot);
				const FVector To = G->CellFloorCenter(FIntPoint(FMath::RoundToInt(Mid.X), FMath::RoundToInt(Mid.Y)));
				TeleportPlayer(From);
				SetControlRotation(FRotator(-16.f, (To - From).Rotation().Yaw, 0.f));
				UE_LOG(LogTemp, Display, TEXT("[AutoTest] room %s: stand (%d,%d), look at (%.1f,%.1f)"), Room.Name, Spot.X, Spot.Y, Mid.X, Mid.Y);
			} });
			Steps.Add({ 1.5f, [=, this] { Shot(FString::Printf(TEXT("mood_%02d_crown_%s"), 10 + r, Room.Name)); } });
		}
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
	// ================================================================ Swatches（M12：Data/Surfaces.csv の見た目を並べて撮る・プレイヤーと宝石の見た目）
	else if (Scenario.Equals(TEXT("Swatches"), ESearchCase::IgnoreCase))
	{
		Steps.Add({ 0.5f, [=, this]
		{
			const FIntPoint Me(4, 12);
			TeleportPlayer(Grid()->CellFloorCenter(Me));
			// 見た目の表を 1 マスずつ並べる（東へ 2 マスおき、8 個ごとに次の列）
			TArray<FName> Names;
			GM->SurfaceRows.GetKeys(Names);
			int32 Textured = 0;
			UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
			for (int32 i = 0; i < Names.Num(); ++i)
			{
				const FIntPoint Cell = Me + FIntPoint(2 + (i % 8) * 2, -6 + (i / 8) * 3);
				AStaticMeshActor* Swatch = GetWorld()->SpawnActor<AStaticMeshActor>(Grid()->CellToWorld(Cell, 0), FRotator::ZeroRotator);
				Swatch->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
				Swatch->GetStaticMeshComponent()->SetStaticMesh(Cube);
				Swatch->SetActorScale3D(FVector(1.f, 1.f, 1.6f));
				Textured += UKakurenboLibrary::ApplySurface(Swatch->GetStaticMeshComponent(), GM->SurfaceRows[Names[i]]) ? 1 : 0;
				UE_LOG(LogTemp, Display, TEXT("[AutoTest] swatch %d %s at (%d,%d)"), i, *Names[i].ToString(), Cell.X, Cell.Y);
			}
			const bool bHasFab = FPackageName::DoesPackageExist(TEXT("/Game/Kakurenbo/Wood/Wood01_Base_Color"));
			Check(FString::Printf(TEXT("surfaces load their textures when the Fab packs are in the project (%d of %d, fab %d)"), Textured, Names.Num(), bHasFab),
				!bHasFab || Textured >= Names.Num() - 1);
			// 置いた壁（木・石・鉄・消音）も並べる
			for (int32 Type = 0; Type < GM->WallTypes.Num(); ++Type)
			{
				Grid()->PlaceBlock(Me + FIntPoint(2 + Type * 2, 4), Type, 10.0, GM->WallTypes[Type].Color);
			}
			const APlaceableBlock* Wood = Grid()->GetTopBlock(Me + FIntPoint(2, 4));
			Check(FString::Printf(TEXT("placed walls use their surface when the textures exist (wood textured %d, surface '%s')"), Wood && Wood->IsTextured(), *GM->WallTypes[0].Surface.ToString()),
				!bHasFab || GM->WallTypes[0].Surface.IsNone() || (Wood && Wood->IsTextured()));
			const bool bHasPlayer = FPackageName::DoesPackageExist(TEXT("/Game/Player/Male_002"));
			Check(FString::Printf(TEXT("the player uses the character model with moving legs when it is imported (model %d, animated %d)"), bHasPlayer, GetHider()->HasAnimatedLook()),
				!bHasPlayer || GetHider()->HasAnimatedLook());
			const bool bHasMannequin = FPackageName::DoesPackageExist(TEXT("/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple"));
			Check(FString::Printf(TEXT("the player moves with the official mannequin animations when they are in the project (mannequin %d, official %d, %d bones)"), bHasMannequin, GetHider()->HasOfficialAnimation(), GetHider()->GetDrivenBoneCount()),
				!bHasPlayer || !bHasMannequin || (GetHider()->HasOfficialAnimation() && GetHider()->GetDrivenBoneCount() >= 20));
			int32 Gems = 0;
			for (const ATreasureActor* Treasure : GM->GetTreasures())
			{
				Gems += Treasure->HasGemLook() ? 1 : 0;
			}
			const bool bHasGem = FPackageName::DoesPackageExist(TEXT("/Game/Gem/Circle_001"));
			Check(FString::Printf(TEXT("treasures are gems when imported (%d of %d, gem assets %d)"), Gems, GM->GetTreasures().Num(), bHasGem), !bHasGem || Gems == GM->GetTreasures().Num());
			// 宝石を 1 つ目の前へ
			if (GM->GetTreasures().Num() > 0)
			{
				GM->GetTreasures()[0]->SetActorLocation(Grid()->CellFloorCenter(Me + FIntPoint(2, 1)));
			}
			SetControlRotation(FRotator(-30.f, 0.f, 0.f));
		} });
		Steps.Add({ 3.5f, [=, this] { if (AOniCharacter* Oni = KeepOnlyOni(0)) { Oni->Deactivate(); } } });
		Steps.Add({ 3.5f, [=, this] { Shot(TEXT("swatches_01_front")); SetControlRotation(FRotator(-55.f, 30.f, 0.f)); } });
		Steps.Add({ 1.0f, [=, this] { Shot(TEXT("swatches_02_high")); } });
		// プレイヤーの見た目を近くで（歩かせる）
		Steps.Add({ 0.3f, [=, this] { SetControlRotation(FRotator(-10.f, 0.f, 0.f)); GetHider()->ThirdPersonDistance = 320.f; } });
		Steps.Add({ 0.6f, [=, this] { Shot(TEXT("swatches_03_player_idle")); SimulateKey(EKeys::D, IE_Pressed); } });
		Steps.Add({ 1.4f, [=, this] { Shot(TEXT("swatches_04_player_walk")); } });
		Steps.Add({ 0.2f, [=, this] { Shot(TEXT("swatches_05_player_walk")); SimulateKey(EKeys::D, IE_Released); } });
	}
	// ================================================================ Crouch（しのび足でしゃがむと、体の半分くらいの高さの物の陰に隠れられる）
	else if (Scenario.Equals(TEXT("Crouch"), ESearchCase::IgnoreCase))
	{
		struct FCrouch { FIntPoint Me = FIntPoint::ZeroValue; bool bSeenStanding = false; };
		TSharedRef<FCrouch> State = MakeShared<FCrouch>();
		Steps.Add({ 0.3f, [=] { SetStageOniTypes({ EOniType::Balanced }); } });
		Steps.Add({ 3.3f, [=, this]
		{
			State->Me = FIntPoint(6, 12);
			TeleportPlayer(Grid()->CellFloorCenter(State->Me));
			// 高さ 80cm の箱（テーブルくらい）をプレイヤーのすぐ東に置き、鬼を 5 マス東から見させる
			UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
			AStaticMeshActor* Box = GetWorld()->SpawnActor<AStaticMeshActor>(Grid()->CellFloorCenter(State->Me + FIntPoint(1, 0)) + FVector(0.f, 0.f, 40.f), FRotator::ZeroRotator);
			Box->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
			Box->GetStaticMeshComponent()->SetStaticMesh(Cube);
			Box->GetStaticMeshComponent()->SetCollisionProfileName(TEXT("BlockAll"));
			Box->SetActorScale3D(FVector(1.f, 1.4f, 0.8f));
			if (AOniCharacter* Oni = KeepOnlyOni(0))
			{
				Oni->Activate(GetHider());
				Oni->Deactivate(); // 動かさず、見えるかどうかだけ調べる
				Oni->SetActorLocation(Grid()->CellFloorCenter(State->Me + FIntPoint(5, 0)) + FVector(0.f, 0.f, 100.f), false, nullptr, ETeleportType::TeleportPhysics);
				Oni->SetActorRotation(FRotator(0.f, 180.f, 0.f));
			}
			SetControlRotation(FRotator(-15.f, 0.f, 0.f));
		} });
		Steps.Add({ 0.5f, [=, this]
		{
			State->bSeenStanding = GM->GetOnis()[0]->DebugCanSeeTarget();
			Check(FString::Printf(TEXT("standing behind a waist-high box, the oni can see the player (%d)"), State->bSeenStanding), State->bSeenStanding);
			Shot(TEXT("crouch_01_standing"));
			bTestSneak = true;
		} });
		Steps.Add({ 0.6f, [=, this]
		{
			Check(FString::Printf(TEXT("crouching behind the same box hides the player (crouched %d, seen %d)"), GetHider()->bIsCrouched, GM->GetOnis()[0]->DebugCanSeeTarget()),
				GetHider()->bIsCrouched && !GM->GetOnis()[0]->DebugCanSeeTarget());
			Shot(TEXT("crouch_02_crouched"));
			SetControlRotation(FRotator(-10.f, 90.f, 0.f)); // 横から姿勢を見る
		} });
		Steps.Add({ 0.8f, [=, this]
		{
			// 足が床に付いている（モデルの一番下が床の近く）
			const UPoseableMeshComponent* Look = GetHider()->LookMesh;
			const float Floor = GetHider()->GetActorLocation().Z - GetHider()->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
			if (Look)
			{
				const float FootZ = FMath::Min(Look->GetSocketLocation(TEXT("foot_l")).Z, Look->GetSocketLocation(TEXT("foot_r")).Z);
				Check(FString::Printf(TEXT("crouching keeps the feet on the floor (foot %.0f cm above the floor)"), FootZ - Floor), FMath::Abs(FootZ - Floor) < 25.f);
			}
			Shot(TEXT("crouch_03_side"));
			bTestSneak = false;
		} });
		Steps.Add({ 0.6f, [=, this]
		{
			Check(FString::Printf(TEXT("standing up again (crouched %d)"), GetHider()->bIsCrouched), !GetHider()->bIsCrouched);
		} });
	}
	// ================================================================ CrouchWalk（しゃがんで家具に向かって歩き続けても、体とカメラががくがくしない）
	else if (Scenario.Equals(TEXT("CrouchWalk"), ESearchCase::IgnoreCase))
	{
		// 本物の館（天文台：地球儀・望遠鏡が柱のように並ぶ）で、周りが空いている 1 マスの家具を探し、そこへ歩いてぶつかり続ける
		struct FFrame { FVector Actor; float Yaw = 0.f; FVector Mesh; FVector Camera; float Speed = 0.f; };
		struct FWalk { FIntPoint Target = FIntPoint(-1, -1); TArray<FFrame> Frames; FTSTicker::FDelegateHandle Ticker; };
		TSharedRef<FWalk> Walk = MakeShared<FWalk>();
		auto Begin = [=, this](bool bSneak, float AngleOffset)
		{
			const FVector Goal = Grid()->CellFloorCenter(Walk->Target);
			TeleportPlayer(Goal - FRotator(0.f, AngleOffset, 0.f).RotateVector(FVector(300.f, 0.f, 0.f)));
			GetPawn()->SetActorRotation(FRotator(0.f, AngleOffset, 0.f));
			SetControlRotation(FRotator(-20.f, AngleOffset, 0.f));
			bTestSneak = bSneak;
			SimulateKey(EKeys::W, IE_Pressed);
			Walk->Frames.Reset();
			Walk->Ticker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([=, this](float) -> bool
			{
				const AHiderCharacter* H = GetHider();
				FFrame F;
				F.Actor = H->GetActorLocation();
				F.Yaw = H->GetActorRotation().Yaw;
				F.Mesh = H->LookMesh ? H->LookMesh->GetComponentLocation() : H->BodyMesh->GetComponentLocation();
				F.Camera = H->Camera->GetComponentLocation();
				F.Speed = H->GetVelocity().Size2D();
				Walk->Frames.Add(F);
				return true;
			}));
		};
		auto Finish = [=, this](const FString& Label)
		{
			FTSTicker::GetCoreTicker().RemoveTicker(Walk->Ticker);
			SimulateKey(EKeys::W, IE_Released);
			bTestSneak = false;
			// 最後の 1.5 秒（家具にぶつかって押し続けている間）の、フレームごとの揺れ（2 階差分）
			const TArray<FFrame>& Fr = Walk->Frames;
			const int32 Start = FMath::Max(2, Fr.Num() - 90);
			float MeshJerk = 0.f, CamJerk = 0.f, YawStep = 0.f, MeshRel = 0.f, MaxMeshRel = 0.f, ActorJerk = 0.f;
			int32 N = 0;
			for (int32 i = Start; i < Fr.Num(); ++i)
			{
				auto Jerk = [&](auto Get) { return (Get(Fr[i]) - Get(Fr[i - 1]) * 2.f + Get(Fr[i - 2])).Size(); };
				MeshJerk += Jerk([](const FFrame& F) { return F.Mesh; });
				CamJerk += Jerk([](const FFrame& F) { return F.Camera; });
				ActorJerk += Jerk([](const FFrame& F) { return F.Actor; });
				YawStep += FMath::Abs(FRotator::NormalizeAxis(Fr[i].Yaw - Fr[i - 1].Yaw));
				const float Rel = FMath::Abs((Fr[i].Mesh.Z - Fr[i].Actor.Z) - (Fr[i - 1].Mesh.Z - Fr[i - 1].Actor.Z));
				MeshRel += Rel;
				MaxMeshRel = FMath::Max(MaxMeshRel, Rel);
				++N;
			}
			N = FMath::Max(N, 1);
			FString Series;
			for (int32 i = Start; i < Fr.Num(); i += 3)
			{
				Series += FString::Printf(TEXT(" [%.1f,%.1f z%.1f m%.1f y%.0f v%.0f]"), Fr[i].Actor.X, Fr[i].Actor.Y, Fr[i].Actor.Z, Fr[i].Mesh.Z - Fr[i].Actor.Z, Fr[i].Yaw, Fr[i].Speed);
			}
			UE_LOG(LogTemp, Display, TEXT("[AutoTest] crouchwalk %s: frames %d, mesh jerk %.2f, camera jerk %.2f, actor jerk %.2f, yaw step %.2f, mesh-rel step %.2f (max %.2f)%s"),
				*Label, Fr.Num(), MeshJerk / N, CamJerk / N, ActorJerk / N, YawStep / N, MeshRel / N, MaxMeshRel, *Series);
			// 体（モデル）の高さはカプセルに対して動かない（以前は歩く脚の振りでしゃがむ高さが変わり、上下にがくがくしていた）
			Check(FString::Printf(TEXT("%s: the body does not shake while pushing into furniture (mesh jerk %.2f, mesh-rel step %.2f max %.2f, yaw step %.2f)"), *Label, MeshJerk / N, MeshRel / N, MaxMeshRel, YawStep / N),
				MeshJerk / N < 1.5f && MeshRel / N < 0.1f && MaxMeshRel < 0.3f && YawStep / N < 1.5f);
			Check(FString::Printf(TEXT("%s: the camera does not shake (camera jerk %.2f)"), *Label, CamJerk / N), CamJerk / N < 1.5f);
		};
		Steps.Add({ 0.3f, [=, this]
		{
			SetStageOniTypes({ EOniType::Balanced });
			GM->bMapOverride = true;
			// 周り 8 マス＋その先 3 マス西が空いている、1 マスだけの家具（天文台から順に、全部の館から探す。M14 で館が部屋に分かれた）
			TArray<FName> Order = GM->MapOrder;
			Order.Remove(TEXT("Observatory"));
			Order.Insert(TEXT("Observatory"), 0);
			for (const FName& MapName : Order)
			{
				GM->MapOverride = MapName;
				GM->SwitchToMap(MapName);
				for (int32 Y = 2; Y < Grid()->GetSizeY() - 2 && Walk->Target.X < 0; ++Y)
				{
					for (int32 X = 5; X < Grid()->GetSizeX() - 2 && Walk->Target.X < 0; ++X)
					{
						const FIntPoint C(X, Y);
						bool bAlone = Grid()->IsObstacle(C) && GM->GetArena()->GetLayoutChar(C) != KakurenboMaps::WallChar;
						for (int32 DY = -2; DY <= 2 && bAlone; ++DY)
						{
							for (int32 DX = -4; DX <= 2 && bAlone; ++DX)
							{
								bAlone = (DX == 0 && DY == 0) || Grid()->IsWalkable(C + FIntPoint(DX, DY));
							}
						}
						if (bAlone)
						{
							Walk->Target = C;
						}
					}
				}
				if (Walk->Target.X >= 0)
				{
					break;
				}
			}
			Check(FString::Printf(TEXT("found a lone piece of furniture at (%d,%d) in %s ('%c')"), Walk->Target.X, Walk->Target.Y, *GM->GetCurrentMapName().ToString(),
				Walk->Target.X >= 0 ? GM->GetArena()->GetLayoutChar(Walk->Target) : TEXT('-')), Walk->Target.X >= 0);
		} });
		Steps.Add({ 3.3f, [=, this] { if (AOniCharacter* Oni = KeepOnlyOni(0)) { Oni->Deactivate(); } Begin(true, 0.f); } });
		Steps.Add({ 2.5f, [=, this] { Shot(TEXT("crouchwalk_01_sneak_straight")); } });
		Steps.Add({ 0.1f, [=] { Finish(TEXT("sneak straight")); } });
		Steps.Add({ 0.5f, [=] { Begin(true, 25.f); } });
		Steps.Add({ 2.6f, [=] { Finish(TEXT("sneak diagonal")); } });
		Steps.Add({ 0.5f, [=] { Begin(false, 0.f); } });
		Steps.Add({ 2.6f, [=] { Finish(TEXT("standing straight")); } });
		Steps.Add({ 0.5f, [=] { Begin(false, 25.f); } });
		Steps.Add({ 2.6f, [=] { Finish(TEXT("standing diagonal")); } });
	}
	// ================================================================ Title（タイトル・設定・キー設定・一時停止メニュー）
	else if (bTitleScenario)
	{
		auto HUD = [this]() { return GetHUD<AKakurenboHUD>(); };
		auto Settings = [this]() { return UKakurenboSettings::Get(this); };
		// つまみの横の位置（0〜1）をクリックする
		auto ClickSlider = [this, HUD](EKakurenboSettingSlider Slider, float Value01) -> bool
		{
			FVector2D Center;
			FKakurenboUIButton Button;
			if (!HUD() || !HUD()->GetButtonCenter(EKakurenboUIAction::Slider, static_cast<int32>(Slider), Center) || !HUD()->FindButtonAt(Center, Button))
			{
				return false;
			}
			bUseTestCursor = true;
			TestCursorPosition = FVector2D(FMath::Lerp(Button.Rect.Min.X, Button.Rect.Max.X, Value01), Center.Y);
			PlayerInput->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::LeftMouseButton, IE_Pressed, 1.f));
			PlayerInput->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::LeftMouseButton, IE_Released, 1.f));
			return true;
		};
		auto Press = [SimulateKey](const FKey& Key) { SimulateKey(Key, IE_Pressed); SimulateKey(Key, IE_Released); };
		struct FTitleState { float FrozenTime = 0.f; int32 MashBefore = 0; };
		TSharedRef<FTitleState> T = MakeShared<FTitleState>();

		Steps.Add({ 0.6f, [=, this]
		{
			Check(FString::Printf(TEXT("the game starts at the title (phase %s, cursor %d)"), *UEnum::GetValueAsString(GS()->Phase), bShowMouseCursor ? 1 : 0),
				GS()->Phase == EKakurenboPhase::Title && bShowMouseCursor);
			FVector2D Center;
			Check(TEXT("the title has new game / continue / settings / quit"), HUD() && HUD()->GetButtonCenter(EKakurenboUIAction::TitleItem, static_cast<int32>(EKakurenboTitleItem::NewGame), Center)
				&& HUD()->GetButtonCenter(EKakurenboUIAction::TitleItem, static_cast<int32>(EKakurenboTitleItem::Continue), Center)
				&& HUD()->GetButtonCenter(EKakurenboUIAction::TitleItem, static_cast<int32>(EKakurenboTitleItem::Settings), Center));
			Check(TEXT("without a save, continue is disabled"), !IsTitleItemEnabled(EKakurenboTitleItem::Continue) && !GM->HasContinueData());
			Check(TEXT("the title plays the calm music and does not save"), Sound()->GetCurrentMusic() == EKakurenboMusic::Calm);
			Shot(TEXT("title_01_title"));
			ClickUI(EKakurenboUIAction::TitleItem, static_cast<int32>(EKakurenboTitleItem::Continue));
		} });
		Steps.Add({ 0.4f, [=, this]
		{
			Check(TEXT("clicking the disabled continue does nothing"), GS()->Phase == EKakurenboPhase::Title && !IsMenuOpen());
			ClickUI(EKakurenboUIAction::TitleItem, static_cast<int32>(EKakurenboTitleItem::Settings));
		} });
		Steps.Add({ 0.4f, [=, this]
		{
			Check(FString::Printf(TEXT("settings open from the title (menu %d)"), static_cast<int32>(GetMenu())), GetMenu() == EKakurenboMenu::Settings);
			Shot(TEXT("title_02_settings"));
			Check(TEXT("click the master volume at 25%"), ClickSlider(EKakurenboSettingSlider::Master, 0.25f));
		} });
		Steps.Add({ 0.4f, [=, this]
		{
			Check(FString::Printf(TEXT("the volume slider sets the volume (setting %.2f, sound %.2f)"), Settings()->MasterVolume, Sound()->MasterVolume),
				FMath::IsNearlyEqual(Settings()->MasterVolume, 0.25f, 0.03f) && FMath::IsNearlyEqual(Sound()->MasterVolume, Settings()->MasterVolume, 0.001f));
			ClickSlider(EKakurenboSettingSlider::Mouse, 0.99f);
		} });
		Steps.Add({ 0.4f, [=, this]
		{
			Check(FString::Printf(TEXT("the mouse slider changes the sensitivity (%.3f)"), MouseSensitivity),
				MouseSensitivity > UKakurenboSettings::DefaultMouseSensitivity * 2.f && FMath::IsNearlyEqual(MouseSensitivity, Settings()->MouseSensitivity));
			ClickSlider(EKakurenboSettingSlider::Mouse, (UKakurenboSettings::DefaultMouseSensitivity - UKakurenboSettings::MinMouseSensitivity) / (UKakurenboSettings::MaxMouseSensitivity - UKakurenboSettings::MinMouseSensitivity));
			ClickUI(EKakurenboUIAction::Toggle, static_cast<int32>(EKakurenboSettingToggle::InvertY));
		} });
		Steps.Add({ 0.4f, [=, this]
		{
			Check(TEXT("the invert toggle inverts the mouse"), bInvertMouseY && Settings()->bInvertMouseY);
			ClickUI(EKakurenboUIAction::Toggle, static_cast<int32>(EKakurenboSettingToggle::InvertY));
		} });
		Steps.Add({ 0.3f, [=, this] { ClickUI(EKakurenboUIAction::OpenKeys, 0); } });
		Steps.Add({ 0.4f, [=, this]
		{
			Check(TEXT("key config opens"), GetMenu() == EKakurenboMenu::Keys && !bInvertMouseY);
			Shot(TEXT("title_03_keys"));
			// 連打を K にする
			ClickUI(EKakurenboUIAction::KeySlot, static_cast<int32>(EKakurenboAction::Mash) * 2);
		} });
		Steps.Add({ 0.3f, [=, this]
		{
			EKakurenboAction Waiting;
			bool bSecondary = false;
			Check(TEXT("clicking a key slot waits for a key"), IsWaitingForKey(Waiting, bSecondary) && Waiting == EKakurenboAction::Mash && !bSecondary);
			Shot(TEXT("title_04_waiting_key"));
			Press(EKeys::K);
		} });
		Steps.Add({ 0.3f, [=, this]
		{
			Check(FString::Printf(TEXT("pressing K binds mash to K (%s)"), *Settings()->GetKey(EKakurenboAction::Mash).ToString()),
				Settings()->GetKey(EKakurenboAction::Mash) == EKeys::K && GetMenu() == EKakurenboMenu::Keys);
			// ジャンプにも K → 入れ替わる（連打は Space になる）
			ClickUI(EKakurenboUIAction::KeySlot, static_cast<int32>(EKakurenboAction::Climb) * 2);
		} });
		Steps.Add({ 0.3f, [=, this] { Press(EKeys::K); } });
		Steps.Add({ 0.3f, [=, this]
		{
			Check(FString::Printf(TEXT("the same key on another action swaps them (climb %s, mash %s)"), *Settings()->GetKey(EKakurenboAction::Climb).ToString(), *Settings()->GetKey(EKakurenboAction::Mash).ToString()),
				Settings()->GetKey(EKakurenboAction::Climb) == EKeys::K && Settings()->GetKey(EKakurenboAction::Mash) == EKeys::SpaceBar);
			ClickUI(EKakurenboUIAction::ResetKeys, 0);
		} });
		Steps.Add({ 0.3f, [=, this]
		{
			Check(TEXT("reset brings the default keys back"), Settings()->GetKey(EKakurenboAction::Mash) == EKeys::LeftMouseButton && Settings()->GetKey(EKakurenboAction::Climb) == EKeys::SpaceBar);
			ClickUI(EKakurenboUIAction::KeySlot, static_cast<int32>(EKakurenboAction::Mash) * 2);
		} });
		Steps.Add({ 0.3f, [=, this] { Press(EKeys::K); } });
		Steps.Add({ 0.3f, [=, this] { Press(EKeys::Escape); } });
		Steps.Add({ 0.3f, [=, this]
		{
			Check(TEXT("Esc goes back to the settings"), GetMenu() == EKakurenboMenu::Settings);
			Press(EKeys::Escape);
		} });
		Steps.Add({ 0.3f, [=, this]
		{
			Check(TEXT("Esc again closes the settings (title)"), !IsMenuOpen() && GS()->Phase == EKakurenboPhase::Title);
			ClickUI(EKakurenboUIAction::TitleItem, static_cast<int32>(EKakurenboTitleItem::NewGame));
		} });
		Steps.Add({ 0.5f, [=, this]
		{
			Check(FString::Printf(TEXT("new game starts the first hide round (phase %s, stage %d)"), *UEnum::GetValueAsString(GS()->Phase), GS()->Stage),
				GS()->Phase == EKakurenboPhase::Hide && GS()->Stage == 1 && !bShowMouseCursor);
			Check(FString::Printf(TEXT("hide shows the oni's counting line (%s)"), HUD() && HUD()->HasDrawnText(TEXT("もういいかい")) ? TEXT("yes") : TEXT("no")),
				HUD() && HUD()->HasDrawnText(TEXT("もういいかい")) && !HUD()->HasDrawnText(TEXT("ぶつかったらアウト")));
			Shot(TEXT("title_05_hide_countdown"));
		} });
		Steps.Add({ 3.0f, [=, this]
		{
			if (AOniCharacter* Oni = KeepOnlyOni(0))
			{
				Oni->Deactivate();
			}
			T->MashBefore = GS()->MashCountThisRound;
			Press(EKeys::K);
			Press(EKeys::LeftMouseButton);
		} });
		Steps.Add({ 0.3f, [=, this]
		{
			Check(FString::Printf(TEXT("the rebound key mashes and the old one does not (%d -> %d)"), T->MashBefore, GS()->MashCountThisRound), GS()->MashCountThisRound == T->MashBefore + 1);
			Shot(TEXT("title_06_hide"));
			Press(EKeys::Escape);
			// ここから一時停止中。ワールドのタイマーは止まるので、実時間で進める
			RealDelay(0.4f, [=, this]
			{
				T->FrozenTime = GS()->HideTimeRemaining;
				Check(FString::Printf(TEXT("Esc pauses the hide round and shows the menu (paused %d, menu %d, cursor %d)"), IsPaused() ? 1 : 0, static_cast<int32>(GetMenu()), bShowMouseCursor ? 1 : 0),
					IsPaused() && GetMenu() == EKakurenboMenu::Pause && bShowMouseCursor);
				Shot(TEXT("title_07_pause"));
				ClickUI(EKakurenboUIAction::MenuItem, static_cast<int32>(EKakurenboPauseItem::Help));
				RealDelay(0.4f, [=, this]
				{
					Check(FString::Printf(TEXT("the help lists the hide controls with the bound keys (%d)"), HUD() && HUD()->HasDrawnText(TEXT("しゃがむ")) ? 1 : 0),
						GetMenu() == EKakurenboMenu::Help && HUD()->HasDrawnText(TEXT("しゃがむ")) && HUD()->HasDrawnText(TEXT("[K]")));
					Shot(TEXT("title_08_help"));
					Press(EKeys::Escape);
					RealDelay(0.3f, [=, this]
					{
						Check(FString::Printf(TEXT("the round stays frozen while paused (%.2f -> %.2f)"), T->FrozenTime, GS()->HideTimeRemaining),
							FMath::IsNearlyEqual(T->FrozenTime, GS()->HideTimeRemaining) && GetMenu() == EKakurenboMenu::Pause);
						ClickUI(EKakurenboUIAction::MenuItem, static_cast<int32>(EKakurenboPauseItem::Title));
						RealDelay(0.3f, [=, this]
						{
							Check(TEXT("going to the title in a round asks first"), GetMenu() == EKakurenboMenu::ConfirmTitle);
							Shot(TEXT("title_09_confirm"));
							ClickUI(EKakurenboUIAction::ConfirmYes, 0);
							RealDelay(0.4f, [=, this]
							{
								Check(FString::Printf(TEXT("back at the title, unpaused, and continue is enabled (phase %s, paused %d)"), *UEnum::GetValueAsString(GS()->Phase), IsPaused() ? 1 : 0),
									GS()->Phase == EKakurenboPhase::Title && !IsPaused() && !IsMenuOpen() && IsTitleItemEnabled(EKakurenboTitleItem::Continue) && GM->GetOnis().Num() == 0);
							});
						});
					});
				});
			});
		} });
		Steps.Add({ 1.0f, [=, this]
		{
			Shot(TEXT("title_10_back_to_title"));
			// セーブがあるとき（この起動で遊んだとき）の「はじめから」は確かめる
			ClickUI(EKakurenboUIAction::TitleItem, static_cast<int32>(EKakurenboTitleItem::NewGame));
		} });
		Steps.Add({ 0.4f, [=, this]
		{
			Check(TEXT("new game with progress asks before erasing"), GetMenu() == EKakurenboMenu::ConfirmNewGame && GS()->Phase == EKakurenboPhase::Title);
			ClickUI(EKakurenboUIAction::ConfirmNo, 0);
		} });
		Steps.Add({ 0.4f, [=, this]
		{
			Check(TEXT("no keeps the title"), !IsMenuOpen() && GS()->Phase == EKakurenboPhase::Title);
			ClickUI(EKakurenboUIAction::TitleItem, static_cast<int32>(EKakurenboTitleItem::Continue));
		} });
		Steps.Add({ 0.5f, [=, this]
		{
			Check(FString::Printf(TEXT("continue resumes from the build phase (phase %s)"), *UEnum::GetValueAsString(GS()->Phase)), GS()->Phase == EKakurenboPhase::Build);
			Settings()->ResetBindings();
		} });
	}
	// ================================================================ Hints（はじめての操作のヒント・キーの形の案内）
	else if (Scenario.Equals(TEXT("Hints"), ESearchCase::IgnoreCase))
	{
		auto HUD = [this]() { return GetHUD<AKakurenboHUD>(); };
		Steps.Add({ 0.1f, [=, this]
		{
			bShowTipsInAutoTest = true;
			UKakurenboSettings::Get(this)->ResetTips();
		} });
		Steps.Add({ 1.2f, [=, this]
		{
			const FKakurenboTip* Tip = GetCurrentTip();
			Check(FString::Printf(TEXT("the first hide round shows how to move (%s)"), Tip ? *Tip->Text : TEXT("none")), Tip && Tip->Id == FName(TEXT("Move")));
			Check(TEXT("the controls are drawn as keys at the bottom"), HUD() && HUD()->HasDrawnText(TEXT("しゃがむ")));
			Shot(TEXT("hints_01_hide"));
		} });
		// 鬼は止めておく（捕まると結果画面になって、ヒントが消える）
		Steps.Add({ 2.3f, [=, this] { if (AOniCharacter* Oni = KeepOnlyOni(0)) { Oni->Deactivate(); } } });
		Steps.Add({ 4.7f, [=, this]
		{
			const FKakurenboTip* Tip = GetCurrentTip();
			Check(FString::Printf(TEXT("then how to earn coins (%s)"), Tip ? *Tip->Text : TEXT("none")), Tip && Tip->Id == FName(TEXT("Mash")));
			Shot(TEXT("hints_02_mash"));
			KakuSkipTime(1000.f);
		} });
		Steps.Add({ 1.0f, [=, this]
		{
			Shot(TEXT("hints_03_result"));
		} });
		Steps.Add({ 0.2f, [=, this] { KakuNext(); } }); // → 購入
		Steps.Add({ 0.8f, [=, this]
		{
			const FKakurenboTip* Tip = GetCurrentTip();
			Check(FString::Printf(TEXT("the shop explains buying (%s)"), Tip ? *Tip->Text : TEXT("none")), Tip && Tip->Id == FName(TEXT("Shop")));
			Shot(TEXT("hints_04_shop"));
		} });
		Steps.Add({ 0.2f, [=, this] { KakuNext(); } }); // → 設置
		Steps.Add({ 0.8f, [=, this]
		{
			const FKakurenboTip* Tip = GetCurrentTip();
			Check(FString::Printf(TEXT("the build phase explains placing (%s)"), Tip ? *Tip->Text : TEXT("none")), Tip && Tip->Id == FName(TEXT("Build")));
			Shot(TEXT("hints_05_build"));
		} });
		Steps.Add({ 0.2f, [=, this] { KakuNext(); } }); // → かくれんぼ（2 回目）
		Steps.Add({ 1.2f, [=, this]
		{
			const FKakurenboTip* Tip = GetCurrentTip();
			Check(FString::Printf(TEXT("a tip is shown only once (%s)"), Tip ? *Tip->Id.ToString() : TEXT("none")), !Tip || Tip->Id != FName(TEXT("Move")));
			bShowTipsInAutoTest = false;
		} });
	}
	// ================================================================ Hearing（M12：左右の聞こえ方・壁の向こう・気づいた音・心臓の音）
	else if (Scenario.Equals(TEXT("Hearing"), ESearchCase::IgnoreCase))
	{
		struct FHear { FIntPoint Me = FIntPoint::ZeroValue; int32 Notices = 0; int32 Beats = 0; };
		TSharedRef<FHear> Hear = MakeShared<FHear>();
		Steps.Add({ 0.3f, [=] { SetStageOniTypes({ EOniType::Balanced }); } });
		Steps.Add({ 3.3f, [=, this]
		{
			if (AOniCharacter* Oni = KeepOnlyOni(0))
			{
				Oni->Deactivate();
			}
			Hear->Me = FIntPoint(6, 12);
			TeleportPlayer(Grid()->CellFloorCenter(Hear->Me));
			SetControlRotation(FRotator(-10.f, 0.f, 0.f)); // +X（門の方）を向く。右は +Y
		} });
		Steps.Add({ 0.5f, [=, this]
		{
			const FVector Me = GetPawn()->GetActorLocation();
			auto PlayFrom = [=](const FVector& Where) { Sound()->PlayAt(EKakurenboSfx::OniStep, Where); return Sound()->GetLastSpatial(EKakurenboSfx::OniStep); };
			const FKakurenboSpatialDebug Right = PlayFrom(Me + FVector(0.f, 700.f, -80.f));
			Check(FString::Printf(TEXT("a sound on the right is louder in the right ear (pan %.2f, L %.2f, R %.2f)"), Right.Pan, Right.GainL, Right.GainR),
				Right.bValid && Right.Pan > 0.8f && Right.GainR > Right.GainL * 3.f);
			const FKakurenboSpatialDebug Left = PlayFrom(Me + FVector(0.f, -700.f, -80.f));
			Check(FString::Printf(TEXT("a sound on the left is louder in the left ear (pan %.2f, L %.2f, R %.2f)"), Left.Pan, Left.GainL, Left.GainR),
				Left.Pan < -0.8f && Left.GainL > Left.GainR * 3.f);
			const FKakurenboSpatialDebug Front = PlayFrom(Me + FVector(700.f, 0.f, -80.f));
			const FKakurenboSpatialDebug Back = PlayFrom(Me + FVector(-500.f, 0.f, -80.f));
			Check(FString::Printf(TEXT("front is centered and clear, behind is muffled (front pan %.2f behind %d / back behind %d)"), Front.Pan, Front.bBehind, Back.bBehind),
				FMath::Abs(Front.Pan) < 0.1f && !Front.bBehind && Back.bBehind);
			const FKakurenboSpatialDebug Near = PlayFrom(Me + FVector(200.f, 0.f, -80.f));
			const FKakurenboSpatialDebug Mid = PlayFrom(Me + FVector(1000.f, 0.f, -80.f));
			const FKakurenboSpatialDebug Far = PlayFrom(Me + FVector(1700.f, 300.f, -80.f));
			// 距離が 2 倍で約 -7.5dB（はっきり違う大きさ）。遠いほどこもる（高い音が消える）。それでも 17m 先でかすかに聞こえる
			Check(FString::Printf(TEXT("oni steps get clearly quieter and duller with distance (gain 2m %.2f / 10m %.2f / 17m %.2f, lowpass %.2f / %.2f / %.2f)"),
				Near.Gain, Mid.Gain, Far.Gain, Near.LowPass, Mid.LowPass, Far.LowPass),
				Near.Gain >= 0.99f && Mid.Gain < 0.35f && Mid.Gain > 0.12f && Far.Gain < Mid.Gain * 0.7f && Far.Gain > 0.03f
				&& Near.LowPass > 0.99f && Mid.LowPass < Near.LowPass && Far.LowPass < Mid.LowPass);
			// お宝のキラキラも同じ。2m までが最大で、近づくほどはっきり大きくなる
			auto SparkleFrom = [=](const FVector& Where) { Sound()->PlayAt(EKakurenboSfx::TreasureSparkle, Where); return Sound()->GetLastSpatial(EKakurenboSfx::TreasureSparkle); };
			const FKakurenboSpatialDebug GemNear = SparkleFrom(Me + FVector(150.f, 0.f, -30.f));
			const FKakurenboSpatialDebug GemMid = SparkleFrom(Me + FVector(600.f, 0.f, -30.f));
			const FKakurenboSpatialDebug GemFar = SparkleFrom(Me + FVector(1500.f, 0.f, -30.f));
			Check(FString::Printf(TEXT("the treasure sparkle gets clearly louder as you come close (1.5m %.2f / 6m %.2f / 15m %.2f)"), GemNear.Gain, GemMid.Gain, GemFar.Gain),
				GemNear.Gain >= 0.99f && GemMid.Gain < 0.35f && GemFar.Gain < GemMid.Gain * 0.5f && GemFar.Gain > 0.02f && GemFar.LowPass < GemMid.LowPass);
			// 壁の向こうの音はこもる
			Grid()->PlaceBlock(Hear->Me + FIntPoint(2, 0), 0, 1.0, FLinearColor::White);
			Grid()->PlaceBlock(Hear->Me + FIntPoint(2, 0), 0, 1.0, FLinearColor::White);
			const FKakurenboSpatialDebug Behind = PlayFrom(Grid()->CellFloorCenter(Hear->Me + FIntPoint(4, 0)));
			const FKakurenboSpatialDebug Open = PlayFrom(Grid()->CellFloorCenter(Hear->Me + FIntPoint(0, 4)));
			Check(FString::Printf(TEXT("a sound behind a wall is muffled, an open one is not (wall %d, open %d)"), Behind.bOccluded, Open.bOccluded), Behind.bOccluded && !Open.bOccluded);
			Shot(TEXT("hearing_01"));
		} });
		// 鬼が連打に気づくと「ン？」と鳴る
		Steps.Add({ 0.3f, [=, this]
		{
			AOniCharacter* Oni = GM->GetOnis()[0];
			Oni->SetActorLocation(Grid()->CellFloorCenter(Hear->Me + FIntPoint(0, 8)) + FVector(0.f, 0.f, 100.f), false, nullptr, ETeleportType::TeleportPhysics);
			Oni->Activate(GetHider());
			Oni->SightRadius = 0.f;
			Oni->CloseSenseRadius = 0.f;
			Hear->Notices = Oni->GetNoticeSoundCount();
			KakuMash(3);
		} });
		Steps.Add({ 0.3f, [=, this]
		{
			const AOniCharacter* Oni = GM->GetOnis()[0];
			const FKakurenboSpatialDebug Notice = Sound()->GetLastSpatial(EKakurenboSfx::OniNotice);
			Check(FString::Printf(TEXT("the oni says \"hm?\" from its place when it hears mashing (%d, pan %.2f, state %s)"), Oni->GetNoticeSoundCount() - Hear->Notices, Notice.Pan, *UEnum::GetValueAsString(Oni->GetIntent())),
				Oni->GetNoticeSoundCount() - Hear->Notices == 1 && Notice.bValid && Notice.Pan > 0.5f && Oni->GetIntent() == EOniState::Investigate);
			KakuMash(3);
			Hear->Beats = GM->GetHeartbeatCount();
		} });
		Steps.Add({ 0.6f, [=, this]
		{
			// 捕まらないよう、3 マス手前で止まらせる（音を聞いて来ている途中のまま）
			GM->GetOnis()[0]->DebugGoTo(Hear->Me + FIntPoint(0, 3));
		} });
		// 向かってくる鬼が近いと心臓の音が鳴る
		Steps.Add({ 1.5f, [=, this]
		{
			const AOniCharacter* Oni = GM->GetOnis()[0];
			Check(FString::Printf(TEXT("only one \"hm?\" while it is already coming (%d)"), Oni->GetNoticeSoundCount() - Hear->Notices), Oni->GetNoticeSoundCount() - Hear->Notices == 1);
			Check(FString::Printf(TEXT("the heart beats while an oni comes closer (danger %.2f, beats %d, dist %.0f)"), GM->GetCurrentDanger(), GM->GetHeartbeatCount() - Hear->Beats,
				FVector::Dist2D(Oni->GetActorLocation(), GetPawn()->GetActorLocation())),
				GS()->Phase == EKakurenboPhase::Hide && GM->GetCurrentDanger() > 0.4f && GM->GetHeartbeatCount() - Hear->Beats >= 2);
			// 遠くへ行かせて止める → 鳴らない
			AOniCharacter* Mutable = GM->GetOnis()[0];
			Mutable->Deactivate();
			Mutable->SetActorLocation(Grid()->CellFloorCenter(FIntPoint(Grid()->GetSizeX() - 3, 1)) + FVector(0.f, 0.f, 100.f), false, nullptr, ETeleportType::TeleportPhysics);
		} });
		Steps.Add({ 0.5f, [=, this] { Hear->Beats = GM->GetHeartbeatCount(); } });
		Steps.Add({ 1.5f, [=, this]
		{
			Check(FString::Printf(TEXT("no heartbeat when no oni is near (danger %.2f, beats %d)"), GM->GetCurrentDanger(), GM->GetHeartbeatCount() - Hear->Beats),
				GS()->Phase == EKakurenboPhase::Hide && GM->GetCurrentDanger() < 0.05f && GM->GetHeartbeatCount() == Hear->Beats);
		} });
	}
	// ================================================================ Climb（M14：ジャンプの代わりによじ登る）
	else if (Scenario.Equals(TEXT("Climb"), ESearchCase::IgnoreCase))
	{
		// 家具の無い舞台に並べる（東 = +X）：
		//   (4,4) から東へ [通常壁 1 段][通常壁 2 段][すき間][通常壁 1 段]（登る・2 段目へ・すき間は越えられない）
		//   (4,9) から北へ [クライミング壁 1 段]、その東に [通常壁 2 段]（すばやく登る・鬼が飛び乗れない塔）
		//   (11,4) の東に低い家具（80cm）・(11,8) の東に高い家具（220cm）
		struct FClimbState { float FloorZ = 0.f; int32 LeapsWhileStanding = 0; bool bTowerStood = true; int32 Samples = 0; };
		TSharedRef<FClimbState> CS = MakeShared<FClimbState>();
		const FIntPoint Start(4, 4), WallA(5, 4), WallB(6, 4), WallC(8, 4);
		const FIntPoint LadderFrom(4, 9), Ladder(4, 8), Tower(5, 8);
		const FIntPoint LowFrom(11, 4), Low(12, 4), TallFrom(11, 8), Tall(12, 8);
		auto FeetHeight = [this, CS]() { return GetPawn()->GetActorLocation().Z - GetPawn()->GetSimpleCollisionHalfHeight() - CS->FloorZ; };
		auto PressClimb = [=]() { SimulateKey(EKeys::SpaceBar, IE_Pressed); SimulateKey(EKeys::SpaceBar, IE_Released); };
		auto StandAt = [=, this](const FIntPoint& Cell, float Yaw)
		{
			TeleportPlayer(Grid()->CellFloorCenter(Cell));
			GetPawn()->SetActorRotation(FRotator(0.f, Yaw, 0.f));
			SetControlRotation(FRotator(-20.f, Yaw, 0.f));
		};
		auto SpawnBox = [this, Grid](const FIntPoint& Cell, float Height)
		{
			const FVector Center = Grid()->CellFloorCenter(Cell);
			AStaticMeshActor* Box = GetWorld()->SpawnActor<AStaticMeshActor>(Center + FVector(0.f, 0.f, Height * 0.5f), FRotator::ZeroRotator);
			Box->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
			Box->GetStaticMeshComponent()->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
			Box->GetStaticMeshComponent()->SetCollisionProfileName(TEXT("BlockAll"));
			Box->SetActorScale3D(FVector(Grid()->GetCellSize() / 100.f, Grid()->GetCellSize() / 100.f, Height / 100.f));
		};
		const int32 LadderType = GM->WallTypes.IndexOfByPredicate([](const FWallTypeDef& Def) { return Def.bLadder; });
		Steps.Add({ 0.3f, [=, this]
		{
			SetStageOniTypes({ EOniType::Balanced });
			GS()->HideTimeRemaining = GS()->HideTimeLimit = 300.f; // 途中で時間切れにならないように
			CS->FloorZ = Grid()->CellFloorCenter(Start).Z;
			PlaceTestWall(WallA);
			PlaceTestWall(WallB);
			PlaceTestWall(WallB);
			PlaceTestWall(WallC);
			const FWallTypeDef& LadderDef = GM->WallTypes[FMath::Max(LadderType, 0)];
			Grid()->PlaceBlock(Ladder, LadderType, LadderDef.MaxHP, LadderDef.Color);
			PlaceTestWall(Tower);
			PlaceTestWall(Tower);
			Grid()->SetObstacles({ Low, Tall });
			SpawnBox(Low, 80.f);
			SpawnBox(Tall, 220.f);
			StandAt(Start, 0.f);
			const APlaceableBlock* LadderBlock = Grid()->GetTopBlock(Ladder);
			Check(FString::Printf(TEXT("the climbing wall has rungs (%d)"), LadderBlock ? LadderBlock->GetRungCount() : -1), LadderType != INDEX_NONE && LadderBlock && LadderBlock->bLadder && LadderBlock->GetRungCount() == 12);
		} });
		Steps.Add({ 3.4f, [=, this]
		{
			KeepOnlyOni(INDEX_NONE); // 鬼は後で 1 体だけ使う
			StandAt(Start, 0.f);
		} });
		Steps.Add({ 0.3f, [=, this]
		{
			PressClimb();
			NextTick([=, this]
			{
				Check(FString::Printf(TEXT("cannot climb before it is unlocked (climbing %d, unlocked %d)"), GetHider()->IsClimbing(), GM->IsClimbUnlocked()),
					!GetHider()->IsClimbing() && GetHider()->GetClimbCount() == 0 && !GM->IsClimbUnlocked());
				GS()->PrestigeLevels[static_cast<int32>(EPrestigeUpgrade::Climb)] = 1; // 転生のお店で買ったことにする
				GM->ApplyPrestigeToPlayer();
			});
		} });
		// 1) 1 段の壁によじ登る（通常壁は 1 秒）
		Steps.Add({ 0.3f, [=, this]
		{
			PressClimb();
			NextTick([=, this]
			{
				Check(FString::Printf(TEXT("Space climbs the wall in front (climbing %d, %.2f s)"), GetHider()->IsClimbing(), GetHider()->GetLastClimbSeconds()),
					GetHider()->IsClimbing() && FMath::IsNearlyEqual(GetHider()->GetLastClimbSeconds(), GM->WallTypes[0].ClimbSeconds, 0.01f));
			});
		} });
		Steps.Add({ 0.4f, [=, this]
		{
			Check(FString::Printf(TEXT("still climbing (cannot move meanwhile) (climbing %d)"), GetHider()->IsClimbing()), GetHider()->IsClimbing());
			Shot(TEXT("climb_01_climbing"));
		} });
		Steps.Add({ 1.0f, [=, this]
		{
			Check(FString::Printf(TEXT("stands on the 1-high wall (feet %.0f cm, climbing %d)"), FeetHeight(), GetHider()->IsClimbing()),
				!GetHider()->IsClimbing() && FMath::IsNearlyEqual(FeetHeight(), Grid()->GetBlockHeight(), 15.f));
			Shot(TEXT("climb_02_on_wall"));
			PressClimb(); // 2) となりの 2 段の壁へ（1 段ずつ）
		} });
		Steps.Add({ 1.4f, [=, this]
		{
			Check(FString::Printf(TEXT("climbs one more level onto the 2-high wall (feet %.0f cm)"), FeetHeight()),
				!GetHider()->IsClimbing() && FMath::IsNearlyEqual(FeetHeight(), Grid()->GetBlockHeight() * 2.f, 15.f) && GetHider()->GetClimbCount() == 2);
			// 3) すき間の先の壁へは跳べない（ジャンプは無い）
			PressClimb();
			NextTick([=, this]
			{
				Check(FString::Printf(TEXT("cannot cross the gap (climbing %d, vz %.0f)"), GetHider()->IsClimbing(), GetHider()->GetVelocity().Z),
					!GetHider()->IsClimbing() && GetHider()->GetClimbCount() == 2 && GetHider()->GetVelocity().Z < 50.f);
			});
		} });
		Steps.Add({ 0.4f, [=, this] { SimulateKey(EKeys::W, IE_Pressed); } }); // 歩いて縁から下りる
		Steps.Add({ 0.9f, [=, this] { SimulateKey(EKeys::W, IE_Released); } });
		Steps.Add({ 0.6f, [=, this]
		{
			const FIntPoint Here = Grid()->WorldToCell(GetPawn()->GetActorLocation());
			Check(FString::Printf(TEXT("walking off the edge drops nearly straight into the gap, not onto the wall beyond (feet %.0f cm, cell (%d,%d))"), FeetHeight(), Here.X, Here.Y),
				FeetHeight() < 20.f && Here == WallB + FIntPoint(1, 0));
			// 4) 床から 2 段の壁へは登れない（1 段ずつ）
			StandAt(WallB + FIntPoint(0, 1), -90.f);
		} });
		Steps.Add({ 0.3f, [=, this]
		{
			PressClimb();
			NextTick([=, this]
			{
				Check(FString::Printf(TEXT("cannot climb a 2-high wall from the floor (climbing %d)"), GetHider()->IsClimbing()), !GetHider()->IsClimbing());
			});
		} });
		// 5) クライミング壁はすばやく登れる
		Steps.Add({ 0.3f, [=, this] { StandAt(LadderFrom, -90.f); } });
		Steps.Add({ 0.3f, [=, this]
		{
			PressClimb();
			NextTick([=, this]
			{
				Check(FString::Printf(TEXT("the climbing wall is quick to climb (%.2f s, normal %.2f s)"), GetHider()->GetLastClimbSeconds(), GM->WallTypes[0].ClimbSeconds),
					GetHider()->IsClimbing() && GetHider()->GetLastClimbSeconds() < GM->WallTypes[0].ClimbSeconds * 0.5f);
			});
		} });
		Steps.Add({ 0.6f, [=, this]
		{
			Check(FString::Printf(TEXT("already on top of the climbing wall (feet %.0f cm)"), FeetHeight()), !GetHider()->IsClimbing() && FMath::IsNearlyEqual(FeetHeight(), Grid()->GetBlockHeight(), 15.f));
			GetPawn()->SetActorRotation(FRotator(0.f, 0.f, 0.f)); // 東の塔（2 段）へ
			PressClimb();
		} });
		Steps.Add({ 1.4f, [=, this]
		{
			Check(FString::Printf(TEXT("from the climbing wall onto the 2-high tower (feet %.0f cm)"), FeetHeight()), FMath::IsNearlyEqual(FeetHeight(), Grid()->GetBlockHeight() * 2.f, 15.f));
			// 6) 鬼は塔に飛び乗れない（クライミング壁にも乗れない）→ 下から壊すしかない
			AOniCharacter* Oni = GM->GetOnis().Num() > 0 ? GM->GetOnis()[0].Get() : nullptr;
			if (Oni)
			{
				Oni->SetActorLocation(Grid()->CellFloorCenter(Tower + FIntPoint(4, 2)) + FVector(0.f, 0.f, 100.f), false, nullptr, ETeleportType::TeleportPhysics);
				Oni->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
				Oni->Activate(GetHider());
				Oni->SightRadius = 3000.f;
				Oni->SightHalfAngle = 180.f;
				Oni->bDrawDebug = true;
			}
			LookAtOni();
		} });
		for (int32 i = 0; i < 24; ++i)
		{
			Steps.Add({ 0.5f, [=, this]
			{
				const AOniCharacter* Oni = GM->GetOnis().Num() > 0 ? GM->GetOnis()[0].Get() : nullptr;
				const bool bTowerUp = Grid()->GetColumnHeight(Tower) == 2;
				if (Oni && bTowerUp && CS->bTowerStood)
				{
					CS->LeapsWhileStanding = Oni->GetLeapCount();
					++CS->Samples;
				}
				CS->bTowerStood = CS->bTowerStood && bTowerUp;
				if (i == 6)
				{
					Shot(TEXT("climb_03_tower"));
				}
			} });
		}
		Steps.Add({ 0.2f, [=, this]
		{
			Check(FString::Printf(TEXT("the oni could not leap onto the tower or the climbing wall while the tower stood (leaps %d over %d samples)"), CS->LeapsWhileStanding, CS->Samples),
				CS->Samples > 0 && CS->LeapsWhileStanding == 0);
			Check(FString::Printf(TEXT("so it broke the tower from below (walls destroyed %d, phase %s)"), GS()->LastRoundWallsDestroyed, *UEnum::GetValueAsString(GS()->Phase)),
				GS()->LastRoundWallsDestroyed >= 1);
		} });
		// 7) 低い家具にはよじ登れる（家具はゆっくり）・高い家具には登れない
		Steps.Add({ 0.3f, [=, this]
		{
			KeepOnlyOni(INDEX_NONE);
			if (GS()->Phase != EKakurenboPhase::Hide)
			{
				KakuNext(); KakuNext(); KakuNext(); // 捕まっていたら、次のかくれんぼへ
			}
		} });
		Steps.Add({ 3.6f, [=, this]
		{
			KeepOnlyOni(INDEX_NONE);
			GS()->HideTimeRemaining = 300.f;
			StandAt(LowFrom, 0.f);
		} });
		Steps.Add({ 0.3f, [=, this]
		{
			PressClimb();
			NextTick([=, this]
			{
				Check(FString::Printf(TEXT("low furniture can be climbed, slowly (climbing %d, %.2f s)"), GetHider()->IsClimbing(), GetHider()->GetLastClimbSeconds()),
					GetHider()->IsClimbing() && FMath::IsNearlyEqual(GetHider()->GetLastClimbSeconds(), GM->FurnitureClimbSeconds, 0.01f));
			});
		} });
		Steps.Add({ 1.6f, [=, this]
		{
			Check(FString::Printf(TEXT("stands on the low furniture (feet %.0f cm)"), FeetHeight()), FMath::IsNearlyEqual(FeetHeight(), 80.f, 15.f));
			StandAt(TallFrom, 0.f);
		} });
		Steps.Add({ 0.3f, [=, this]
		{
			const int32 Before = GetHider()->GetClimbCount();
			PressClimb();
			NextTick([=, this]
			{
				Check(FString::Printf(TEXT("tall furniture (220 cm) cannot be climbed (climbing %d)"), GetHider()->IsClimbing()), !GetHider()->IsClimbing() && GetHider()->GetClimbCount() == Before);
			});
		} });
	}
	// ================================================================ Walls（M14：通常壁・消音壁・クライミング壁と、壁の補強）
	else if (Scenario.Equals(TEXT("Walls"), ESearchCase::IgnoreCase))
	{
		struct FWallsState { double HPBefore = 0.0; FIntPoint Cell = FIntPoint::ZeroValue; };
		TSharedRef<FWallsState> WS = MakeShared<FWallsState>();
		const int32 LadderType = GM->WallTypes.IndexOfByPredicate([](const FWallTypeDef& Def) { return Def.bLadder; });
		const int32 QuietType = GM->WallTypes.IndexOfByPredicate([](const FWallTypeDef& Def) { return Def.NoiseDamping > 0.f; });
		Steps.Add({ 0.5f, [=, this] { KakuSkipTime(1000.f); } });
		Steps.Add({ 1.f, [=, this]
		{
			KakuNext(); // → 購入
			KakuAddCoins(1000000.0);
			Check(FString::Printf(TEXT("three wall types: normal, soundproof, climbing (%d)"), GM->WallTypes.Num()),
				GM->WallTypes.Num() == 3 && LadderType == 2 && QuietType == 1 && !GM->WallTypes[0].bLadder && GM->WallTypes[0].NoiseDamping <= 0.f);
			// クライミング壁は「よじ登る」を解放するまで買えない
			const TArray<FShopItemView> Items = GM->GetShopItems();
			const int32 StockBefore = GS()->WallStock[LadderType];
			BuyWall(LadderType, 1);
			Check(FString::Printf(TEXT("the climbing wall is locked before climbing is unlocked (locked %d, stock %d)"), Items[GM->GetShopIndexOfWall(LadderType)].bLocked, GS()->WallStock[LadderType]),
				Items[GM->GetShopIndexOfWall(LadderType)].bLocked && GS()->WallStock[LadderType] == StockBefore && !Items[GM->GetShopIndexOfWall(0)].bLocked);
			// 壁の補強：足し算で伸びる（1 レベルで元の耐久の +50%）。説明に「何回で壊れるか」
			WS->HPBefore = GM->GetEffectiveWallTypes()[0].MaxHP;
			KakuBuy(3); // 3 番目の商品 = 壁の補強
			const double After1 = GM->GetEffectiveWallTypes()[0].MaxHP;
			KakuBuy(3);
			const double After2 = GM->GetEffectiveWallTypes()[0].MaxHP;
			const double Step = GM->WallTypes[0].MaxHP * GM->WallReinforcePerLevel;
			Check(FString::Printf(TEXT("wall reinforcement adds a fixed amount per level (%.2f -> %.2f -> %.2f)"), WS->HPBefore, After1, After2),
				GS()->WallReinforceLevel == 2 && FMath::IsNearlyEqual(After1 - WS->HPBefore, Step, 0.001) && FMath::IsNearlyEqual(After2 - After1, Step, 0.001));
			const FString Desc = GM->GetShopItems()[2].Description.ToString();
			Check(FString::Printf(TEXT("the reinforcement shows the hits to break ('%s')"), *Desc), Desc.Contains(TEXT("回")) && Desc.Contains(UKakurenboLibrary::FormatStatNumber(After2)));
			// 転生のお店の「壁の硬さ」はかけ算（補強の上に掛かる）
			GS()->PrestigeLevels[static_cast<int32>(EPrestigeUpgrade::WallHP)] = 1;
			const double WithPrestige = GM->GetEffectiveWallTypes()[0].MaxHP;
			Check(FString::Printf(TEXT("prestige wall hardness multiplies the reinforced HP (%.2f -> %.2f)"), After2, WithPrestige),
				FMath::IsNearlyEqual(WithPrestige, After2 * GM->GetPrestigeValue(EPrestigeUpgrade::WallHP, 1), 0.001));
			GS()->PrestigeLevels[static_cast<int32>(EPrestigeUpgrade::WallHP)] = 0;
			// よじ登るを解放すると、クライミング壁が買える
			GS()->PrestigeLevels[static_cast<int32>(EPrestigeUpgrade::Climb)] = 1;
			BuyWall(0, 2);
			BuyWall(QuietType, 1);
			BuyWall(LadderType, 1);
			Check(FString::Printf(TEXT("after unlocking climbing, the climbing wall can be bought (stock %d)"), GS()->WallStock[LadderType]),
				GS()->WallStock[LadderType] == 1 && !GM->GetShopItems()[GM->GetShopIndexOfWall(LadderType)].bLocked);
			Shot(TEXT("walls_01_shop"));
		} });
		Steps.Add({ 0.5f, [=, this]
		{
			KakuNext(); // → 設置
			const FIntPoint Me = Grid()->WorldToCell(GetPawn()->GetActorLocation());
			WS->Cell = Me + FIntPoint(2, 0);
			KakuPlaceWall(WS->Cell.X, WS->Cell.Y, 0);
			KakuPlaceWall(WS->Cell.X, WS->Cell.Y + 2, QuietType);
			KakuPlaceWall(WS->Cell.X, WS->Cell.Y - 2, LadderType);
			const APlaceableBlock* Normal = Grid()->GetTopBlock(WS->Cell);
			const APlaceableBlock* LadderBlock = Grid()->GetTopBlock(WS->Cell - FIntPoint(0, 2));
			Check(FString::Printf(TEXT("placed walls get the reinforced HP (%.2f, expected %.2f)"), Normal ? Normal->MaxHP : -1.0, GM->GetEffectiveWallTypes()[0].MaxHP),
				Normal && FMath::IsNearlyEqual(Normal->MaxHP, GM->GetEffectiveWallTypes()[0].MaxHP, 0.001));
			Check(TEXT("the climbing wall has rungs, the others do not"), LadderBlock && LadderBlock->bLadder && LadderBlock->GetRungCount() > 0 && Normal && Normal->GetRungCount() == 0);
			GetHider()->AddTopDownTilt(40.f); // 斜めから見る（横木が見えるように）
		} });
		Steps.Add({ 1.0f, [=] { Shot(TEXT("walls_02_build")); } });
		Steps.Add({ 0.3f, [=, this]
		{
			KakuShop(); // 購入パートへ戻って補強をもう 1 つ：置いてある壁もすぐ硬くなる
			const double Before = Grid()->GetTopBlock(WS->Cell)->MaxHP;
			KakuBuy(3);
			const double After = Grid()->GetTopBlock(WS->Cell)->MaxHP;
			Check(FString::Printf(TEXT("placed walls get harder right away (%.2f -> %.2f, expected %.2f)"), Before, After, GM->GetEffectiveWallTypes()[0].MaxHP),
				FMath::IsNearlyEqual(After, GM->GetEffectiveWallTypes()[0].MaxHP, 0.001) && After > Before);
		} });
	}
	// ================================================================ Gamepad（M14：コントローラーで遊ぶ）
	else if (Scenario.Equals(TEXT("Gamepad"), ESearchCase::IgnoreCase))
	{
		// コントローラーの入力は PlayerController の InputKey を通す（最後に触った機器の切り替えも確かめる）
		auto PadPress = [this](const FKey& Key)
		{
			InputKey(FInputKeyEventArgs::CreateSimulated(Key, IE_Pressed, 1.f));
			InputKey(FInputKeyEventArgs::CreateSimulated(Key, IE_Released, 0.f));
		};
		auto PadHold = [this](const FKey& Key, bool bDown) { InputKey(FInputKeyEventArgs::CreateSimulated(Key, bDown ? IE_Pressed : IE_Released, bDown ? 1.f : 0.f)); };
		auto PadAxis = [this](const FKey& Key, float Value) { InputKey(FInputKeyEventArgs::CreateSimulated(Key, IE_Axis, Value, 1)); };
		struct FPadState { FVector Pos = FVector::ZeroVector; FRotator Rot = FRotator::ZeroRotator; int32 Mash = 0; FIntPoint Cell = FIntPoint::ZeroValue; int32 Stock = 0; float Volume = 0.f; };
		TSharedRef<FPadState> PS = MakeShared<FPadState>();
		Steps.Add({ 3.4f, [=, this]
		{
			SetStageOniTypes({ EOniType::Balanced });
			KeepOnlyOni(INDEX_NONE); // 鬼に捕まらないように
			GS()->HideTimeRemaining = GS()->HideTimeLimit = 300.f;
			PS->Pos = GetPawn()->GetActorLocation();
			SetControlRotation(FRotator(-20.f, 0.f, 0.f));
			PadAxis(EKeys::Gamepad_LeftY, 1.f); // 左スティックを前へ
		} });
		Steps.Add({ 0.6f, [=, this]
		{
			PadAxis(EKeys::Gamepad_LeftY, 0.f);
			const float Moved = FVector::DotProduct(GetPawn()->GetActorLocation() - PS->Pos, FVector::ForwardVector);
			Check(FString::Printf(TEXT("the left stick walks forward (%.0f cm) and the game switches to the controller (%d)"), Moved, IsUsingGamepad()), Moved > 100.f && IsUsingGamepad());
			Check(FString::Printf(TEXT("the on-screen controls show controller buttons (climb key '%s')"), *KeyName(EKakurenboAction::Climb)), KeyName(EKakurenboAction::Climb) == TEXT("A"));
			PS->Rot = GetControlRotation();
			PadAxis(EKeys::Gamepad_RightX, 1.f); // 右スティックでカメラ
		} });
		Steps.Add({ 0.5f, [=, this]
		{
			PadAxis(EKeys::Gamepad_RightX, 0.f);
			const float DYaw = FRotator::NormalizeAxis(GetControlRotation().Yaw - PS->Rot.Yaw);
			Check(FString::Printf(TEXT("the right stick turns the camera (dYaw %.1f)"), DYaw), DYaw > 40.f);
			const AKakurenboHUD* HUD = GetHUD<AKakurenboHUD>();
			Check(TEXT("the hide controls line shows controller buttons"), HUD && HUD->HasDrawnText(TEXT("[RT] 連打")) && HUD->HasDrawnText(TEXT("[L スティック] 移動")));
			PS->Mash = GS()->MashCountThisRound;
			PadPress(EKeys::Gamepad_RightTrigger); // RT：連打
			PadHold(EKeys::Gamepad_LeftTrigger, true); // LT：しゃがむ
			NextTick([=, this]
			{
				Check(FString::Printf(TEXT("RT mashes (%d -> %d), LT crouches (%d)"), PS->Mash, GS()->MashCountThisRound, GetHider()->IsSneaking()),
					GS()->MashCountThisRound == PS->Mash + 1 && GetHider()->IsSneaking());
			});
			Shot(TEXT("gamepad_01_hide"));
		} });
		Steps.Add({ 0.3f, [=, this]
		{
			PadHold(EKeys::Gamepad_LeftTrigger, false);
			// A：よじ登る（目の前に壁を置く）
			GS()->PrestigeLevels[static_cast<int32>(EPrestigeUpgrade::Climb)] = 1;
			GM->ApplyPrestigeToPlayer();
			const FIntPoint Me = Grid()->WorldToCell(GetPawn()->GetActorLocation());
			TeleportPlayer(Grid()->CellFloorCenter(Me));
			PlaceTestWall(Me + FIntPoint(1, 0));
			GetPawn()->SetActorRotation(FRotator::ZeroRotator);
		} });
		Steps.Add({ 0.3f, [=, this]
		{
			PadPress(EKeys::Gamepad_FaceButton_Bottom);
			NextTick([=, this] { Check(FString::Printf(TEXT("A climbs (climbing %d)"), GetHider()->IsClimbing()), GetHider()->IsClimbing()); });
		} });
		// START：一時停止メニュー。十字キーで選んで A、B でもどる（一時停止中はワールドのタイマーが止まるので実時間で進める）
		Steps.Add({ 1.4f, [=, this]
		{
			PadPress(EKeys::Gamepad_Special_Right);
			RealDelay(0.3f, [=, this]
			{
				Check(FString::Printf(TEXT("START opens the pause menu (menu %d, paused %d)"), static_cast<int32>(GetMenu()), IsPaused()), GetMenu() == EKakurenboMenu::Pause && IsPaused());
				Check(TEXT("no mouse cursor while using the controller"), !bShowMouseCursor);
				PadPress(EKeys::Gamepad_DPad_Down);
				RealDelay(0.2f, [=, this]
				{
					Check(TEXT("the first menu item was selected, D-pad down moves to the next"), IsUIFocused(EKakurenboUIAction::MenuItem, 1));
					PadPress(EKeys::Gamepad_DPad_Down);
					RealDelay(0.2f, [=, this]
					{
						Check(TEXT("D-pad down moves to the settings item"), IsUIFocused(EKakurenboUIAction::MenuItem, static_cast<int32>(EKakurenboPauseItem::Settings)));
						PadPress(EKeys::Gamepad_FaceButton_Bottom);
						RealDelay(0.3f, [=, this]
						{
							Check(FString::Printf(TEXT("A opens the settings (menu %d)"), static_cast<int32>(GetMenu())), GetMenu() == EKakurenboMenu::Settings);
							PS->Volume = Settings()->MasterVolume;
							PadPress(EKeys::Gamepad_DPad_Left);
							RealDelay(0.2f, [=, this]
							{
								Check(FString::Printf(TEXT("the first slider is selected and left turns it down (%.2f -> %.2f)"), PS->Volume, Settings()->MasterVolume),
									IsUIFocused(EKakurenboUIAction::Slider, 0) && FMath::IsNearlyEqual(Settings()->MasterVolume, PS->Volume - 0.05f, 0.001f));
								Shot(TEXT("gamepad_02_settings"));
								RealDelay(0.3f, [=, this]
								{
									PadPress(EKeys::Gamepad_FaceButton_Right); // B：もどる
									RealDelay(0.2f, [=, this]
									{
										Check(TEXT("B goes back to the pause menu"), GetMenu() == EKakurenboMenu::Pause);
										PadPress(EKeys::Gamepad_Special_Right); // START：閉じる
										RealDelay(0.2f, [=, this] { Check(TEXT("START closes the menu and resumes"), !IsMenuOpen() && !IsPaused()); });
									});
								});
							});
						});
					});
				});
			});
		} });
		// リザルト → 購入（十字キーで選んで A で買う・LB/RB でお店の切り替え）
		Steps.Add({ 3.0f, [=, this] { KakuSkipTime(1000.f); } });
		Steps.Add({ 1.0f, [=, this]
		{
			Check(TEXT("result: the next button is selected"), GS()->Phase == EKakurenboPhase::Result && IsUIFocused(EKakurenboUIAction::NextPhase, 0));
			PadPress(EKeys::Gamepad_FaceButton_Bottom);
		} });
		Steps.Add({ 0.5f, [=, this]
		{
			Check(FString::Printf(TEXT("A on the next button goes to the shop (%s)"), *UEnum::GetValueAsString(GS()->Phase)), GS()->Phase == EKakurenboPhase::Shop);
			KakuAddCoins(100000.0);
			// 設置パートのカーソルの通り道に、自分（スタート位置）や前に置いた壁が無いように
			Grid()->ClearAllBlocks();
			TeleportPlayer(Grid()->CellFloorCenter(FIntPoint(1, 1)));
			PadPress(EKeys::Gamepad_DPad_Down);
		} });
		Steps.Add({ 0.2f, [=, this] { PadPress(EKeys::Gamepad_DPad_Down); } });
		Steps.Add({ 0.2f, [=, this] { PadPress(EKeys::Gamepad_DPad_Down); } });
		Steps.Add({ 0.2f, [=, this]
		{
			Check(TEXT("D-pad moves down the shop list to the first wall"), IsUIFocused(EKakurenboUIAction::ShopItem, GM->GetShopIndexOfWall(0)));
			PS->Stock = GS()->WallStock[0];
			PadPress(EKeys::Gamepad_FaceButton_Bottom);
		} });
		Steps.Add({ 0.3f, [=, this]
		{
			Check(FString::Printf(TEXT("A buys the selected item (stock %d -> %d)"), PS->Stock, GS()->WallStock[0]), GS()->WallStock[0] == PS->Stock + 1);
			Shot(TEXT("gamepad_03_shop"));
			PadPress(EKeys::Gamepad_RightShoulder);
		} });
		Steps.Add({ 0.3f, [=, this]
		{
			Check(TEXT("RB switches to the prestige shop"), IsPrestigeShopTab());
			PadPress(EKeys::Gamepad_LeftShoulder);
		} });
		Steps.Add({ 0.3f, [=, this]
		{
			Check(TEXT("LB switches back"), !IsPrestigeShopTab());
			PadPress(EKeys::Gamepad_FaceButton_Top); // Y：設置へ
		} });
		// 設置：左スティックでカーソル（カメラが付いてくる）、A で置く、X で回収、十字キーで 1 マス
		Steps.Add({ 0.6f, [=, this]
		{
			Check(FString::Printf(TEXT("Y goes to the build phase (%s)"), *UEnum::GetValueAsString(GS()->Phase)), GS()->Phase == EKakurenboPhase::Build);
			PS->Cell = BuildTargetCell;
			PadAxis(EKeys::Gamepad_LeftX, 1.f);
		} });
		Steps.Add({ 0.4f, [=, this]
		{
			PadAxis(EKeys::Gamepad_LeftX, 0.f);
			Check(FString::Printf(TEXT("the left stick moves the build cursor ((%d,%d) -> (%d,%d))"), PS->Cell.X, PS->Cell.Y, BuildTargetCell.X, BuildTargetCell.Y),
				bHasBuildTarget && BuildTargetCell != PS->Cell);
			PS->Cell = BuildTargetCell;
			PadPress(EKeys::Gamepad_DPad_Right);
		} });
		Steps.Add({ 0.3f, [=, this]
		{
			const int32 Moved = FMath::Abs(BuildTargetCell.X - PS->Cell.X) + FMath::Abs(BuildTargetCell.Y - PS->Cell.Y);
			Check(FString::Printf(TEXT("the D-pad moves the cursor one cell ((%d,%d) -> (%d,%d))"), PS->Cell.X, PS->Cell.Y, BuildTargetCell.X, BuildTargetCell.Y), Moved == 1);
			PS->Stock = GS()->WallStock[0];
			PS->Cell = BuildTargetCell;
			PadPress(EKeys::Gamepad_FaceButton_Bottom);
		} });
		Steps.Add({ 0.3f, [=, this]
		{
			Check(FString::Printf(TEXT("A places a wall at the cursor (height %d, stock %d -> %d)"), Grid()->GetColumnHeight(PS->Cell), PS->Stock, GS()->WallStock[0]),
				Grid()->GetColumnHeight(PS->Cell) == 1 && GS()->WallStock[0] == PS->Stock - 1);
			Shot(TEXT("gamepad_04_build"));
			PadPress(EKeys::Gamepad_FaceButton_Left);
		} });
		Steps.Add({ 0.3f, [=, this]
		{
			Check(FString::Printf(TEXT("X picks it up (height %d, stock %d)"), Grid()->GetColumnHeight(PS->Cell), GS()->WallStock[0]),
				Grid()->GetColumnHeight(PS->Cell) == 0 && GS()->WallStock[0] == PS->Stock);
			PadPress(EKeys::Gamepad_RightShoulder);
		} });
		Steps.Add({ 0.3f, [=, this]
		{
			Check(FString::Printf(TEXT("RB selects the next item (slot %d)"), SelectedBuildSlot), SelectedBuildSlot == 1);
			PadPress(EKeys::Gamepad_FaceButton_Right); // B：購入へ戻る
		} });
		Steps.Add({ 0.4f, [=, this]
		{
			Check(TEXT("B goes back to the shop"), GS()->Phase == EKakurenboPhase::Shop);
			PadPress(EKeys::Gamepad_FaceButton_Top);
		} });
		Steps.Add({ 0.4f, [=, this] { PadPress(EKeys::Gamepad_FaceButton_Top); } });
		Steps.Add({ 0.5f, [=, this]
		{
			Check(TEXT("Y twice starts hiding"), GS()->Phase == EKakurenboPhase::Hide);
			// マウスを動かすとキーボード・マウスに戻る
			InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::MouseX, IE_Axis, 25.f, 1));
			NextTick([=, this]
			{
				Check(FString::Printf(TEXT("moving the mouse switches back to keyboard and mouse (gamepad %d, climb key '%s')"), IsUsingGamepad(), *KeyName(EKakurenboAction::Climb)),
					!IsUsingGamepad() && KeyName(EKakurenboAction::Climb) == TEXT("Space"));
			});
		} });
		// M14：任天堂の配置（設定画面の「コントローラーの配置」）。＋でメニュー、下のボタン（任天堂の B）で戻る
		Steps.Add({ 0.5f, [=, this]
		{
			ToggleSetting(EKakurenboSettingToggle::PadLayout);
			Check(FString::Printf(TEXT("the settings toggle switches to the Nintendo layout (%d)"), UKakurenboSettings::IsNintendoLayout()), UKakurenboSettings::IsNintendoLayout());
			PadPress(EKeys::Gamepad_Special_Right); // ＋：メニュー
			RealDelay(0.3f, [=, this]
			{
				Check(FString::Printf(TEXT("nintendo layout: + opens the menu, hints show [A] %s / [B] %s / %s"), *UIKeyLabel(EKakurenboUIKey::Confirm), *UIKeyLabel(EKakurenboUIKey::Cancel),
					*UIKeyLabel(EKakurenboUIKey::Menu)), GetMenu() == EKakurenboMenu::Pause && UIKeyLabel(EKakurenboUIKey::Menu) == TEXT("[＋]")
					&& UIKeyLabel(EKakurenboUIKey::Cancel) == TEXT("[B]") && KeyName(EKakurenboAction::Climb) == TEXT("A"));
				Shot(TEXT("gamepad_nintendo_menu"));
				PadPress(EKeys::Gamepad_FaceButton_Bottom); // 任天堂の B（下のボタン）：もどる
				RealDelay(0.3f, [=, this]
				{
					Check(FString::Printf(TEXT("nintendo layout: the bottom button (B) closes the menu (menu %d)"), static_cast<int32>(GetMenu())), !IsMenuOpen() && !IsPaused());
					UKakurenboSettings::SetPadLayout(EKakurenboPadLayout::Xbox);
				});
			});
		} });
	}
	// ================================================================ CrouchFind（M14：しゃがむと無敵、にならない。近くではのぞき込まれる・すぐそばでは気配で気づかれる）
	else if (Scenario.Equals(TEXT("CrouchFind"), ESearchCase::IgnoreCase))
	{
		struct FCrouch { TObjectPtr<AStaticMeshActor> Sofa; int32 NoticesBeforeWall = 0; };
		TSharedRef<FCrouch> C = MakeShared<FCrouch>();
		const FIntPoint Me(6, 12);
		Steps.Add({ 0.3f, [=] { SetStageOniTypes({ EOniType::Balanced }); } });
		Steps.Add({ 3.3f, [=, this]
		{
			GS()->HideTimeRemaining = 600.f;
			AOniCharacter* Oni = KeepOnlyOni(0);
			if (Oni)
			{
				Oni->Deactivate(); // 見え方だけを調べる（動かない）
			}
			TeleportPlayer(Grid()->CellFloorCenter(Me));
			bTestSneak = true; // しゃがむ
			// 東側に、ソファくらいの高さ（110cm）の家具を、プレイヤーにくっつけて置く（家具の陰にしゃがむ）
			UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
			const FVector Floor = Grid()->CellFloorCenter(Me);
			C->Sofa = GetWorld()->SpawnActor<AStaticMeshActor>(Floor + FVector(85.f, 0.f, 55.f), FRotator::ZeroRotator);
			C->Sofa->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
			C->Sofa->GetStaticMeshComponent()->SetStaticMesh(Cube);
			C->Sofa->SetActorScale3D(FVector(1.f, 1.f, 1.1f));
		} });
		auto PlaceOni = [=, this](float DistanceCm)
		{
			AOniCharacter* Oni = GM->GetOnis()[0];
			const FVector Floor = Grid()->CellFloorCenter(Me);
			Oni->SetActorLocation(Floor + FVector(DistanceCm, 0.f, Oni->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 2.f), false, nullptr, ETeleportType::TeleportPhysics);
			Oni->SetActorRotation(FRotator(0.f, 180.f, 0.f)); // プレイヤーの方を向く
			return Oni;
		};
		Steps.Add({ 0.6f, [=, this]
		{
			Check(FString::Printf(TEXT("the player is crouching (%d)"), GetHider()->bIsCrouched ? 1 : 0), GetHider()->bIsCrouched);
			const AOniCharacter* Oni = PlaceOni(800.f);
			Check(TEXT("from 8 m, a crouching player behind low furniture (110 cm) is hidden"), !Oni->CanSeeTarget());
			const AOniCharacter* Near = PlaceOni(230.f);
			Check(TEXT("from 2.3 m, the oni peeks over the furniture and sees the crouching player"), Near->CanSeeTarget());
			Shot(TEXT("crouchfind_01_peek"));
		} });
		Steps.Add({ 0.3f, [=, this]
		{
			// 立っている相手は、壁 1 段（160cm）の陰なら近くても見えない（今まで通り）
			bTestSneak = false;
			C->Sofa->SetActorHiddenInGame(true);
			C->Sofa->SetActorEnableCollision(false);
			PlaceTestWall(Me + FIntPoint(1, 0));
		} });
		Steps.Add({ 0.6f, [=, this]
		{
			Check(FString::Printf(TEXT("the player stands up (%d)"), GetHider()->bIsCrouched ? 1 : 0), !GetHider()->bIsCrouched);
			const AOniCharacter* Oni = PlaceOni(300.f);
			Check(TEXT("a standing player behind a one-tier wall is still hidden from 3 m"), !Oni->CanSeeTarget());
			Grid()->DamageBlocksInRadius(Grid()->CellToWorld(Me + FIntPoint(1, 0), 0), 10.f, 1000.0); // 壁をどける
			// 気配：見えない（目を閉じた）鬼をすぐそば（1.2m・背を向けて）に置き、動かないようにする
			bTestSneak = true;
			AOniCharacter* Sense = PlaceOni(120.f);
			Sense->SetActorRotation(FRotator(0.f, 0.f, 0.f)); // 背を向ける
			Sense->SightRadius = 0.f;
			Sense->CloseSenseRadius = 0.f;
			Sense->HearingRadius = 0.f;
			Sense->StepHearingRadius = 0.f;
			Sense->Activate(GetHider());
			Sense->GetCharacterMovement()->DisableMovement();
		} });
		Steps.Add({ 2.0f, [=, this]
		{
			const AOniCharacter* Oni = GM->GetOnis()[0];
			Check(FString::Printf(TEXT("an oni lingering right next to a hidden player notices them (presence notices %d, notice sounds %d)"), Oni->GetPresenceNoticeCount(), Oni->GetNoticeSoundCount()),
				Oni->GetPresenceNoticeCount() >= 1 && Oni->GetNoticeSoundCount() >= 1);
			// 壁 1 段の向こうの気配は伝わらない
			C->NoticesBeforeWall = Oni->GetPresenceNoticeCount();
			AOniCharacter* Behind = GM->GetOnis()[0];
			Behind->Deactivate();
			PlaceTestWall(Me + FIntPoint(1, 0));
			PlaceOni(200.f);
			Behind->Activate(GetHider());
			Behind->GetCharacterMovement()->DisableMovement();
			C->Sofa->SetActorLocation(FVector(0.f, 0.f, -5000.f));
		} });
		Steps.Add({ 2.0f, [=, this]
		{
			const AOniCharacter* Oni = GM->GetOnis()[0];
			Check(FString::Printf(TEXT("presence does not pass through a wall (presence %.2f, notices %d -> %d)"), Oni->GetPresence(), C->NoticesBeforeWall, Oni->GetPresenceNoticeCount()),
				Oni->GetPresenceNoticeCount() == C->NoticesBeforeWall && Oni->GetPresence() < 0.05f);
			bTestSneak = false;
		} });
	}
	// ================================================================ CarefulFind（M14：慎重鬼 2 体が、本物の館（客間）で家具のとなりにしゃがんで隠れたプレイヤーを見つけるまでの時間。
	//   のぞき込み・気配なし / あり を比べる。約 8 分。探し方を変えたら流して、前と比べる）
	else if (Scenario.Equals(TEXT("CarefulFind"), ESearchCase::IgnoreCase))
	{
		struct FFind { TArray<FIntPoint> Spots; TArray<float> Times; float Start = 0.f; bool bFound = false; };
		TSharedRef<FFind> F = MakeShared<FFind>();
		constexpr int32 Variants = 2;
		constexpr int32 SpotCount = 4;
		constexpr int32 PollSeconds = 45;
		static const TCHAR* VariantNames[Variants] = { TEXT("no-peek-presence"), TEXT("peek+presence") };
		Steps.Add({ 0.3f, [=, this]
		{
			SetStageOniTypes({ EOniType::Careful, EOniType::Careful });
			GM->bMapOverride = true;
			GM->MapOverride = TEXT("Rooms");
			GM->SwitchToMap(TEXT("Rooms"));
			// 隠れ場所：門から遠い西半分の、家具のとなりの歩けるマス（4 つ。6 マス以上離す）
			UKakurenboGridSubsystem* G = Grid();
			const FIntPoint Gate = GM->GetOniGateCells()[0];
			TArray<TPair<int32, FIntPoint>> Candidates;
			for (int32 Y = 1; Y < G->GetSizeY() - 1; ++Y)
			{
				for (int32 X = 1; X < G->GetSizeX() / 2; ++X)
				{
					const FIntPoint P(X, Y);
					if (!G->IsWalkable(P) || G->IsObstacle(P))
					{
						continue;
					}
					int32 Furniture = 0;
					for (const FIntPoint& D : { FIntPoint(1, 0), FIntPoint(-1, 0), FIntPoint(0, 1), FIntPoint(0, -1) })
					{
						const TCHAR Ch = GM->GetArena()->GetLayoutChar(P + D);
						Furniture += (G->IsObstacle(P + D) && Ch != KakurenboMaps::WallChar) ? 1 : 0;
					}
					if (Furniture > 0)
					{
						Candidates.Emplace(FMath::Abs(P.X - Gate.X) + FMath::Abs(P.Y - Gate.Y), P);
					}
				}
			}
			Candidates.Sort([](const TPair<int32, FIntPoint>& A, const TPair<int32, FIntPoint>& B) { return A.Key > B.Key; });
			for (const TPair<int32, FIntPoint>& Cand : Candidates)
			{
				bool bFar = true;
				for (const FIntPoint& Spot : F->Spots)
				{
					bFar &= FMath::Abs(Spot.X - Cand.Value.X) + FMath::Abs(Spot.Y - Cand.Value.Y) >= 6;
				}
				if (bFar && F->Spots.Num() < SpotCount)
				{
					F->Spots.Add(Cand.Value);
				}
			}
			Check(FString::Printf(TEXT("found %d hiding spots next to furniture"), F->Spots.Num()), F->Spots.Num() == SpotCount);
			KakuSkipTime(1000.f); // 今のラウンドを終わらせて、試すたびに新しいラウンドで鬼を門から出す
		} });
		for (int32 Trial = 0; Trial < Variants * SpotCount; ++Trial)
		{
			const int32 Variant = Trial / SpotCount;
			const int32 SpotIndex = Trial % SpotCount;
			Steps.Add({ 0.6f, [=, this] { KakuNext(); } }); // → 購入
			Steps.Add({ 0.5f, [=, this] { KakuNext(); } }); // → 設置
			Steps.Add({ 0.5f, [=, this]
			{
				KakuNext(); // → かくれんぼ（鬼は 3 秒後に門から出てくる）
				F->bFound = false;
			} });
			Steps.Add({ 0.3f, [=, this]
			{
				GS()->HideTimeRemaining = GS()->HideTimeLimit = 300.f;
				if (UKakurenboOniBlackboard* BB = GetWorld()->GetSubsystem<UKakurenboOniBlackboard>())
				{
					BB->Reset(Grid()->GetSizeX(), Grid()->GetSizeY());
				}
				if (F->Spots.IsValidIndex(SpotIndex))
				{
					TeleportPlayer(Grid()->CellFloorCenter(F->Spots[SpotIndex]));
				}
				bTestSneak = true;
			} });
			Steps.Add({ 3.3f, [=, this]
			{
				for (AOniCharacter* Oni : GM->GetOnis())
				{
					if (Variant == 0)
					{
						Oni->PeekRadius = 0.f;
						Oni->PresenceRadius = 0.f;
					}
				}
				F->Start = GetWorld()->GetTimeSeconds();
			} });
			for (int32 Poll = 0; Poll < PollSeconds; ++Poll)
			{
				Steps.Add({ 1.0f, [=, this]
				{
					if (!F->bFound && GS()->Phase != EKakurenboPhase::Hide)
					{
						F->bFound = true;
						F->Times.Add(GetWorld()->GetTimeSeconds() - F->Start);
					}
				} });
			}
			Steps.Add({ 0.1f, [=, this]
			{
				if (!F->bFound)
				{
					F->Times.Add(-1.f); // 見つからなかった
					KakuSkipTime(1000.f);
				}
				UE_LOG(LogTemp, Display, TEXT("[AutoTest] carefulfind %s spot %d (%d,%d): %s"), VariantNames[Variant], SpotIndex,
					F->Spots.IsValidIndex(SpotIndex) ? F->Spots[SpotIndex].X : -1, F->Spots.IsValidIndex(SpotIndex) ? F->Spots[SpotIndex].Y : -1,
					F->Times.Last() < 0.f ? *FString::Printf(TEXT("not found in %d s"), PollSeconds) : *FString::Printf(TEXT("found after %.0f s"), F->Times.Last()));
				bTestSneak = false;
			} });
		}
		Steps.Add({ 0.5f, [=, this]
		{
			FString Summary;
			int32 FoundFull = 0;
			for (int32 i = 0; i < F->Times.Num(); ++i)
			{
				Summary += FString::Printf(TEXT(" %s#%d=%s"), VariantNames[i / SpotCount], i % SpotCount, F->Times[i] < 0.f ? TEXT("-") : *FString::Printf(TEXT("%.0fs"), F->Times[i]));
				FoundFull += (i / SpotCount == Variants - 1 && F->Times[i] >= 0.f) ? 1 : 0;
			}
			Check(FString::Printf(TEXT("careful onis find a crouching player hidden next to furniture (the current search finds %d of %d):%s"), FoundFull, SpotCount, *Summary),
				F->Times.Num() == Variants * SpotCount && FoundFull >= SpotCount - 1);
		} });
	}
	// ================================================================ CameraWall（M14：壁に当たって寄ったカメラは、壁から離れたらゆっくり戻る。グワングワン揺れない）
	else if (Scenario.Equals(TEXT("CameraWall"), ESearchCase::IgnoreCase))
	{
		struct FCam { FIntPoint Spot = FIntPoint(-1, -1); FIntPoint Dir = FIntPoint(1, 0); float Short = 0.f; float Soon = 0.f; };
		TSharedRef<FCam> Cam = MakeShared<FCam>();
		Steps.Add({ 0.3f, [=, this]
		{
			SetStageOniTypes({ EOniType::Balanced });
			GM->bMapOverride = true;
			GM->MapOverride = TEXT("Crown");
			GM->SwitchToMap(TEXT("Crown"));
		} });
		Steps.Add({ 1.0f, [=, this]
		{
			GS()->HideTimeRemaining = 600.f;
			KeepOnlyOni(-1);
			// 後ろが部屋の壁（#）で、前に 7 マス歩ける場所を探す
			const AKakurenboArena* Arena = GM->GetArena();
			UKakurenboGridSubsystem* G = Grid();
			for (int32 Y = 0; Y < G->GetSizeY() && Cam->Spot.X < 0; ++Y)
			{
				for (int32 X = 0; X < G->GetSizeX() && Cam->Spot.X < 0; ++X)
				{
					for (const FIntPoint& D : { FIntPoint(1, 0), FIntPoint(-1, 0), FIntPoint(0, 1), FIntPoint(0, -1) })
					{
						const FIntPoint P(X, Y);
						bool bOpen = Arena->GetLayoutChar(P - D) == KakurenboMaps::WallChar;
						for (int32 k = 0; k <= 7 && bOpen; ++k)
						{
							bOpen = G->IsWalkable(P + D * k) && !G->IsObstacle(P + D * k);
						}
						if (bOpen)
						{
							Cam->Spot = P;
							Cam->Dir = D;
							break;
						}
					}
				}
			}
			Check(FString::Printf(TEXT("found a spot with a room wall behind (%d,%d dir %d,%d)"), Cam->Spot.X, Cam->Spot.Y, Cam->Dir.X, Cam->Dir.Y), Cam->Spot.X >= 0);
			const float Yaw = FVector(Cam->Dir.X, Cam->Dir.Y, 0.f).Rotation().Yaw;
			TeleportPlayer(G->CellFloorCenter(Cam->Spot));
			GetPawn()->SetActorRotation(FRotator(0.f, Yaw, 0.f));
			SetControlRotation(FRotator(-15.f, Yaw, 0.f)); // カメラは後ろ＝壁の方
		} });
		Steps.Add({ 1.0f, [=, this]
		{
			Cam->Short = GetHider()->GetCameraArmLength();
			Check(FString::Printf(TEXT("camera moves in front of the wall behind (arm %.0f)"), Cam->Short), Cam->Short < 200.f);
			Shot(TEXT("camerawall_01_wall_behind"));
			const float Yaw = FVector(-Cam->Dir.X, -Cam->Dir.Y, 0.f).Rotation().Yaw;
			SetControlRotation(FRotator(-15.f, Yaw, 0.f)); // 向きを変える：カメラの後ろは 7 マス空いている
		} });
		Steps.Add({ 0.12f, [=, this]
		{
			Cam->Soon = GetHider()->GetCameraArmLength();
			Check(FString::Printf(TEXT("after turning away from the wall the camera eases out instead of jumping (%.0f -> %.0f in 0.12 s)"), Cam->Short, Cam->Soon),
				Cam->Soon >= Cam->Short - 1.f && Cam->Soon < Cam->Short + 180.f);
		} });
		Steps.Add({ 2.5f, [=, this]
		{
			const float Late = GetHider()->GetCameraArmLength();
			Check(FString::Printf(TEXT("the camera returns to its distance (%.0f -> %.0f, wanted %.0f)"), Cam->Short, Late, GetHider()->ThirdPersonDistance),
				Late > GetHider()->ThirdPersonDistance - 40.f);
			Shot(TEXT("camerawall_02_open"));
		} });
	}
	// ================================================================ GemShop（M15：お宝で宝石・全部取るとボーナス・宝石のお店・まとめ買い）
	else if (Scenario.Equals(TEXT("GemShop"), ESearchCase::IgnoreCase))
	{
		struct FGemTest { int32 Treasures = 0; int32 GemsBefore = 0; double CoinsBefore = 0.0; int32 MashLevel = 0; };
		TSharedRef<FGemTest> T = MakeShared<FGemTest>();
		Steps.Add({ 0.5f, [=, this]
		{
			SetStageOniTypes({ EOniType::Balanced });
			T->Treasures = GM->GetTreasures().Num();
			T->GemsBefore = GS()->Gems;
			T->CoinsBefore = GS()->Coins;
			Check(FString::Printf(TEXT("treasures spawned (%d)"), T->Treasures), T->Treasures > 0);
		} });
		// お宝を 1 つずつ取りに行く（鬼が出てくる前に）
		for (int32 i = 0; i < 4; ++i)
		{
			Steps.Add({ 0.3f, [=, this]
			{
				if (GM->GetTreasures().Num() > 0 && GM->GetTreasures()[0])
				{
					TeleportPlayer(GM->GetTreasures()[0]->GetActorLocation());
				}
			} });
		}
		Steps.Add({ 0.4f, [=, this]
		{
			const AKakurenboGameState* S = GS();
			Check(FString::Printf(TEXT("every treasure gives a gem, plus the complete bonus (gems %d -> %d, treasures %d, bonus %d)"), T->GemsBefore, S->Gems, T->Treasures, GM->AllTreasureBonusGems),
				S->Gems == T->GemsBefore + T->Treasures * GM->GetGemsPerTreasure() + GM->AllTreasureBonusGems && S->bAllTreasuresThisRound);
			Check(FString::Printf(TEXT("treasures give no coins (coins %.1f -> %.1f, only time income)"), T->CoinsBefore, S->Coins), S->Coins - T->CoinsBefore < 10.0 && S->TreasureBonusCoinsThisRound == 0.0);
			Check(TEXT("the complete sound played"), Sound()->GetPlayCount(EKakurenboSfx::Gem) == 1);
			KakuSkipTime(1000.f);
		} });
		Steps.Add({ 1.f, [=, this]
		{
			Check(TEXT("result shows the gems"), GS()->GemsThisRound == T->Treasures + GM->AllTreasureBonusGems);
			Shot(TEXT("gemshop_01_result"));
		} });
		Steps.Add({ 0.5f, [=, this]
		{
			KakuNext(); // → 購入
			KakuAddCoins(100000.0);
			T->MashLevel = GS()->MashIncomeLevel;
			BuyAmount = EKakurenboBuyAmount::Ten;
		} });
		// まとめ買い：×10 で連打コイン強化を押す
		Steps.Add({ 0.3f, [=, this]
		{
			Check(TEXT("the x10 button is shown"), GetHUD<AKakurenboHUD>() && [this] { FVector2D C; return GetHUD<AKakurenboHUD>()->GetButtonCenter(EKakurenboUIAction::BuyAmount, 1, C); }());
			ClickUI(EKakurenboUIAction::ShopItem, 0);
			NextTick([=, this]
			{
				Check(FString::Printf(TEXT("x10 buys ten levels at once (Lv %d -> %d)"), T->MashLevel, GS()->MashIncomeLevel), GS()->MashIncomeLevel == T->MashLevel + 10);
			});
		} });
		Steps.Add({ 0.3f, [=, this] { Shot(TEXT("gemshop_02_coin_shop_x10")); } });
		Steps.Add({ 0.3f, [=, this]
		{
			// 買えるだけ：通常壁を押すと、コインが次の 1 個に足りなくなるまで買う
			ClickUI(EKakurenboUIAction::BuyAmount, 2);
			NextTick([=, this]
			{
				Check(TEXT("max is selected"), BuyAmount == EKakurenboBuyAmount::Max);
				const int32 Before = GS()->WallStock[0];
				KakuBuy(GM->GetShopIndexOfWall(0) + 1);
				Check(FString::Printf(TEXT("max buys as many walls as the coins allow (stock %d -> %d, coins %.0f, next %.0f)"), Before, GS()->WallStock[0], GS()->Coins, GM->GetWallCost(0)),
					GS()->WallStock[0] > Before + 10 && GS()->Coins < GM->GetWallCost(0));
				BuyAmount = EKakurenboBuyAmount::One;
				KakuAddCoins(1000.0);
			});
		} });
		// 宝石のお店
		Steps.Add({ 0.4f, [=, this]
		{
			Check(TEXT("the gem tab is clickable"), ClickUI(EKakurenboUIAction::ShopTab, static_cast<int32>(EKakurenboShopTab::Gem)));
			NextTick([=, this] { Check(TEXT("clicking the gem tab opens the gem shop"), IsGemShopTab()); });
		} });
		Steps.Add({ 0.4f, [=, this] { Shot(TEXT("gemshop_03_gem_shop")); } });
		Steps.Add({ 0.3f, [=, this]
		{
			const int32 Items = GM->GetGemShopItems().Num();
			Check(FString::Printf(TEXT("five gem items (%d)"), Items), Items == 5);
			// 宝石が足りないと買えない
			const int32 Gems = GS()->Gems;
			GS()->Gems = 0;
			KakuBuyGem(1);
			Check(TEXT("without gems nothing is bought"), GS()->TrapStock[GM->GetGemShopTrapType(0)] == 0);
			GS()->Gems = Gems + 60;
			int32 Spent = 0;
			for (int32 i = 0; i < Items; ++i)
			{
				Spent += GM->GetGemTrapCost(GM->GetGemShopTrapType(i));
				KakuBuyGem(i + 1);
			}
			bool bAll = true;
			for (int32 i = 0; i < Items; ++i)
			{
				bAll &= GS()->TrapStock[GM->GetGemShopTrapType(i)] == 1;
			}
			Check(FString::Printf(TEXT("each gem item bought once with gems (spent %d, gems %d -> %d)"), Spent, Gems + 60, GS()->Gems), bAll && GS()->Gems == Gems + 60 - Spent);
			Check(TEXT("the gem price grows with the number owned"), GM->GetGemTrapCost(GM->GetGemShopTrapType(0)) > GM->TrapTypes[GM->GetGemShopTrapType(0)].Cost);
			Check(TEXT("gem items are not in the coin shop"), GM->GetShopIndexOfTrap(GM->GetGemShopTrapType(0)) == INDEX_NONE && GM->GetShopItems().Num() == 3 + GM->WallTypes.Num() + 2);
		} });
		Steps.Add({ 0.4f, [=, this] { Shot(TEXT("gemshop_04_gem_shop_bought")); } });
		Steps.Add({ 0.3f, [=, this] { CycleShopTab(+1); } });
		Steps.Add({ 0.4f, [=, this]
		{
			Check(TEXT("Tab goes coin -> gem -> prestige"), IsPrestigeShopTab());
			Shot(TEXT("gemshop_05_prestige_shop"));
		} });
		Steps.Add({ 0.4f, [=, this]
		{
			KakuNext(); // → 設置
			const TArray<int32> Slots = GetBuildSlots();
			const int32 Daruma = GM->FindTrapTypeOfKind(ETrapKind::Daruma);
			Check(FString::Printf(TEXT("build slots show the owned gem items but not daruma (%d slots)"), Slots.Num()),
				Slots.Num() == GM->WallTypes.Num() + 2 + 4 && !Slots.Contains(GM->WallTypes.Num() + Daruma));
			GetHider()->SetOverheadYaw(0.f);
		} });
		Steps.Add({ 0.6f, [=, this] { Shot(TEXT("gemshop_06_build_slots")); } });
	}
	// ================================================================ OneWay（M15：一方通行ののれん）
	else if (Scenario.Equals(TEXT("OneWay"), ESearchCase::IgnoreCase))
	{
		struct FOneWayTest { FIntPoint Room, Gate, Outside; int32 Type = INDEX_NONE; FVector BlockedFrom; };
		TSharedRef<FOneWayTest> T = MakeShared<FOneWayTest>();
		auto TheGate = [T, Grid]() { return Grid()->GetTrap(T->Gate); };
		auto TheOni = [GM]() -> AOniCharacter* { return GM->GetOnis().Num() > 0 ? GM->GetOnis()[0].Get() : nullptr; };
		Steps.Add({ 0.5f, [=, this] { SetStageOniTypes({ EOniType::Balanced }); KakuSkipTime(1000.f); } });
		Steps.Add({ 1.f, [=, this]
		{
			KakuNext(); // → 購入
			KakuAddGems(30);
			T->Type = GM->FindTrapTypeOfKind(ETrapKind::OneWay);
			KakuBuyGem(GM->GetGemShopIndexOfTrap(T->Type) + 1);
			Check(TEXT("one-way gate bought with gems"), GS()->TrapStock[T->Type] == 1);
		} });
		Steps.Add({ 0.5f, [=, this]
		{
			KakuNext(); // → 設置
			UKakurenboGridSubsystem* G = Grid();
			const FIntPoint Me = G->WorldToCell(GetPawn()->GetActorLocation());
			// プレイヤーから離れた所に、東だけ開いた部屋（3×3 の壁の輪）。東の開いたマスにのれん（西向き＝鬼は外から中へだけ通れる）
			T->Room = FIntPoint(FMath::Clamp(Me.X, 4, G->GetSizeX() - 9), Me.Y + (Me.Y < G->GetSizeY() / 2 ? 6 : -6));
			T->Gate = T->Room + FIntPoint(1, 0);
			T->Outside = T->Room + FIntPoint(4, 0);
			// 部屋の壁はとても硬くする（中から出るには、のれんを壊すしかない＝行き止まり）
			for (int32 DY = -1; DY <= 1; ++DY)
			{
				for (int32 DX = -1; DX <= 1; ++DX)
				{
					if ((DX != 0 || DY != 0) && !(DX == 1 && DY == 0))
					{
						G->PlaceBlock(T->Room + FIntPoint(DX, DY), 0, 1000.0, FLinearColor::Gray);
					}
				}
			}
			GM->OneWayPlaceDir = 2;
			KakuPlaceTrap(T->Gate.X, T->Gate.Y, T->Type);
			Check(FString::Printf(TEXT("gate placed facing west (dir %d)"), G->GetTrapDir(T->Gate)), TheGate() && G->GetTrapDir(T->Gate) == 2);
			TArray<FIntPoint> Path;
			FKakurenboPathGrid Walk = G->BuildWalkGrid();
			Check(TEXT("onis can walk in through the gate"), KakurenboPathfinding::FindPath(Walk, T->Outside, T->Room, Path) && Path.Contains(T->Gate));
			Check(TEXT("onis cannot walk out through the gate"), !KakurenboPathfinding::FindPath(Walk, T->Room, T->Outside, Path));
			// 置いたのれんをクリックすると 90 度ずつ回る（2 回で東向き＝逆）
			Check(TEXT("rotate the gate"), GM->RotateTrap(T->Gate) && GM->RotateTrap(T->Gate) && G->GetTrapDir(T->Gate) == 0);
			Walk = G->BuildWalkGrid();
			Check(TEXT("after rotating, onis can only walk out"), !KakurenboPathfinding::FindPath(Walk, T->Outside, T->Room, Path) && KakurenboPathfinding::FindPath(Walk, T->Room, T->Outside, Path));
			GM->RotateTrap(T->Gate);
			GM->RotateTrap(T->Gate);
			GetHider()->SetTopDownFocus(G->CellFloorCenter(T->Room));
			GetHider()->TopDownDistance = 1400.f;
		} });
		Steps.Add({ 0.6f, [=] { Shot(TEXT("oneway_01_build")); } });
		Steps.Add({ 0.3f, [=, this]
		{
			KakuNext(); // → かくれんぼ
			// プレイヤーはどちらからでも通れる：外から中へ、中から外へ（のれんを通り抜ける）
			ACharacter* Player = Cast<ACharacter>(GetPawn());
			const float Z = Player->GetActorLocation().Z;
			const FVector Out = Grid()->CellFloorCenter(T->Gate + FIntPoint(1, 0));
			const FVector In = Grid()->CellFloorCenter(T->Room);
			const FVector Start = Player->GetActorLocation();
			Player->SetActorLocation(FVector(In.X, In.Y, Z), false, nullptr, ETeleportType::TeleportPhysics);
			const bool bOut = Player->SetActorLocation(FVector(Out.X, Out.Y, Z), true) && FVector::Dist2D(Player->GetActorLocation(), Out) < 5.f;
			const bool bIn = Player->SetActorLocation(FVector(In.X, In.Y, Z), true) && FVector::Dist2D(Player->GetActorLocation(), In) < 5.f;
			Check(TEXT("the player passes the gate both ways"), bOut && bIn);
			Player->SetActorLocation(Start, false, nullptr, ETeleportType::TeleportPhysics);
		} });
		// 鬼：矢印の向き（外 → 中）なら通れる
		Steps.Add({ 3.3f, [=, this]
		{
			AOniCharacter* Oni = KeepOnlyOni(0);
			MakeBlindListener(Oni);
			Oni->SetActorLocation(Grid()->CellFloorCenter(T->Outside) + FVector(0.f, 0.f, 100.f), false, nullptr, ETeleportType::TeleportPhysics);
			// 外から押しても箱にぶつかる（向きが逆の側からは通れない）を先に確かめる：中から外へ動かしてみる
			const FVector In = Grid()->CellFloorCenter(T->Room) + FVector(0.f, 0.f, Oni->GetActorLocation().Z - Grid()->CellFloorCenter(T->Room).Z);
			Oni->DebugGoTo(T->Room);
		} });
		Steps.Add({ 3.5f, [=, this]
		{
			AOniCharacter* Oni = TheOni();
			Check(FString::Printf(TEXT("the oni walked in through the gate (cell %d,%d, passes %d)"), Grid()->WorldToCell(Oni->GetActorLocation()).X, Grid()->WorldToCell(Oni->GetActorLocation()).Y, TheGate() ? TheGate()->GetPassCount() : -1),
				Grid()->WorldToCell(Oni->GetActorLocation()) == T->Room && TheGate() && TheGate()->GetPassCount() == 1);
			LookAtOni();
			ShotLater(TEXT("oneway_02_inside"));
			// 中から外へは、のれんを通れない（壁を壊すしかない）。まっすぐ押し出してみる
			const FVector From = Oni->GetActorLocation();
			const FVector To = Grid()->CellFloorCenter(T->Gate + FIntPoint(1, 0)) + FVector(0.f, 0.f, From.Z - Grid()->CellFloorCenter(T->Room).Z);
			Oni->SetActorLocation(To, true);
			Check(FString::Printf(TEXT("the gate blocks the oni going the wrong way (stopped %.0f cm short)"), FVector::Dist2D(Oni->GetActorLocation(), To)), FVector::Dist2D(Oni->GetActorLocation(), To) > 60.f);
			Oni->SetActorLocation(From, false, nullptr, ETeleportType::TeleportPhysics);
			Check(FString::Printf(TEXT("passing does not wear the gate out, and it is twice as hard as a normal wall (HP %.1f, wall %.1f)"), TheGate()->GetGateHP(), GM->GetEffectiveWallTypes()[0].MaxHP),
				FMath::IsNearlyEqual(TheGate()->GetGateHP(), GM->GetEffectiveWallTypes()[0].MaxHP * 2.0, 0.01));
			// 中から外へ出たい：ほかに道が無い（行き止まり）ので、のれんを壊して出る
			Oni->DebugGoTo(T->Outside);
		} });
		Steps.Add({ 2.f, [=, this]
		{
			const ATrapActor* Gate = TheGate();
			Check(FString::Printf(TEXT("in a dead end the oni attacks the gate (HP %.1f, state %s)"), Gate ? Gate->GetGateHP() : -1.0, *UEnum::GetValueAsString(GM->GetOnis()[0]->GetOniState())),
				Gate && Gate->GetGateHP() < GM->GetOneWayMaxHP());
		} });
		Steps.Add({ 8.f, [=, this]
		{
			Check(FString::Printf(TEXT("the gate is broken (live %d, design %d)"), TheGate() ? 1 : 0, Grid()->GetTrapDesign(T->Gate)),
				!TheGate() && Grid()->GetTrapDesign(T->Gate) == T->Type && GS()->TrapsTriggeredThisRound >= 1);
			Check(TEXT("the walls are untouched (the gate was the only way)"), Grid()->GetColumnHeight(T->Room + FIntPoint(-1, 0)) == 1 && Grid()->GetColumnHeight(T->Room + FIntPoint(0, 1)) == 1);
		} });
	}
	// ================================================================ Darkness（M15：真っ暗ブロック。館じゅうが暗く、1 ラウンドで壊れる）
	else if (Scenario.Equals(TEXT("Darkness"), ESearchCase::IgnoreCase))
	{
		struct FDarkTest { FIntPoint Cell; int32 Type = INDEX_NONE; };
		TSharedRef<FDarkTest> T = MakeShared<FDarkTest>();
		Steps.Add({ 0.5f, [=, this] { SetStageOniTypes({ EOniType::Balanced }); KakuSkipTime(1000.f); } });
		Steps.Add({ 1.f, [=, this]
		{
			KakuNext(); // → 購入
			KakuAddGems(30);
			T->Type = GM->FindTrapTypeOfKind(ETrapKind::Darkness);
			KakuBuyGem(GM->GetGemShopIndexOfTrap(T->Type) + 1);
			KakuBuyGem(GM->GetGemShopIndexOfTrap(T->Type) + 1);
		} });
		Steps.Add({ 0.5f, [=, this]
		{
			KakuNext(); // → 設置
			T->Cell = Grid()->WorldToCell(GetPawn()->GetActorLocation()) + FIntPoint(2, 2);
			KakuPlaceTrap(T->Cell.X, T->Cell.Y, T->Type);
			Check(TEXT("darkness block placed"), Grid()->GetTrap(T->Cell) != nullptr);
		} });
		Steps.Add({ 0.4f, [=, this] { KakuNext(); } }); // → かくれんぼ
		Steps.Add({ 3.4f, [=, this]
		{
			Check(TEXT("the whole mansion is dark this round"), GM->IsMansionDark());
			AOniCharacter* Oni = KeepOnlyOni(0);
			Oni->Deactivate(); // その場に立たせて、見えるかだけ調べる
			const FVector Me = GetPawn()->GetActorLocation();
			const FVector Far = Me + FVector(600.f, 0.f, 0.f);
			Oni->SetActorLocation(FVector(Far.X, Far.Y, Me.Z + 20.f), false, nullptr, ETeleportType::TeleportPhysics);
			Oni->SetActorRotation(FRotator(0.f, 180.f, 0.f));
			Check(FString::Printf(TEXT("in the dark the oni cannot see 6 m away (sight %.0f)"), Oni->GetEffectiveSightRadius()), !Oni->DebugCanSeeTarget() && Oni->GetEffectiveSightRadius() <= 300.f);
			Oni->SetActorLocation(FVector(Me.X + 220.f, Me.Y, Me.Z + 20.f), false, nullptr, ETeleportType::TeleportPhysics);
			Check(TEXT("but it still sees 2 m away"), Oni->DebugCanSeeTarget());
			LookAtOni();
			ShotLater(TEXT("darkness_01_dark"));
		} });
		Steps.Add({ 1.f, [=, this] { KakuSkipTime(1000.f); } });
		Steps.Add({ 0.6f, [=, this]
		{
			Check(TEXT("after the round the darkness block is used up (design stays)"), !Grid()->GetTrap(T->Cell) && Grid()->GetTrapDesign(T->Cell) == T->Type && !GM->IsMansionDark());
			KakuNext(); // → 購入
		} });
		Steps.Add({ 0.4f, [=, this]
		{
			KakuNext(); // → 設置（在庫から置き直す）
			Check(TEXT("the second darkness block refills from stock"), Grid()->GetTrap(T->Cell) != nullptr && GS()->TrapStock[T->Type] == 0);
		} });
	}
	// ================================================================ Cat（M15：迷い猫）
	else if (Scenario.Equals(TEXT("Cat"), ESearchCase::IgnoreCase))
	{
		struct FCatTest { FIntPoint Cell; int32 Type = INDEX_NONE; };
		TSharedRef<FCatTest> T = MakeShared<FCatTest>();
		auto TheCat = [T, Grid]() { return Grid()->GetTrap(T->Cell); };
		Steps.Add({ 0.5f, [=, this] { SetStageOniTypes({ EOniType::Treasure }); KakuSkipTime(1000.f); } });
		Steps.Add({ 1.f, [=, this]
		{
			KakuNext(); // → 購入
			KakuAddGems(30);
			T->Type = GM->FindTrapTypeOfKind(ETrapKind::Cat);
			KakuBuyGem(GM->GetGemShopIndexOfTrap(T->Type) + 1);
		} });
		Steps.Add({ 0.5f, [=, this]
		{
			KakuNext(); // → 設置
			const FIntPoint Me = Grid()->WorldToCell(GetPawn()->GetActorLocation());
			T->Cell = FIntPoint(FMath::Clamp(Me.X - 7, 2, Grid()->GetSizeX() - 3), Me.Y);
			KakuPlaceTrap(T->Cell.X, T->Cell.Y, T->Type);
			Check(TEXT("cat placed"), TheCat() != nullptr);
		} });
		Steps.Add({ 0.4f, [=, this] { KakuNext(); } }); // → かくれんぼ
		Steps.Add({ 3.3f, [=, this]
		{
			// 宝物鬼（ふだんは連打もおとりも聞かない）を猫の 5 m 先へ
			AOniCharacter* Oni = KeepOnlyOni(0);
			Oni->SightRadius = 0.f;
			Oni->CloseSenseRadius = 0.f;
			Oni->SetActorLocation(Grid()->CellFloorCenter(T->Cell + FIntPoint(0, 5)) + FVector(0.f, 0.f, 100.f), false, nullptr, ETeleportType::TeleportPhysics);
		} });
		Steps.Add({ 7.f, [=, this]
		{
			const AOniCharacter* Oni = GM->GetOnis()[0];
			Check(FString::Printf(TEXT("the cat meows (%d)"), TheCat() ? TheCat()->GetMeowCount() : -1), TheCat() && TheCat()->GetMeowCount() >= 1);
			Check(FString::Printf(TEXT("even the treasure oni comes and pets the cat (pets %d, intent %s)"), TheCat() ? TheCat()->GetPetCount() : -1, *UEnum::GetValueAsString(Oni->GetIntent())),
				TheCat() && TheCat()->GetPetCount() >= 1);
			LookAtOni();
			ShotLater(TEXT("cat_01_petting"));
		} });
		Steps.Add({ 0.1f, [=, this]
		{
			Check(TEXT("a petting oni cannot catch the player"), GM->GetOnis()[0]->GetOniState() == EOniState::Stunned || TheCat()->GetPetCount() >= 1);
		} });
		Steps.Add({ 5.5f, [=, this]
		{
			// なで終わったら、猫はその鬼の頭の上に乗る（もうなでられない）
			const AOniCharacter* Oni = GM->GetOnis()[0];
			Check(FString::Printf(TEXT("after petting, the cat rides on the oni's head (pets %d)"), TheCat() ? TheCat()->GetPetCount() : -1),
				TheCat() && TheCat()->IsOnOniHead() && TheCat()->GetGadgetLocation().Z > Oni->GetActorLocation().Z + 50.f && TheCat()->GetPetCount() == 1);
			LookAtOni();
			ShotLater(TEXT("cat_02_on_head"));
		} });
		Steps.Add({ 3.f, [=, this]
		{
			Check(TEXT("the cat cannot be petted again"), TheCat() && TheCat()->GetPetCount() == 1 && TheCat()->IsOnOniHead());
		} });
	}
	// ================================================================ FakeHider（M15：身代わり人形）
	else if (Scenario.Equals(TEXT("FakeHider"), ESearchCase::IgnoreCase))
	{
		struct FFakeTest { FIntPoint Cell; int32 Type = INDEX_NONE; FVector Home; int32 Steps = 0; };
		TSharedRef<FFakeTest> T = MakeShared<FFakeTest>();
		auto TheFake = [GM]() -> AFakeHiderCharacter* { const TArray<AHiderCharacter*> F = GM->GetFakeHiders(); return F.Num() > 0 ? Cast<AFakeHiderCharacter>(F[0]) : nullptr; };
		Steps.Add({ 0.5f, [=, this] { SetStageOniTypes({ EOniType::Balanced }); KakuSkipTime(1000.f); } });
		Steps.Add({ 1.f, [=, this]
		{
			KakuNext(); // → 購入
			KakuAddGems(30);
			T->Type = GM->FindTrapTypeOfKind(ETrapKind::FakeHider);
			KakuBuyGem(GM->GetGemShopIndexOfTrap(T->Type) + 1);
		} });
		Steps.Add({ 0.5f, [=, this]
		{
			KakuNext(); // → 設置
			// プレイヤーは隅、人形は真ん中
			TeleportPlayer(Grid()->CellFloorCenter(FIntPoint(1, 1)));
			T->Cell = FIntPoint(Grid()->GetSizeX() / 2, Grid()->GetSizeY() / 2);
			KakuPlaceTrap(T->Cell.X, T->Cell.Y, T->Type);
			Check(TEXT("the fake hider stands on its cell"), TheFake() != nullptr);
			if (TheFake())
			{
				T->Home = TheFake()->GetActorLocation();
			}
			GetHider()->SetTopDownFocus(Grid()->CellFloorCenter(T->Cell));
			GetHider()->TopDownDistance = 1200.f;
			GetHider()->TopDownPitch = -50.f;
		} });
		Steps.Add({ 0.8f, [=] { Shot(TEXT("fake_01_build")); } });
		Steps.Add({ 0.3f, [=, this] { GetHider()->TopDownPitch = -90.f; KakuNext(); } }); // → かくれんぼ
		Steps.Add({ 3.3f, [=, this]
		{
			// 鬼を人形の 4 m 東に、人形の方を向けて立たせる（プレイヤーは遠い隅）
			AOniCharacter* Oni = KeepOnlyOni(0);
			Oni->ChaseSpeed = 300.f; // 追いつかないように遅く（逃げるところを見る）
			const FVector Fake = TheFake()->GetActorLocation();
			Oni->SetActorLocation(Fake + FVector(400.f, 0.f, 20.f), false, nullptr, ETeleportType::TeleportPhysics);
			Oni->SetActorRotation(FRotator(0.f, 180.f, 0.f));
			T->Steps = GS()->StepsThisRound;
		} });
		Steps.Add({ 1.5f, [=, this]
		{
			const AOniCharacter* Oni = GM->GetOnis()[0];
			AFakeHiderCharacter* Fake = TheFake();
			Check(FString::Printf(TEXT("the oni chases the fake hider (intent %s, chasing player %d)"), *UEnum::GetValueAsString(Oni->GetIntent()), Oni->IsChasingPlayer() ? 1 : 0),
				Fake && Oni->GetIntent() == EOniState::Chase && Oni->GetChaseTarget() == Fake);
			Check(FString::Printf(TEXT("the fake hider flees away from the oni (moved %.0f cm, dir %.2f)"), Fake ? FVector::Dist2D(Fake->GetActorLocation(), T->Home) : -1.f,
				Fake ? FVector::DotProduct(Fake->GetFleeDirection(), (Fake->GetActorLocation() - Oni->GetActorLocation()).GetSafeNormal2D()) : -1.f),
				Fake && Fake->IsFleeing() && FVector::Dist2D(Fake->GetActorLocation(), T->Home) > 150.f);
			Check(FString::Printf(TEXT("the fake hider makes loud noise while fleeing (%d)"), Fake ? Fake->GetNoiseCount() : -1), Fake && Fake->GetNoiseCount() >= 1);
			Check(TEXT("the fake hider's steps are not the player's steps"), GS()->StepsThisRound == T->Steps);
			LookAtOni();
			ShotLater(TEXT("fake_02_fleeing"));
		} });
		Steps.Add({ 0.6f, [=, this]
		{
			// 鬼を人形に触れさせる → 「にせもの！」で人形は消え、プレイヤーは見つかっていない
			AOniCharacter* Oni = GM->GetOnis()[0];
			Oni->SetActorLocation(TheFake()->GetActorLocation() + FVector(40.f, 0.f, 10.f), false, nullptr, ETeleportType::TeleportPhysics);
		} });
		Steps.Add({ 0.4f, [=, this]
		{
			const AOniCharacter* Oni = GM->GetOnis()[0];
			Check(TEXT("caught fake hider disappears (design stays)"), !TheFake() && !Grid()->GetTrap(T->Cell) && Grid()->GetTrapDesign(T->Cell) == T->Type);
			Check(FString::Printf(TEXT("the oni gives up and the player is safe (phase %s, intent %s)"), *UEnum::GetValueAsString(GS()->Phase), *UEnum::GetValueAsString(Oni->GetIntent())),
				GS()->Phase == EKakurenboPhase::Hide && Oni->GetIntent() != EOniState::Chase);
		} });
	}
	// ================================================================ Daruma（M15：だるまさんがころんだ）
	else if (Scenario.Equals(TEXT("Daruma"), ESearchCase::IgnoreCase))
	{
		struct FDarumaTest { FVector OniAt; };
		TSharedRef<FDarumaTest> T = MakeShared<FDarumaTest>();
		auto TheOni = [GM]() -> AOniCharacter* { return GM->GetOnis().Num() > 0 ? GM->GetOnis()[0].Get() : nullptr; };
		Steps.Add({ 0.5f, [=, this] { SetStageOniTypes({ EOniType::Balanced }); KakuSkipTime(1000.f); } });
		Steps.Add({ 1.f, [=, this]
		{
			KakuNext(); // → 購入
			KakuAddGems(30);
			const int32 Type = GM->FindTrapTypeOfKind(ETrapKind::Daruma);
			KakuBuyGem(GM->GetGemShopIndexOfTrap(Type) + 1);
			Check(TEXT("daruma bought"), GS()->TrapStock[Type] == 1);
		} });
		Steps.Add({ 0.5f, [=, this] { KakuNext(); TeleportPlayer(Grid()->CellFloorCenter(FIntPoint(3, Grid()->GetSizeY() / 2))); } }); // → 設置
		Steps.Add({ 0.4f, [=, this] { KakuNext(); } }); // → かくれんぼ
		Steps.Add({ 3.3f, [=, this]
		{
			// 鬼を 9 m 先に置いて、こちらへ向かわせる。カメラは鬼の方
			AOniCharacter* Oni = KeepOnlyOni(0);
			MakeBlindListener(Oni);
			Oni->SetActorLocation(GetPawn()->GetActorLocation() + FVector(900.f, 0.f, 20.f), false, nullptr, ETeleportType::TeleportPhysics);
			LookAtOni();
			Check(TEXT("daruma can be used"), GM->CanUseDaruma());
			SimulateKey(EKeys::G, IE_Pressed);
			SimulateKey(EKeys::G, IE_Released);
		} });
		Steps.Add({ 0.2f, [=, this]
		{
			Check(FString::Printf(TEXT("pressing G calls daruma-san ga koronda (%.1f s, short)"), GM->GetDarumaTimeLeft()), GM->IsDarumaActive() && GM->GetDarumaTimeLeft() <= 3.f);
			AOniCharacter* Oni = TheOni();
			Oni->DebugGoTo(Grid()->WorldToCell(GetPawn()->GetActorLocation()));
			T->OniAt = Oni->GetActorLocation();
		} });
		Steps.Add({ 1.f, [=, this]
		{
			const AOniCharacter* Oni = TheOni();
			Check(FString::Printf(TEXT("the watched oni cannot move (moved %.0f cm, frozen %d)"), FVector::Dist2D(Oni->GetActorLocation(), T->OniAt), Oni->IsDarumaFrozen() ? 1 : 0),
				FVector::Dist2D(Oni->GetActorLocation(), T->OniAt) < 30.f && Oni->IsDarumaFrozen());
			Shot(TEXT("daruma_01_frozen"));
		} });
		Steps.Add({ 0.3f, [=, this]
		{
			// 目をそらす（後ろを向く）と動き出す
			SetControlRotation(GetControlRotation() + FRotator(0.f, 180.f, 0.f));
			T->OniAt = TheOni()->GetActorLocation();
		} });
		Steps.Add({ 0.6f, [=, this]
		{
			const AOniCharacter* Oni = TheOni();
			Check(FString::Printf(TEXT("when not watched the oni moves (moved %.0f cm, frozen %d, intent %s, watched %d)"), FVector::Dist2D(Oni->GetActorLocation(), T->OniAt), Oni->IsDarumaFrozen() ? 1 : 0,
				*UEnum::GetValueAsString(Oni->GetIntent()), GM->IsOniWatchedByPlayer(Oni) ? 1 : 0),
				FVector::Dist2D(Oni->GetActorLocation(), T->OniAt) > 40.f && !Oni->IsDarumaFrozen());
			LookAtOni();
		} });
		Steps.Add({ 0.3f, [=, this]
		{
			// 止まっている鬼でも、ぶつかれば見つかる（無敵ではない）
			AOniCharacter* Oni = TheOni();
			Check(TEXT("frozen again when looked at"), Oni->IsDarumaFrozen());
			TeleportPlayer(Oni->GetActorLocation() - (Oni->GetActorLocation() - GetPawn()->GetActorLocation()).GetSafeNormal2D() * 70.f);
		} });
		Steps.Add({ 0.3f, [=, this]
		{
			Check(FString::Printf(TEXT("bumping into a frozen oni still gets you found (phase %s)"), *UEnum::GetValueAsString(GS()->Phase)), GS()->Phase == EKakurenboPhase::Result && !GS()->bLastRoundCleared);
		} });
	}
	// ================================================================ PowerBox（M15：パワー鬼は前方 2×2×2 の壁に当たる）
	else if (Scenario.Equals(TEXT("PowerBox"), ESearchCase::IgnoreCase))
	{
		struct FBoxTest { FIntPoint Base; };
		TSharedRef<FBoxTest> T = MakeShared<FBoxTest>();
		Steps.Add({ 0.3f, [=, this] { SetStageOniTypes({ EOniType::Breaker }); } });
		Steps.Add({ 3.3f, [=, this]
		{
			AOniCharacter* Oni = KeepOnlyOni(0);
			UKakurenboGridSubsystem* G = Grid();
			Check(FString::Printf(TEXT("power oni: 2x2x2 box, slow windup %.1f s, short sight %.0f"), Oni->AttackWindup, Oni->SightRadius),
				Oni->AttackBoxWidth == 2 && Oni->AttackBoxDepth == 2 && Oni->AttackBoxLevels == 2 && Oni->AttackWindup >= 1.5f && Oni->SightRadius < 700.f);
			TeleportPlayer(G->CellFloorCenter(FIntPoint(1, 1)));
			// 鬼の東に 2×2 マス × 2 段の壁、その奥（3 マス目）と、横（反対側）と、3 段目にも壁
			T->Base = FIntPoint(G->GetSizeX() / 2, G->GetSizeY() / 2);
			auto Put = [G](const FIntPoint& Cell, int32 Levels)
			{
				for (int32 L = 0; L < Levels; ++L)
				{
					G->PlaceBlock(Cell, 0, 0.5, FLinearColor::Gray);
				}
			};
			Put(T->Base, 3);
			Put(T->Base + FIntPoint(1, 0), 2);
			Put(T->Base + FIntPoint(0, 1), 2);
			Put(T->Base + FIntPoint(1, 1), 2);
			Put(T->Base + FIntPoint(2, 0), 2);  // 奥行き 3 マス目：当たらない
			Put(T->Base + FIntPoint(0, -1), 2); // 反対側の横：当たらない
			// 鬼は壁の西、少し南（+Y）寄りに立って東を向く → 横は +Y 側へ広がる
			Oni->SetActorLocation(G->CellFloorCenter(T->Base + FIntPoint(-1, 0)) + FVector(0.f, 20.f, 100.f), false, nullptr, ETeleportType::TeleportPhysics);
			Oni->SetActorRotation(FRotator::ZeroRotator);
			Oni->DebugGoTo(T->Base); // 行き先が壁：目の前の壁を攻撃する
		} });
		Steps.Add({ 3.0f, [=, this]
		{
			UKakurenboGridSubsystem* G = Grid();
			auto H = [G, T](int32 DX, int32 DY) { return G->GetColumnHeight(T->Base + FIntPoint(DX, DY)); };
			Check(FString::Printf(TEXT("the 2x2x2 box in front is destroyed (heights %d %d %d %d)"), H(0, 0), H(1, 0), H(0, 1), H(1, 1)),
				H(0, 0) == 1 && H(1, 0) == 0 && H(0, 1) == 0 && H(1, 1) == 0);
			Check(FString::Printf(TEXT("walls outside the box are not hit (behind %d, other side %d)"), H(2, 0), H(0, -1)), H(2, 0) == 2 && H(0, -1) == 2);
			LookAtOni();
			ShotLater(TEXT("powerbox_01"));
		} });
	}
	// ================================================================ BuildRay（M15：設置パートのカーソルが、見えない壁に当たらない）
	else if (Scenario.Equals(TEXT("BuildRay"), ESearchCase::IgnoreCase))
	{
		Steps.Add({ 0.5f, [=, this] { KakuSkipTime(1000.f); } });
		Steps.Add({ 1.f, [=, this] { KakuNext(); } });
		Steps.Add({ 0.5f, [=, this]
		{
			KakuNext(); // → 設置
			GetHider()->TopDownPitch = -60.f; // 斜めに傾ける（見えない高い壁に一番当たりやすい）
		} });
		for (int32 Pass = 0; Pass < 2; ++Pass)
		{
			Steps.Add({ 1.0f, [=, this]
			{
				UKakurenboGridSubsystem* G = Grid();
				int32 Tested = 0, Correct = 0, HiddenFirst = 0;
				FString Wrong;
				for (int32 Y = 0; Y < G->GetSizeY(); ++Y)
				{
					for (int32 X = 0; X < G->GetSizeX(); ++X)
					{
						const FIntPoint C(X, Y);
						if (!G->IsWalkable(C))
						{
							continue;
						}
						FVector2D Screen;
						if (!ProjectWorldLocationToScreen(G->CellFloorCenter(C) + FVector(0.f, 0.f, 2.f), Screen, true))
						{
							continue;
						}
						int32 VX = 0, VY = 0;
						GetViewportSize(VX, VY);
						if (Screen.X < 5 || Screen.Y < 5 || Screen.X > VX - 5 || Screen.Y > VY - 5)
						{
							continue;
						}
						// そのマスの床が画面で見えているか（見えている家具・低い壁の陰になっていないか）。見えている物だけに当てて調べる
						FVector O, D;
						DeprojectScreenPositionToWorld(Screen.X, Screen.Y, O, D);
						FCollisionQueryParams RayParams(SCENE_QUERY_STAT(KakuRayRaw), false, GetPawn());
						FHitResult Raw;
						bool bFirst = true;
						bool bHiddenWasFirst = false;
						bool bVisibleFloor = false;
						for (int32 Attempt = 0; Attempt < 12; ++Attempt)
						{
							if (!GetWorld()->LineTraceSingleByChannel(Raw, O, O + D * 20000.f, ECC_Visibility, RayParams))
							{
								break;
							}
							const UPrimitiveComponent* Comp = Raw.GetComponent();
							const bool bHiddenComp = Comp && (Comp->bHiddenInGame || !Comp->IsVisible()) && !Comp->ComponentHasTag(AKakurenboArena::FurnitureCollisionTag);
							if (!bHiddenComp)
							{
								bVisibleFloor = FVector::Dist(Raw.ImpactPoint, G->CellFloorCenter(C)) < 30.f;
								break;
							}
							bHiddenWasFirst |= bFirst;
							bFirst = false;
							RayParams.AddIgnoredComponent(Comp);
						}
						if (!bVisibleFloor)
						{
							continue; // 見えている物の陰（正しく指せなくてよい）
						}
						// 前の作り（見えない物にも当たる）だと、ほかのマスを指していた
						HiddenFirst += bHiddenWasFirst ? 1 : 0;
						++Tested;
						UpdateBuildTargetAt(Screen);
						if (bHasBuildTarget && BuildTargetCell == C)
						{
							++Correct;
						}
						else if (Wrong.Len() < 160)
						{
							Wrong += FString::Printf(TEXT(" (%d,%d)->(%d,%d)"), X, Y, BuildTargetCell.X, BuildTargetCell.Y);
						}
					}
				}
				UE_LOG(LogTemp, Display, TEXT("[AutoTest] buildray pitch %.0f: %d of %d floor cells picked correctly, %d rays first hit something hidden (ignored now). wrong:%s"),
					GetHider()->TopDownPitch, Correct, Tested, HiddenFirst, *Wrong);
				Check(FString::Printf(TEXT("the build cursor points at the floor cell under it (pitch %.0f: %d / %d, hidden hits ignored %d)"), GetHider()->TopDownPitch, Correct, Tested, HiddenFirst),
					Tested > 50 && Correct >= Tested * 0.97f);
				Shot(FString::Printf(TEXT("buildray_%02d"), Pass + 1));
				GetHider()->TopDownPitch = -90.f;
			} });
		}
	}
	// ================================================================ Heartbeat（M15：鬼が近いほど心臓の音が大きい）
	else if (Scenario.Equals(TEXT("Heartbeat"), ESearchCase::IgnoreCase))
	{
		TSharedRef<float> FarVolume = MakeShared<float>(0.f);
		Steps.Add({ 0.3f, [=] { SetStageOniTypes({ EOniType::Balanced }); } });
		Steps.Add({ 3.3f, [=, this]
		{
			AOniCharacter* Oni = KeepOnlyOni(0);
			Oni->GetCharacterMovement()->DisableMovement(); // 動かさない（近さだけを変える。鬼としては働いている）
			Oni->SightRadius = 0.f;
			Oni->CloseSenseRadius = 0.f;
			Oni->SetActorLocation(GetPawn()->GetActorLocation() + FVector(1400.f, 0.f, 20.f), false, nullptr, ETeleportType::TeleportPhysics);
		} });
		Steps.Add({ 2.f, [=, this]
		{
			*FarVolume = GM->GetLastHeartbeatVolume();
			Check(FString::Printf(TEXT("a far oni (14 m) gives a quiet heartbeat (closeness %.2f, volume %.2f)"), GM->GetCurrentCloseness(), *FarVolume), GM->GetHeartbeatCount() > 0 && *FarVolume > 0.f);
			AOniCharacter* Oni = GM->GetOnis()[0];
			Oni->SetActorLocation(GetPawn()->GetActorLocation() + FVector(260.f, 0.f, 20.f), false, nullptr, ETeleportType::TeleportPhysics);
		} });
		Steps.Add({ 1.5f, [=, this]
		{
			Check(FString::Printf(TEXT("a near oni (2.6 m) gives a much louder heartbeat (%.2f -> %.2f)"), *FarVolume, GM->GetLastHeartbeatVolume()), GM->GetLastHeartbeatVolume() > *FarVolume * 2.5f);
		} });
	}
	// ================================================================ OniIntro（M15：初めて出てくる鬼のチュートリアル・鬼の図鑑）
	else if (Scenario.Equals(TEXT("OniIntro"), ESearchCase::IgnoreCase))
	{
		Steps.Add({ 0.5f, [=, this]
		{
			// 最初のラウンドはもう始まっているので、ここから出すようにする（先に付けると最初のラウンドで止まってしまう）
			bShowOniIntroInAutoTest = true;
			if (UKakurenboSettings* S = UKakurenboSettings::Get(this))
			{
				S->ResetTips(); // まだ誰も紹介していない
			}
			SetStageOniTypes({ EOniType::Scout, EOniType::Breaker, EOniType::Detector });
			KakuSkipTime(1000.f);
		} });
		Steps.Add({ 1.f, [=, this] { KakuNext(); } }); // → 購入
		Steps.Add({ 0.5f, [=, this] { KakuNext(); } }); // → 設置
		Steps.Add({ 0.5f, [=, this]
		{
			KakuNext(); // → かくれんぼ：新しい鬼が 3 種類いるので、チュートリアルが出てゲームが止まる
			// ここからは止まっているので、実時間で進める
			RealDelay(0.6f, [=, this]
			{
				Check(FString::Printf(TEXT("the oni tutorial opens for the new types (menu %d, pages %d, paused %d)"), static_cast<int32>(GetMenu()), GetOniIntroPages().Num(), IsPaused() ? 1 : 0),
					GetMenu() == EKakurenboMenu::OniIntro && GetOniIntroPages().Num() == 3 && IsPaused() && !IsOniBook());
				const AKakurenboOniStudio* Studio = GM->GetOniStudio();
				Check(TEXT("each oni has a photo"), Studio && Studio->GetPortrait(EOniType::Scout) && Studio->GetPortrait(EOniType::Breaker) && Studio->GetPortrait(EOniType::Detector));
				Shot(TEXT("oniintro_01_scout"));
				RealDelay(0.5f, [=, this]
				{
					SimulateKey(EKeys::Enter, IE_Pressed);
					SimulateKey(EKeys::Enter, IE_Released);
					// キーは次のフレームで効く。フレーム数で待ってから確かめる（撮影の直後は 1 フレームが長く、秒では足りないことがある）
					TSharedRef<int32> Frames = MakeShared<int32>(0);
					FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([=, this](float) -> bool
					{
						if (++*Frames < 10)
						{
							return true;
						}
						Check(TEXT("Enter shows the next oni"), GetOniIntroPage() == 1);
						return false;
					}));
					RealDelay(1.0f, [=, this]
					{
						Shot(TEXT("oniintro_02_breaker"));
						RealDelay(0.5f, [=, this]
						{
							TurnOniIntroPage(1);
							RealDelay(0.5f, [=, this]
							{
								Shot(TEXT("oniintro_03_detector"));
								RealDelay(0.5f, [=, this]
								{
									Check(TEXT("the last page has the OK button"), ClickUI(EKakurenboUIAction::OniPage, 1));
									RealDelay(0.3f, [=, this]
									{
										Check(FString::Printf(TEXT("OK closes the tutorial and the round goes on (menu %d, paused %d)"), static_cast<int32>(GetMenu()), IsPaused() ? 1 : 0),
											GetMenu() == EKakurenboMenu::None && !IsPaused());
									});
								});
							});
						});
					});
				});
			});
		} });
		// 次のラウンド：もう紹介したので出ない
		Steps.Add({ 6.f, [=, this] { KakuSkipTime(1000.f); } });
		Steps.Add({ 1.f, [=, this] { KakuNext(); } });
		Steps.Add({ 0.5f, [=, this] { KakuNext(); } });
		Steps.Add({ 0.5f, [=, this] { KakuNext(); } });
		Steps.Add({ 0.6f, [=, this]
		{
			Check(TEXT("onis already introduced are not shown again"), GetMenu() == EKakurenboMenu::None && GS()->Phase == EKakurenboPhase::Hide);
			// 一時停止メニューの「鬼の図鑑」：今まで紹介した鬼
			OpenMenu(EKakurenboMenu::Pause);
			ActivatePauseItem(EKakurenboPauseItem::OniBook);
			RealDelay(0.5f, [=, this]
			{
				Check(FString::Printf(TEXT("the oni book lists the introduced onis (%d)"), GetOniIntroPages().Num()), GetMenu() == EKakurenboMenu::OniIntro && IsOniBook() && GetOniIntroPages().Num() == 3);
				// 全部の種類の写真を確かめる（図鑑に 6 種類を出して 1 枚ずつ撮る）
				const UEnum* TypeEnum = StaticEnum<EOniType>();
				TArray<EOniType> All;
				for (int32 i = 0; i < TypeEnum->NumEnums() - 1; ++i)
				{
					All.Add(static_cast<EOniType>(TypeEnum->GetValueByIndex(i)));
				}
				CloseMenu();
				OpenOniIntro(All, true);
				for (int32 i = 0; i < All.Num(); ++i)
				{
					RealDelay(0.6f + i * 0.7f, [=, this]
					{
						Shot(FString::Printf(TEXT("oniintro_book_%d"), i + 1));
						RealDelay(0.3f, [=, this] { TurnOniIntroPage(1); });
					});
				}
				RealDelay(0.6f + All.Num() * 0.7f + 0.5f, [=, this]
				{
					Check(TEXT("the book goes back to the pause menu"), GetMenu() == EKakurenboMenu::Pause);
					CloseAllMenus();
				});
			});
		} });
		Steps.Add({ 7.f, [=] {} });
	}
	// ================================================================ ShelfGap（M15：家具のすき間。床が見えているのに体が入らないマスを全部の館で数える）
	else if (Scenario.Equals(TEXT("ShelfGap"), ESearchCase::IgnoreCase))
	{
		struct FGapStats
		{
			int32 Cells = 0;          // 床が見えている家具のマス
			int32 Blocked = 0;        // そのうち、体が入らないマス
			TMap<TCHAR, int32> BlockedByKey;
			TMap<TCHAR, int32> OpenByKey;
		};
		TSharedRef<FGapStats> Total = MakeShared<FGapStats>();
		TSharedRef<FString> Report = MakeShared<FString>();
		const TArray<FName> MapNames = GM->MapOrder;
		for (int32 m = 0; m < MapNames.Num(); ++m)
		{
			const FName MapName = MapNames[m];
			Steps.Add({ m == 0 ? 0.5f : 0.2f, [=, this]
			{
				GM->bMapOverride = true;
				GM->MapOverride = MapName;
				GM->SwitchToMap(MapName);
				KeepOnlyOni(-1);
				GS()->HideTimeRemaining = 600.f;
			} });
			Steps.Add({ 0.6f, [=, this]
			{
				UKakurenboGridSubsystem* G = Grid();
				const AKakurenboArena* Arena = GM->GetArena();
				const float Cell = G->GetCellSize();
				// 体の大きさ（しゃがんだプレイヤー：半径 35・高さの半分 35）
				const FCollisionShape Body = FCollisionShape::MakeCapsule(35.f, 35.f);
				const FCollisionShape Probe = FCollisionShape::MakeSphere(6.f);
				FCollisionQueryParams Params(SCENE_QUERY_STAT(KakuShelfGap), false, GetPawn());
				int32 MapCells = 0, MapBlocked = 0;
				FString Examples;
				for (int32 Y = 0; Y < G->GetSizeY(); ++Y)
				{
					for (int32 X = 0; X < G->GetSizeX(); ++X)
					{
						const FIntPoint C(X, Y);
						const TCHAR Key = Arena->GetLayoutChar(C);
						if (!G->IsObstacle(C) || Key == TEXT('#') || Key == TEXT('.'))
						{
							continue;
						}
						const FVector Floor = G->CellFloorCenter(C);
						// 床が見えている割合（マスの中を 10cm おきに、床の少し上に何も無いか）
						int32 Open = 0, Samples = 0;
						for (int32 i = 0; i < 9; ++i)
						{
							for (int32 j = 0; j < 9; ++j)
							{
								const FVector P = Floor + FVector(-40.f + i * 10.f, -40.f + j * 10.f, 30.f);
								++Samples;
								Open += GetWorld()->OverlapBlockingTestByChannel(P, FQuat::Identity, ECC_Pawn, Probe, Params) ? 0 : 1;
							}
						}
						if (Open * 4 < Samples) // 床が 1/4 も見えていなければ、すき間ではない
						{
							continue;
						}
						// 背の高い家具（本棚・たんす・食器棚。M15 で直したもの）だけを数える。低い家具（ベッド・長いす）はよじ登れるので数えない
						bool bTall = false;
						for (int32 i = 0; i < 5 && !bTall; ++i)
						{
							for (int32 j = 0; j < 5 && !bTall; ++j)
							{
								bTall = GetWorld()->OverlapBlockingTestByChannel(Floor + FVector(-40.f + i * 20.f, -40.f + j * 20.f, 170.f), FQuat::Identity, ECC_Pawn, Probe, Params);
							}
						}
						if (!bTall)
						{
							continue;
						}
						// 体が入るか（体の中心がこのマスの中にあって、何にも重ならない場所があるか）
						bool bFits = false;
						for (int32 i = 0; i < 11 && !bFits; ++i)
						{
							for (int32 j = 0; j < 11 && !bFits; ++j)
							{
								const FVector P = Floor + FVector(-50.f + i * 10.f, -50.f + j * 10.f, 37.f);
								bFits = !GetWorld()->OverlapBlockingTestByChannel(P, FQuat::Identity, ECC_Pawn, Body, Params);
							}
						}
						++MapCells;
						Total->OpenByKey.FindOrAdd(Key)++;
						if (!bFits)
						{
							++MapBlocked;
							Total->BlockedByKey.FindOrAdd(Key)++;
							if (Examples.Len() < 200)
							{
								Examples += FString::Printf(TEXT(" %c(%d,%d,%d%%)"), Key, X, Y, Open * 100 / Samples);
							}
						}
					}
				}
				Total->Cells += MapCells;
				Total->Blocked += MapBlocked;
				UE_LOG(LogTemp, Display, TEXT("[AutoTest] shelfgap %s: %d tall furniture cells with open floor, %d cannot be entered:%s"), *MapName.ToString(), MapCells, MapBlocked, *Examples);
			} });
		}
		Steps.Add({ 0.3f, [=, this]
		{
			FString ByKey;
			for (const TPair<TCHAR, int32>& Pair : Total->OpenByKey)
			{
				ByKey += FString::Printf(TEXT(" %c:%d/%d"), Pair.Key, Total->BlockedByKey.FindRef(Pair.Key), Pair.Value);
			}
			UE_LOG(LogTemp, Display, TEXT("[AutoTest] shelfgap blocked/open by furniture:%s"), *ByKey);
			Check(FString::Printf(TEXT("every gap in front of tall furniture (shelves) fits the player (%d of %d cells do not)"), Total->Blocked, Total->Cells), Total->Blocked == 0 && Total->Cells > 0);
		} });
	}
	// ================================================================ Lineup（M14：Furniture.csv の家具を全部、壁ぎわに並べて部屋の側から写す。向きと大きさの確認用）
	else if (Scenario.Equals(TEXT("Lineup"), ESearchCase::IgnoreCase))
	{
		// 北の壁ぎわ（部屋の方＝南を向く）と南の壁ぎわ（北を向く）に 1 マスずつあけて並べる。文字・幅・奥行き
		struct FLineupItem { TCHAR Key; int32 W; int32 H; };
		static const FLineupItem North[] = { { 'B', 1, 1 }, { 'S', 1, 1 }, { 'O', 1, 1 }, { 'X', 1, 1 }, { 'K', 1, 1 }, { 'F', 2, 1 }, { 'V', 2, 1 }, { 'Q', 2, 1 },
			{ 'E', 2, 1 }, { 'N', 1, 1 }, { 'J', 2, 1 }, { 'D', 2, 2 }, { 'Y', 2, 1 }, { 'Z', 1, 1 } };
		static const FLineupItem South[] = { { 'A', 1, 1 }, { 'H', 1, 1 }, { 'T', 1, 1 }, { 'G', 1, 1 }, { 'P', 1, 1 }, { 'R', 1, 1 }, { 'L', 1, 1 }, { 'U', 1, 1 },
			{ 'W', 1, 1 }, { 'M', 1, 1 }, { '1', 1, 1 }, { '2', 1, 1 }, { '3', 1, 1 }, { '4', 2, 1 }, { '5', 1, 1 }, { 'I', 4, 2 } };
		constexpr int32 SizeX = 37;
		constexpr int32 SizeY = 10;
		TArray<FString> Rows;
		Rows.Init(FString::ChrN(SizeX, TEXT('.')), SizeY);
		TArray<TPair<TCHAR, FIntPoint>> Labels;
		auto Put = [&Rows, &Labels](const FLineupItem* Items, int32 Count, bool bNorth)
		{
			int32 X = 1;
			for (int32 i = 0; i < Count; ++i)
			{
				const FLineupItem& It = Items[i];
				const int32 Y0 = bNorth ? 0 : SizeY - It.H;
				for (int32 dy = 0; dy < It.H; ++dy)
				{
					for (int32 dx = 0; dx < It.W; ++dx)
					{
						Rows[Y0 + dy][X + dx] = It.Key;
					}
				}
				Labels.Add(TPair<TCHAR, FIntPoint>(It.Key, FIntPoint(X, bNorth ? Y0 + It.H : Y0 - 1)));
				X += It.W + 1;
			}
		};
		Put(North, UE_ARRAY_COUNT(North), true);
		Put(South, UE_ARRAY_COUNT(South), false);
		FFileHelper::SaveStringToFile(FString::Join(Rows, TEXT("\n")) + TEXT("\n"), *(FPaths::ProjectSavedDir() / TEXT("lineup.txt")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
		Steps.Add({ 0.5f, [=, this]
		{
			// 一時的なマップ（居間の色で、間取りだけ Saved/lineup.txt）
			FKakurenboMapRow Row = GM->MapRows.FindRef(TEXT("Parlor"));
			Row.LayoutFile = TEXT("../../Saved/lineup.txt");
			GM->MapRows.Add(TEXT("Lineup"), Row);
			GM->bMapOverride = true;
			GM->MapOverride = TEXT("Lineup");
			GM->SwitchToMap(TEXT("Lineup"));
			GS()->HideTimeRemaining = 600.f;
			const AKakurenboArena* Arena = GM->GetArena();
			Check(FString::Printf(TEXT("lineup map built (%dx%d, %d pieces, %d with meshes)"), Grid()->GetSizeX(), Grid()->GetSizeY(), Arena->GetFurniturePieceCount(), Arena->GetFurnitureMeshCount()),
				Grid()->GetSizeX() == SizeX && Arena->GetFurniturePieceCount() == UE_ARRAY_COUNT(North) + UE_ARRAY_COUNT(South));
			Check(FString::Printf(TEXT("lineup: furniture stays in its cells (overhang %.1f, straddle %d)"), Arena->GetMaxFurnitureOverhang(), Arena->GetFurnitureStraddleCount()),
				Arena->GetMaxFurnitureOverhang() < 8.f && Arena->GetFurnitureStraddleCount() == 0);
			for (const TPair<TCHAR, FIntPoint>& Label : Labels)
			{
				DrawDebugString(GetWorld(), Grid()->CellFloorCenter(Label.Value) + FVector(0.f, 0.f, 30.f), FString::ChrN(1, Label.Key), nullptr, FColor::White, 120.f, true, 2.0f);
			}
		} });
		// 北の列（南を向いているはず）→ 南の列（北を向いているはず）を、真ん中の通路から写す
		for (int32 Side = 0; Side < 2; ++Side)
		{
			for (int32 Part = 0; Part < 4; ++Part)
			{
				Steps.Add({ Side == 0 && Part == 0 ? 6.f : 0.5f, [=, this]
				{
					KeepOnlyOni(-1); // 鬼は全部どける（後から出てきた鬼も）
					GS()->HideTimeRemaining = 600.f;
					TeleportPlayer(Grid()->CellFloorCenter(FIntPoint(5 + Part * 9, Side == 0 ? 6 : 3)));
					SetControlRotation(FRotator(-12.f, Side == 0 ? -90.f : 90.f, 0.f));
				} });
				Steps.Add({ 1.2f, [=] { Shot(FString::Printf(TEXT("lineup_%d_%d"), Side + 1, Part + 1)); } });
			}
		}
		// M14：家具の当たり判定は見えているメッシュの大きさ。浅い衣装だんす（O：奥行き約 55cm）の前のすき間には入れる（家具のマスの中まで歩ける）
		Steps.Add({ 0.3f, [=, this]
		{
			KeepOnlyOni(-1);
			const FIntPoint Shelf(5, 0);
			Check(FString::Printf(TEXT("lineup: (5,0) is the wardrobe O (%c)"), GM->GetArena()->GetLayoutChar(Shelf)), GM->GetArena()->GetLayoutChar(Shelf) == TEXT('O'));
			TeleportPlayer(Grid()->CellFloorCenter(Shelf + FIntPoint(0, 3)));
			GetPawn()->SetActorRotation(FRotator(0.f, -90.f, 0.f));
			SetControlRotation(FRotator(-20.f, -90.f, 0.f));
			SimulateKey(EKeys::W, IE_Pressed);
		} });
		Steps.Add({ 2.0f, [=, this]
		{
			SimulateKey(EKeys::W, IE_Released);
			const FVector Me = GetPawn()->GetActorLocation();
			const FIntPoint Cell = Grid()->WorldToCell(Me);
			const float IntoCell = static_cast<float>(Grid()->CellFloorCenter(FIntPoint(5, 1)).Y - Grid()->GetCellSize() * 0.5f - Me.Y);
			Check(FString::Printf(TEXT("lineup: the player walks into the gap in front of the shallow wardrobe (cell %d,%d, %.0f cm into the furniture cell)"), Cell.X, Cell.Y, IntoCell),
				Cell == FIntPoint(5, 0) && IntoCell > 5.f);
			Shot(TEXT("lineup_3_gap"));
		} });
	}
	// ================================================================ Catalog（家具のメッシュを並べて写す。素材選び用。Saved/catalog.txt に /Game/... を 1 行ずつ、"#" で次のページ）
	else if (Scenario.Equals(TEXT("Catalog"), ESearchCase::IgnoreCase))
	{
		TArray<FString> Lines;
		FFileHelper::LoadFileToStringArray(Lines, *(FPaths::ProjectSavedDir() / TEXT("catalog.txt")));
		TArray<TArray<FString>> Pages;
		Pages.AddDefaulted();
		for (const FString& Line : Lines)
		{
			const FString Trimmed = Line.TrimStartAndEnd();
			if (Trimmed.StartsWith(TEXT("#")))
			{
				Pages.AddDefaulted();
			}
			else if (!Trimmed.IsEmpty())
			{
				Pages.Last().Add(Trimmed);
			}
		}
		TSharedRef<TArray<TObjectPtr<AActor>>> Spawned = MakeShared<TArray<TObjectPtr<AActor>>>();
		Steps.Add({ 0.5f, [=, this] { KakuSkipTime(1000.f); } });
		Steps.Add({ 1.0f, [=, this] { KakuNext(); } });
		Steps.Add({ 0.5f, [=, this]
		{
			KakuNext(); // → 設置（真上から。斜めに傾けて見る）
			if (MyHUD)
			{
				MyHUD->bShowHUD = false;
			}
			const FVector Center = Grid()->CellFloorCenter(FIntPoint(Grid()->GetSizeX() / 2, Grid()->GetSizeY() / 2));
			GetHider()->SetTopDownFocus(Center);
			GetHider()->TopDownDistance = 2300.f;
			GetHider()->TopDownPitch = -48.f;
			GetHider()->SetOverheadYaw(0.f);
			// 見やすいように明るくする
			for (int32 i = 0; i < 4; ++i)
			{
				APointLight* Light = GetWorld()->SpawnActor<APointLight>(Center + FVector(-500.f + (i % 2) * 1000.f, -500.f + (i / 2) * 1000.f, 600.f), FRotator::ZeroRotator);
				Light->PointLightComponent->SetIntensityUnits(ELightUnits::Candelas);
				Light->PointLightComponent->SetIntensity(400.f);
				Light->PointLightComponent->SetAttenuationRadius(2500.f);
				Light->PointLightComponent->SetCastShadows(false);
			}
		} });
		for (int32 PageIndex = 0; PageIndex < Pages.Num(); ++PageIndex)
		{
			const TArray<FString> Page = Pages[PageIndex];
			Steps.Add({ 0.5f, [=, this]
			{
				for (AActor* Old : *Spawned)
				{
					if (Old)
					{
						Old->Destroy();
					}
				}
				Spawned->Reset();
				const FVector Center = Grid()->CellFloorCenter(FIntPoint(Grid()->GetSizeX() / 2, Grid()->GetSizeY() / 2));
				for (int32 i = 0; i < Page.Num(); ++i)
				{
					UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, *Page[i]);
					const int32 Col = i % 5;
					const int32 Row = i / 5;
					// 画面の上 = +X（カメラの向き）。列は Y 方向、行は X 方向（奥から手前へ）
					const FVector Slot = Center + FVector(450.f - Row * 330.f, -840.f + Col * 420.f, 0.f);
					FString Label = FString::Printf(TEXT("%d %s"), i + 1, *FPaths::GetBaseFilename(Page[i]));
					if (Mesh)
					{
						const FBox Box = Mesh->GetBoundingBox();
						const FVector Size = Box.GetSize();
						const float Scale = static_cast<float>(FMath::Min(1.0, 300.0 / FMath::Max3(Size.X, Size.Y, 1.0)));
						AStaticMeshActor* Actor = GetWorld()->SpawnActor<AStaticMeshActor>(Slot, FRotator::ZeroRotator);
						Actor->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
						Actor->GetStaticMeshComponent()->SetStaticMesh(Mesh);
						Actor->SetActorScale3D(FVector(Scale));
						Actor->SetActorLocation(Slot - Box.GetCenter() * Scale + FVector(0.f, 0.f, Box.GetExtent().Z * Scale + 1.f));
						Spawned->Add(Actor);
						Label += FString::Printf(TEXT(" %.0fx%.0fx%.0f"), Size.X, Size.Y, Size.Z);
						UE_LOG(LogTemp, Display, TEXT("[AutoTest] catalog %d-%d %s size %.0f x %.0f x %.0f"), PageIndex + 1, i + 1, *Page[i], Size.X, Size.Y, Size.Z);
					}
					else
					{
						Label += TEXT(" (missing)");
						UE_LOG(LogTemp, Warning, TEXT("[AutoTest] catalog %d-%d %s missing"), PageIndex + 1, i + 1, *Page[i]);
					}
					DrawDebugString(GetWorld(), Slot + FVector(-130.f, -150.f, 10.f), Label, nullptr, FColor::White, 30.f, true, 1.0f);
				}
			} });
			// 2 段ずつ近くから写す（はじめはシェーダーの準備に時間がかかるので長めに待つ）
			for (int32 Half = 0; Half < 2; ++Half)
			{
				Steps.Add({ PageIndex == 0 && Half == 0 ? 12.f : 2.5f, [=, this]
				{
					const FVector Center = Grid()->CellFloorCenter(FIntPoint(Grid()->GetSizeX() / 2, Grid()->GetSizeY() / 2));
					GetHider()->SetTopDownFocus(Center + FVector(285.f - Half * 660.f, 0.f, 0.f));
					GetHider()->TopDownDistance = 1500.f;
				} });
				Steps.Add({ 1.5f, [=] { Shot(FString::Printf(TEXT("catalog_%02d_%d"), PageIndex + 1, Half + 1)); } });
			}
		}
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
