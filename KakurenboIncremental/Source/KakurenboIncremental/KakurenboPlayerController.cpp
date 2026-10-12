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
#include "KakurenboSettings.h"
#include "KakurenboSoundSubsystem.h"
#include "Kismet/KismetSystemLibrary.h"
#include "OniCharacter.h"
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

UKakurenboSettings* AKakurenboPlayerController::Settings() const
{
	return UKakurenboSettings::Get(this);
}

AHiderCharacter* AKakurenboPlayerController::GetHider() const
{
	return Cast<AHiderCharacter>(GetPawn());
}

bool AKakurenboPlayerController::IsActionDown(EKakurenboAction Action) const
{
	const UKakurenboSettings* S = Settings();
	return S && S->IsDown(this, Action);
}

bool AKakurenboPlayerController::WasActionPressed(EKakurenboAction Action) const
{
	const UKakurenboSettings* S = Settings();
	return S && S->WasPressed(this, Action);
}

FString AKakurenboPlayerController::KeyName(EKakurenboAction Action, bool bBoth) const
{
	const UKakurenboSettings* S = Settings();
	if (!S)
	{
		return FString();
	}
	if (bBoth)
	{
		return S->ActionKeyLabel(Action);
	}
	// 画面の案内は 1 つ目のキーだけ（1 つ目が空なら 2 つ目）
	const FKey Primary = S->GetKey(Action, false);
	return UKakurenboSettings::KeyLabel(Primary.IsValid() ? Primary : S->GetKey(Action, true));
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
	// 保存してある設定（音量・マウス感度）を反映する
	if (const UKakurenboSettings* S = Settings())
	{
		S->Apply(this);
	}
}

void AKakurenboPlayerController::PlayerTick(float DeltaTime)
{
	// Super の中で入力処理（→ PostProcessInput）と視点の更新が行われる
	Super::PlayerTick(DeltaTime);

	AKakurenboGameMode* GM = GetKakurenboGameMode();
	if (GM && GM->GetPhase() == EKakurenboPhase::Build && !IsMenuOpen())
	{
		DrawBuildPreview();
	}
}

void AKakurenboPlayerController::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	// 一時停止中はタイマーが止まるので、BGM の波形が切れないようここで足す
	if (IsPaused())
	{
		if (UKakurenboSoundSubsystem* Sound = GetWorld()->GetSubsystem<UKakurenboSoundSubsystem>())
		{
			Sound->RefillMusic();
		}
	}
	UpdateTips(DeltaSeconds);
}

void AKakurenboPlayerController::PostProcessInput(const float DeltaTime, const bool bGamePaused)
{
	AKakurenboGameMode* GM = GetKakurenboGameMode();
	if (GM && GetHider())
	{
		const EKakurenboPhase Phase = GM->GetPhase();
		ApplyViewForPhase(Phase);

		// タイトルはカメラがゆっくり館を回る
		if (Phase == EKakurenboPhase::Title)
		{
			GetHider()->AddOverheadYaw(TitleOrbitSpeed * DeltaTime);
		}

		if (IsMenuOpen())
		{
			HandleMenuInput();
		}
		else if (Phase == EKakurenboPhase::Title)
		{
			HandleTitleInput();
		}
		else
		{
			switch (Phase)
			{
			case EKakurenboPhase::Shop:
				HandleOverheadCamera(DeltaTime);
				HandleShopInput();
				break;
			case EKakurenboPhase::Build:
				HandleOverheadCamera(DeltaTime);
				HandleTopDownPan(DeltaTime);
				HandleBuildInput();
				break;
			case EKakurenboPhase::Hide:
				HandleMouseLook();
				HandleHideInput();
				break;
			case EKakurenboPhase::Result:
				HandleOverheadCamera(DeltaTime);
				if (WasInputKeyJustPressed(EKeys::LeftMouseButton))
				{
					HandleUIClick();
				}
				break;
			default:
				break;
			}

			if (GM->GetPhase() == Phase && WasActionPressed(EKakurenboAction::Next))
			{
				GM->AdvancePhase();
			}
			// Esc: メニュー（一時停止・設定・タイトルへ）。F1: 操作説明
			if (WasInputKeyJustPressed(EKeys::Escape))
			{
				OpenMenu(EKakurenboMenu::Pause);
			}
			else if (WasActionPressed(EKakurenboAction::Help))
			{
				OpenMenu(EKakurenboMenu::Help);
			}
		}
		ApplyInputMode();
	}

	// 親クラスの処理（視点入力を無視する設定のときに回転入力を消す）は最後に呼ぶ
	Super::PostProcessInput(DeltaTime, bGamePaused);
}

