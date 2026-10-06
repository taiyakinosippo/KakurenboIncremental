#include "KakurenboPlayerController.h"

#include "Camera/PlayerCameraManager.h"
#include "DrawDebugHelpers.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerInput.h"
#include "HiderCharacter.h"
#include "KakurenboGameMode.h"
#include "KakurenboGameState.h"
#include "KakurenboGridSubsystem.h"
#include "PlaceableBlock.h"

AKakurenboPlayerController::AKakurenboPlayerController()
{
	bShowMouseCursor = false;
}

AKakurenboGameMode* AKakurenboPlayerController::GetKakurenboGameMode() const
{
	// GetAuthGameMode: シングルプレイでは常に取得できる
	return GetWorld() ? GetWorld()->GetAuthGameMode<AKakurenboGameMode>() : nullptr;
}

AHiderCharacter* AKakurenboPlayerController::GetHider() const
{
	return Cast<AHiderCharacter>(GetPawn());
}

void AKakurenboPlayerController::BeginPlay()
{
	Super::BeginPlay();

	// 三人称カメラの上下の角度を制限する（真上・真下まで回り込まないように）
	if (PlayerCameraManager)
	{
		PlayerCameraManager->ViewPitchMin = ThirdPersonPitchMin;
		PlayerCameraManager->ViewPitchMax = ThirdPersonPitchMax;
	}
}

void AKakurenboPlayerController::PlayerTick(float DeltaTime)
{
	// Super の中で入力処理（→ PostProcessInput）と視点の更新が行われる
	Super::PlayerTick(DeltaTime);

	AKakurenboGameMode* GM = GetKakurenboGameMode();
	if (GM && GM->GetPhase() == EKakurenboPhase::Build)
	{
		DrawBuildPreview();
	}
}

void AKakurenboPlayerController::PostProcessInput(const float DeltaTime, const bool bGamePaused)
{
	AKakurenboGameMode* GM = GetKakurenboGameMode();
	if (GM && GetHider())
	{
		const EKakurenboPhase Phase = GM->GetPhase();
		ApplyViewForPhase(Phase);

		switch (Phase)
		{
		case EKakurenboPhase::Shop:
			HandleOverheadCamera(DeltaTime, true);
			HandleShopInput();
			break;
		case EKakurenboPhase::Build:
			HandleOverheadCamera(DeltaTime, false);
			HandleBuildInput();
			break;
		case EKakurenboPhase::Hide:
			HandleMouseLook();
			HandleHideInput();
			break;
		case EKakurenboPhase::Result:
			HandleOverheadCamera(DeltaTime, true);
			break;
		}

		if (WasInputKeyJustPressed(EKeys::Enter))
		{
			GM->AdvancePhase();
		}
	}

	// 親クラスの処理（視点入力を無視する設定のときに回転入力を消す）は最後に呼ぶ
	Super::PostProcessInput(DeltaTime, bGamePaused);
}

// ---------------------------------------------------------------- 視点の切り替え

void AKakurenboPlayerController::ApplyViewForPhase(EKakurenboPhase Phase)
{
	AHiderCharacter* Hider = GetHider();
	if (!Hider || (bViewInitialized && Phase == ViewPhase))
	{
		return;
	}
	const bool bFirstTime = !bViewInitialized;
	bViewInitialized = true;
	ViewPhase = Phase;

	if (Phase == EKakurenboPhase::Hide)
	{
		// 俯瞰 → 三人称：俯瞰カメラが向いていた方向を、少し見下ろす角度で向いて始める
		if (bFirstTime || Hider->GetViewMode() == EHiderViewMode::Overhead)
		{
			SetControlRotation(FRotator(-20.f, bFirstTime ? GetControlRotation().Yaw : Hider->GetOverheadYaw(), 0.f));
		}
		Hider->SetViewMode(EHiderViewMode::ThirdPerson);

		// カーソルを消してマウスを捕まえる（マウスの動き＝カメラの回転）
		bShowMouseCursor = false;
		SetInputMode(FInputModeGameOnly());
		ApplyAutoTestInputIsolation();
		return;
	}

	// 三人称 → 俯瞰：三人称で向いていた方向から見下ろす
	if (Hider->GetViewMode() == EHiderViewMode::ThirdPerson)
	{
		Hider->SetOverheadYaw(GetControlRotation().Yaw);
	}
	Hider->SetViewMode(EHiderViewMode::Overhead);

	if (Phase == EKakurenboPhase::Build)
	{
		// 設置パートはカーソルでマスを指す
		bShowMouseCursor = true;
		FInputModeGameAndUI Mode;
		Mode.SetHideCursorDuringCapture(false);                         // クリック中もカーソルを消さない
		Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		SetInputMode(Mode);
	}
	else
	{
		// 購入・リザルトはマウスの左右でカメラを回す
		bShowMouseCursor = false;
		SetInputMode(FInputModeGameOnly());
	}
	ApplyAutoTestInputIsolation();
	ClearBuildTarget();
}

