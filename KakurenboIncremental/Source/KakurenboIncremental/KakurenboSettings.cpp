#include "KakurenboSettings.h"

#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "GameFramework/GameUserSettings.h"
#include "KakurenboPlayerController.h"
#include "KakurenboSoundSubsystem.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"

namespace
{
	constexpr int32 NumActions = static_cast<int32>(EKakurenboAction::Count);

	FName ActionName(EKakurenboAction Action)
	{
		// UENUM の名前（"MoveForward" など）。セーブのキーに使う
		return FName(*StaticEnum<EKakurenboAction>()->GetNameStringByValue(static_cast<int64>(Action)));
	}
}

void UKakurenboSettings::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	bPersist = !FParse::Param(FCommandLine::Get(), TEXT("KakuNoSave"));
	ResetBindings();
	Load();
}

UKakurenboSettings* UKakurenboSettings::Get(const UObject* WorldContext)
{
	const UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	const UGameInstance* GI = World ? World->GetGameInstance() : nullptr;
	return GI ? GI->GetSubsystem<UKakurenboSettings>() : nullptr;
}

void UKakurenboSettings::Load()
{
	if (!bPersist || !UGameplayStatics::DoesSaveGameExist(SlotName, 0))
	{
		return;
	}
	const UKakurenboSettingsSave* Saved = Cast<UKakurenboSettingsSave>(UGameplayStatics::LoadGameFromSlot(SlotName, 0));
	if (!Saved)
	{
		return;
	}
	MasterVolume = FMath::Clamp(Saved->MasterVolume, 0.f, 1.f);
	MusicVolume = FMath::Clamp(Saved->MusicVolume, 0.f, 1.f);
	SfxVolume = FMath::Clamp(Saved->SfxVolume, 0.f, 1.f);
	MouseSensitivity = FMath::Clamp(Saved->MouseSensitivity, MinMouseSensitivity, MaxMouseSensitivity);
	GamepadSensitivity = Saved->Version >= 2 ? FMath::Clamp(Saved->GamepadSensitivity, MinGamepadSensitivity, MaxGamepadSensitivity) : 1.f;
	bInvertMouseY = Saved->bInvertMouseY;
	bShowHints = Saved->bShowHints;
	for (int32 i = 0; i < NumActions; ++i)
	{
		const FKakurenboKeyBinding* Found = Saved->Bindings.Find(ActionName(static_cast<EKakurenboAction>(i)));
		if (!Found && static_cast<EKakurenboAction>(i) == EKakurenboAction::Climb)
		{
			Found = Saved->Bindings.Find(TEXT("Jump")); // 版 1 の「ジャンプ」のキーは「よじ登る」に使う
		}
		if (Found)
		{
			// 今は使えないキー（古いセーブ）なら初期設定のまま
			if (!Found->Primary.IsValid() || CanBindKey(Found->Primary))
			{
				Keys.At(i) = *Found;
			}
		}
	}
	SeenTips = TSet<FName>(Saved->SeenTips);
}

void UKakurenboSettings::Save()
{
	if (!bPersist)
	{
		return;
	}
	UKakurenboSettingsSave* Out = Cast<UKakurenboSettingsSave>(UGameplayStatics::CreateSaveGameObject(UKakurenboSettingsSave::StaticClass()));
	Out->MasterVolume = MasterVolume;
	Out->MusicVolume = MusicVolume;
	Out->SfxVolume = SfxVolume;
	Out->MouseSensitivity = MouseSensitivity;
	Out->GamepadSensitivity = GamepadSensitivity;
	Out->bInvertMouseY = bInvertMouseY;
	Out->bShowHints = bShowHints;
	for (int32 i = 0; i < NumActions; ++i)
	{
		Out->Bindings.Add(ActionName(static_cast<EKakurenboAction>(i)), Keys.At(i));
	}
	Out->SeenTips = SeenTips.Array();
	UGameplayStatics::SaveGameToSlot(Out, SlotName, 0);
}

