#include "KakurenboPlayerController.h"

#include "Camera/PlayerCameraManager.h"
#include "DrawDebugHelpers.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerInput.h"
#include "HiderCharacter.h"
#include "KakurenboGameMode.h"
#include "KakurenboArena.h"
#include "KakurenboGameState.h"
#include "KakurenboGridSubsystem.h"
#include "KakurenboHUD.h"
#include "KakurenboOniStudio.h"
#include "KakurenboSettings.h"
#include "KakurenboSoundSubsystem.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Misc/App.h"
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
	// コントローラーで遊んでいるときは、コントローラーのボタン（あれば）
	if (bUsingGamepad && !bBoth)
	{
		const FKey Pad = UKakurenboSettings::GamepadKey(Action);
		if (Pad.IsValid())
		{
			return UKakurenboSettings::KeyLabel(Pad);
		}
	}
	if (bBoth)
	{
		return S->ActionKeyLabel(Action);
	}
	// 画面の案内は 1 つ目のキーだけ（1 つ目が空なら 2 つ目）
	const FKey Primary = S->GetKey(Action, false);
	return UKakurenboSettings::KeyLabel(Primary.IsValid() ? Primary : S->GetKey(Action, true));
}

FString AKakurenboPlayerController::UIKeyLabel(EKakurenboUIKey Key) const
{
	auto K = [this](EKakurenboAction Action) { return FString::Printf(TEXT("[%s]"), *KeyName(Action)); };
	if (bUsingGamepad)
	{
		// ボタンの名前はボタン配置（Xbox / 任天堂）で変わる（UKakurenboSettings::KeyLabel）
		auto P = [](const FKey& PadKey) { return FString::Printf(TEXT("[%s]"), *UKakurenboSettings::KeyLabel(PadKey)); };
		switch (Key)
		{
		case EKakurenboUIKey::Confirm:    return P(UKakurenboSettings::PadFace(EKakurenboPadFace::A));
		case EKakurenboUIKey::Cancel:     return P(UKakurenboSettings::PadFace(EKakurenboPadFace::B));
		case EKakurenboUIKey::Menu:       return P(EKeys::Gamepad_Special_Right);
		case EKakurenboUIKey::Place:      return P(UKakurenboSettings::PadFace(EKakurenboPadFace::A));
		case EKakurenboUIKey::PickUp:     return P(UKakurenboSettings::PadFace(EKakurenboPadFace::X));
		case EKakurenboUIKey::SelectSlot: return P(EKeys::Gamepad_LeftShoulder) + P(EKeys::Gamepad_RightShoulder);
		case EKakurenboUIKey::Zoom:       return P(EKeys::Gamepad_LeftTrigger) + P(EKeys::Gamepad_RightTrigger);
		case EKakurenboUIKey::Look:       return TEXT("[R スティック]");
		case EKakurenboUIKey::Move:       return TEXT("[L スティック]");
		case EKakurenboUIKey::Cursor:     return TEXT("[L スティック]");
		case EKakurenboUIKey::Rotate:     return TEXT("[R スティック]");
		case EKakurenboUIKey::Navigate:   return TEXT("[十字キー]");
		case EKakurenboUIKey::Buy:        return P(UKakurenboSettings::PadFace(EKakurenboPadFace::A));
		case EKakurenboUIKey::Tilt:       return TEXT("[R スティック↕]");
		default:                          return FString();
		}
	}
	switch (Key)
	{
	case EKakurenboUIKey::Confirm:    return TEXT("[左クリック]");
	case EKakurenboUIKey::Cancel:     return TEXT("[Esc]");
	case EKakurenboUIKey::Menu:       return TEXT("[Esc]");
	case EKakurenboUIKey::Place:      return TEXT("[左クリック]");
	case EKakurenboUIKey::PickUp:     return TEXT("[右クリック]");
	case EKakurenboUIKey::SelectSlot: return TEXT("下の欄・数字キー");
	case EKakurenboUIKey::Zoom:       return TEXT("ホイール");
	case EKakurenboUIKey::Look:       return TEXT("マウス");
	case EKakurenboUIKey::Move:       return K(EKakurenboAction::MoveForward) + K(EKakurenboAction::MoveLeft) + K(EKakurenboAction::MoveBack) + K(EKakurenboAction::MoveRight);
	case EKakurenboUIKey::Cursor:     return TEXT("マウス");
	case EKakurenboUIKey::Rotate:     return K(EKakurenboAction::CameraLeft) + K(EKakurenboAction::CameraRight);
	case EKakurenboUIKey::Navigate:   return TEXT("マウス");
	case EKakurenboUIKey::Buy:        return TEXT("クリック・数字キー");
	case EKakurenboUIKey::Tilt:       return K(EKakurenboAction::TiltDown) + K(EKakurenboAction::TiltUp);
	default:                          return FString();
	}
}

bool AKakurenboPlayerController::InputKey(const FInputKeyEventArgs& Params)
{
	// 最後に触った機器を覚える（画面の案内・カーソルを出すか・設置パートのカーソルの動かし方が変わる）。
	// スティックの小さなぶれ・マウスの小さな揺れでは切り替えない
	if (Params.Key.IsGamepadKey())
	{
		if (Params.Key.IsAnalog() ? FMath::Abs(Params.AmountDepressed) > 0.4f : Params.Event == IE_Pressed)
		{
			bUsingGamepad = true;
		}
	}
	else if (Params.Key == EKeys::MouseX || Params.Key == EKeys::MouseY)
	{
		if (FMath::Abs(Params.AmountDepressed) > 3.f)
		{
			bUsingGamepad = false;
		}
	}
	else if (Params.Event == IE_Pressed)
	{
		bUsingGamepad = false;
	}
	return Super::InputKey(Params);
}

FVector2D AKakurenboPlayerController::GetStick(bool bRight) const
{
	if (!PlayerInput)
	{
		return FVector2D::ZeroVector;
	}
	const FVector2D Raw(PlayerInput->GetRawKeyValue(bRight ? EKeys::Gamepad_RightX : EKeys::Gamepad_LeftX),
		PlayerInput->GetRawKeyValue(bRight ? EKeys::Gamepad_RightY : EKeys::Gamepad_LeftY));
	// 円の遊び：中心付近は 0、そこから外側を 0〜1 に伸ばす（倒しきると 1）
	const float Length = Raw.Size();
	if (Length <= GamepadDeadZone)
	{
		return FVector2D::ZeroVector;
	}
	const float Scaled = FMath::Clamp((Length - GamepadDeadZone) / (1.f - GamepadDeadZone), 0.f, 1.f);
	return Raw / Length * Scaled;
}