void AKakurenboPlayerController::ApplyAutoTestInputIsolation()
{
	// SetInputMode は入力の無視を解除してしまうので、切り替えのたびにかけ直す
	if (bIgnoreRealInputForAutoTest && GetLocalPlayer() && GetLocalPlayer()->ViewportClient)
	{
		GetLocalPlayer()->ViewportClient->SetIgnoreInput(true);
	}
}

void AKakurenboPlayerController::HandleMouseLook()
{
	if (!PlayerInput)
	{
		return;
	}
	// 生のマウス移動量を使う（DefaultInput.ini の感度設定の影響を受けないように）
	const float DX = PlayerInput->GetRawKeyValue(EKeys::MouseX);
	const float DY = PlayerInput->GetRawKeyValue(EKeys::MouseY); // 上に動かすとプラス

	// AddYawInput / AddPitchInput は、プロジェクト設定「Enable Legacy Input Scales」が有効だと
	// 古い倍率（左右 ×2.5、上下 ×-2.5 ＝上下反転）が勝手にかかる。
	// 設定に左右されないよう、回転入力（このあと UpdateRotation で視点に反映される）へ直接足す
	RotationInput.Yaw += DX * MouseSensitivity;
	RotationInput.Pitch += (bInvertMouseY ? -DY : DY) * MouseSensitivity;
}

void AKakurenboPlayerController::HandleOverheadCamera(float DeltaTime, bool bMouseOrbits)
{
	AHiderCharacter* Hider = GetHider();

	// Q / E で回転
	float Rotate = 0.f;
	if (IsInputKeyDown(EKeys::Q)) Rotate -= 1.f;
	if (IsInputKeyDown(EKeys::E)) Rotate += 1.f;
	Hider->AddOverheadYaw(Rotate * OverheadRotateSpeed * DeltaTime);

	// カーソルを出していないパートでは、マウスの左右でそのまま回す
	if (bMouseOrbits && PlayerInput)
	{
		Hider->AddOverheadYaw(PlayerInput->GetRawKeyValue(EKeys::MouseX) * MouseSensitivity);
	}

	// ホイールを押しながらドラッグで回す（カーソルの移動量で判定）
	float MouseX = 0.f, MouseY = 0.f;
	if (IsInputKeyDown(EKeys::MiddleMouseButton) && GetMousePosition(MouseX, MouseY))
	{
		if (bDraggingCamera)
		{
			Hider->AddOverheadYaw((MouseX - LastDragMousePosition.X) * OverheadDragSensitivity);
		}
		LastDragMousePosition = FVector2D(MouseX, MouseY);
		bDraggingCamera = true;
	}
	else
	{
		bDraggingCamera = false;
	}

	HandleZoom();
}

void AKakurenboPlayerController::HandleZoom()
{
	AHiderCharacter* Hider = GetHider();
	if (WasInputKeyJustPressed(EKeys::MouseScrollUp))
	{
		Hider->AddZoom(-ZoomStep);
	}
	if (WasInputKeyJustPressed(EKeys::MouseScrollDown))
	{
		Hider->AddZoom(ZoomStep);
	}
}

void AKakurenboPlayerController::HandleCharacterMovement()
{
	AHiderCharacter* Hider = GetHider();

	// WASD を今のカメラの向き基準の移動に変換する（画面の奥＝W）
	const FRotator YawRot(0.f, Hider->GetViewYaw(), 0.f);
	const FVector Forward = FRotationMatrix(YawRot).GetUnitAxis(EAxis::X);
	const FVector Right = FRotationMatrix(YawRot).GetUnitAxis(EAxis::Y);

	float F = 0.f, R = 0.f;
	if (IsInputKeyDown(EKeys::W)) F += 1.f;
	if (IsInputKeyDown(EKeys::S)) F -= 1.f;
	if (IsInputKeyDown(EKeys::D)) R += 1.f;
	if (IsInputKeyDown(EKeys::A)) R -= 1.f;
	Hider->AddMovementInput(Forward, F);
	Hider->AddMovementInput(Right, R);

	if (WasInputKeyJustPressed(EKeys::SpaceBar))
	{
		Hider->Jump();
	}
}