void UKakurenboSettings::Apply(const UObject* WorldContext) const
{
	const UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	if (!World)
	{
		return;
	}
	if (UKakurenboSoundSubsystem* Sound = World->GetSubsystem<UKakurenboSoundSubsystem>())
	{
		Sound->SfxVolume = SfxVolume;
		Sound->SetMasterVolume(MasterVolume);
		Sound->SetMusicVolume(MusicVolume);
	}
	if (AKakurenboPlayerController* PC = Cast<AKakurenboPlayerController>(World->GetFirstPlayerController()))
	{
		PC->MouseSensitivity = MouseSensitivity;
		PC->GamepadSensitivity = GamepadSensitivity;
		PC->bInvertMouseY = bInvertMouseY;
	}
}

bool UKakurenboSettings::IsFullscreen()
{
	// 今の画面がフルスクリーンか（起動オプション -windowed などで、保存してある設定と違うことがある）
	if (GEngine && GEngine->GameViewport)
	{
		return GEngine->GameViewport->IsFullScreenViewport();
	}
	const UGameUserSettings* S = GEngine ? GEngine->GetGameUserSettings() : nullptr;
	return S && S->GetFullscreenMode() != EWindowMode::Windowed;
}

void UKakurenboSettings::SetFullscreen(bool bFullscreen)
{
	if (UGameUserSettings* S = GEngine ? GEngine->GetGameUserSettings() : nullptr)
	{
		// ボーダーレスのフルスクリーン（画面の解像度はそのまま）
		S->SetFullscreenMode(bFullscreen ? EWindowMode::WindowedFullscreen : EWindowMode::Windowed);
		if (bFullscreen)
		{
			S->SetScreenResolution(S->GetDesktopResolution());
		}
		S->ApplySettings(false);
		S->SaveSettings();
	}
}

// ---------------------------------------------------------------- キー設定

FKakurenboKeyBinding UKakurenboSettings::DefaultBinding(EKakurenboAction Action)
{
	auto Make = [](const FKey& A, const FKey& B = EKeys::Invalid)
	{
		FKakurenboKeyBinding Binding;
		Binding.Primary = A;
		Binding.Secondary = B;
		return Binding;
	};
	switch (Action)
	{
	case EKakurenboAction::MoveForward: return Make(EKeys::W, EKeys::Up);
	case EKakurenboAction::MoveBack:    return Make(EKeys::S, EKeys::Down);
	case EKakurenboAction::MoveLeft:    return Make(EKeys::A, EKeys::Left);
	case EKakurenboAction::MoveRight:   return Make(EKeys::D, EKeys::Right);
	case EKakurenboAction::Mash:        return Make(EKeys::LeftMouseButton, EKeys::F);
	case EKakurenboAction::Sneak:       return Make(EKeys::LeftControl, EKeys::C);
	case EKakurenboAction::Dash:        return Make(EKeys::LeftShift, EKeys::RightShift);
	case EKakurenboAction::Climb:       return Make(EKeys::SpaceBar);
	case EKakurenboAction::CameraLeft:  return Make(EKeys::Q);
	case EKakurenboAction::CameraRight: return Make(EKeys::E);
	case EKakurenboAction::TiltDown:    return Make(EKeys::Z);
	case EKakurenboAction::TiltUp:      return Make(EKeys::X);
	case EKakurenboAction::Next:        return Make(EKeys::Enter);
	case EKakurenboAction::SetStart:    return Make(EKeys::T);
	case EKakurenboAction::BackToShop:  return Make(EKeys::B, EKeys::BackSpace);
	case EKakurenboAction::ClearAll:    return Make(EKeys::Delete);
	case EKakurenboAction::SwitchShop:  return Make(EKeys::Tab);
	case EKakurenboAction::Refill:      return Make(EKeys::R);
	case EKakurenboAction::Prestige:    return Make(EKeys::P);
	case EKakurenboAction::Help:        return Make(EKeys::F1, EKeys::H);
	default:                            return Make(EKeys::Invalid);
	}
}