FIntPoint AKakurenboPlayerController::ReadNavigation(float RealDeltaSeconds, bool bWithStick)
{
	FIntPoint Dir = FIntPoint::ZeroValue;
	const FVector2D Stick = bWithStick ? GetStick(false) : FVector2D::ZeroVector;
	// 押している間（と、同じフレームで押して離したとき）
	auto Held = [this](const FKey& Key) { return IsInputKeyDown(Key) || WasInputKeyJustPressed(Key); };
	if (Held(EKeys::Gamepad_DPad_Up) || Stick.Y > 0.6f)
	{
		Dir.Y = -1;
	}
	else if (Held(EKeys::Gamepad_DPad_Down) || Stick.Y < -0.6f)
	{
		Dir.Y = 1;
	}
	else if (Held(EKeys::Gamepad_DPad_Left) || Stick.X < -0.6f)
	{
		Dir.X = -1;
	}
	else if (Held(EKeys::Gamepad_DPad_Right) || Stick.X > 0.6f)
	{
		Dir.X = 1;
	}
	if (Dir == FIntPoint::ZeroValue || Dir != HeldNavDir)
	{
		// 押した瞬間（離したらくり返しをやめる）
		HeldNavDir = Dir;
		NavRepeatTimer = 0.4f;
		return Dir;
	}
	// 押し続けると少し待ってから、くり返し進む
	NavRepeatTimer -= RealDeltaSeconds;
	if (NavRepeatTimer <= 0.f)
	{
		NavRepeatTimer = 0.11f;
		return Dir;
	}
	return FIntPoint::ZeroValue;
}

bool AKakurenboPlayerController::WasMenuKeyPressed() const
{
	return WasInputKeyJustPressed(EKeys::Escape) || WasInputKeyJustPressed(EKeys::Gamepad_Special_Right);
}

bool AKakurenboPlayerController::WasBackKeyPressed() const
{
	return WasInputKeyJustPressed(EKeys::Escape) || WasInputKeyJustPressed(UKakurenboSettings::PadFace(EKakurenboPadFace::B));
}

bool AKakurenboPlayerController::IsUIFocused(EKakurenboUIAction Action, int32 Index) const
{
	return bUsingGamepad && FocusAction == Action && FocusIndex == Index && Action != EKakurenboUIAction::None;
}