// ---------------------------------------------------------------- 視点・入力モードの切り替え

void AKakurenboPlayerController::ApplyViewForPhase(EKakurenboPhase Phase)
{
	AHiderCharacter* Hider = GetHider();
	if (!Hider || (bViewInitialized && Phase == ViewPhase))
	{
		return;
	}
	const bool bFirstTime = !bViewInitialized;
	const bool bWasTitle = bViewInitialized && ViewPhase == EKakurenboPhase::Title;
	bViewInitialized = true;
	ViewPhase = Phase;
	Tips.Reset(); // 前のパートのヒントは消す
	HideTipClock = 0.f;

	Hider->SetSneaking(false);
	Hider->SetTitleCamera(Phase == EKakurenboPhase::Title);
	if (Phase == EKakurenboPhase::Title)
	{
		// タイトル：斜め上から館を見ながらゆっくり回る。メニューは「つづきから」（無ければ「はじめから」）を選んでおく
		CloseAllMenus();
		Hider->SetViewMode(EHiderViewMode::Overhead);
		TitleSelection = static_cast<int32>(IsTitleItemEnabled(EKakurenboTitleItem::Continue) ? EKakurenboTitleItem::Continue : EKakurenboTitleItem::NewGame);
		ClearBuildTarget();
		return;
	}
	if (Phase == EKakurenboPhase::Hide)
	{
		// 俯瞰・真上 → 三人称：俯瞰カメラが向いていた方向を、少し見下ろす角度で向いて始める
		if (bFirstTime || bWasTitle || Hider->GetViewMode() != EHiderViewMode::ThirdPerson)
		{
			SetControlRotation(FRotator(-20.f, bFirstTime ? GetControlRotation().Yaw : Hider->GetOverheadYaw(), 0.f));
		}
		Hider->SetViewMode(EHiderViewMode::ThirdPerson);
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

	if (Phase == EKakurenboPhase::Shop)
	{
		bPrestigeShopTab = false; // 購入パートはコインのお店から
	}
	ClearBuildTarget();
}

void AKakurenboPlayerController::ApplyInputMode()
{
	const AKakurenboGameMode* GM = GetKakurenboGameMode();
	if (!GM)
	{
		return;
	}
	// カーソルを出さないのは、メニューを開いていないかくれんぼ中だけ（マウスの動き＝カメラの回転）
	const bool bWantCursor = IsMenuOpen() || GM->GetPhase() != EKakurenboPhase::Hide;
	if (bCursorModeKnown && bWantCursor == bCursorModeApplied)
	{
		return;
	}
	bCursorModeKnown = true;
	bCursorModeApplied = bWantCursor;
	if (bWantCursor)
	{
		// ボタンやマスをカーソルで指す
		bShowMouseCursor = true;
		FInputModeGameAndUI Mode;
		Mode.SetHideCursorDuringCapture(false);                         // クリック中もカーソルを消さない
		Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		SetInputMode(Mode);
	}
	else
	{
		// カーソルを消してマウスを捕まえる
		bShowMouseCursor = false;
		SetInputMode(FInputModeGameOnly());
	}
	ApplyAutoTestInputIsolation();
}

void AKakurenboPlayerController::ApplyAutoTestInputIsolation()
{
	// SetInputMode は入力の無視を解除してしまうので、切り替えのたびにかけ直す
	if (bIgnoreRealInputForAutoTest && GetLocalPlayer() && GetLocalPlayer()->ViewportClient)
	{
		GetLocalPlayer()->ViewportClient->SetIgnoreInput(true);
	}
}

// ---------------------------------------------------------------- メニュー

void AKakurenboPlayerController::OpenMenu(EKakurenboMenu Menu)
{
	if (Menu == EKakurenboMenu::None)
	{
		return;
	}
	if (MenuStack.Num() == 0)
	{
		// ゲーム中に開いたら止める（タイトルでは止めない）
		const AKakurenboGameMode* GM = GetKakurenboGameMode();
		if (GM && GM->GetPhase() != EKakurenboPhase::Title && !IsPaused())
		{
			SetPause(true);
			bPausedByMenu = IsPaused();
		}
		ClearBuildTarget();
		bDraggingCamera = false;
		if (AHiderCharacter* Hider = GetHider())
		{
			Hider->SetSneaking(false);
		}
	}
	MenuStack.Add(Menu);
	WaitingKeySlot = INDEX_NONE;
	DraggingSlider = INDEX_NONE;
	UKakurenboSoundSubsystem::Play2D(this, EKakurenboSfx::Place, 0.4f, 1.5f);
}

void AKakurenboPlayerController::CloseMenu()
{
	if (MenuStack.Num() > 0)
	{
		MenuStack.Pop();
	}
	WaitingKeySlot = INDEX_NONE;
	DraggingSlider = INDEX_NONE;
	if (MenuStack.Num() == 0 && bPausedByMenu)
	{
		bPausedByMenu = false;
		SetPause(false);
	}
	UKakurenboSoundSubsystem::Play2D(this, EKakurenboSfx::PickUp, 0.35f, 1.4f);
}

void AKakurenboPlayerController::CloseAllMenus()
{
	if (MenuStack.Num() == 0 && !bPausedByMenu)
	{
		return;
	}
	MenuStack.Reset();
	WaitingKeySlot = INDEX_NONE;
	DraggingSlider = INDEX_NONE;
	if (bPausedByMenu)
	{
		bPausedByMenu = false;
		SetPause(false);
	}
}

bool AKakurenboPlayerController::IsWaitingForKey(EKakurenboAction& OutAction, bool& bOutSecondary) const
{
	if (WaitingKeySlot == INDEX_NONE)
	{
		return false;
	}
	OutAction = static_cast<EKakurenboAction>(WaitingKeySlot / 2);
	bOutSecondary = (WaitingKeySlot % 2) == 1;
	return true;
}

bool AKakurenboPlayerController::IsTitleItemEnabled(EKakurenboTitleItem Item) const
{
	const AKakurenboGameMode* GM = GetKakurenboGameMode();
	return Item != EKakurenboTitleItem::Continue || (GM && GM->HasContinueData());
}

void AKakurenboPlayerController::HandleMenuInput()
{
	UKakurenboSettings* S = Settings();

	// キー設定：押されたキーを割り当てる（Esc で取り消し）
	if (WaitingKeySlot != INDEX_NONE && PlayerInput && S)
	{
		if (WasInputKeyJustPressed(EKeys::Escape))
		{
			WaitingKeySlot = INDEX_NONE;
			return;
		}
		// エンジンが知っているすべてのキーから、このフレームに押されたものを探す（待っている間だけ）
		TArray<FKey> AllKeys;
		EKeys::GetAllKeys(AllKeys);
		for (const FKey& Key : AllKeys)
		{
			if (UKakurenboSettings::CanBindKey(Key) && WasInputKeyJustPressed(Key))
			{
				const EKakurenboAction Action = static_cast<EKakurenboAction>(WaitingKeySlot / 2);
				S->Bind(Action, Key, WaitingKeySlot % 2 == 1);
				S->Save();
				WaitingKeySlot = INDEX_NONE;
				UKakurenboSoundSubsystem::Play2D(this, EKakurenboSfx::Buy, 0.6f, 1.2f);
				return;
			}
		}
		return;
	}

	// 設定のつまみ：押している間はカーソルの位置に合わせて動かす
	if (DraggingSlider != INDEX_NONE)
	{
		FVector2D Cursor;
		if (IsInputKeyDown(EKeys::LeftMouseButton) && GetUICursorPosition(Cursor))
		{
			const float Width = FMath::Max(DraggingSliderRect.GetSize().X, 1.0);
			SetSliderValue(static_cast<EKakurenboSettingSlider>(DraggingSlider), (Cursor.X - DraggingSliderRect.Min.X) / Width);
		}
		else
		{
			DraggingSlider = INDEX_NONE;
			if (S)
			{
				S->Save();
			}
		}
	}

	if (WasInputKeyJustPressed(EKeys::LeftMouseButton))
	{
		HandleUIClick();
	}
	// キー設定：右クリックで 2 つ目のキーを外す
	if (WasInputKeyJustPressed(EKeys::RightMouseButton) && S)
	{
		FVector2D Cursor;
		FKakurenboUIButton Button;
		const AKakurenboHUD* HUD = GetHUD<AKakurenboHUD>();
		if (HUD && GetUICursorPosition(Cursor) && HUD->FindButtonAt(Cursor, Button) && Button.Action == EKakurenboUIAction::KeySlot && Button.Index % 2 == 1)
		{
			S->ClearSecondary(static_cast<EKakurenboAction>(Button.Index / 2));
			S->Save();
		}
	}

	const EKakurenboMenu Menu = GetMenu();
	if (WasInputKeyJustPressed(EKeys::Escape) || (Menu == EKakurenboMenu::Help && WasActionPressed(EKakurenboAction::Help)))
	{
		CloseMenu();
	}
	else if ((Menu == EKakurenboMenu::ConfirmNewGame || Menu == EKakurenboMenu::ConfirmTitle) && WasInputKeyJustPressed(EKeys::Enter))
	{
		// 確認は Enter でも「はい」
		const bool bNewGame = Menu == EKakurenboMenu::ConfirmNewGame;
		CloseAllMenus();
		if (AKakurenboGameMode* GM = GetKakurenboGameMode())
		{
			bNewGame ? GM->StartNewGame() : GM->ReturnToTitle();
		}
	}
}

void AKakurenboPlayerController::HandleTitleInput()
{
	constexpr int32 Num = static_cast<int32>(EKakurenboTitleItem::Count);
	auto Step = [this](int32 Dir)
	{
		for (int32 i = 0; i < Num; ++i)
		{
			TitleSelection = (TitleSelection + Dir + Num) % Num;
			if (IsTitleItemEnabled(static_cast<EKakurenboTitleItem>(TitleSelection)))
			{
				break;
			}
		}
	};
	if (!IsTitleItemEnabled(static_cast<EKakurenboTitleItem>(TitleSelection)))
	{
		Step(1);
	}
	if (WasInputKeyJustPressed(EKeys::Up) || WasInputKeyJustPressed(EKeys::W))
	{
		Step(-1);
		UKakurenboSoundSubsystem::Play2D(this, EKakurenboSfx::Place, 0.3f, 1.8f);
	}
	if (WasInputKeyJustPressed(EKeys::Down) || WasInputKeyJustPressed(EKeys::S))
	{
		Step(1);
		UKakurenboSoundSubsystem::Play2D(this, EKakurenboSfx::Place, 0.3f, 1.8f);
	}
	// カーソルを乗せた項目を選ぶ
	FVector2D Cursor;
	FKakurenboUIButton Button;
	const AKakurenboHUD* HUD = GetHUD<AKakurenboHUD>();
	if (HUD && GetUICursorPosition(Cursor) && HUD->FindButtonAt(Cursor, Button) && Button.Action == EKakurenboUIAction::TitleItem
		&& IsTitleItemEnabled(static_cast<EKakurenboTitleItem>(Button.Index)))
	{
		TitleSelection = Button.Index;
	}
	if (WasInputKeyJustPressed(EKeys::Enter) || WasInputKeyJustPressed(EKeys::SpaceBar))
	{
		ActivateTitleItem(static_cast<EKakurenboTitleItem>(TitleSelection));
	}
	else if (WasInputKeyJustPressed(EKeys::LeftMouseButton))
	{
		HandleUIClick();
	}
}

void AKakurenboPlayerController::ActivateTitleItem(EKakurenboTitleItem Item)
{
	AKakurenboGameMode* GM = GetKakurenboGameMode();
	if (!GM || !IsTitleItemEnabled(Item))
	{
		UKakurenboSoundSubsystem::Play2D(this, EKakurenboSfx::BuyFail, 0.6f);
		return;
	}
	switch (Item)
	{
	case EKakurenboTitleItem::Continue:
		UKakurenboSoundSubsystem::Play2D(this, EKakurenboSfx::Buy, 0.7f, 0.9f);
		GM->ContinueGame();
		break;
	case EKakurenboTitleItem::NewGame:
		if (GM->HasContinueData())
		{
			OpenMenu(EKakurenboMenu::ConfirmNewGame); // セーブが消えるので確かめる
		}
		else
		{
			UKakurenboSoundSubsystem::Play2D(this, EKakurenboSfx::Buy, 0.7f, 0.9f);
			GM->StartNewGame();
		}
		break;
	case EKakurenboTitleItem::Settings:
		OpenMenu(EKakurenboMenu::Settings);
		break;
	case EKakurenboTitleItem::Quit:
		// ゲームを終わる（配布用の .exe でも動く方法）
		UKismetSystemLibrary::QuitGame(this, this, EQuitPreference::Quit, false);
		break;
	default:
		break;
	}
}

void AKakurenboPlayerController::ActivatePauseItem(EKakurenboPauseItem Item)
{
	AKakurenboGameMode* GM = GetKakurenboGameMode();
	switch (Item)
	{
	case EKakurenboPauseItem::Resume:
		CloseAllMenus();
		break;
	case EKakurenboPauseItem::Help:
		OpenMenu(EKakurenboMenu::Help);
		break;
	case EKakurenboPauseItem::Settings:
		OpenMenu(EKakurenboMenu::Settings);
		break;
	case EKakurenboPauseItem::Title:
		if (GM && GM->GetPhase() == EKakurenboPhase::Hide)
		{
			OpenMenu(EKakurenboMenu::ConfirmTitle); // このラウンドの稼ぎがなくなるので確かめる
		}
		else
		{
			CloseAllMenus();
			if (GM)
			{
				GM->ReturnToTitle();
			}
		}
		break;
	default:
		break;
	}
}

void AKakurenboPlayerController::SetSliderValue(EKakurenboSettingSlider Slider, float Value01)
{
	UKakurenboSettings* S = Settings();
	if (!S)
	{
		return;
	}
	const float V = FMath::Clamp(Value01, 0.f, 1.f);
	switch (Slider)
	{
	case EKakurenboSettingSlider::Master: S->MasterVolume = V; break;
	case EKakurenboSettingSlider::Music:  S->MusicVolume = V; break;
	case EKakurenboSettingSlider::Sfx:    S->SfxVolume = V; break;
	case EKakurenboSettingSlider::Mouse:
		S->MouseSensitivity = FMath::Lerp(UKakurenboSettings::MinMouseSensitivity, UKakurenboSettings::MaxMouseSensitivity, V);
		break;
	default: break;
	}
	S->Apply(this);
}

void AKakurenboPlayerController::ToggleSetting(EKakurenboSettingToggle Toggle)
{
	UKakurenboSettings* S = Settings();
	if (!S)
	{
		return;
	}
	switch (Toggle)
	{
	case EKakurenboSettingToggle::InvertY:    S->bInvertMouseY = !S->bInvertMouseY; break;
	case EKakurenboSettingToggle::Hints:
		S->bShowHints = !S->bShowHints;
		if (!S->bShowHints)
		{
			Tips.Reset();
		}
		break;
	case EKakurenboSettingToggle::Fullscreen: UKakurenboSettings::SetFullscreen(!UKakurenboSettings::IsFullscreen()); break;
	default: break;
	}
	S->Apply(this);
	S->Save();
	UKakurenboSoundSubsystem::Play2D(this, EKakurenboSfx::Place, 0.5f, 1.3f);
}

// ---------------------------------------------------------------- 操作のヒント

void AKakurenboPlayerController::QueueTip(FName Id, const FString& Text)
{
	UKakurenboSettings* S = Settings();
	if (!S || !S->bShowHints || S->HasSeenTip(Id) || (bIgnoreRealInputForAutoTest && !bShowTipsInAutoTest))
	{
		return;
	}
	for (const FKakurenboTip& Tip : Tips)
	{
		if (Tip.Id == Id)
		{
			return;
		}
	}
	S->MarkTipSeen(Id); // 出すと決めたら「見た」にする（同じヒントは 1 回だけ）
	FKakurenboTip& Tip = Tips.AddDefaulted_GetRef();
	Tip.Id = Id;
	Tip.Text = Text;
}

void AKakurenboPlayerController::UpdateTips(float RealDeltaSeconds)
{
	const AKakurenboGameMode* GM = GetKakurenboGameMode();
	const AKakurenboGameState* State = GetWorld()->GetGameState<AKakurenboGameState>();
	if (!GM || !State || IsMenuOpen())
	{
		return;
	}
	if (Tips.Num() > 0)
	{
		Tips[0].Age += RealDeltaSeconds;
		if (Tips[0].Age >= TipSeconds)
		{
			Tips.RemoveAt(0);
		}
	}

	// はじめての場面で、その場で使える操作を教える（[ ] はキーの形で描く）
	HideTipClock += RealDeltaSeconds;
	auto K = [this](EKakurenboAction Action) { return FString::Printf(TEXT("[%s]"), *KeyName(Action)); };
	switch (State->Phase)
	{
	case EKakurenboPhase::Hide:
	{
		if (HideTipClock > 0.5f)
		{
			QueueTip(TEXT("Move"), FString::Printf(TEXT("%s%s%s%s で歩く　マウスで見回す"),
				*K(EKakurenboAction::MoveForward), *K(EKakurenboAction::MoveLeft), *K(EKakurenboAction::MoveBack), *K(EKakurenboAction::MoveRight)));
		}
		if (State->HideStartCountdown <= 0.f)
		{
			QueueTip(TEXT("Mash"), FString::Printf(TEXT("%s でコインを稼げる…でも、その音で鬼が寄ってくる"), *K(EKakurenboAction::Mash)));
			QueueTip(TEXT("Touch"), TEXT("鬼にさわられたら見つかる。物陰に隠れて、時間まで逃げきろう"));
		}
		if (HideTipClock > 14.f)
		{
			QueueTip(TEXT("Sneak"), FString::Printf(TEXT("%s を押している間しゃがむ。足音が小さくなり、低い家具にも隠れられる"), *K(EKakurenboAction::Sneak)));
			QueueTip(TEXT("Treasure"), TEXT("キラキラと鳴る方に、お宝がある"));
		}
		if (GM->IsJumpUnlocked())
		{
			QueueTip(TEXT("Jump"), FString::Printf(TEXT("%s でジャンプ（壁 1 段に飛び乗れる）"), *K(EKakurenboAction::Jump)));
		}
		if (GM->IsDashUnlocked())
		{
			QueueTip(TEXT("Dash"), FString::Printf(TEXT("%s で煙幕ダッシュ。煙の向こうは鬼から見えない（1 ラウンドに %d 回）"), *K(EKakurenboAction::Dash), GM->GetSmokeUsesPerRound()));
		}
		for (const AOniCharacter* Oni : GM->GetOnis())
		{
			if (Oni && Oni->IsActive() && Oni->GetIntent() == EOniState::Chase)
			{
				QueueTip(TEXT("Chased"), GM->IsDashUnlocked()
					? FString::Printf(TEXT("見つかった！ %s で煙幕を投げて、見えないうちに逃げろ"), *K(EKakurenboAction::Dash))
					: FString(TEXT("見つかった！ 壁や家具の陰に回りこんで、見失わせよう")));
				break;
			}
		}
		break;
	}
	case EKakurenboPhase::Shop:
		QueueTip(TEXT("Shop"), FString::Printf(TEXT("商品をクリックで買う。買った壁や罠は、次の設置で置ける　%s 次へ"), *K(EKakurenboAction::Next)));
		if (HideTipClock > 1.f)
		{
			QueueTip(TEXT("Menu"), FString::Printf(TEXT("[Esc] メニュー・設定（音量・マウス感度・キー設定）　%s 操作説明"), *K(EKakurenboAction::Help)));
		}
		if (GM->CanPrestige())
		{
			QueueTip(TEXT("Prestige"), TEXT("転生できるようになった。転生ポイントで、ずっと残る強化が買える"));
		}
		break;
	case EKakurenboPhase::Build:
		QueueTip(TEXT("Build"), FString::Printf(TEXT("[左クリック] 置く　[右クリック] 回収　%s スタート位置をカーソルの場所へ"), *K(EKakurenboAction::SetStart)));
		if (HideTipClock > 3.f)
		{
			QueueTip(TEXT("BuildCamera"), FString::Printf(TEXT("%s カメラを動かす　%s%s 回す　%s 傾ける　ホイールでズーム"),
				*K(EKakurenboAction::MoveForward), *K(EKakurenboAction::CameraLeft), *K(EKakurenboAction::CameraRight), *K(EKakurenboAction::TiltDown)));
		}
		break;
	default:
		break;
	}
}

// ---------------------------------------------------------------- カメラ

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

void AKakurenboPlayerController::HandleOverheadCamera(float DeltaTime)
{
	AHiderCharacter* Hider = GetHider();

	// Q / E で回転
	float Rotate = 0.f;
	if (IsActionDown(EKakurenboAction::CameraLeft)) Rotate -= 1.f;
	if (IsActionDown(EKakurenboAction::CameraRight)) Rotate += 1.f;
	Hider->AddOverheadYaw(Rotate * OverheadRotateSpeed * DeltaTime);

	// 設置パート（真上から）は Z / X で傾ける（Z: 斜め横から見る / X: 真上に戻す）。積んだ壁の高さが見やすくなる
	const bool bTopDown = Hider->GetViewMode() == EHiderViewMode::TopDown;
	if (bTopDown)
	{
		float Tilt = 0.f;
		if (IsActionDown(EKakurenboAction::TiltDown)) Tilt += 1.f;
		if (IsActionDown(EKakurenboAction::TiltUp)) Tilt -= 1.f;
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
	if (IsActionDown(EKakurenboAction::MoveForward)) F += 1.f;
	if (IsActionDown(EKakurenboAction::MoveBack)) F -= 1.f;
	if (IsActionDown(EKakurenboAction::MoveRight)) R += 1.f;
	if (IsActionDown(EKakurenboAction::MoveLeft)) R -= 1.f;
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
	if (IsActionDown(EKakurenboAction::MoveForward)) F += 1.f;
	if (IsActionDown(EKakurenboAction::MoveBack)) F -= 1.f;
	if (IsActionDown(EKakurenboAction::MoveRight)) R += 1.f;
	if (IsActionDown(EKakurenboAction::MoveLeft)) R -= 1.f;
	Hider->AddMovementInput(Forward, F);
	Hider->AddMovementInput(Right, R);

	if (WasActionPressed(EKakurenboAction::Jump))
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
	if (WasActionPressed(EKakurenboAction::SwitchShop))
	{
		bPrestigeShopTab = !bPrestigeShopTab;
	}
	if (const int32 Number = GetPressedNumberKey())
	{
		BuyItem(Number);
	}
	if (WasActionPressed(EKakurenboAction::Prestige))
	{
		RequestPrestige();
	}
	if (WasActionPressed(EKakurenboAction::Refill))
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

void AKakurenboPlayerController::RequestClearAll()
{
	// 全部回収は取り返しがつかない（置き直しが大変）ので、2 回押して決める
	AKakurenboGameMode* GM = GetKakurenboGameMode();
	if (!GM || GM->GetPhase() != EKakurenboPhase::Build)
	{
		return;
	}
	if (IsClearAllConfirmPending())
	{
		ClearAllConfirmUntil = -1.f;
		GM->ClearAllPlaced();
	}
	else
	{
		ClearAllConfirmUntil = GetWorld()->GetTimeSeconds() + PrestigeConfirmSeconds;
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
	UKakurenboSettings* S = Settings();
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
	case EKakurenboUIAction::ClearAll:  RequestClearAll(); break;
	case EKakurenboUIAction::TitleItem:
		TitleSelection = Button.Index;
		ActivateTitleItem(static_cast<EKakurenboTitleItem>(Button.Index));
		break;
	case EKakurenboUIAction::OpenMenu:  OpenMenu(EKakurenboMenu::Pause); break;
	case EKakurenboUIAction::OpenHelp:  OpenMenu(EKakurenboMenu::Help); break;
	case EKakurenboUIAction::MenuItem:  ActivatePauseItem(static_cast<EKakurenboPauseItem>(Button.Index)); break;
	case EKakurenboUIAction::Slider:
		// つまみ：押したところの値にして、そのままドラッグで動かせる
		DraggingSlider = Button.Index;
		DraggingSliderRect = Button.Rect;
		SetSliderValue(static_cast<EKakurenboSettingSlider>(Button.Index), (Cursor.X - Button.Rect.Min.X) / FMath::Max(Button.Rect.GetSize().X, 1.0));
		break;
	case EKakurenboUIAction::Toggle:    ToggleSetting(static_cast<EKakurenboSettingToggle>(Button.Index)); break;
	case EKakurenboUIAction::OpenKeys:  OpenMenu(EKakurenboMenu::Keys); break;
	case EKakurenboUIAction::KeySlot:   WaitingKeySlot = Button.Index; break;
	case EKakurenboUIAction::ResetKeys:
		if (S)
		{
			S->ResetBindings();
			S->Save();
			UKakurenboSoundSubsystem::Play2D(this, EKakurenboSfx::PickUp, 0.6f);
		}
		break;
	case EKakurenboUIAction::ResetTips:
		if (S)
		{
			S->ResetTips();
			UKakurenboSoundSubsystem::Play2D(this, EKakurenboSfx::Buy, 0.6f, 1.2f);
		}
		break;
	case EKakurenboUIAction::Back:      CloseMenu(); break;
	case EKakurenboUIAction::ConfirmYes:
	{
		const EKakurenboMenu Menu = GetMenu();
		CloseAllMenus();
		if (Menu == EKakurenboMenu::ConfirmNewGame)
		{
			GM->StartNewGame();
		}
		else if (Menu == EKakurenboMenu::ConfirmTitle)
		{
			GM->ReturnToTitle();
		}
		break;
	}
	case EKakurenboUIAction::ConfirmNo: CloseMenu(); break;
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
	if (WasActionPressed(EKakurenboAction::SetStart) && bHasBuildTarget)
	{
		SetStartCell(BuildTargetCell);
	}

	// B / BackSpace: 購入パートへ戻る（置いた壁・罠はそのまま）
	if (WasActionPressed(EKakurenboAction::BackToShop))
	{
		GM->ReturnToShop();
	}
	// Delete: 置いた壁・罠を全部回収（2 回押す）
	if (WasActionPressed(EKakurenboAction::ClearAll))
	{
		RequestClearAll();
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

	// Ctrl（または C）を押している間はしのび足（しゃがむ・ゆっくり・足音が小さい）
	if (AHiderCharacter* Hider = GetHider())
	{
		Hider->SetSneaking(bTestSneak || IsActionDown(EKakurenboAction::Sneak));
	}

	if (WasActionPressed(EKakurenboAction::Mash))
	{
		DoMash();
	}
	if (WasActionPressed(EKakurenboAction::Dash))
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
	// 速くなる代わりに大きな音が出る（回数が残っていなければ何も起きない）
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
	if (UKakurenboSettings* S = Settings())
	{
		S->MasterVolume = FMath::Clamp(Volume, 0.f, 1.f);
		S->Apply(this);
	}
}

void AKakurenboPlayerController::KakuSens(float Sensitivity)
{
	MouseSensitivity = FMath::Max(0.001f, Sensitivity);
	if (UKakurenboSettings* S = Settings())
	{
		S->MouseSensitivity = MouseSensitivity;
	}
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
	if (UKakurenboSettings* S = Settings())
	{
		S->MusicVolume = FMath::Clamp(Volume, 0.f, 1.f);
		S->Apply(this);
	}
}

void AKakurenboPlayerController::KakuSetStart(int32 X, int32 Y)
{
	if (!SetStartCell(FIntPoint(X, Y)))
	{
		UE_LOG(LogTemp, Warning, TEXT("KakuSetStart (%d,%d) failed"), X, Y);
	}
}