FKey UKakurenboSettings::GamepadKey(EKakurenboAction Action)
{
	// コントローラーのボタンは決まっている（キー設定では変えない）。スティック（移動・カメラ）とメニューの操作は PlayerController が直接見る
	switch (Action)
	{
	case EKakurenboAction::MoveForward: return EKeys::Gamepad_DPad_Up;
	case EKakurenboAction::MoveBack:    return EKeys::Gamepad_DPad_Down;
	case EKakurenboAction::MoveLeft:    return EKeys::Gamepad_DPad_Left;
	case EKakurenboAction::MoveRight:   return EKeys::Gamepad_DPad_Right;
	case EKakurenboAction::Mash:        return EKeys::Gamepad_RightTrigger;     // RT：連打しながら右スティックでカメラも回せる
	case EKakurenboAction::Sneak:       return EKeys::Gamepad_LeftTrigger;      // LT を押している間しゃがむ
	case EKakurenboAction::Dash:        return EKeys::Gamepad_RightShoulder;    // RB
	case EKakurenboAction::Climb:       return EKeys::Gamepad_FaceButton_Bottom; // A
	case EKakurenboAction::Next:        return EKeys::Gamepad_FaceButton_Top;   // Y
	case EKakurenboAction::SetStart:    return EKeys::Gamepad_LeftThumbstick;   // L スティック押し込み
	case EKakurenboAction::BackToShop:  return EKeys::Gamepad_FaceButton_Right; // B
	case EKakurenboAction::ClearAll:    return EKeys::Gamepad_RightThumbstick;  // R スティック押し込み（2 回）
	case EKakurenboAction::SwitchShop:  return EKeys::Gamepad_LeftShoulder;     // LB（RB でも切り替わる）
	case EKakurenboAction::Refill:      return EKeys::Gamepad_FaceButton_Left;  // X
	case EKakurenboAction::Help:        return EKeys::Gamepad_Special_Left;     // BACK（ビュー）
	default:                            return EKeys::Invalid;                  // カメラ（右スティック）・転生（ボタンを選んで A）
	}
}

void FKakurenboKeyMap::Reset()
{
	Bindings.SetNum(NumActions);
	for (int32 i = 0; i < NumActions; ++i)
	{
		Bindings[i] = UKakurenboSettings::DefaultBinding(static_cast<EKakurenboAction>(i));
	}
}

FKey FKakurenboKeyMap::Get(EKakurenboAction Action, bool bSecondary) const
{
	const int32 Index = static_cast<int32>(Action);
	if (!Bindings.IsValidIndex(Index))
	{
		return EKeys::Invalid;
	}
	return bSecondary ? Bindings[Index].Secondary : Bindings[Index].Primary;
}

bool FKakurenboKeyMap::Bind(EKakurenboAction Action, const FKey& Key, bool bSecondary)
{
	const int32 Index = static_cast<int32>(Action);
	if (!Bindings.IsValidIndex(Index) || !UKakurenboSettings::CanBindKey(Key))
	{
		return false;
	}
	FKey& Slot = bSecondary ? Bindings[Index].Secondary : Bindings[Index].Primary;
	const FKey Old = Slot;
	// 同じキーを使っている場所があれば、そこには今のキーを渡す（入れ替え。同じキーが 2 つの操作に付かないように）
	for (FKakurenboKeyBinding& Other : Bindings)
	{
		for (FKey* OtherSlot : { &Other.Primary, &Other.Secondary })
		{
			if (OtherSlot != &Slot && *OtherSlot == Key)
			{
				*OtherSlot = Old;
			}
		}
	}
	Slot = Key;
	// 1 つ目が空になったら 2 つ目を繰り上げる
	for (FKakurenboKeyBinding& Binding : Bindings)
	{
		if (!Binding.Primary.IsValid() && Binding.Secondary.IsValid())
		{
			Swap(Binding.Primary, Binding.Secondary);
		}
	}
	return true;
}

void FKakurenboKeyMap::ClearSecondary(EKakurenboAction Action)
{
	const int32 Index = static_cast<int32>(Action);
	if (Bindings.IsValidIndex(Index))
	{
		Bindings[Index].Secondary = EKeys::Invalid;
	}
}

FString FKakurenboKeyMap::Label(EKakurenboAction Action) const
{
	const FKey A = Get(Action, false);
	const FKey B = Get(Action, true);
	if (A.IsValid() && B.IsValid())
	{
		return UKakurenboSettings::KeyLabel(A) + TEXT(" / ") + UKakurenboSettings::KeyLabel(B);
	}
	return UKakurenboSettings::KeyLabel(A.IsValid() ? A : B);
}

void UKakurenboSettings::ResetBindings()
{
	Keys.Reset();
}

FKey UKakurenboSettings::GetKey(EKakurenboAction Action, bool bSecondary) const
{
	return Keys.Get(Action, bSecondary);
}

