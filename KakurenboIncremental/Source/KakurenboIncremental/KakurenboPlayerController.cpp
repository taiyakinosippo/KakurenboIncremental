#include "KakurenboPlayerController.h"

#include "GameFramework/CharacterMovementComponent.h"
#include "HiderCharacter.h"
#include "KakurenboGameMode.h"
#include "KakurenboGameState.h"
#include "Misc/Paths.h"
#include "TimerManager.h"
#include "UnrealClient.h"

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

// ---------------------------------------------------------------- 自動テスト

void AKakurenboPlayerController::KakuAutoTest()
{
	struct FStep
	{
		float Delay;
		TFunction<void()> Run;
	};

	auto Shot = [](const FString& Name)
	{
		const FString Path = FPaths::ProjectSavedDir() / TEXT("AutoTest") / (Name + TEXT(".png"));
		FScreenshotRequest::RequestScreenshot(Path, true, false);
		UE_LOG(LogTemp, Display, TEXT("[AutoTest] screenshot %s"), *Path);
	};
	auto Log = [this](const FString& Label)
	{
		if (const AKakurenboGameState* S = GetWorld()->GetGameState<AKakurenboGameState>())
		{
			UE_LOG(LogTemp, Display, TEXT("[AutoTest] %s: Phase=%s Stage=%d Coins=%.2f Earned=%.2f Mash=%d MashLv=%d TimeLv=%d Remaining=%.1f"),
				*Label, *UEnum::GetValueAsString(S->Phase), S->Stage, S->Coins, S->CoinsEarnedThisRound,
				S->MashCountThisRound, S->MashIncomeLevel, S->TimeIncomeLevel, S->HideTimeRemaining);
		}
	};

	TArray<FStep> Steps;
	Steps.Add({ 1.f, [=] { Log(TEXT("start")); Shot(TEXT("01_hide_countdown")); } });
	Steps.Add({ 4.f, [=, this] { KakuMash(30); Log(TEXT("after mash 30")); Shot(TEXT("02_hide_mashing")); } });
	Steps.Add({ 1.f, [=, this] { KakuSkipTime(1000.f); } });
	Steps.Add({ 1.f, [=] { Log(TEXT("result")); Shot(TEXT("03_result")); } });
	Steps.Add({ 1.f, [=, this] { KakuNext(); KakuAddCoins(1000.0); KakuBuy(1); KakuBuy(2); KakuBuy(2); Log(TEXT("shop bought")); Shot(TEXT("04_shop")); } });
	Steps.Add({ 1.f, [=, this] { KakuNext(); Log(TEXT("build")); Shot(TEXT("05_build")); } });
	Steps.Add({ 1.f, [=, this] { KakuNext(); Log(TEXT("hide stage 2")); } });
	Steps.Add({ 1.f, [this] { UE_LOG(LogTemp, Display, TEXT("[AutoTest] done")); ConsoleCommand(TEXT("quit")); } });

	float Time = 0.f;
	for (const FStep& Step : Steps)
	{
		Time += Step.Delay;
		FTimerHandle Handle;
		GetWorldTimerManager().SetTimer(Handle, FTimerDelegate::CreateLambda(Step.Run), Time, false);
	}
}
