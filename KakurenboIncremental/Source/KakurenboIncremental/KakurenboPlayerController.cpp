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
#include "KakurenboHUD.h"
#include "KakurenboSoundSubsystem.h"
#include "PlaceableBlock.h"
#include "TrapActor.h"

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
			HandleOverheadCamera(DeltaTime, false); // カーソルで商品を選ぶので、マウスの左右では回さない
			HandleShopInput();
			break;
		case EKakurenboPhase::Build:
			HandleOverheadCamera(DeltaTime, false);
			HandleTopDownPan(DeltaTime);
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

	Hider->SetSneaking(false);
	if (Phase == EKakurenboPhase::Hide)
	{
		// 俯瞰・真上 → 三人称：俯瞰カメラが向いていた方向を、少し見下ろす角度で向いて始める
		if (bFirstTime || Hider->GetViewMode() != EHiderViewMode::ThirdPerson)
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

	if (Phase == EKakurenboPhase::Build)
	{
		// 設置パートは真上から舞台全体を見る（プレイヤーは表示しない）
		if (const UKakurenboGridSubsystem* Grid = GetWorld()->GetSubsystem<UKakurenboGridSubsystem>(); Grid && Grid->IsConfigured())
		{
			const FVector Center = (Grid->CellFloorCenter(FIntPoint(0, 0)) + Grid->CellFloorCenter(FIntPoint(Grid->GetSizeX() - 1, Grid->GetSizeY() - 1))) * 0.5f;
			Hider->SetTopDownFocus(Center);
			// 館の大きさが変わったら、全体が入る高さにする（24 マスで 2600cm。同じ大きさの間はズームを覚えておく）
			const FIntPoint Size(Grid->GetSizeX(), Grid->GetSizeY());
			if (Size != LastTopDownFitSize)
			{
				LastTopDownFitSize = Size;
				Hider->TopDownMaxDistance = FMath::Max(Hider->TopDownMaxDistance, FMath::Max(Size.X, Size.Y) * Grid->GetCellSize() * 1.6f);
				Hider->TopDownDistance = FMath::Clamp(FMath::Max(Size.X, Size.Y) * Grid->GetCellSize() * 1.08f, Hider->TopDownMinDistance, Hider->TopDownMaxDistance);
			}
		}
		Hider->SetViewMode(EHiderViewMode::TopDown);
	}
	else
	{
		// 購入・リザルトは斜め上から
		Hider->SetViewMode(EHiderViewMode::Overhead);
	}

	if (Phase == EKakurenboPhase::Build || Phase == EKakurenboPhase::Shop)
	{
		if (Phase == EKakurenboPhase::Shop)
		{
			bPrestigeShopTab = false; // 購入パートはコインのお店から
		}
		// 購入・設置パートはカーソルでボタンやマスを指す
		bShowMouseCursor = true;
		FInputModeGameAndUI Mode;
		Mode.SetHideCursorDuringCapture(false);                         // クリック中もカーソルを消さない
		Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		SetInputMode(Mode);
	}
	else
	{
		// リザルトはマウスの左右でカメラを回す
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

	// 設置パート（真上から）は Z / X で傾ける（Z: 斜め横から見る / X: 真上に戻す）。積んだ壁の高さが見やすくなる
	const bool bTopDown = Hider->GetViewMode() == EHiderViewMode::TopDown;
	if (bTopDown)
	{
		float Tilt = 0.f;
		if (IsInputKeyDown(EKeys::Z)) Tilt += 1.f;
		if (IsInputKeyDown(EKeys::X)) Tilt -= 1.f;
		Hider->AddTopDownTilt(Tilt * OverheadRotateSpeed * 0.6f * DeltaTime);
	}

	// ホイールを押しながらドラッグで回す（カーソルの移動量で判定）。設置パートでは上下のドラッグで傾ける
	float MouseX = 0.f, MouseY = 0.f;
	if (IsInputKeyDown(EKeys::MiddleMouseButton) && GetMousePosition(MouseX, MouseY))
	{
		if (bDraggingCamera)
		{
			Hider->AddOverheadYaw((MouseX - LastDragMousePosition.X) * OverheadDragSensitivity);
			if (bTopDown)
			{
				Hider->AddTopDownTilt(-(MouseY - LastDragMousePosition.Y) * OverheadDragSensitivity); // 上へドラッグで傾く
			}
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

void AKakurenboPlayerController::HandleTopDownPan(float DeltaTime)
{
	AHiderCharacter* Hider = GetHider();
	const UKakurenboGridSubsystem* Grid = GetWorld()->GetSubsystem<UKakurenboGridSubsystem>();
	if (!Hider || !Grid || !Grid->IsConfigured())
	{
		return;
	}
	// 画面の上 = カメラの向き（OverheadYaw）
	const FRotator YawRot(0.f, Hider->GetOverheadYaw(), 0.f);
	const FVector Forward = FRotationMatrix(YawRot).GetUnitAxis(EAxis::X);
	const FVector Right = FRotationMatrix(YawRot).GetUnitAxis(EAxis::Y);
	float F = 0.f, R = 0.f;
	if (IsInputKeyDown(EKeys::W)) F += 1.f;
	if (IsInputKeyDown(EKeys::S)) F -= 1.f;
	if (IsInputKeyDown(EKeys::D)) R += 1.f;
	if (IsInputKeyDown(EKeys::A)) R -= 1.f;
	if (F == 0.f && R == 0.f)
	{
		return;
	}
	// 高いところから見ているほど速く動かす
	const float Speed = TopDownPanSpeed * FMath::Max(0.3f, Hider->TopDownDistance / 2600.f);
	FVector Focus = Hider->GetTopDownFocus() + (Forward * F + Right * R).GetClampedToMaxSize(1.f) * Speed * DeltaTime;

	// 舞台の外へ行きすぎないように
	const FVector Min = Grid->CellFloorCenter(FIntPoint(0, 0));
	const FVector Max = Grid->CellFloorCenter(FIntPoint(Grid->GetSizeX() - 1, Grid->GetSizeY() - 1));
	Focus.X = FMath::Clamp(Focus.X, Min.X, Max.X);
	Focus.Y = FMath::Clamp(Focus.Y, Min.Y, Max.Y);
	Hider->SetTopDownFocus(Focus);
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
	if (WasInputKeyJustPressed(EKeys::Tab))
	{
		bPrestigeShopTab = !bPrestigeShopTab;
	}
	if (const int32 Number = GetPressedNumberKey())
	{
		BuyItem(Number);
	}
	if (WasInputKeyJustPressed(EKeys::P))
	{
		RequestPrestige();
	}
	if (WasInputKeyJustPressed(EKeys::R))
	{
		if (AKakurenboGameMode* GM = GetKakurenboGameMode())
		{
			GM->BuyAllMissing();
		}
	}
	if (WasInputKeyJustPressed(EKeys::LeftMouseButton))
	{
		HandleUIClick();
	}
}

void AKakurenboPlayerController::RequestPrestige()
{
	// 転生は取り返しがつかないので、2 回押して決める
	AKakurenboGameMode* GM = GetKakurenboGameMode();
	if (!GM || !GM->CanPrestige())
	{
		return;
	}
	if (IsPrestigeConfirmPending())
	{
		PrestigeConfirmUntil = -1.f;
		GM->Prestige();
	}
	else
	{
		PrestigeConfirmUntil = GetWorld()->GetTimeSeconds() + PrestigeConfirmSeconds;
		UKakurenboSoundSubsystem::Play2D(this, EKakurenboSfx::CountdownBeep, 0.6f);
	}
}

bool AKakurenboPlayerController::GetUICursorPosition(FVector2D& OutPosition) const
{
	if (bUseTestCursor)
	{
		OutPosition = TestCursorPosition;
		return true;
	}
	float X = 0.f, Y = 0.f;
	if (!bShowMouseCursor || !GetMousePosition(X, Y))
	{
		return false;
	}
	OutPosition = FVector2D(X, Y);
	return true;
}

bool AKakurenboPlayerController::HandleUIClick()
{
	AKakurenboGameMode* GM = GetKakurenboGameMode();
	const AKakurenboHUD* HUD = GetHUD<AKakurenboHUD>();
	FVector2D Cursor;
	FKakurenboUIButton Button;
	if (!GM || !HUD || !GetUICursorPosition(Cursor) || !HUD->FindButtonAt(Cursor, Button))
	{
		return false;
	}
	switch (Button.Action)
	{
	case EKakurenboUIAction::ShopTab:   bPrestigeShopTab = (Button.Index == 1); break;
	case EKakurenboUIAction::ShopItem:  BuyItem(Button.Index + 1); break;
	case EKakurenboUIAction::Prestige:  RequestPrestige(); break;
	case EKakurenboUIAction::NextPhase: GM->AdvancePhase(); break;
	case EKakurenboUIAction::BuildSlot: SelectedBuildSlot = Button.Index; break;
	case EKakurenboUIAction::RefillAll: GM->BuyAllMissing(); break;
	case EKakurenboUIAction::BackToShop: GM->ReturnToShop(); break;
	default: break;
	}
	return true; // パネルの上のクリックは、後ろの床に壁を置かない
}

bool AKakurenboPlayerController::IsPrestigeConfirmPending() const
{
	return GetWorld() && GetWorld()->GetTimeSeconds() < PrestigeConfirmUntil;
}

bool AKakurenboPlayerController::BuyItem(int32 ItemNumber)
{
	AKakurenboGameMode* GM = GetKakurenboGameMode();
	if (!GM)
	{
		return false;
	}
	return bPrestigeShopTab ? GM->TryBuyPrestigeUpgrade(ItemNumber - 1) : GM->TryBuyShopItem(ItemNumber - 1);
}

// ---------------------------------------------------------------- 設置パート

bool AKakurenboPlayerController::IsTrapSlotSelected() const
{
	const AKakurenboGameMode* GM = GetKakurenboGameMode();
	return GM && SelectedBuildSlot >= GM->WallTypes.Num();
}

int32 AKakurenboPlayerController::GetSelectedWallType() const
{
	return IsTrapSlotSelected() ? INDEX_NONE : SelectedBuildSlot;
}

int32 AKakurenboPlayerController::GetSelectedTrapType() const
{
	const AKakurenboGameMode* GM = GetKakurenboGameMode();
	return (GM && IsTrapSlotSelected()) ? SelectedBuildSlot - GM->WallTypes.Num() : INDEX_NONE;
}

void AKakurenboPlayerController::HandleBuildInput()
{
	AKakurenboGameMode* GM = GetKakurenboGameMode();

	// 置く物を選ぶ（壁の種類 → 罠の種類の順に番号が付く）
	const int32 NumSlots = GM->WallTypes.Num() + GM->TrapTypes.Num();
	const int32 Number = GetPressedNumberKey();
	if (Number >= 1 && Number <= NumSlots)
	{
		SelectedBuildSlot = Number - 1;
	}

	// カーソルの先の置き場所
	float MouseX = 0.f, MouseY = 0.f;
	if (bUseTestCursor)
	{
		UpdateBuildTargetAt(TestCursorPosition);
	}
	else if (GetMousePosition(MouseX, MouseY))
	{
		UpdateBuildTargetAt(FVector2D(MouseX, MouseY));
	}
	else
	{
		ClearBuildTarget();
	}

	// 左クリック：画面下の欄やボタンの上ならそれを押す。そうでなければ置く
	if (WasInputKeyJustPressed(EKeys::LeftMouseButton) && !HandleUIClick() && GM->GetPhase() == EKakurenboPhase::Build && bCanPlaceAtTarget)
	{
		if (IsTrapSlotSelected())
		{
			GM->PlaceTrap(BuildTargetCell, GetSelectedTrapType());
		}
		else
		{
			GM->PlaceWall(BuildTargetCell, GetSelectedWallType());
		}
	}
	if (WasInputKeyJustPressed(EKeys::RightMouseButton))
	{
		if (PickUpTarget)
		{
			GM->PickUpWall(PickUpTarget);
			PickUpTarget = nullptr;
		}
		else if (bHasTrapPickUpTarget)
		{
			GM->PickUpTrap(TrapPickUpCell);
			bHasTrapPickUpTarget = false;
		}
	}

	// T: かくれんぼを始める場所をカーソルのマスへ
	if (WasInputKeyJustPressed(EKeys::T) && bHasBuildTarget)
	{
		SetStartCell(BuildTargetCell);
	}

	// B / BackSpace: 購入パートへ戻る（置いた壁・罠はそのまま）
	if (WasInputKeyJustPressed(EKeys::B) || WasInputKeyJustPressed(EKeys::BackSpace))
	{
		GM->ReturnToShop();
	}
}

bool AKakurenboPlayerController::SetStartCell(const FIntPoint& Cell)
{
	AKakurenboGameMode* GM = GetKakurenboGameMode();
	FText Reason;
	if (GM && GM->MovePlayerStart(Cell, &Reason))
	{
		return true;
	}
	if (GM && !Reason.IsEmpty())
	{
		GM->ShowNotice(Reason, 2.5f);
	}
	return false;
}

void AKakurenboPlayerController::ClearBuildTarget()
{
	bHasBuildTarget = false;
	bCanPlaceAtTarget = false;
	BuildTargetReason = FText::GetEmpty();
	PickUpTarget = nullptr;
	bHasTrapPickUpTarget = false;
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
	bCanPlaceAtTarget = IsTrapSlotSelected()
		? GM->CanPlaceTrap(BuildTargetCell, GetSelectedTrapType(), &BuildTargetReason)
		: GM->CanPlaceWall(BuildTargetCell, GetSelectedWallType(), &BuildTargetReason);

	// 罠には当たり判定が無いので、指しているマスに罠（か消えた罠の設計図）があればそれを回収の対象にする
	if (!PickUpTarget && Grid->HasTrap(BuildTargetCell))
	{
		bHasTrapPickUpTarget = true;
		TrapPickUpCell = BuildTargetCell;
	}
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

	// 罠用の平たい枠（床の上 8cm）
	const FVector TrapExtent(Cell * 0.45f, Cell * 0.45f, 4.f);
	auto TrapBoxCenter = [Grid](const FIntPoint& C) { return Grid->CellFloorCenter(C) + FVector(0.f, 0.f, 4.f); };

	// 壊れたまま直せていない壁・在庫が無くて置き直せていない罠（設計図にはある）を赤い枠で示す
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
			if (Grid->GetTrapDesign(C) != INDEX_NONE && !Grid->GetTrap(C))
			{
				DrawDebugBox(GetWorld(), TrapBoxCenter(C), TrapExtent, FColor(255, 60, 60), false, 0.f, 0, 2.f);
			}
		}
	}

	// 置き場所のプレビュー（緑: 置ける / 赤: 置けない）
	if (bHasBuildTarget)
	{
		const FColor Color = bCanPlaceAtTarget ? FColor(80, 255, 80) : FColor(255, 70, 70);
		if (IsTrapSlotSelected())
		{
			DrawDebugBox(GetWorld(), TrapBoxCenter(BuildTargetCell), TrapExtent, Color, false, 0.f, 0, 3.f);
		}
		else
		{
			const FVector Center = Grid->CellToWorld(BuildTargetCell, Grid->GetColumnHeight(BuildTargetCell));
			DrawDebugBox(GetWorld(), Center, BlockExtent, Color, false, 0.f, 0, 3.f);
		}
	}
	// スタート位置（プレイヤーは表示していないので、床に青い印を描く）
	if (const APawn* P = GetPawn())
	{
		const FVector Floor(P->GetActorLocation().X, P->GetActorLocation().Y, Grid->CellFloorCenter(FIntPoint(0, 0)).Z + 3.f);
		// 名前を HUD の Blue と分ける（ビルドで複数の .cpp が 1 つにまとめられると、同じ名前がぶつかってエラーになる）
		const FColor StartMarkerColor(60, 150, 255);
		DrawDebugCircle(GetWorld(), Floor, Cell * 0.4f, 32, StartMarkerColor, false, 0.f, 0, 6.f, FVector(1, 0, 0), FVector(0, 1, 0), false);
		DrawDebugCircle(GetWorld(), Floor, Cell * 0.2f, 24, StartMarkerColor, false, 0.f, 0, 6.f, FVector(1, 0, 0), FVector(0, 1, 0), false);
	}

	// 回収できる壁・罠を黄色で囲む
	if (PickUpTarget)
	{
		DrawDebugBox(GetWorld(), PickUpTarget->GetActorLocation(), BlockExtent * 1.04f, FColor(255, 220, 60), false, 0.f, 0, 2.f);
	}
	else if (bHasTrapPickUpTarget)
	{
		DrawDebugBox(GetWorld(), TrapBoxCenter(TrapPickUpCell), TrapExtent * FVector(1.08f, 1.08f, 1.5f), FColor(255, 220, 60), false, 0.f, 0, 2.f);
	}
}

// ---------------------------------------------------------------- かくれんぼパート

void AKakurenboPlayerController::HandleHideInput()
{
	HandleCharacterMovement();
	HandleZoom();

	// Ctrl（または C）を押している間はしのび足（ゆっくり・足音が小さい）
	if (AHiderCharacter* Hider = GetHider())
	{
		Hider->SetSneaking(bTestSneak || IsInputKeyDown(EKeys::LeftControl) || IsInputKeyDown(EKeys::RightControl) || IsInputKeyDown(EKeys::C));
	}

	if (WasInputKeyJustPressed(EKeys::LeftMouseButton) || WasInputKeyJustPressed(EKeys::F))
	{
		DoMash();
	}
	if (WasInputKeyJustPressed(EKeys::LeftShift) || WasInputKeyJustPressed(EKeys::RightShift))
	{
		DoDash();
	}
}

void AKakurenboPlayerController::DoDash()
{
	AKakurenboGameMode* GM = GetKakurenboGameMode();
	AHiderCharacter* Hider = GetHider();
	if (!GM || !Hider || GM->GetPhase() != EKakurenboPhase::Hide)
	{
		return;
	}
	// 速くなる代わりに大きな音が出る（クールタイム中は何も起きない）
	if (Hider->TryStartDash())
	{
		GM->HandleDash(Hider->GetActorLocation(), Hider->DashNoiseLoudness);
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

void AKakurenboPlayerController::KakuBuyPrestige(int32 ItemNumber)
{
	if (AKakurenboGameMode* GM = GetKakurenboGameMode())
	{
		GM->TryBuyPrestigeUpgrade(ItemNumber - 1);
	}
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

void AKakurenboPlayerController::KakuPlaceTrap(int32 X, int32 Y, int32 TrapType)
{
	if (AKakurenboGameMode* GM = GetKakurenboGameMode())
	{
		FText Reason;
		if (!GM->CanPlaceTrap(FIntPoint(X, Y), TrapType, &Reason) || !GM->PlaceTrap(FIntPoint(X, Y), TrapType))
		{
			UE_LOG(LogTemp, Warning, TEXT("KakuPlaceTrap (%d,%d) type %d failed: %s"), X, Y, TrapType, *Reason.ToString());
		}
	}
}

void AKakurenboPlayerController::KakuVolume(float Volume)
{
	if (UKakurenboSoundSubsystem* Sound = GetWorld()->GetSubsystem<UKakurenboSoundSubsystem>())
	{
		Sound->MasterVolume = FMath::Clamp(Volume, 0.f, 1.f);
	}
}

void AKakurenboPlayerController::KakuSens(float Sensitivity)
{
	MouseSensitivity = FMath::Max(0.001f, Sensitivity);
}

void AKakurenboPlayerController::KakuResetSave()
{
	if (AKakurenboGameMode* GM = GetKakurenboGameMode())
	{
		GM->ResetProgress();
	}
}

void AKakurenboPlayerController::KakuPrestige()
{
	if (AKakurenboGameMode* GM = GetKakurenboGameMode())
	{
		if (!GM->Prestige())
		{
			UE_LOG(LogTemp, Warning, TEXT("KakuPrestige failed (phase %s, points on reset %d)"), *UEnum::GetValueAsString(GM->GetPhase()), GM->GetPrestigePointsOnReset());
		}
	}
}

void AKakurenboPlayerController::KakuDash()
{
	DoDash();
}

void AKakurenboPlayerController::KakuShop()
{
	if (AKakurenboGameMode* GM = GetKakurenboGameMode())
	{
		GM->ReturnToShop();
	}
}

void AKakurenboPlayerController::KakuMusicVolume(float Volume)
{
	if (UKakurenboSoundSubsystem* Sound = GetWorld()->GetSubsystem<UKakurenboSoundSubsystem>())
	{
		Sound->SetMusicVolume(Volume);
	}
}

void AKakurenboPlayerController::KakuSetStart(int32 X, int32 Y)
{
	if (!SetStartCell(FIntPoint(X, Y)))
	{
		UE_LOG(LogTemp, Warning, TEXT("KakuSetStart (%d,%d) failed"), X, Y);
	}
}