bool UKakurenboSettings::IsDown(const APlayerController* PC, EKakurenboAction Action) const
{
	if (!PC)
	{
		return false;
	}
	const FKey A = GetKey(Action, false);
	const FKey B = GetKey(Action, true);
	const FKey Pad = GamepadKey(Action);
	return (A.IsValid() && PC->IsInputKeyDown(A)) || (B.IsValid() && PC->IsInputKeyDown(B)) || (Pad.IsValid() && PC->IsInputKeyDown(Pad));
}

bool UKakurenboSettings::WasPressed(const APlayerController* PC, EKakurenboAction Action) const
{
	if (!PC)
	{
		return false;
	}
	const FKey A = GetKey(Action, false);
	const FKey B = GetKey(Action, true);
	const FKey Pad = GamepadKey(Action);
	return (A.IsValid() && PC->WasInputKeyJustPressed(A)) || (B.IsValid() && PC->WasInputKeyJustPressed(B)) || (Pad.IsValid() && PC->WasInputKeyJustPressed(Pad));
}

bool UKakurenboSettings::CanBindKey(const FKey& Key)
{
	// コントローラーのボタンは決まっているので、キーボード・マウスの欄には付けない
	if (!Key.IsValid() || Key.IsAxis1D() || Key.IsAxis2D() || Key.IsAxis3D() || Key.IsTouch() || Key.IsGesture() || Key.IsGamepadKey())
	{
		return false;
	}
	// Esc はメニュー（取り消し）、ホイールはズーム、~ はコンソールに使う
	return Key != EKeys::Escape && Key != EKeys::MouseScrollUp && Key != EKeys::MouseScrollDown && Key != EKeys::Tilde && Key != EKeys::AnyKey;
}

bool UKakurenboSettings::Bind(EKakurenboAction Action, const FKey& Key, bool bSecondary)
{
	return Keys.Bind(Action, Key, bSecondary);
}

void UKakurenboSettings::ClearSecondary(EKakurenboAction Action)
{
	Keys.ClearSecondary(Action);
}

FString UKakurenboSettings::KeyLabel(const FKey& Key)
{
	if (!Key.IsValid())
	{
		return TEXT("－");
	}
	struct FNamed { FKey Key; const TCHAR* Label; };
	static const FNamed Names[] = {
		{ EKeys::LeftMouseButton, TEXT("左クリック") },
		{ EKeys::RightMouseButton, TEXT("右クリック") },
		{ EKeys::MiddleMouseButton, TEXT("ホイール押し") },
		{ EKeys::ThumbMouseButton, TEXT("マウス 4") },
		{ EKeys::ThumbMouseButton2, TEXT("マウス 5") },
		{ EKeys::SpaceBar, TEXT("Space") },
		{ EKeys::LeftShift, TEXT("Shift") },
		{ EKeys::RightShift, TEXT("右 Shift") },
		{ EKeys::LeftControl, TEXT("Ctrl") },
		{ EKeys::RightControl, TEXT("右 Ctrl") },
		{ EKeys::LeftAlt, TEXT("Alt") },
		{ EKeys::RightAlt, TEXT("右 Alt") },
		{ EKeys::Enter, TEXT("Enter") },
		{ EKeys::BackSpace, TEXT("BackSpace") },
		{ EKeys::Delete, TEXT("Delete") },
		{ EKeys::Tab, TEXT("Tab") },
		{ EKeys::Up, TEXT("↑") },
		{ EKeys::Down, TEXT("↓") },
		{ EKeys::Left, TEXT("←") },
		{ EKeys::Right, TEXT("→") },
		// コントローラー（Xbox の並び）
		{ EKeys::Gamepad_FaceButton_Bottom, TEXT("A") },
		{ EKeys::Gamepad_FaceButton_Right, TEXT("B") },
		{ EKeys::Gamepad_FaceButton_Left, TEXT("X") },
		{ EKeys::Gamepad_FaceButton_Top, TEXT("Y") },
		{ EKeys::Gamepad_LeftShoulder, TEXT("LB") },
		{ EKeys::Gamepad_RightShoulder, TEXT("RB") },
		{ EKeys::Gamepad_LeftTrigger, TEXT("LT") },
		{ EKeys::Gamepad_RightTrigger, TEXT("RT") },
		{ EKeys::Gamepad_LeftThumbstick, TEXT("L 押し込み") },
		{ EKeys::Gamepad_RightThumbstick, TEXT("R 押し込み") },
		{ EKeys::Gamepad_Special_Left, TEXT("BACK") },
		{ EKeys::Gamepad_Special_Right, TEXT("START") },
		{ EKeys::Gamepad_DPad_Up, TEXT("十字↑") },
		{ EKeys::Gamepad_DPad_Down, TEXT("十字↓") },
		{ EKeys::Gamepad_DPad_Left, TEXT("十字←") },
		{ EKeys::Gamepad_DPad_Right, TEXT("十字→") },
	};
	for (const FNamed& Named : Names)
	{
		if (Named.Key == Key)
		{
			return Named.Label;
		}
	}
	return Key.GetDisplayName(false).ToString();
}

