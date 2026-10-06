#include "KakurenboPlayerController.h"

#include "GameFramework/CharacterMovementComponent.h"
#include "HiderCharacter.h"
#include "KakurenboGameMode.h"
#include "KakurenboGameState.h"
#include "KakurenboGridSubsystem.h"
#include "PlaceableBlock.h"
#include "DrawDebugHelpers.h"

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

void AKakurenboPlayerController::PlayerTick(float DeltaTime)
{
	// Super の中で入力が処理され、このフレームのキー状態が確定する
	Super::PlayerTick(DeltaTime);

	AKakurenboGameMode* GM = GetKakurenboGameMode();
	if (!GM)
	{
		return;
	}

	TickLook();

	switch (GM->GetPhase())
	{
	case EKakurenboPhase::Shop:   TickShop(); break;
	case EKakurenboPhase::Build:  TickBuild(); break;
	case EKakurenboPhase::Hide:   TickHide(); break;
	case EKakurenboPhase::Result: break;
	}

	if (WasInputKeyJustPressed(EKeys::Enter))
	{
		GM->AdvancePhase();
	}
}

void AKakurenboPlayerController::TickLook()
{
	float DX = 0.f, DY = 0.f;
	GetInputMouseDelta(DX, DY);
	AddYawInput(DX * MouseSensitivity);
	AddPitchInput(-DY * MouseSensitivity);
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

void AKakurenboPlayerController::TickShop()
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

void AKakurenboPlayerController::TickBuild()
{
	AHiderCharacter* Hider = GetHider();
	if (!Hider)
	{
		return;
	}

	// WASD をカメラの向き（水平方向）基準の移動に変換する
	const FRotator YawRot(0.f, GetControlRotation().Yaw, 0.f);
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

	// 壁の種類を選ぶ（数字キー / ホイール）
	AKakurenboGameMode* GM = GetKakurenboGameMode();
	const int32 NumTypes = GM ? GM->WallTypes.Num() : 0;
	if (NumTypes > 0)
	{
		const int32 Number = GetPressedNumberKey();
		if (Number >= 1 && Number <= NumTypes)
		{
			SelectedWallType = Number - 1;
		}
		if (WasInputKeyJustPressed(EKeys::MouseScrollUp))
		{
			SelectedWallType = (SelectedWallType + NumTypes - 1) % NumTypes;
		}
		if (WasInputKeyJustPressed(EKeys::MouseScrollDown))
		{
			SelectedWallType = (SelectedWallType + 1) % NumTypes;
		}
	}

	UpdateBuildTarget();
	DrawBuildPreview();

	if (WasInputKeyJustPressed(EKeys::LeftMouseButton) && bCanPlaceAtTarget && GM)
	{
		GM->PlaceWall(BuildTargetCell, SelectedWallType);
	}
	if (WasInputKeyJustPressed(EKeys::RightMouseButton) && PickUpTarget && GM)
	{
		GM->PickUpWall(PickUpTarget);
		PickUpTarget = nullptr;
	}
}

void AKakurenboPlayerController::UpdateBuildTarget()
{
	bHasBuildTarget = false;
	bCanPlaceAtTarget = false;
	BuildTargetReason = FText::GetEmpty();
	PickUpTarget = nullptr;

	AKakurenboGameMode* GM = GetKakurenboGameMode();
	UKakurenboGridSubsystem* Grid = GetWorld()->GetSubsystem<UKakurenboGridSubsystem>();
	if (!GM || !Grid || !Grid->IsConfigured())
	{
		return;
	}

	// カメラの中心から視線の先へレイを飛ばす
	FVector ViewLoc;
	FRotator ViewRot;
	GetPlayerViewPoint(ViewLoc, ViewRot);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(BuildTrace), false, GetPawn());
	FHitResult Hit;
	if (!GetWorld()->LineTraceSingleByChannel(Hit, ViewLoc, ViewLoc + ViewRot.Vector() * BuildReach, ECC_Visibility, Params))
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

	// 置き場所のプレビュー（緑: 置ける / 赤: 置けない）
	if (bHasBuildTarget)
	{
		const FVector Center = Grid->CellToWorld(BuildTargetCell, Grid->GetColumnHeight(BuildTargetCell));
		DrawDebugBox(GetWorld(), Center, FVector(Cell * 0.5f), bCanPlaceAtTarget ? FColor(80, 255, 80) : FColor(255, 70, 70), false, 0.f, 0, 3.f);
	}
	// 回収できる壁を黄色で囲む
	if (PickUpTarget)
	{
		DrawDebugBox(GetWorld(), PickUpTarget->GetActorLocation(), FVector(Cell * 0.52f), FColor(255, 220, 60), false, 0.f, 0, 2.f);
	}
}

// ---------------------------------------------------------------- かくれんぼパート

void AKakurenboPlayerController::TickHide()
{
	// かくれんぼ中は移動できない（連打のみ）
	if (WasInputKeyJustPressed(EKeys::SpaceBar) || WasInputKeyJustPressed(EKeys::LeftMouseButton))
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