int32 AKakurenboPlayerController::GetPressedNumberKey() const
{
	static const FKey NumberKeys[] = {
		EKeys::One, EKeys::Two, EKeys::Three, EKeys::Four, EKeys::Five,
		EKeys::Six, EKeys::Seven, EKeys::Eight, EKeys::Nine };
	static const FKey NumPadKeys[] = {
		EKeys::NumPadOne, EKeys::NumPadTwo, EKeys::NumPadThree, EKeys::NumPadFour, EKeys::NumPadFive,
		EKeys::NumPadSix, EKeys::NumPadSeven, EKeys::NumPadEight, EKeys::NumPadNine };

	for (int32 i = 0; i < 9; ++i)
	{
		if (WasInputKeyJustPressed(NumberKeys[i]) || WasInputKeyJustPressed(NumPadKeys[i]))
		{
			return i + 1;
		}
	}
	return 0;
}

// ---------------------------------------------------------------- 購入パート

void AKakurenboPlayerController::HandleShopInput()
{
	if (const int32 Number = GetPressedNumberKey())
	{
		BuyItem(Number);
	}
}

bool AKakurenboPlayerController::BuyItem(int32 ItemNumber)
{
	AKakurenboGameMode* GM = GetKakurenboGameMode();
	return GM && GM->TryBuyShopItem(ItemNumber - 1);
}

// ---------------------------------------------------------------- 設置パート

void AKakurenboPlayerController::HandleBuildInput()
{
	AKakurenboGameMode* GM = GetKakurenboGameMode();

	HandleCharacterMovement();

	// 壁の種類を選ぶ
	const int32 NumTypes = GM->WallTypes.Num();
	const int32 Number = GetPressedNumberKey();
	if (Number >= 1 && Number <= NumTypes)
	{
		SelectedWallType = Number - 1;
	}

	// カーソルの先の置き場所
	float MouseX = 0.f, MouseY = 0.f;
	if (GetMousePosition(MouseX, MouseY))
	{
		UpdateBuildTargetAt(FVector2D(MouseX, MouseY));
	}
	else
	{
		ClearBuildTarget();
	}

	if (WasInputKeyJustPressed(EKeys::LeftMouseButton) && bCanPlaceAtTarget)
	{
		GM->PlaceWall(BuildTargetCell, SelectedWallType);
	}
	if (WasInputKeyJustPressed(EKeys::RightMouseButton) && PickUpTarget)
	{
		GM->PickUpWall(PickUpTarget);
		PickUpTarget = nullptr;
	}
}

void AKakurenboPlayerController::ClearBuildTarget()
{
	bHasBuildTarget = false;
	bCanPlaceAtTarget = false;
	BuildTargetReason = FText::GetEmpty();
	PickUpTarget = nullptr;
}

void AKakurenboPlayerController::UpdateBuildTargetAt(const FVector2D& ScreenPosition)
{
	ClearBuildTarget();

	AKakurenboGameMode* GM = GetKakurenboGameMode();
	UKakurenboGridSubsystem* Grid = GetWorld()->GetSubsystem<UKakurenboGridSubsystem>();
	if (!GM || !Grid || !Grid->IsConfigured())
	{
		return;
	}

	// 画面上の点を 3D 空間の「カメラから伸びる線」に変換してレイを飛ばす
	FVector RayOrigin, RayDirection;
	if (!DeprojectScreenPositionToWorld(ScreenPosition.X, ScreenPosition.Y, RayOrigin, RayDirection))
	{
		return;
	}
	FCollisionQueryParams Params(SCENE_QUERY_STAT(BuildTrace), false, GetPawn());
	FHitResult Hit;
	if (!GetWorld()->LineTraceSingleByChannel(Hit, RayOrigin, RayOrigin + RayDirection * 20000.f, ECC_Visibility, Params))
	{
		return;
	}

	PickUpTarget = Cast<APlaceableBlock>(Hit.GetActor());

	// 当たった面の外側半マスの位置にあるマスへ置く。
	// （ブロックの上面に当たれば同じマスの上に積む、側面なら隣のマス、床ならそのマス）
	const FVector Probe = Hit.ImpactPoint + Hit.ImpactNormal * (Grid->GetCellSize() * 0.5f);
	BuildTargetCell = Grid->WorldToCell(Probe);
	if (!Grid->IsInside(BuildTargetCell))
	{
		return;
	}
	bHasBuildTarget = true;
	bCanPlaceAtTarget = GM->CanPlaceWall(BuildTargetCell, SelectedWallType, &BuildTargetReason);
}