bool AKakurenboPlayerController::GetGamepadBuildCursor(FVector& OutWorld) const
{
	if (!bUsingGamepad || !bGamepadCursorValid)
	{
		return false;
	}
	OutWorld = GamepadCursor;
	return true;
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
	// コントローラーで遊んでいる間にマウスを動かしたら、キーボード・マウスに戻す（カーソルが出る）。
	// 自動テスト中は本物のマウスを見ない（テストの途中で触っても結果が変わらないように）
	float MouseX = 0.f, MouseY = 0.f;
	if (!bIgnoreRealInputForAutoTest && GetMousePosition(MouseX, MouseY))
	{
		const FVector2D Mouse(MouseX, MouseY);
		if (bUsingGamepad && LastMousePosition.X >= 0.f && FVector2D::Distance(Mouse, LastMousePosition) > 4.f)
		{
			bUsingGamepad = false;
		}
		LastMousePosition = Mouse;
	}

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

		// 一時停止中もメニューを動かせるよう、くり返しの待ち時間は実時間で測る
		const float RealDelta = FApp::GetDeltaTime();
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
				if (bUsingGamepad)
				{
					HandleGamepadFocus(RealDelta, false);
				}
				break;
			case EKakurenboPhase::Build:
				HandleOverheadCamera(DeltaTime);
				HandleTopDownPan(DeltaTime);
				HandleBuildInput();
				break;
			case EKakurenboPhase::Hide:
				HandleMouseLook();
				HandleGamepadLook(DeltaTime);
				HandleHideInput();
				break;
			case EKakurenboPhase::Result:
				HandleOverheadCamera(DeltaTime);
				if (WasInputKeyJustPressed(EKeys::LeftMouseButton))
				{
					HandleUIClick();
				}
				if (bUsingGamepad)
				{
					HandleGamepadFocus(RealDelta, false);
				}
				break;
			default:
				break;
			}

			if (GM->GetPhase() == Phase && WasActionPressed(EKakurenboAction::Next))
			{
				GM->AdvancePhase();
			}
			// Esc・START: メニュー（一時停止・設定・タイトルへ）。F1・BACK: 操作説明
			if (WasMenuKeyPressed())
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
	// しゃがみ（切り替え）はプレイのたびに元に戻す（ユーザーの希望）。パートが変わったら立つ
	bSneakToggled = false;
	HideTipClock = 0.f;
	FocusAction = EKakurenboUIAction::None; // コントローラー：新しいパートの最初のボタンを選ぶ
	bGamepadCursorValid = false;

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
		ShowNewOniIntro(); // 初めて出てくる鬼がいれば、チュートリアル（ゲームは止まる）
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
		ShopTab = EKakurenboShopTab::Coin; // 購入パートはコインのお店から
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
	// UI のパート（メニュー・購入・設置・リザルト・タイトル）はカーソルでボタンやマスを指す。
	// かくれんぼ中（メニューを閉じている）はマウスの動き＝カメラの回転。コントローラーで遊んでいる間はカーソルを出さない（選んでいるボタンを明るくする）
	const bool bUIMode = IsMenuOpen() || GM->GetPhase() != EKakurenboPhase::Hide;
	const bool bWantCursor = bUIMode && !bUsingGamepad;
	if (bCursorModeKnown && bWantCursor == bCursorModeApplied && bUIMode == bUIModeApplied)
	{
		return;
	}
	const bool bModeChanged = !bCursorModeKnown || bUIMode != bUIModeApplied;
	bCursorModeKnown = true;
	bCursorModeApplied = bWantCursor;
	bUIModeApplied = bUIMode;
	bShowMouseCursor = bWantCursor;
	if (bModeChanged)
	{
		if (bUIMode)
		{
			FInputModeGameAndUI Mode;
			Mode.SetHideCursorDuringCapture(false);                         // クリック中もカーソルを消さない
			Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
			SetInputMode(Mode);
		}
		else
		{
			// カーソルを消してマウスを捕まえる
			SetInputMode(FInputModeGameOnly());
		}
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
	FocusAction = EKakurenboUIAction::None; // コントローラー：開いたメニューの最初のボタンを選ぶ
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
	FocusAction = EKakurenboUIAction::None;
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
	FocusAction = EKakurenboUIAction::None;
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

FName AKakurenboPlayerController::OniIntroTipId(EOniType Type)
{
	return FName(*FString::Printf(TEXT("Oni_%s"), *StaticEnum<EOniType>()->GetNameStringByValue(static_cast<int64>(Type))));
}

TArray<EOniType> AKakurenboPlayerController::GetIntroducedOniTypes() const
{
	TArray<EOniType> Types;
	const UKakurenboSettings* S = Settings();
	const UEnum* TypeEnum = StaticEnum<EOniType>();
	for (int32 i = 0; i < TypeEnum->NumEnums() - 1; ++i)
	{
		const EOniType Type = static_cast<EOniType>(TypeEnum->GetValueByIndex(i));
		if (S && S->HasSeenTip(OniIntroTipId(Type)))
		{
			Types.Add(Type);
		}
	}
	if (Types.Num() == 0)
	{
		Types.Add(EOniType::Balanced); // まだ紹介していなくても、標準鬼は見られる
	}
	return Types;
}

void AKakurenboPlayerController::ShowNewOniIntro()
{
	const AKakurenboGameMode* GM = GetKakurenboGameMode();
	UKakurenboSettings* S = Settings();
	if (!GM || !S || !S->bShowHints || (bIgnoreRealInputForAutoTest && !bShowOniIntroInAutoTest) || GM->GetPhase() != EKakurenboPhase::Hide)
	{
		return; // 自動テスト中は出さない（ゲームが止まるので）。ヒントを切っていても出さない
	}
	TArray<EOniType> NewTypes;
	for (const EOniType Type : GM->GetStageSettings().OniTypes)
	{
		if (!NewTypes.Contains(Type) && !S->HasSeenTip(OniIntroTipId(Type)))
		{
			NewTypes.Add(Type);
		}
	}
	if (NewTypes.Num() == 0)
	{
		return;
	}
	for (const EOniType Type : NewTypes)
	{
		S->MarkTipSeen(OniIntroTipId(Type));
	}
	OpenOniIntro(NewTypes, false);
}

void AKakurenboPlayerController::OpenOniIntro(const TArray<EOniType>& Types, bool bBook)
{
	if (Types.Num() == 0)
	{
		return;
	}
	OniIntroPages = Types;
	OniIntroPage = 0;
	bOniBook = bBook;
	if (const AKakurenboGameMode* GM = GetKakurenboGameMode(); GM && GM->GetOniStudio())
	{
		GM->GetOniStudio()->CaptureAll(); // 写真を撮り直す（テクスチャが読み込まれた後のきれいな写真）
	}
	OpenMenu(EKakurenboMenu::OniIntro);
	UKakurenboSoundSubsystem::Play2D(this, bBook ? EKakurenboSfx::Place : EKakurenboSfx::Alert, bBook ? 0.4f : 0.7f, bBook ? 1.5f : 0.9f);
}

void AKakurenboPlayerController::TurnOniIntroPage(int32 Delta)
{
	if (GetMenu() != EKakurenboMenu::OniIntro)
	{
		return;
	}
	const int32 Next = OniIntroPage + Delta;
	if (Next >= OniIntroPages.Num())
	{
		CloseMenu(); // 最後のページ：閉じてかくれんぼへ（図鑑なら一時停止メニューへ）
		return;
	}
	OniIntroPage = FMath::Clamp(Next, 0, OniIntroPages.Num() - 1);
	FocusAction = EKakurenboUIAction::None;
	UKakurenboSoundSubsystem::Play2D(this, EKakurenboSfx::Place, 0.35f, 1.7f);
}

bool AKakurenboPlayerController::IsTitleItemEnabled(EKakurenboTitleItem Item) const
{
	const AKakurenboGameMode* GM = GetKakurenboGameMode();
	return Item != EKakurenboTitleItem::Continue || (GM && GM->HasContinueData());
}

void AKakurenboPlayerController::HandleMenuInput()
{
	UKakurenboSettings* S = Settings();

	// キー設定：押されたキーを割り当てる（Esc・コントローラーの B で取り消し。コントローラーのボタンは割り当てない）
	if (WaitingKeySlot != INDEX_NONE && PlayerInput && S)
	{
		if (WasBackKeyPressed())
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

	// コントローラー：十字キー・左スティックでボタンを選び、A で押す（つまみは左右で値を変える）
	if (bUsingGamepad)
	{
		HandleGamepadFocus(FApp::GetDeltaTime(), true);
		if (!IsMenuOpen())
		{
			return; // A で「つづける」などを押してメニューが閉じた
		}
	}

	const EKakurenboMenu Menu = GetMenu();
	if (WasInputKeyJustPressed(EKeys::Gamepad_Special_Right))
	{
		CloseAllMenus(); // START：メニューを閉じてゲームに戻る
	}
	else if (WasBackKeyPressed() || (Menu == EKakurenboMenu::Help && WasActionPressed(EKakurenboAction::Help)))
	{
		CloseMenu();
	}
	else if (Menu == EKakurenboMenu::OniIntro)
	{
		// 鬼のチュートリアル：Enter・Space・→ でつぎ（最後は閉じる）、← でまえ
		if (WasInputKeyJustPressed(EKeys::Enter) || WasInputKeyJustPressed(EKeys::SpaceBar) || WasInputKeyJustPressed(EKeys::Right) || WasInputKeyJustPressed(EKeys::D))
		{
			TurnOniIntroPage(1);
		}
		else if (WasInputKeyJustPressed(EKeys::Left) || WasInputKeyJustPressed(EKeys::A))
		{
			TurnOniIntroPage(-1);
		}
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
	// 十字キー・左スティック（押し続けるとくり返し進む）
	const FIntPoint Nav = ReadNavigation(FApp::GetDeltaTime(), true);
	if (WasInputKeyJustPressed(EKeys::Up) || WasInputKeyJustPressed(EKeys::W) || Nav.Y < 0)
	{
		Step(-1);
		UKakurenboSoundSubsystem::Play2D(this, EKakurenboSfx::Place, 0.3f, 1.8f);
	}
	if (WasInputKeyJustPressed(EKeys::Down) || WasInputKeyJustPressed(EKeys::S) || Nav.Y > 0)
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
	if (WasInputKeyJustPressed(EKeys::Enter) || WasInputKeyJustPressed(EKeys::SpaceBar) || WasInputKeyJustPressed(UKakurenboSettings::PadFace(EKakurenboPadFace::A))
		|| WasInputKeyJustPressed(EKeys::Gamepad_Special_Right))
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
	case EKakurenboPauseItem::OniBook:
		OpenOniIntro(GetIntroducedOniTypes(), true);
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
	case EKakurenboSettingSlider::Gamepad:
		S->GamepadSensitivity = FMath::Lerp(UKakurenboSettings::MinGamepadSensitivity, UKakurenboSettings::MaxGamepadSensitivity, V);
		break;
	default: break;
	}
	S->Apply(this);
}

float AKakurenboPlayerController::GetSliderValue(EKakurenboSettingSlider Slider) const
{
	const UKakurenboSettings* S = Settings();
	if (!S)
	{
		return 0.f;
	}
	switch (Slider)
	{
	case EKakurenboSettingSlider::Master: return S->MasterVolume;
	case EKakurenboSettingSlider::Music:  return S->MusicVolume;
	case EKakurenboSettingSlider::Sfx:    return S->SfxVolume;
	case EKakurenboSettingSlider::Mouse:
		return (S->MouseSensitivity - UKakurenboSettings::MinMouseSensitivity) / (UKakurenboSettings::MaxMouseSensitivity - UKakurenboSettings::MinMouseSensitivity);
	case EKakurenboSettingSlider::Gamepad:
		return (S->GamepadSensitivity - UKakurenboSettings::MinGamepadSensitivity) / (UKakurenboSettings::MaxGamepadSensitivity - UKakurenboSettings::MinGamepadSensitivity);
	default: return 0.f;
	}
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
	case EKakurenboSettingToggle::PadLayout:
		S->SetPadLayout(UKakurenboSettings::IsNintendoLayout() ? EKakurenboPadLayout::Xbox : EKakurenboPadLayout::Nintendo);
		break;
	case EKakurenboSettingToggle::CrouchToggle:
		S->bCrouchToggle = !S->bCrouchToggle;
		bSneakToggled = false;
		break;
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

	// はじめての場面で、その場で使える操作を教える（[ ] はキーの形で描く。キーボード・マウスかコントローラーかで変わる）
	HideTipClock += RealDeltaSeconds;
	auto K = [this](EKakurenboAction Action) { return FString::Printf(TEXT("[%s]"), *KeyName(Action)); };
	auto U = [this](EKakurenboUIKey Key) { return UIKeyLabel(Key); };
	switch (State->Phase)
	{
	case EKakurenboPhase::Hide:
	{
		if (HideTipClock > 0.5f)
		{
			QueueTip(TEXT("Move"), FString::Printf(TEXT("%s で歩く　%s で見回す"), *U(EKakurenboUIKey::Move), *U(EKakurenboUIKey::Look)));
		}
		if (State->HideStartCountdown <= 0.f)
		{
			QueueTip(TEXT("Mash"), FString::Printf(TEXT("%s でコインを稼げる…でも、その音で鬼が寄ってくる"), *K(EKakurenboAction::Mash)));
			QueueTip(TEXT("Touch"), TEXT("鬼にさわられたら見つかる。物陰に隠れて、時間まで逃げきろう"));
		}
		if (HideTipClock > 14.f)
		{
			const UKakurenboSettings* S = Settings();
			QueueTip(TEXT("Sneak"), FString::Printf(TEXT("%s で%s。足音が小さくなり、低い家具にも隠れられる"), *K(EKakurenboAction::Sneak),
				(!S || S->bCrouchToggle) ? TEXT("しゃがむ（もう 1 回で立つ）") : TEXT("押している間しゃがむ")));
			QueueTip(TEXT("Treasure"), TEXT("キラキラと鳴る方に、お宝（宝石）がある。全部取るとボーナス"));
		}
		if (GM->IsClimbUnlocked())
		{
			QueueTip(TEXT("Climb"), FString::Printf(TEXT("壁や低い家具に向かって %s でよじ登る（1 段ずつ。鬼はクライミング壁と 2 段の壁には飛び乗れない）"), *K(EKakurenboAction::Climb)));
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
		QueueTip(TEXT("Shop"), FString::Printf(TEXT("%s 商品を買う。買った壁や罠は、次の設置で置ける　%s 次へ"), *U(EKakurenboUIKey::Buy), *K(EKakurenboAction::Next)));
		if (HideTipClock > 1.f)
		{
			QueueTip(TEXT("Menu"), FString::Printf(TEXT("%s メニュー・設定（音量・感度・キー設定）　%s 操作説明"), *U(EKakurenboUIKey::Menu), *K(EKakurenboAction::Help)));
		}
		if (GM->CanPrestige())
		{
			QueueTip(TEXT("Prestige"), TEXT("転生できるようになった。転生ポイントで、ずっと残る強化が買える"));
		}
		if (const AKakurenboGameState* GemState = GetWorld()->GetGameState<AKakurenboGameState>(); GemState && GemState->Gems >= 2)
		{
			QueueTip(TEXT("GemShop"), FString::Printf(TEXT("宝石がたまった！ %s 「宝石のお店」で、ふしぎな道具と交換できる"), *K(EKakurenboAction::SwitchShop)));
		}
		break;
	case EKakurenboPhase::Build:
		QueueTip(TEXT("Build"), FString::Printf(TEXT("%s 置く　%s 回収　%s スタート位置をカーソルの場所へ"), *U(EKakurenboUIKey::Place), *U(EKakurenboUIKey::PickUp), *K(EKakurenboAction::SetStart)));
		if (HideTipClock > 3.f)
		{
			QueueTip(TEXT("BuildCamera"), bUsingGamepad
				? FString::Printf(TEXT("%s カーソル（カメラが付いてくる）　%s 回す・傾ける　%s ズーム"), *U(EKakurenboUIKey::Cursor), *U(EKakurenboUIKey::Rotate), *U(EKakurenboUIKey::Zoom))
				: FString::Printf(TEXT("%s カメラを動かす　%s 回す　%s 傾ける　ホイールでズーム"),
					*K(EKakurenboAction::MoveForward), *U(EKakurenboUIKey::Rotate), *K(EKakurenboAction::TiltDown)));
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

	// Q / E（右スティックの左右）で回転
	const FVector2D RightStick = GetStick(true);
	float Rotate = RightStick.X;
	if (IsActionDown(EKakurenboAction::CameraLeft)) Rotate -= 1.f;
	if (IsActionDown(EKakurenboAction::CameraRight)) Rotate += 1.f;
	Hider->AddOverheadYaw(FMath::Clamp(Rotate, -1.f, 1.f) * OverheadRotateSpeed * DeltaTime);

	// 設置パート（真上から）は Z / X（右スティックの上下）で傾ける（Z: 斜め横から見る / X: 真上に戻す）。積んだ壁の高さが見やすくなる
	const bool bTopDown = Hider->GetViewMode() == EHiderViewMode::TopDown;
	if (bTopDown)
	{
		float Tilt = RightStick.Y;
		if (IsActionDown(EKakurenboAction::TiltDown)) Tilt += 1.f;
		if (IsActionDown(EKakurenboAction::TiltUp)) Tilt -= 1.f;
		Hider->AddTopDownTilt(FMath::Clamp(Tilt, -1.f, 1.f) * OverheadRotateSpeed * 0.6f * DeltaTime);
	}

	// LT・RT でズーム（RT で近づく）
	if (PlayerInput)
	{
		const float Zoom = PlayerInput->GetRawKeyValue(EKeys::Gamepad_RightTriggerAxis) - PlayerInput->GetRawKeyValue(EKeys::Gamepad_LeftTriggerAxis);
		if (FMath::Abs(Zoom) > 0.15f)
		{
			Hider->AddZoom(-Zoom * GamepadZoomSpeed * DeltaTime);
		}
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
	if (!Hider || !Grid || !Grid->IsConfigured() || bUsingGamepad || IsMenuOpen())
	{
		return; // コントローラーのときは、カメラがカーソルに付いていく（HandleGamepadBuild）
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
	if (Hider->IsClimbing())
	{
		return; // よじ登っている間は動けない
	}

	// WASD（左スティック）を今のカメラの向き基準の移動に変換する（画面の奥＝W）
	const FRotator YawRot(0.f, Hider->GetViewYaw(), 0.f);
	const FVector Forward = FRotationMatrix(YawRot).GetUnitAxis(EAxis::X);
	const FVector Right = FRotationMatrix(YawRot).GetUnitAxis(EAxis::Y);

	// スティックを少しだけ倒すとゆっくり歩く（足音は歩いた距離で鳴るので、ゆっくりでも鳴る）
	const FVector2D Stick = GetStick(false);
	float F = Stick.Y, R = Stick.X;
	if (IsActionDown(EKakurenboAction::MoveForward)) F += 1.f;
	if (IsActionDown(EKakurenboAction::MoveBack)) F -= 1.f;
	if (IsActionDown(EKakurenboAction::MoveRight)) R += 1.f;
	if (IsActionDown(EKakurenboAction::MoveLeft)) R -= 1.f;
	const FVector Move = (Forward * F + Right * R).GetClampedToMaxSize(1.f);
	if (!Hider->GetCharacterMovement()->IsFalling())
	{
		Hider->AddMovementInput(Move, 1.f); // 空中では向きも速さも変えられない（縁から下りるとほぼ真下に落ちる）
	}

	// よじ登る：歩こうとしている方向（無ければ体の向き）の壁・家具へ
	if (WasActionPressed(EKakurenboAction::Climb))
	{
		DoClimb(Move);
	}
}

void AKakurenboPlayerController::DoClimb(const FVector& Dir)
{
	AHiderCharacter* Hider = GetHider();
	AKakurenboGameMode* GM = GetKakurenboGameMode();
	if (!Hider || !GM || GM->GetPhase() != EKakurenboPhase::Hide)
	{
		return;
	}
	Hider->TryStartClimb(Dir.SizeSquared2D() > 0.01f ? Dir : Hider->GetActorForwardVector());
}

void AKakurenboPlayerController::HandleGamepadLook(float DeltaTime)
{
	// 右スティックでカメラを回す（マウスと同じく回転入力に直接足す。上下反転の設定も同じ）
	const FVector2D Stick = GetStick(true);
	if (Stick.IsNearlyZero())
	{
		return;
	}
	RotationInput.Yaw += Stick.X * GamepadLookYawSpeed * GamepadSensitivity * DeltaTime;
	RotationInput.Pitch += (bInvertMouseY ? -Stick.Y : Stick.Y) * GamepadLookPitchSpeed * GamepadSensitivity * DeltaTime;
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
	// お店の切り替え（Tab・LB で次へ。コントローラーは RB で前へ）
	if (WasActionPressed(EKakurenboAction::SwitchShop))
	{
		CycleShopTab(+1);
	}
	if (WasInputKeyJustPressed(EKeys::Gamepad_RightShoulder))
	{
		CycleShopTab(-1);
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
	const AKakurenboHUD* HUD = GetHUD<AKakurenboHUD>();
	FVector2D Cursor;
	FKakurenboUIButton Button;
	if (!GetKakurenboGameMode() || !HUD || !GetUICursorPosition(Cursor) || !HUD->FindButtonAt(Cursor, Button))
	{
		return false;
	}
	ActivateUIButton(Button, Cursor, true);
	return true; // パネルの上のクリックは、後ろの床に壁を置かない
}

void AKakurenboPlayerController::ActivateUIButton(const FKakurenboUIButton& Button, const FVector2D& Point, bool bFromMouse)
{
	AKakurenboGameMode* GM = GetKakurenboGameMode();
	UKakurenboSettings* S = Settings();
	if (!GM)
	{
		return;
	}
	const FVector2D Cursor = Point;
	switch (Button.Action)
	{
	case EKakurenboUIAction::ShopTab:   ShopTab = static_cast<EKakurenboShopTab>(FMath::Clamp(Button.Index, 0, 2)); break;
	case EKakurenboUIAction::BuyAmount: BuyAmount = static_cast<EKakurenboBuyAmount>(FMath::Clamp(Button.Index, 0, 2)); break;
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
		// つまみ：押したところの値にして、そのままドラッグで動かせる（コントローラーは左右で変える）
		if (bFromMouse)
		{
			DraggingSlider = Button.Index;
			DraggingSliderRect = Button.Rect;
			SetSliderValue(static_cast<EKakurenboSettingSlider>(Button.Index), (Cursor.X - Button.Rect.Min.X) / FMath::Max(Button.Rect.GetSize().X, 1.0));
		}
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
	case EKakurenboUIAction::OniPage:   TurnOniIntroPage(Button.Index < 0 ? -1 : 1); break;
	default: break;
	}
}

int32 AKakurenboPlayerController::EnsureFocus(const TArray<FKakurenboUIButton>& Buttons)
{
	const int32 Current = Buttons.IndexOfByPredicate([this](const FKakurenboUIButton& B) { return B.Action == FocusAction && B.Index == FocusIndex; });
	if (Current != INDEX_NONE)
	{
		return Current;
	}
	// その画面で最初に選んでおくボタン（確認は「いいえ」、リザルトは「つぎへ」、購入は 1 つ目の商品…）
	TArray<TPair<EKakurenboUIAction, int32>> Preferred;
	switch (GetMenu())
	{
	case EKakurenboMenu::Pause:          Preferred.Add({ EKakurenboUIAction::MenuItem, 0 }); break;
	case EKakurenboMenu::Settings:       Preferred.Add({ EKakurenboUIAction::Slider, 0 }); break;
	case EKakurenboMenu::Keys:           Preferred.Add({ EKakurenboUIAction::Back, 0 }); break;
	case EKakurenboMenu::Help:           Preferred.Add({ EKakurenboUIAction::Back, 0 }); break;
	case EKakurenboMenu::ConfirmNewGame:
	case EKakurenboMenu::ConfirmTitle:   Preferred.Add({ EKakurenboUIAction::ConfirmNo, 0 }); break;
	case EKakurenboMenu::OniIntro:       Preferred.Add({ EKakurenboUIAction::OniPage, 1 }); break;
	default:
		Preferred.Add({ EKakurenboUIAction::ShopItem, 0 });
		Preferred.Add({ EKakurenboUIAction::NextPhase, 0 });
		break;
	}
	for (const TPair<EKakurenboUIAction, int32>& Want : Preferred)
	{
		const int32 Found = Buttons.IndexOfByPredicate([&Want](const FKakurenboUIButton& B) { return B.Action == Want.Key && B.Index == Want.Value; });
		if (Found != INDEX_NONE)
		{
			FocusAction = Want.Key;
			FocusIndex = Want.Value;
			return Found;
		}
	}
	FocusAction = Buttons[0].Action;
	FocusIndex = Buttons[0].Index;
	return 0;
}

void AKakurenboPlayerController::HandleGamepadFocus(float RealDeltaSeconds, bool bSliders)
{
	const AKakurenboHUD* HUD = GetHUD<AKakurenboHUD>();
	const AKakurenboGameMode* GM = GetKakurenboGameMode();
	// 画面が変わった直後（まだ前の画面のボタンしか無い）は何もしない
	if (!HUD || !GM || HUD->GetButtons().Num() == 0 || !HUD->AreButtonsFor(GM->GetPhase(), GetMenu()))
	{
		return;
	}
	const TArray<FKakurenboUIButton> Buttons = HUD->GetButtons(); // 押したボタンで画面が変わっても困らないよう写しを使う
	const int32 Current = EnsureFocus(Buttons);
	const FKakurenboUIButton& Focused = Buttons[Current];

	const FIntPoint Nav = ReadNavigation(RealDeltaSeconds, true);
	if (Nav != FIntPoint::ZeroValue)
	{
		if (bSliders && Focused.Action == EKakurenboUIAction::Slider && Nav.X != 0)
		{
			// つまみは左右で値を変える（5% ずつ）
			const EKakurenboSettingSlider Slider = static_cast<EKakurenboSettingSlider>(Focused.Index);
			SetSliderValue(Slider, GetSliderValue(Slider) + Nav.X * 0.05f);
			if (UKakurenboSettings* S = Settings())
			{
				S->Save();
			}
		}
		else
		{
			// 押した方向にある一番近いボタンへ（真横・真上から外れているほど遠いとみなす）
			const FVector2D From = Focused.Rect.GetCenter();
			const FVector2D Dir(Nav.X, Nav.Y);
			int32 Best = INDEX_NONE;
			float BestScore = TNumericLimits<float>::Max();
			for (int32 i = 0; i < Buttons.Num(); ++i)
			{
				const FVector2D Delta = Buttons[i].Rect.GetCenter() - From;
				const float Along = FVector2D::DotProduct(Delta, Dir);
				const float Across = FMath::Abs(FVector2D::CrossProduct(Dir, Delta));
				if (i == Current || Along <= 1.f || Across > Along * 3.f)
				{
					continue;
				}
				const float Score = Along + Across * 2.f;
				if (Score < BestScore)
				{
					BestScore = Score;
					Best = i;
				}
			}
			if (Best != INDEX_NONE)
			{
				FocusAction = Buttons[Best].Action;
				FocusIndex = Buttons[Best].Index;
				UKakurenboSoundSubsystem::Play2D(this, EKakurenboSfx::Place, 0.25f, 1.9f);
			}
		}
	}
	if (WasInputKeyJustPressed(UKakurenboSettings::PadFace(EKakurenboPadFace::A)))
	{
		const FKakurenboUIButton Pressed = Buttons[Buttons.IndexOfByPredicate([this](const FKakurenboUIButton& B) { return B.Action == FocusAction && B.Index == FocusIndex; })];
		ActivateUIButton(Pressed, Pressed.Rect.GetCenter(), false);
	}
}

bool AKakurenboPlayerController::IsPrestigeConfirmPending() const
{
	return GetWorld() && GetWorld()->GetTimeSeconds() < PrestigeConfirmUntil;
}

void AKakurenboPlayerController::CycleShopTab(int32 Delta)
{
	// 画面の並び（コイン → 宝石 → 転生）の順に回る
	static const EKakurenboShopTab Order[] = { EKakurenboShopTab::Coin, EKakurenboShopTab::Gem, EKakurenboShopTab::Prestige };
	int32 Index = 0;
	for (int32 i = 0; i < UE_ARRAY_COUNT(Order); ++i)
	{
		Index = Order[i] == ShopTab ? i : Index;
	}
	ShopTab = Order[(Index + Delta + UE_ARRAY_COUNT(Order) * 4) % UE_ARRAY_COUNT(Order)];
}

bool AKakurenboPlayerController::BuyItem(int32 ItemNumber)
{
	AKakurenboGameMode* GM = GetKakurenboGameMode();
	if (!GM)
	{
		return false;
	}
	switch (ShopTab)
	{
	case EKakurenboShopTab::Prestige: return GM->TryBuyPrestigeUpgrade(ItemNumber - 1);
	case EKakurenboShopTab::Gem:      return GM->TryBuyGemShopItem(ItemNumber - 1, GetBuyCount()) > 0;
	default:                          return GM->TryBuyShopItem(ItemNumber - 1, GetBuyCount()) > 0;
	}
}

// ---------------------------------------------------------------- 設置パート

TArray<int32> AKakurenboPlayerController::GetBuildSlots() const
{
	// 壁 → コインの罠 → 宝石の道具（持っているもの・設計図にあるものだけ。だるまさんがころんだは置かないので出さない）
	TArray<int32> Slots;
	const AKakurenboGameMode* GM = GetKakurenboGameMode();
	const AKakurenboGameState* State = GetWorld()->GetGameState<AKakurenboGameState>();
	const UKakurenboGridSubsystem* Grid = GetWorld()->GetSubsystem<UKakurenboGridSubsystem>();
	if (!GM)
	{
		return Slots;
	}
	const int32 NumWalls = GM->WallTypes.Num();
	for (int32 i = 0; i < NumWalls; ++i)
	{
		Slots.Add(i);
	}
	for (int32 t = 0; t < GM->TrapTypes.Num(); ++t)
	{
		if (!GM->IsPlaceableTrap(t))
		{
			continue;
		}
		if (GM->IsGemTrap(t))
		{
			const int32 Stock = (State && State->TrapStock.IsValidIndex(t)) ? State->TrapStock[t] : 0;
			const int32 Missing = Grid ? Grid->GetMissingTrapCount(t) : 0;
			if (Stock + GM->GetOwnedTrapCount(t) + Missing <= 0 && SelectedBuildSlot != NumWalls + t)
			{
				continue;
			}
		}
		Slots.Add(NumWalls + t);
	}
	return Slots;
}

void AKakurenboPlayerController::SelectBuildSlotByOffset(int32 Delta)
{
	const TArray<int32> Slots = GetBuildSlots();
	if (Slots.Num() == 0)
	{
		return;
	}
	const int32 Current = FMath::Max(0, Slots.IndexOfByKey(SelectedBuildSlot));
	SelectedBuildSlot = Slots[(Current + Delta + Slots.Num() * 4) % Slots.Num()];
}

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

	// 置く物を選ぶ（下の欄の順：壁 → 罠 → 宝石の道具。数字キーは欄の番号）
	const TArray<int32> Slots = GetBuildSlots();
	const int32 Number = GetPressedNumberKey();
	if (Number >= 1 && Number <= Slots.Num())
	{
		SelectedBuildSlot = Slots[Number - 1];
	}

	if (bUsingGamepad)
	{
		// コントローラー：カーソルはスティックで動かすマス（置く・回収は A・X）
		HandleGamepadBuild(GetWorld()->GetDeltaSeconds());
	}
	else
	{
		bGamepadCursorValid = false; // 次にコントローラーに持ち替えたら、カメラの真ん中から始める
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
	}

	// 左クリック：画面下の欄やボタンの上ならそれを押す。一方通行ののれんを指していれば向きを変える。そうでなければ置く
	if (!bUsingGamepad && WasInputKeyJustPressed(EKeys::LeftMouseButton) && !HandleUIClick() && GM->GetPhase() == EKakurenboPhase::Build
		&& bHasTrapPickUpTarget && IsOneWayAt(TrapPickUpCell))
	{
		GM->RotateTrap(TrapPickUpCell);
	}
	else if (!bUsingGamepad && WasInputKeyJustPressed(EKeys::LeftMouseButton) && GM->GetPhase() == EKakurenboPhase::Build && bCanPlaceAtTarget)
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
	if (WasInputKeyJustPressed(EKeys::RightMouseButton) || (bUsingGamepad && WasInputKeyJustPressed(UKakurenboSettings::PadFace(EKakurenboPadFace::X))))
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

void AKakurenboPlayerController::HandleGamepadBuild(float DeltaTime)
{
	AKakurenboGameMode* GM = GetKakurenboGameMode();
	AHiderCharacter* Hider = GetHider();
	const UKakurenboGridSubsystem* Grid = GetWorld()->GetSubsystem<UKakurenboGridSubsystem>();
	if (!GM || !Hider || !Grid || !Grid->IsConfigured())
	{
		return;
	}
	if (!bGamepadCursorValid)
	{
		GamepadCursor = Hider->GetTopDownFocus(); // カメラの真ん中から
		bGamepadCursorValid = true;
	}
	// 画面の上 = カメラの向き
	const FRotator YawRot(0.f, Hider->GetOverheadYaw(), 0.f);
	const FVector Forward = FRotationMatrix(YawRot).GetUnitAxis(EAxis::X);
	const FVector Right = FRotationMatrix(YawRot).GetUnitAxis(EAxis::Y);

	// 左スティック：なめらかに動かす（高いところから見ているほど速い）
	const FVector2D Stick = GetStick(false);
	const float Speed = GamepadCursorSpeed * FMath::Max(0.3f, Hider->TopDownDistance / 2600.f);
	GamepadCursor += (Forward * Stick.Y + Right * Stick.X) * Speed * DeltaTime;

	// 十字キー：1 マスずつ（画面の上下左右に一番近いマスの並びの方向へ）
	const FIntPoint Nav = ReadNavigation(FApp::GetDeltaTime(), false);
	if (Nav != FIntPoint::ZeroValue)
	{
		const FVector Want = Forward * -Nav.Y + Right * Nav.X;
		const FIntPoint Step = FMath::Abs(Want.X) >= FMath::Abs(Want.Y) ? FIntPoint(Want.X > 0.f ? 1 : -1, 0) : FIntPoint(0, Want.Y > 0.f ? 1 : -1);
		const FVector Floor = Grid->CellFloorCenter(Grid->WorldToCell(GamepadCursor) + Step);
		GamepadCursor = FVector(Floor.X, Floor.Y, GamepadCursor.Z);
	}

	// 館の中に収める
	const FVector Min = Grid->CellFloorCenter(FIntPoint(0, 0));
	const FVector Max = Grid->CellFloorCenter(FIntPoint(Grid->GetSizeX() - 1, Grid->GetSizeY() - 1));
	GamepadCursor.X = FMath::Clamp(GamepadCursor.X, Min.X, Max.X);
	GamepadCursor.Y = FMath::Clamp(GamepadCursor.Y, Min.Y, Max.Y);
	GamepadCursor.Z = Min.Z;

	// カメラはカーソルに付いていく（カーソルはいつも画面の真ん中あたり）
	const FVector Focus = Hider->GetTopDownFocus();
	Hider->SetTopDownFocus(FMath::VInterpTo(Focus, FVector(GamepadCursor.X, GamepadCursor.Y, Focus.Z), DeltaTime, 6.f));

	UpdateBuildTargetAtCell(Grid->WorldToCell(GamepadCursor));

	// LB・RB：置く物（壁の種類 → 罠の種類の順にぐるっと回る）
	if (WasInputKeyJustPressed(EKeys::Gamepad_LeftShoulder))
	{
		SelectBuildSlotByOffset(-1);
	}
	if (WasInputKeyJustPressed(EKeys::Gamepad_RightShoulder))
	{
		SelectBuildSlotByOffset(+1);
	}
	// A：置く（のれんの上なら向きを変える）
	if (WasInputKeyJustPressed(UKakurenboSettings::PadFace(EKakurenboPadFace::A)) && bHasTrapPickUpTarget && IsOneWayAt(TrapPickUpCell))
	{
		GM->RotateTrap(TrapPickUpCell);
	}
	else if (WasInputKeyJustPressed(UKakurenboSettings::PadFace(EKakurenboPadFace::A)) && bCanPlaceAtTarget)
	{
		if (IsTrapSlotSelected())
		{
			GM->PlaceTrap(BuildTargetCell, GetSelectedTrapType());
		}
		else
		{
			GM->PlaceWall(BuildTargetCell, GetSelectedWallType());
		}
		UpdateBuildTargetAtCell(BuildTargetCell);
	}
}

void AKakurenboPlayerController::UpdateBuildTargetAtCell(const FIntPoint& Cell)
{
	ClearBuildTarget();
	AKakurenboGameMode* GM = GetKakurenboGameMode();
	UKakurenboGridSubsystem* Grid = GetWorld()->GetSubsystem<UKakurenboGridSubsystem>();
	if (!GM || !Grid || !Grid->IsConfigured() || !Grid->IsInside(Cell))
	{
		return;
	}
	// そのマスの一番上に置く・一番上の壁を回収する（マウスのように面を選べないので、いつも一番上）
	BuildTargetCell = Cell;
	bHasBuildTarget = true;
	bCanPlaceAtTarget = IsTrapSlotSelected()
		? GM->CanPlaceTrap(Cell, GetSelectedTrapType(), &BuildTargetReason)
		: GM->CanPlaceWall(Cell, GetSelectedWallType(), &BuildTargetReason);
	PickUpTarget = Grid->GetTopBlock(Cell);
	if (!PickUpTarget && Grid->HasTrap(Cell))
	{
		bHasTrapPickUpTarget = true;
		TrapPickUpCell = Cell;
	}
}

bool AKakurenboPlayerController::IsOneWayAt(const FIntPoint& Cell) const
{
	const UKakurenboGridSubsystem* Grid = GetWorld()->GetSubsystem<UKakurenboGridSubsystem>();
	const ATrapActor* Trap = Grid ? Grid->GetTrap(Cell) : nullptr;
	return Trap && Trap->GetKind() == ETrapKind::OneWay;
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
	// 見えていない物（設置パートでは隠している外周・部屋の壁の高い部分、出入り口の上の壁など）にはレイを当てない。
	// 隠しているだけで当たり判定は残っているので、そのまま飛ばすと斜めのレイが見えない壁に当たり、手前の別のマスを指してしまう
	// （「ブロックが置けない」の原因だった）。当たった物が見えていなければ、それを無視してもう一度飛ばす
	FCollisionQueryParams Params(SCENE_QUERY_STAT(BuildTrace), false, GetPawn());
	FHitResult Hit;
	bool bHit = false;
	for (int32 Attempt = 0; Attempt < 12; ++Attempt)
	{
		if (!GetWorld()->LineTraceSingleByChannel(Hit, RayOrigin, RayOrigin + RayDirection * 20000.f, ECC_Visibility, Params))
		{
			break;
		}
		const UPrimitiveComponent* HitComp = Hit.GetComponent();
		// 家具の当たり判定の箱は隠してあるが、見えている家具のメッシュと同じ形なので当ててよい（タグで見分ける）
		const bool bHiddenThing = HitComp && (HitComp->bHiddenInGame || !HitComp->IsVisible() || (Hit.GetActor() && Hit.GetActor()->IsHidden()))
			&& !HitComp->ComponentHasTag(AKakurenboArena::FurnitureCollisionTag);
		if (!bHiddenThing)
		{
			bHit = true;
			break;
		}
		Params.AddIgnoredComponent(HitComp);
	}
	if (!bHit)
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

	// しゃがむ（しのび足：ゆっくり・足音が小さい）。M15：設定で「1 回押して切り替え」（初期設定）と「押している間」を選べる
	if (AHiderCharacter* Hider = GetHider())
	{
		const UKakurenboSettings* S = Settings();
		if (S && S->bCrouchToggle)
		{
			if (WasActionPressed(EKakurenboAction::Sneak))
			{
				bSneakToggled = !bSneakToggled;
			}
			Hider->SetSneaking(bTestSneak || bSneakToggled);
		}
		else
		{
			Hider->SetSneaking(bTestSneak || IsActionDown(EKakurenboAction::Sneak));
		}
	}

	if (WasActionPressed(EKakurenboAction::Mash))
	{
		DoMash();
	}
	// だるまさんがころんだ（宝石のお店の道具。持っていれば）
	if (WasActionPressed(EKakurenboAction::UseItem))
	{
		if (AKakurenboGameMode* GM = GetKakurenboGameMode())
		{
			GM->UseDaruma();
		}
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

void AKakurenboPlayerController::KakuAddGems(int32 Amount)
{
	if (AKakurenboGameMode* GM = GetKakurenboGameMode())
	{
		GM->DebugAddGems(Amount);
	}
}

void AKakurenboPlayerController::KakuBuyGem(int32 ItemNumber)
{
	if (AKakurenboGameMode* GM = GetKakurenboGameMode())
	{
		GM->TryBuyGemShopItem(ItemNumber - 1);
	}
}

void AKakurenboPlayerController::KakuDaruma()
{
	if (AKakurenboGameMode* GM = GetKakurenboGameMode())
	{
		GM->UseDaruma();
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