FString UKakurenboSettings::ActionKeyLabel(EKakurenboAction Action) const
{
	return Keys.Label(Action);
}

FText UKakurenboSettings::ActionDisplayName(EKakurenboAction Action)
{
	switch (Action)
	{
	case EKakurenboAction::MoveForward: return NSLOCTEXT("Kakurenbo", "ActForward", "前へ");
	case EKakurenboAction::MoveBack:    return NSLOCTEXT("Kakurenbo", "ActBack", "後ろへ");
	case EKakurenboAction::MoveLeft:    return NSLOCTEXT("Kakurenbo", "ActLeft", "左へ");
	case EKakurenboAction::MoveRight:   return NSLOCTEXT("Kakurenbo", "ActRight", "右へ");
	case EKakurenboAction::Mash:        return NSLOCTEXT("Kakurenbo", "ActMash", "連打（コインを稼ぐ）");
	case EKakurenboAction::Sneak:       return NSLOCTEXT("Kakurenbo", "ActSneak", "しゃがむ・しのび足");
	case EKakurenboAction::Dash:        return NSLOCTEXT("Kakurenbo", "ActDash", "煙幕ダッシュ");
	case EKakurenboAction::Climb:       return NSLOCTEXT("Kakurenbo", "ActClimb", "よじ登る");
	case EKakurenboAction::CameraLeft:  return NSLOCTEXT("Kakurenbo", "ActCamLeft", "カメラを左に回す");
	case EKakurenboAction::CameraRight: return NSLOCTEXT("Kakurenbo", "ActCamRight", "カメラを右に回す");
	case EKakurenboAction::TiltDown:    return NSLOCTEXT("Kakurenbo", "ActTiltDown", "設置：カメラを傾ける");
	case EKakurenboAction::TiltUp:      return NSLOCTEXT("Kakurenbo", "ActTiltUp", "設置：真上に戻す");
	case EKakurenboAction::Next:        return NSLOCTEXT("Kakurenbo", "ActNext", "次へ進む");
	case EKakurenboAction::SetStart:    return NSLOCTEXT("Kakurenbo", "ActStart", "設置：スタート位置");
	case EKakurenboAction::BackToShop:  return NSLOCTEXT("Kakurenbo", "ActBackShop", "設置：購入へ戻る");
	case EKakurenboAction::ClearAll:    return NSLOCTEXT("Kakurenbo", "ActClear", "設置：全部回収");
	case EKakurenboAction::SwitchShop:  return NSLOCTEXT("Kakurenbo", "ActSwitchShop", "購入：お店の切り替え");
	case EKakurenboAction::Refill:      return NSLOCTEXT("Kakurenbo", "ActRefill", "購入：まとめて補充");
	case EKakurenboAction::Prestige:    return NSLOCTEXT("Kakurenbo", "ActPrestige", "購入：転生");
	case EKakurenboAction::Help:        return NSLOCTEXT("Kakurenbo", "ActHelp", "操作説明");
	default:                            return FText::GetEmpty();
	}
}

// ---------------------------------------------------------------- 操作のヒント

void UKakurenboSettings::MarkTipSeen(FName Tip)
{
	if (!SeenTips.Contains(Tip))
	{
		SeenTips.Add(Tip);
		Save();
	}
}

void UKakurenboSettings::ResetTips()
{
	SeenTips.Reset();
	Save();
}