void AKakurenboPlayerController::DrawBuildPreview() const
{
	UKakurenboGridSubsystem* Grid = GetWorld()->GetSubsystem<UKakurenboGridSubsystem>();
	if (!Grid || !Grid->IsConfigured())
	{
		return;
	}
	const float Cell = Grid->GetCellSize();
	const FVector BlockExtent(Cell * 0.5f, Cell * 0.5f, Grid->GetBlockHeight() * 0.5f);

	// 床にグリッド線を引く
	const FVector Origin = Grid->CellFloorCenter(FIntPoint(0, 0)) - FVector(Cell * 0.5f, Cell * 0.5f, -1.f);
	const float LenX = Grid->GetSizeX() * Cell;
	const float LenY = Grid->GetSizeY() * Cell;
	const FColor LineColor(255, 255, 255, 40);
	for (int32 X = 0; X <= Grid->GetSizeX(); ++X)
	{
		DrawDebugLine(GetWorld(), Origin + FVector(X * Cell, 0, 0), Origin + FVector(X * Cell, LenY, 0), LineColor, false, 0.f, 0, 1.f);
	}
	for (int32 Y = 0; Y <= Grid->GetSizeY(); ++Y)
	{
		DrawDebugLine(GetWorld(), Origin + FVector(0, Y * Cell, 0), Origin + FVector(LenX, Y * Cell, 0), LineColor, false, 0.f, 0, 1.f);
	}

	// 壊れたまま直せていない壁（設計図にはある）を赤い枠で示す
	for (int32 Y = 0; Y < Grid->GetSizeY(); ++Y)
	{
		for (int32 X = 0; X < Grid->GetSizeX(); ++X)
		{
			const FIntPoint C(X, Y);
			const int32 Height = Grid->GetColumnHeight(C);
			const int32 DesignHeight = Grid->GetDesignHeight(C);
			for (int32 Level = Height; Level < DesignHeight; ++Level)
			{
				DrawDebugBox(GetWorld(), Grid->CellToWorld(C, Level), BlockExtent * 0.95f, FColor(255, 60, 60), false, 0.f, 0, 2.f);
			}
		}
	}

	// 置き場所のプレビュー（緑: 置ける / 赤: 置けない）
	if (bHasBuildTarget)
	{
		const FVector Center = Grid->CellToWorld(BuildTargetCell, Grid->GetColumnHeight(BuildTargetCell));
		DrawDebugBox(GetWorld(), Center, BlockExtent, bCanPlaceAtTarget ? FColor(80, 255, 80) : FColor(255, 70, 70), false, 0.f, 0, 3.f);
	}
	// 回収できる壁を黄色で囲む
	if (PickUpTarget)
	{
		DrawDebugBox(GetWorld(), PickUpTarget->GetActorLocation(), BlockExtent * 1.04f, FColor(255, 220, 60), false, 0.f, 0, 2.f);
	}
}

// ---------------------------------------------------------------- かくれんぼパート

void AKakurenboPlayerController::HandleHideInput()
{
	HandleCharacterMovement();
	HandleZoom();

	if (WasInputKeyJustPressed(EKeys::LeftMouseButton) || WasInputKeyJustPressed(EKeys::F))
	{
		DoMash();
	}
}

void AKakurenboPlayerController::DoMash()
{
	AKakurenboGameMode* GM = GetKakurenboGameMode();
	AHiderCharacter* Hider = GetHider();
	if (!GM || !Hider)
	{
		return;
	}
	GM->HandleMash(Hider->GetActorLocation());
	Hider->PlayMashFeedback();
}

// ---------------------------------------------------------------- デバッグ

void AKakurenboPlayerController::KakuAddCoins(double Amount)
{
	if (AKakurenboGameMode* GM = GetKakurenboGameMode())
	{
		GM->DebugAddCoins(Amount);
	}
}

void AKakurenboPlayerController::KakuSkipTime(float Seconds)
{
	if (AKakurenboGameMode* GM = GetKakurenboGameMode())
	{
		GM->DebugSkipTime(Seconds);
	}
}

void AKakurenboPlayerController::KakuNext()
{
	if (AKakurenboGameMode* GM = GetKakurenboGameMode())
	{
		GM->AdvancePhase();
	}
}

void AKakurenboPlayerController::KakuMash(int32 Count)
{
	for (int32 i = 0; i < Count; ++i)
	{
		DoMash();
	}
}

void AKakurenboPlayerController::KakuBuy(int32 ItemNumber)
{
	BuyItem(ItemNumber);
}

void AKakurenboPlayerController::KakuPlaceWall(int32 X, int32 Y, int32 WallType)
{
	if (AKakurenboGameMode* GM = GetKakurenboGameMode())
	{
		FText Reason;
		if (!GM->CanPlaceWall(FIntPoint(X, Y), WallType, &Reason) || !GM->PlaceWall(FIntPoint(X, Y), WallType))
		{
			UE_LOG(LogTemp, Warning, TEXT("KakuPlaceWall (%d,%d) type %d failed: %s (pawn at %s)"),
				X, Y, WallType, *Reason.ToString(), GetPawn() ? *GetPawn()->GetActorLocation().ToString() : TEXT("none"));
		}
	}
}

void AKakurenboPlayerController::KakuSens(float Sensitivity)
{
	MouseSensitivity = FMath::Max(0.001f, Sensitivity);
}
