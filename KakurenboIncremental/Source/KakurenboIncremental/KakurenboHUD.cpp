#include "KakurenboHUD.h"

#include "CanvasItem.h"
#include "Engine/Canvas.h"
#include "Engine/Font.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "GlobalRenderResources.h"
#include "HiderCharacter.h"
#include "KakurenboArena.h"
#include "KakurenboGameMode.h"
#include "KakurenboGameState.h"
#include "KakurenboGridSubsystem.h"
#include "KakurenboLibrary.h"
#include "KakurenboPlayerController.h"
#include "KakurenboSettings.h"
#include "OniCharacter.h"
#include "Rendering/SlateRenderer.h"
#include "TreasureActor.h"

namespace
{
	FString Big(double V) { return UKakurenboLibrary::FormatBigNumber(V); }
	/** 収入・耐久など、小数に意味がある値 */
	FString Stat(double V) { return UKakurenboLibrary::FormatStatNumber(V); }

	const FLinearColor Gold(1.f, 0.85f, 0.2f);
	const FLinearColor Gray(0.6f, 0.6f, 0.6f);
	const FLinearColor Good(0.4f, 1.f, 0.5f);
	const FLinearColor Bad(1.f, 0.35f, 0.3f);
	const FLinearColor Warn(1.f, 0.6f, 0.2f);
	const FLinearColor Purple(0.8f, 0.6f, 1.f);
	const FLinearColor Blue(0.4f, 0.7f, 1.f);
	const FLinearColor PaleText(0.85f, 0.82f, 0.92f);

	/** 左上の状態表示の下に出すパネルの上端（1080p 基準） */
	constexpr float BelowStatusY = 192.f;

	/** 鬼の様子（短く、雰囲気に合う言葉で）。壁を壊している最中はそれを優先して出す */
	void DescribeOni(const AOniCharacter* Oni, FString& OutText, FLinearColor& OutColor)
	{
		if (Oni->GetOniState() == EOniState::Attack)
		{
			OutText = TEXT("壁をこわしている");
			OutColor = Warn;
			return;
		}
		if (Oni->OniType == EOniType::Detector && Oni->GetIntent() == EOniState::Wander)
		{
			const bool bJustCalled = Oni->GetLastSummonTime() >= 0.f && Oni->GetWorld()->GetTimeSeconds() - Oni->GetLastSummonTime() < 3.f;
			OutText = bJustCalled ? TEXT("仲間を呼んだ！") : Oni->IsAtGuardPost() ? TEXT("見張っている") : TEXT("見張りへ向かう");
			OutColor = bJustCalled ? Bad : Purple;
			return;
		}
		if (Oni->OniType == EOniType::Treasure && Oni->GetIntent() == EOniState::Wander && Oni->IsPatrollingTreasure())
		{
			OutText = TEXT("お宝を見まわっている");
			OutColor = Gold;
			return;
		}
		switch (Oni->GetIntent())
		{
		case EOniState::Chase:       OutText = TEXT("追ってくる！"); OutColor = Bad; break;
		case EOniState::Investigate: OutText = TEXT("音のほうへ…"); OutColor = Warn; break;
		case EOniState::Inspect:     OutText = TEXT("あやしんでいる"); OutColor = Warn; break;
		case EOniState::Stunned:     OutText = TEXT("動けない"); OutColor = FLinearColor(0.6f, 1.f, 0.4f); break;
		default:                     OutText = TEXT("さまよっている"); OutColor = FLinearColor(0.85f, 0.85f, 0.9f); break;
		}
	}

	/** 種類名（CSV の DisplayName。無ければ「鬼」） */
	FString OniName(const AOniCharacter* Oni)
	{
		return Oni->TypeDisplayName.IsEmpty() ? FString(TEXT("鬼")) : Oni->TypeDisplayName.ToString();
	}

	/** 操作のキーを [ ] で囲む（RichText でキーの形になる） */
	FString KeyOf(const AKakurenboPlayerController* PC, EKakurenboAction Action)
	{
		return PC ? FString::Printf(TEXT("[%s]"), *PC->KeyName(Action)) : FString();
	}
}

void AKakurenboHUD::DrawHUD()
{
	Super::DrawHUD();

	LastFrameTexts = MoveTemp(DrawnTexts);
	DrawnTexts.Reset();
	if (!Canvas)
	{
		return;
	}
	UIScale = Canvas->ClipY / 1080.f;
	Buttons.Reset(); // ボタンは毎フレーム描き直す

	AKakurenboGameState* State = GetWorld()->GetGameState<AKakurenboGameState>();
	AKakurenboGameMode* GM = GetWorld()->GetAuthGameMode<AKakurenboGameMode>();
	const AKakurenboPlayerController* PC = Cast<AKakurenboPlayerController>(GetOwningPlayerController());
	if (!State || !GM)
	{
		return;
	}

	if (State->Phase == EKakurenboPhase::Title)
	{
		// ゲーム中の大きな文字・連打の「+〇」は持ち越さない
		Popups.Reset();
		MashPopups.Reset();
		DrawTitle(State, GM);
	}
	else
	{
		DrawStatusPanel(State, GM);
		switch (State->Phase)
		{
		case EKakurenboPhase::Shop:   DrawShop(State, GM); break;
		case EKakurenboPhase::Build:  DrawBuild(State, GM); break;
		case EKakurenboPhase::Hide:   DrawHide(State, GM); break;
		case EKakurenboPhase::Result: DrawResult(State, GM); break;
		default: break;
		}
		DrawCornerButtons(State->Phase);
	}

	// 画面上部のお知らせ（一番上に重ねて描く）
	if (!State->NoticeText.IsEmpty() && GetWorld()->GetTimeSeconds() < State->NoticeUntilTime)
	{
		const float CanvasW = Canvas->ClipX / UIScale;
		Panel(CanvasW * 0.5f - 360, 116, 720, 44, FLinearColor(0.08f, 0.04f, 0.14f, 0.85f));
		Frame(CanvasW * 0.5f - 360, 116, 720, 44, FLinearColor(0.7f, 0.55f, 1.f, 0.5f), 1.f);
		Text(State->NoticeText.ToString(), 0, 122, 22, FLinearColor::White, true);
	}

	DrawTip();
	DrawPopupsAndFlash();

	// メニューは一番上。後ろのボタンは押せないようにする
	if (PC && PC->IsMenuOpen())
	{
		Buttons.Reset();
		DimScreen(0.6f);
		DrawMenu(GM);
	}
}

void AKakurenboHUD::Flash(const FLinearColor& Color, float Duration)
{
	FlashColor = Color;
	FlashDuration = FMath::Max(0.05f, Duration);
	FlashStartTime = GetWorld()->GetTimeSeconds();
}

float AKakurenboHUD::GetFlashAlpha() const
{
	const float T = (GetWorld()->GetTimeSeconds() - FlashStartTime) / FMath::Max(FlashDuration, 0.05f);
	return (T >= 0.f && T < 1.f) ? FlashColor.A * (1.f - T) : 0.f;
}

void AKakurenboHUD::AddPopup(const FString& Text, const FLinearColor& Color)
{
	FBigPopup& Popup = Popups.AddDefaulted_GetRef();
	Popup.Text = Text;
	Popup.Color = Color;
	if (Popups.Num() > 4)
	{
		Popups.RemoveAt(0);
	}
}

bool AKakurenboHUD::HasDrawnText(const FString& Part) const
{
	for (const FString& Drawn : LastFrameTexts)
	{
		if (Drawn.Contains(Part))
		{
			return true;
		}
	}
	return false;
}

void AKakurenboHUD::DrawPopupsAndFlash()
{
	// 大きな文字：ふわっと上がりながら消える。新しいものほど下に出す
	constexpr float Lifetime = 1.4f;
	for (int32 i = Popups.Num() - 1; i >= 0; --i)
	{
		FBigPopup& Popup = Popups[i];
		Popup.Age += RenderDelta;
		if (Popup.Age >= Lifetime)
		{
			Popups.RemoveAt(i);
		}
	}
	for (int32 i = 0; i < Popups.Num(); ++i)
	{
		const FBigPopup& Popup = Popups[i];
		const float T = Popup.Age / Lifetime;
		const float Alpha = T < 0.7f ? 1.f : 1.f - (T - 0.7f) / 0.3f;
		const float Y = 250.f + (Popups.Num() - 1 - i) * -46.f - 40.f * T;
		FCanvasTextItem Item(FVector2D(Canvas->ClipX * 0.5f, Y * UIScale), FText::FromString(Popup.Text), MakeFont(36),
			FLinearColor(Popup.Color.R, Popup.Color.G, Popup.Color.B, Alpha));
		Item.bCentreX = true;
		Item.EnableShadow(FLinearColor(0.f, 0.f, 0.f, 0.8f * Alpha));
		Canvas->DrawItem(Item);
		DrawnTexts.Add(Popup.Text);
	}

	// 画面全体の点滅（見つかった: 赤 / 逃げ切った: 金）
	const float FlashAlpha = GetFlashAlpha();
	if (FlashAlpha > 0.f)
	{
		FCanvasTileItem Tile(FVector2D::ZeroVector, FVector2D(Canvas->ClipX, Canvas->ClipY), FLinearColor(FlashColor.R, FlashColor.G, FlashColor.B, FlashAlpha));
		Tile.BlendMode = SE_BLEND_Translucent;
		Canvas->DrawItem(Tile);
	}
}

// ---------------------------------------------------------------- 描画の補助

FSlateFontInfo AKakurenboHUD::MakeFont(int32 Size)
{
	// エンジン付属の Roboto は日本語用のフォールバック（DroidSansFallback）を持っているので文字化けしない。
	// Canvas は UFont オブジェクトが必須なので、FontObject を指定した FSlateFontInfo を使う
	if (!HUDFont)
	{
		HUDFont = LoadObject<UFont>(nullptr, TEXT("/Engine/EngineFonts/Roboto.Roboto"));
	}
	return FSlateFontInfo(HUDFont, FMath::Max(1, FMath::RoundToInt(Size * UIScale)));
}

float AKakurenboHUD::MeasureText(const FString& Str, int32 Size)
{
	if (Str.IsEmpty() || !FSlateApplication::IsInitialized() || !FSlateApplication::Get().GetRenderer())
	{
		return Str.Len() * Size * 0.6f;
	}
	// Slate の文字の測り方（Canvas の文字と同じ大きさで測れる）。ピクセルを 1080p 基準に戻す
	const TSharedRef<FSlateFontMeasure> Measure = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
	return Measure->Measure(Str, MakeFont(Size)).X / FMath::Max(UIScale, 0.01f);
}

void AKakurenboHUD::Text(const FString& Str, float X, float Y, int32 Size, const FLinearColor& Color, bool bCenterX)
{
	TextPx(Str, bCenterX ? Canvas->ClipX * 0.5f : X * UIScale, Y * UIScale, Size, Color, bCenterX);
}

void AKakurenboHUD::TextPx(const FString& Str, float Px, float Py, int32 Size, const FLinearColor& Color, bool bCenterX)
{
	FCanvasTextItem Item(FVector2D(Px, Py), FText::FromString(Str), MakeFont(Size), Color);
	Item.EnableShadow(FLinearColor(0.f, 0.f, 0.f, 0.8f * Color.A));
	Item.bCentreX = bCenterX;
	Canvas->DrawItem(Item);
	DrawnTexts.Add(Str);
}

void AKakurenboHUD::Panel(float X, float Y, float W, float H, const FLinearColor& Color)
{
	FCanvasTileItem Tile(FVector2D(X * UIScale, Y * UIScale), FVector2D(W * UIScale, H * UIScale), Color);
	Tile.BlendMode = SE_BLEND_Translucent;
	Canvas->DrawItem(Tile);
}

void AKakurenboHUD::Frame(float X, float Y, float W, float H, const FLinearColor& Color, float Thickness)
{
	Panel(X, Y, W, Thickness, Color);
	Panel(X, Y + H - Thickness, W, Thickness, Color);
	Panel(X, Y + Thickness, Thickness, H - Thickness * 2.f, Color);
	Panel(X + W - Thickness, Y + Thickness, Thickness, H - Thickness * 2.f, Color);
}

void AKakurenboHUD::DimScreen(float Alpha)
{
	FCanvasTileItem Tile(FVector2D::ZeroVector, FVector2D(Canvas->ClipX, Canvas->ClipY), FLinearColor(0.01f, 0.f, 0.03f, Alpha));
	Tile.BlendMode = SE_BLEND_Translucent;
	Canvas->DrawItem(Tile);
}

float AKakurenboHUD::KeyCap(const FString& Label, float X, float Y, int32 Size, float Alpha)
{
	// 濃い紫の小さな板に明るい縁：キーボードのキーに見える形
	const float Pad = Size * 0.4f;
	const float W = MeasureText(Label, Size) + Pad * 2.f;
	const float H = Size * 1.45f;
	const float Top = Y - Size * 0.12f;
	Panel(X, Top, W, H, FLinearColor(0.16f, 0.1f, 0.24f, 0.95f * Alpha));
	Frame(X, Top, W, H, FLinearColor(0.85f, 0.8f, 1.f, 0.75f * Alpha), FMath::Max(1.f, Size * 0.08f));
	Panel(X + 2.f, Top + H - Size * 0.16f, W - 4.f, Size * 0.1f, FLinearColor(0.f, 0.f, 0.f, 0.35f * Alpha)); // 下の影
	TextPx(Label, (X + Pad) * UIScale, Y * UIScale, Size, FLinearColor(1.f, 1.f, 1.f, Alpha), false);
	return W;
}

float AKakurenboHUD::RichText(const FString& Str, float X, float Y, int32 Size, const FLinearColor& Color, bool bCenterX, float Alpha)
{
	// 「[キー] 説明」を、文とキーの形に分けて左から並べる
	struct FPart { FString Text; bool bKey = false; };
	TArray<FPart> Parts;
	FString Current;
	bool bInKey = false;
	for (const TCHAR C : Str)
	{
		if (C == TEXT('[') && !bInKey)
		{
			if (!Current.IsEmpty()) { Parts.Add({ Current, false }); }
			Current.Reset();
			bInKey = true;
		}
		else if (C == TEXT(']') && bInKey)
		{
			Parts.Add({ Current, true });
			Current.Reset();
			bInKey = false;
		}
		else
		{
			Current.AppendChar(C);
		}
	}
	if (!Current.IsEmpty())
	{
		Parts.Add({ bInKey ? TEXT("[") + Current : Current, false });
	}

	const float KeyGap = Size * 0.15f;
	const float Pad = Size * 0.4f;
	float Total = 0.f;
	for (const FPart& Part : Parts)
	{
		Total += Part.bKey ? MeasureText(Part.Text, Size) + Pad * 2.f + KeyGap : MeasureText(Part.Text, Size);
	}
	float Cursor = X;
	if (bCenterX)
	{
		const float Mid = X < 0.f ? Canvas->ClipX / UIScale * 0.5f : X;
		Cursor = Mid - Total * 0.5f;
	}
	for (const FPart& Part : Parts)
	{
		if (Part.bKey)
		{
			Cursor += KeyCap(Part.Text, Cursor, Y, Size, Alpha) + KeyGap;
		}
		else
		{
			TextPx(Part.Text, Cursor * UIScale, Y * UIScale, Size, FLinearColor(Color.R, Color.G, Color.B, Color.A * Alpha), false);
			Cursor += MeasureText(Part.Text, Size);
		}
	}
	DrawnTexts.Add(Str);
	return Total;
}

void AKakurenboHUD::AddButton(float X, float Y, float W, float H, EKakurenboUIAction Action, int32 Index)
{
	FKakurenboUIButton& Button = Buttons.AddDefaulted_GetRef();
	Button.Rect = FBox2D(FVector2D(X, Y) * UIScale, FVector2D(X + W, Y + H) * UIScale);
	Button.Action = Action;
	Button.Index = Index;
}

bool AKakurenboHUD::IsCursorOver(float X, float Y, float W, float H) const
{
	const AKakurenboPlayerController* PC = Cast<AKakurenboPlayerController>(GetOwningPlayerController());
	FVector2D Cursor;
	return PC && PC->GetUICursorPosition(Cursor) && FBox2D(FVector2D(X, Y) * UIScale, FVector2D(X + W, Y + H) * UIScale).IsInside(Cursor);
}

void AKakurenboHUD::DrawButton(float X, float Y, float W, float H, const FString& Label, EKakurenboUIAction Action, int32 Index,
	bool bSelected, int32 Size, const FLinearColor& TextColor)
{
	AddButton(X, Y, W, H, Action, Index);
	const bool bHovered = IsCursorOver(X, Y, W, H);
	const FLinearColor Back = bSelected ? FLinearColor(1.f, 0.7f, 0.2f, 0.8f)
		: bHovered ? FLinearColor(0.45f, 0.25f, 0.6f, 0.9f) : FLinearColor(0.12f, 0.05f, 0.2f, 0.82f);
	Panel(X, Y, W, H, Back);
	Frame(X, Y, W, H, bHovered ? FLinearColor(1.f, 1.f, 1.f, 0.7f) : FLinearColor(0.7f, 0.55f, 1.f, 0.35f), bHovered ? 2.f : 1.f);
	RichText(Label, X + W * 0.5f, Y + (H - Size * 1.2f) * 0.5f, Size, TextColor, true);
}

bool AKakurenboHUD::FindButtonAt(const FVector2D& ScreenPx, FKakurenboUIButton& OutButton) const
{
	// 後から描いたもの（上に重なっているもの）を優先する
	for (int32 i = Buttons.Num() - 1; i >= 0; --i)
	{
		if (Buttons[i].Rect.IsInside(ScreenPx))
		{
			OutButton = Buttons[i];
			return true;
		}
	}
	return false;
}

bool AKakurenboHUD::GetButtonCenter(EKakurenboUIAction Action, int32 Index, FVector2D& OutScreenPx) const
{
	for (const FKakurenboUIButton& Button : Buttons)
	{
		if (Button.Action == Action && Button.Index == Index)
		{
			OutScreenPx = Button.Rect.GetCenter();
			return true;
		}
	}
	return false;
}

void AKakurenboHUD::TrianglePx(const FVector2D& A, const FVector2D& B, const FVector2D& C, const FLinearColor& Color)
{
	FCanvasTriangleItem Triangle(A, B, C, GWhiteTexture);
	Triangle.SetColor(Color);
	Triangle.BlendMode = SE_BLEND_Translucent;
	Canvas->DrawItem(Triangle);
}

void AKakurenboHUD::DrawSlider(float X, float Y, float W, float Value01, int32 Index, const FString& ValueText)
{
	// 押せる範囲は線の幅ちょうど（押した横の位置が値になる）
	AddButton(X, Y, W, 46, EKakurenboUIAction::Slider, Index);
	const bool bHovered = IsCursorOver(X, Y, W, 46);
	const float V = FMath::Clamp(Value01, 0.f, 1.f);
	Panel(X, Y + 18, W, 10, FLinearColor(0.f, 0.f, 0.f, 0.6f));
	Panel(X, Y + 18, W * V, 10, FLinearColor(0.75f, 0.5f, 1.f, 0.95f));
	Panel(X + W * V - 9, Y + 6, 18, 34, bHovered ? FLinearColor(1.f, 1.f, 1.f, 1.f) : FLinearColor(0.9f, 0.85f, 1.f, 0.95f));
	Text(ValueText, X + W + 24, Y + 8, 20, FLinearColor::White);
}

void AKakurenboHUD::DrawToggle(float X, float Y, float W, bool bOn, int32 Index, const FString& OnText, const FString& OffText)
{
	DrawButton(X, Y, W, 44, bOn ? OnText : OffText, EKakurenboUIAction::Toggle, Index, bOn, 20, bOn ? FLinearColor::Black : FLinearColor::White);
}

void AKakurenboHUD::DrawWorldIndicator(const FVector& WorldLocation, const FString& Label, const FLinearColor& Color, float ArrowRadiusRatio, float Scale)
{
	APlayerController* PC = GetOwningPlayerController();
	if (!PC)
	{
		return;
	}
	const float W = Canvas->ClipX;
	const float H = Canvas->ClipY;
	const float S = UIScale * Scale;

	// 画面内に見えていれば、その上に ▼ を出す（壁の向こうにあっても表示する）
	FVector2D Screen;
	const float Margin = 60.f * UIScale;
	if (PC->ProjectWorldLocationToScreen(WorldLocation, Screen, true)
		&& Screen.X > Margin && Screen.X < W - Margin && Screen.Y > Margin && Screen.Y < H - Margin)
	{
		TrianglePx(Screen + FVector2D(-14.f, -30.f) * S, Screen + FVector2D(14.f, -30.f) * S, Screen + FVector2D(0.f, -6.f) * S, Color);
		TextPx(Label, Screen.X, Screen.Y - 62.f * S, 18, Color, true);
		return;
	}

	// 画面外なら、画面中央を囲む円の上に「その方向」を指す矢印を出す（上＝カメラの正面、下＝背後）
	FVector CamLoc;
	FRotator CamRot;
	PC->GetPlayerViewPoint(CamLoc, CamRot);
	const FVector ToTarget = WorldLocation - CamLoc;
	const float BearingDeg = FMath::RadiansToDegrees(FMath::Atan2(ToTarget.Y, ToTarget.X));
	const float RelRad = FMath::DegreesToRadians(FRotator::NormalizeAxis(BearingDeg - CamRot.Yaw));
	const FVector2D Dir(FMath::Sin(RelRad), -FMath::Cos(RelRad)); // 右＝時計回り
	const FVector2D Perp(-Dir.Y, Dir.X);
	const FVector2D Center(W * 0.5f, H * 0.5f);
	const float Radius = H * ArrowRadiusRatio;

	const FVector2D Base = Center + Dir * Radius;
	const FVector2D Tip = Center + Dir * (Radius + 34.f * S);
	TrianglePx(Tip, Base + Perp * 22.f * S, Base - Perp * 22.f * S, Color);
	const FVector2D LabelPos = Center + Dir * (Radius - 34.f * S);
	TextPx(Label, LabelPos.X, LabelPos.Y - 12.f * S, 18, Color, true);
}

void AKakurenboHUD::DrawWorldLabel(const FVector& WorldLocation, const FString& Label, const FLinearColor& Color, float OffsetY)
{
	APlayerController* PC = GetOwningPlayerController();
	FVector2D Screen;
	if (PC && PC->ProjectWorldLocationToScreen(WorldLocation, Screen, true))
	{
		TextPx(Label, Screen.X, Screen.Y + (OffsetY - 12.f) * UIScale, 18, Color, true);
	}
}

// ---------------------------------------------------------------- 共通

void AKakurenboHUD::DrawStatusPanel(AKakurenboGameState* State, AKakurenboGameMode* GM)
{
	const FText MapName = GM->GetCurrentMapDisplayName();
	const FString MapText = MapName.IsEmpty() ? FString() : FString::Printf(TEXT("　%s"), *MapName.ToString());

	// かくれんぼ中は小さく（コインとこのラウンドの稼ぎだけ）
	if (State->Phase == EKakurenboPhase::Hide)
	{
		Panel(20, 20, 360, 112, FLinearColor(0.03f, 0.01f, 0.06f, 0.55f));
		Text(FString::Printf(TEXT("ステージ %d%s"), State->Stage, *MapText), 36, 28, 18, FLinearColor(1.f, 0.75f, 0.9f));
		Text(FString::Printf(TEXT("コイン  %s"), *Big(State->Coins)), 36, 54, 28, Gold);
		Text(FString::Printf(TEXT("このラウンド +%s"), *Big(State->CoinsEarnedThisRound)), 36, 96, 16, Gray);
		return;
	}

	static const TCHAR* PhaseNames[] = { TEXT("購入"), TEXT("設置"), TEXT("かくれんぼ"), TEXT("結果"), TEXT("") };
	const int32 PhaseIndex = FMath::Clamp(static_cast<int32>(State->Phase), 0, UE_ARRAY_COUNT(PhaseNames) - 1);
	const bool bPrestiged = State->PrestigeCount > 0 || State->TotalPrestigePoints > 0;
	Panel(20, 20, 400, bPrestiged ? 158 : 130);
	Text(FString::Printf(TEXT("ステージ %d ・ %s"), State->Stage, PhaseNames[PhaseIndex]), 36, 28, 22);
	if (!MapName.IsEmpty())
	{
		Text(FString::Printf(TEXT("館：%s"), *MapName.ToString()), 280, 76, 16, FLinearColor(1.f, 0.7f, 0.9f));
	}
	Text(FString::Printf(TEXT("コイン  %s"), *Big(State->Coins)), 36, 64, 30, Gold);
	Text(FString::Printf(TEXT("連打 +%s / 時間 +%s/秒"), *Stat(GM->GetMashIncome()), *Stat(GM->GetTimeIncomePerSecond())), 36, 108, 18, Gray);
	if (bPrestiged)
	{
		Text(FString::Printf(TEXT("転生 %d 回・転生ポイント %d pt"), State->PrestigeCount, State->PrestigePoints), 36, 134, 18, Purple);
	}
}

void AKakurenboHUD::DrawCornerButtons(EKakurenboPhase Phase)
{
	const float CanvasW = Canvas->ClipX / UIScale;
	const AKakurenboPlayerController* PC = Cast<AKakurenboPlayerController>(GetOwningPlayerController());
	if (Phase == EKakurenboPhase::Hide)
	{
		// かくれんぼ中はカーソルが無いので、キーだけ小さく
		const UKakurenboSettings* S = UKakurenboSettings::Get(this);
		if (!S || S->bShowHints)
		{
			RichText(FString::Printf(TEXT("[Esc] メニュー　%s 操作説明"), *KeyOf(PC, EKakurenboAction::Help)), CanvasW - 330, 24, 16, PaleText, false, 0.75f);
		}
		return;
	}
	// 右上：操作説明・メニュー（クリックでも開ける）
	DrawButton(CanvasW - 470, 20, 220, 46, FString::Printf(TEXT("操作説明 %s"), *KeyOf(PC, EKakurenboAction::Help)), EKakurenboUIAction::OpenHelp, 0, false, 19);
	DrawButton(CanvasW - 240, 20, 220, 46, TEXT("メニュー [Esc]"), EKakurenboUIAction::OpenMenu, 0, false, 19);
}

void AKakurenboHUD::DrawTip()
{
	const AKakurenboPlayerController* PC = Cast<AKakurenboPlayerController>(GetOwningPlayerController());
	const FKakurenboTip* Tip = PC ? PC->GetCurrentTip() : nullptr;
	if (!Tip || (PC && PC->IsMenuOpen()))
	{
		return;
	}
	// ふわっと出て、しばらくしたら消える
	const float FadeIn = FMath::Clamp(Tip->Age / 0.4f, 0.f, 1.f);
	const float FadeOut = FMath::Clamp((PC->TipSeconds - Tip->Age) / 0.6f, 0.f, 1.f);
	const float Alpha = FMath::Min(FadeIn, FadeOut);
	const float CanvasW = Canvas->ClipX / UIScale;
	const float CanvasH = Canvas->ClipY / UIScale;
	// 購入・設置パートは画面の下が混んでいる（商品・置く物の欄とボタン）ので上に出す。お知らせが出ていればその下
	const AKakurenboGameState* State = GetWorld()->GetGameState<AKakurenboGameState>();
	const bool bTop = State && (State->Phase == EKakurenboPhase::Shop || State->Phase == EKakurenboPhase::Build);
	const bool bNotice = State && !State->NoticeText.IsEmpty() && GetWorld()->GetTimeSeconds() < State->NoticeUntilTime;
	const float BaseY = bTop ? (bNotice ? 182.f : 112.f) : CanvasH - 236.f;
	const float Y = BaseY + (1.f - FadeIn) * 12.f;
	const float W = FMath::Min(CanvasW - 80.f, MeasureText(Tip->Text, 22) + 320.f);
	Panel((CanvasW - W) * 0.5f, Y - 12, W, 56, FLinearColor(0.06f, 0.03f, 0.12f, 0.85f * Alpha));
	Panel((CanvasW - W) * 0.5f, Y - 12, 6, 56, FLinearColor(1.f, 0.8f, 0.3f, 0.9f * Alpha));
	Text(TEXT("ヒント"), (CanvasW - W) * 0.5f + 20, Y - 2, 16, FLinearColor(1.f, 0.8f, 0.3f, Alpha));
	RichText(Tip->Text, -1.f, Y, 22, FLinearColor::White, true, Alpha);
}

TArray<FString> AKakurenboHUD::GetHelpLines(EKakurenboPhase Phase) const
{
	const AKakurenboPlayerController* PC = Cast<AKakurenboPlayerController>(GetOwningPlayerController());
	const AKakurenboGameMode* GM = GetWorld()->GetAuthGameMode<AKakurenboGameMode>();
	auto K = [PC](EKakurenboAction A) { return KeyOf(PC, A); };
	const FString Move = K(EKakurenboAction::MoveForward) + K(EKakurenboAction::MoveLeft) + K(EKakurenboAction::MoveBack) + K(EKakurenboAction::MoveRight);
	TArray<FString> Lines;
	switch (Phase)
	{
	case EKakurenboPhase::Hide:
		Lines.Add(FString::Printf(TEXT("%s 歩く（カメラの向きに）　マウス 見回す　ホイール カメラの距離"), *Move));
		Lines.Add(FString::Printf(TEXT("%s 連打：コインを稼ぐ（音が出て、鬼が寄ってくる）"), *K(EKakurenboAction::Mash)));
		Lines.Add(FString::Printf(TEXT("%s 押している間しゃがむ：足音が小さく、低い家具の陰に隠れられる"), *K(EKakurenboAction::Sneak)));
		Lines.Add(FString::Printf(TEXT("%s 煙幕ダッシュ：煙の向こうは鬼から見えない%s"), *K(EKakurenboAction::Dash),
			GM && GM->IsDashUnlocked() ? TEXT("") : TEXT("（転生のお店で解放）")));
		Lines.Add(FString::Printf(TEXT("%s ジャンプ：壁 1 段に飛び乗れる%s"), *K(EKakurenboAction::Jump), GM && GM->IsJumpUnlocked() ? TEXT("") : TEXT("（転生のお店で解放）")));
		Lines.Add(TEXT("鬼にさわられたら見つかる。時間まで逃げきれば大きな報酬"));
		Lines.Add(TEXT("鬼の足音（どたどた）とお宝のキラキラは、音の左右と大きさで場所がわかる"));
		break;
	case EKakurenboPhase::Shop:
		Lines.Add(TEXT("クリック・[1]〜[8] 商品を買う（壁と罠は、次の設置で置く）"));
		Lines.Add(FString::Printf(TEXT("%s コインのお店 ↔ 転生のお店"), *K(EKakurenboAction::SwitchShop)));
		Lines.Add(FString::Printf(TEXT("%s 壊れた壁・使った罠をまとめて補充"), *K(EKakurenboAction::Refill)));
		Lines.Add(FString::Printf(TEXT("%s を 2 回 転生（ステージ %d から）"), *K(EKakurenboAction::Prestige), GM ? GM->PrestigeSettings.MinStage : 5));
		Lines.Add(FString::Printf(TEXT("%s 設置へ"), *K(EKakurenboAction::Next)));
		Lines.Add(FString::Printf(TEXT("%s%s カメラを回す　ホイール ズーム　ホイールを押してドラッグ 回す"), *K(EKakurenboAction::CameraLeft), *K(EKakurenboAction::CameraRight)));
		break;
	case EKakurenboPhase::Build:
		Lines.Add(TEXT("[左クリック] 置く　[右クリック] 回収　下の欄・[1]〜[6] 置く物を選ぶ"));
		Lines.Add(FString::Printf(TEXT("%s スタート位置（青い印）をカーソルのマスへ"), *K(EKakurenboAction::SetStart)));
		Lines.Add(FString::Printf(TEXT("%s カメラを動かす　ホイール ズーム"), *Move));
		Lines.Add(FString::Printf(TEXT("%s%s 回す　%s 傾ける　%s 真上に戻す（ホイールを押してドラッグでも）"),
			*K(EKakurenboAction::CameraLeft), *K(EKakurenboAction::CameraRight), *K(EKakurenboAction::TiltDown), *K(EKakurenboAction::TiltUp)));
		Lines.Add(FString::Printf(TEXT("%s を 2 回 全部回収　%s 購入へ戻る　%s かくれんぼ開始"),
			*K(EKakurenboAction::ClearAll), *K(EKakurenboAction::BackToShop), *K(EKakurenboAction::Next)));
		break;
	case EKakurenboPhase::Result:
		Lines.Add(FString::Printf(TEXT("%s・ボタン 次へ"), *K(EKakurenboAction::Next)));
		Lines.Add(FString::Printf(TEXT("%s%s カメラを回す　ホイール ズーム"), *K(EKakurenboAction::CameraLeft), *K(EKakurenboAction::CameraRight)));
		break;
	default:
		break;
	}
	Lines.Add(FString::Printf(TEXT("[Esc] メニュー（設定・キー設定・タイトルへ）　%s 操作説明"), *K(EKakurenboAction::Help)));
	return Lines;
}

// ---------------------------------------------------------------- タイトル

void AKakurenboHUD::DrawTitle(AKakurenboGameState* State, AKakurenboGameMode* GM)
{
	const AKakurenboPlayerController* PC = Cast<AKakurenboPlayerController>(GetOwningPlayerController());
	const float CanvasH = Canvas->ClipY / UIScale;

	// 左側を暗くして文字を読みやすく（右へ行くほど館が見える）
	for (int32 i = 0; i < 12; ++i)
	{
		Panel(i * 70.f, 0, 70, CanvasH, FLinearColor(0.02f, 0.f, 0.05f, 0.82f * (1.f - i / 12.f)));
	}

	// 題名
	Text(TEXT("かくれんぼ"), 110, 150, 100, FLinearColor(1.f, 0.88f, 0.95f));
	Text(TEXT("インクリメンタル"), 118, 290, 40, Gold);
	Text(TEXT("夜の館で、鬼から隠れて稼ぐ"), 120, 352, 22, PaleText);

	// メニュー
	struct FItem { EKakurenboTitleItem Item; FString Label; };
	const TArray<FItem> Items = {
		{ EKakurenboTitleItem::Continue, TEXT("つづきから") },
		{ EKakurenboTitleItem::NewGame, TEXT("はじめから") },
		{ EKakurenboTitleItem::Settings, TEXT("設定") },
		{ EKakurenboTitleItem::Quit, TEXT("やめる") },
	};
	float Y = 440.f;
	for (const FItem& Item : Items)
	{
		const int32 Index = static_cast<int32>(Item.Item);
		const bool bEnabled = PC && PC->IsTitleItemEnabled(Item.Item);
		const bool bSelected = PC && PC->GetTitleSelection() == Index && bEnabled;
		AddButton(110, Y, 460, 66, EKakurenboUIAction::TitleItem, Index);
		Panel(110, Y, 460, 66, bSelected ? FLinearColor(0.5f, 0.25f, 0.7f, 0.85f) : FLinearColor(0.1f, 0.04f, 0.16f, 0.7f));
		if (bSelected)
		{
			Panel(110, Y, 8, 66, Gold);
			Frame(110, Y, 460, 66, FLinearColor(1.f, 0.9f, 1.f, 0.7f), 2.f);
		}
		Text(Item.Label, 146, Y + 14, 30, bEnabled ? FLinearColor::White : FLinearColor(0.45f, 0.42f, 0.5f));
		if (Item.Item == EKakurenboTitleItem::Continue)
		{
			const FString Info = bEnabled
				? FString::Printf(TEXT("ステージ %d%s"), State->Stage, State->PrestigeCount > 0 ? *FString::Printf(TEXT("・転生 %d 回"), State->PrestigeCount) : TEXT(""))
				: FString(TEXT("セーブがありません"));
			Text(Info, 110 + 460 - 20 - MeasureText(Info, 18), Y + 24, 18, bEnabled ? Gold : FLinearColor(0.45f, 0.42f, 0.5f));
		}
		Y += 82.f;
	}
	RichText(TEXT("クリック・[↑][↓] と [Enter] で選ぶ"), 116, CanvasH - 70, 18, Gray);
}

// ---------------------------------------------------------------- メニュー

void AKakurenboHUD::DrawMenu(AKakurenboGameMode* GM)
{
	const AKakurenboPlayerController* PC = Cast<AKakurenboPlayerController>(GetOwningPlayerController());
	if (!PC)
	{
		return;
	}
	switch (PC->GetMenu())
	{
	case EKakurenboMenu::Pause:    DrawPauseMenu(GM); break;
	case EKakurenboMenu::Settings: DrawSettings(); break;
	case EKakurenboMenu::Keys:     DrawKeyConfig(); break;
	case EKakurenboMenu::Help:     DrawHelp(GM->GetPhase()); break;
	case EKakurenboMenu::ConfirmNewGame:
		DrawConfirm(TEXT("はじめから"), TEXT("セーブを消して、ステージ 1 からはじめます。よろしいですか？"), TEXT("はじめから [Enter]"), TEXT("やめる [Esc]"));
		break;
	case EKakurenboMenu::ConfirmTitle:
		DrawConfirm(TEXT("タイトルへ"), TEXT("このかくれんぼで稼いだコインはなくなります（始める前の状態から、つづきを遊べます）"), TEXT("タイトルへ [Enter]"), TEXT("つづける [Esc]"));
		break;
	default:
		break;
	}
}

void AKakurenboHUD::DrawPauseMenu(AKakurenboGameMode* GM)
{
	const float CanvasW = Canvas->ClipX / UIScale;
	const float W = 560, H = 480, X = (CanvasW - W) * 0.5f, Y = 250;
	Panel(X, Y, W, H, FLinearColor(0.06f, 0.02f, 0.11f, 0.95f));
	Frame(X, Y, W, H, FLinearColor(0.75f, 0.55f, 1.f, 0.6f), 2.f);
	Text(GM->GetPhase() == EKakurenboPhase::Hide ? TEXT("ひと休み") : TEXT("メニュー"), 0, Y + 28, 36, FLinearColor::White, true);
	const TCHAR* Labels[] = { TEXT("つづける [Esc]"), TEXT("操作説明"), TEXT("設定（音量・マウス・キー）"), TEXT("タイトルへ") };
	for (int32 i = 0; i < static_cast<int32>(EKakurenboPauseItem::Count); ++i)
	{
		DrawButton(X + 60, Y + 110 + i * 84, W - 120, 64, Labels[i], EKakurenboUIAction::MenuItem, i, false, 24,
			i == static_cast<int32>(EKakurenboPauseItem::Title) ? FLinearColor(1.f, 0.75f, 0.75f) : FLinearColor::White);
	}
}

void AKakurenboHUD::DrawSettings()
{
	const UKakurenboSettings* S = UKakurenboSettings::Get(this);
	if (!S)
	{
		return;
	}
	const float CanvasW = Canvas->ClipX / UIScale;
	const float W = 960, H = 760, X = (CanvasW - W) * 0.5f, Y = 150;
	Panel(X, Y, W, H, FLinearColor(0.06f, 0.02f, 0.11f, 0.96f));
	Frame(X, Y, W, H, FLinearColor(0.75f, 0.55f, 1.f, 0.6f), 2.f);
	Text(TEXT("設定"), 0, Y + 24, 36, FLinearColor::White, true);

	const float LabelX = X + 60, ControlX = X + 360, SliderW = 400;
	float RowY = Y + 110;
	auto Label = [&](const TCHAR* Str) { Text(Str, LabelX, RowY + 8, 24, FLinearColor::White); };
	auto Percent = [](float V) { return FString::Printf(TEXT("%d%%"), FMath::RoundToInt(V * 100.f)); };

	Label(TEXT("全体の音量"));
	DrawSlider(ControlX, RowY, SliderW, S->MasterVolume, static_cast<int32>(EKakurenboSettingSlider::Master), Percent(S->MasterVolume));
	RowY += 72;
	Label(TEXT("BGM"));
	DrawSlider(ControlX, RowY, SliderW, S->MusicVolume, static_cast<int32>(EKakurenboSettingSlider::Music), Percent(S->MusicVolume));
	RowY += 72;
	Label(TEXT("効果音"));
	DrawSlider(ControlX, RowY, SliderW, S->SfxVolume, static_cast<int32>(EKakurenboSettingSlider::Sfx), Percent(S->SfxVolume));
	RowY += 72;
	Label(TEXT("マウス感度"));
	const float Sens01 = (S->MouseSensitivity - UKakurenboSettings::MinMouseSensitivity) / (UKakurenboSettings::MaxMouseSensitivity - UKakurenboSettings::MinMouseSensitivity);
	DrawSlider(ControlX, RowY, SliderW, Sens01, static_cast<int32>(EKakurenboSettingSlider::Mouse),
		FString::Printf(TEXT("×%.2f"), S->MouseSensitivity / UKakurenboSettings::DefaultMouseSensitivity));
	RowY += 72;
	Label(TEXT("マウスの上下"));
	DrawToggle(ControlX, RowY, 260, S->bInvertMouseY, static_cast<int32>(EKakurenboSettingToggle::InvertY), TEXT("反転する"), TEXT("そのまま"));
	RowY += 66;
	Label(TEXT("操作のヒント"));
	DrawToggle(ControlX, RowY, 260, S->bShowHints, static_cast<int32>(EKakurenboSettingToggle::Hints), TEXT("表示する"), TEXT("表示しない"));
	DrawButton(ControlX + 280, RowY, 280, 44, TEXT("ヒントをもう一度見る"), EKakurenboUIAction::ResetTips, 0, false, 18);
	RowY += 66;
	Label(TEXT("画面"));
	DrawToggle(ControlX, RowY, 260, UKakurenboSettings::IsFullscreen(), static_cast<int32>(EKakurenboSettingToggle::Fullscreen), TEXT("フルスクリーン"), TEXT("ウィンドウ"));
	RowY += 90;

	DrawButton(X + 60, RowY, 400, 60, TEXT("キー設定…"), EKakurenboUIAction::OpenKeys, 0, false, 24);
	DrawButton(X + W - 460, RowY, 400, 60, TEXT("もどる [Esc]"), EKakurenboUIAction::Back, 0, false, 24, Gold);
}

void AKakurenboHUD::DrawKeyConfig()
{
	const UKakurenboSettings* S = UKakurenboSettings::Get(this);
	const AKakurenboPlayerController* PC = Cast<AKakurenboPlayerController>(GetOwningPlayerController());
	if (!S || !PC)
	{
		return;
	}
	const float CanvasW = Canvas->ClipX / UIScale;
	const float W = 1300, H = 900, X = (CanvasW - W) * 0.5f, Y = 90;
	Panel(X, Y, W, H, FLinearColor(0.06f, 0.02f, 0.11f, 0.97f));
	Frame(X, Y, W, H, FLinearColor(0.75f, 0.55f, 1.f, 0.6f), 2.f);
	Text(TEXT("キー設定"), 0, Y + 20, 34, FLinearColor::White, true);

	EKakurenboAction WaitingAction = EKakurenboAction::Count;
	bool bWaitingSecondary = false;
	const bool bWaiting = PC->IsWaitingForKey(WaitingAction, bWaitingSecondary);

	constexpr int32 ActionCount = static_cast<int32>(EKakurenboAction::Count);
	const int32 PerColumn = (ActionCount + 1) / 2;
	for (int32 i = 0; i < ActionCount; ++i)
	{
		const EKakurenboAction Action = static_cast<EKakurenboAction>(i);
		const float ColX = X + 40 + (i / PerColumn) * 630;
		const float RowY = Y + 90 + (i % PerColumn) * 60;
		Text(UKakurenboSettings::ActionDisplayName(Action).ToString(), ColX, RowY + 10, 20, FLinearColor::White);
		for (int32 Slot = 0; Slot < 2; ++Slot)
		{
			const bool bThisWaiting = bWaiting && WaitingAction == Action && bWaitingSecondary == (Slot == 1);
			const FKey Key = S->GetKey(Action, Slot == 1);
			const FString Label = bThisWaiting ? FString(TEXT("キーを押す…")) : UKakurenboSettings::KeyLabel(Key);
			DrawButton(ColX + 300 + Slot * 150, RowY, 140, 46, Label, EKakurenboUIAction::KeySlot, i * 2 + Slot, bThisWaiting, 18,
				bThisWaiting ? FLinearColor::Black : (Key.IsValid() ? FLinearColor::White : Gray));
		}
	}
	const float FootY = Y + 90 + PerColumn * 60 + 14;
	RichText(TEXT("欄をクリックして、使いたいキーを押す　[Esc] 取り消し　右クリック：2 つ目を外す　ほかの操作と同じキーなら入れ替わる"), -1.f, FootY, 18, PaleText, true);
	DrawButton(X + 60, Y + H - 84, 380, 60, TEXT("初期設定に戻す"), EKakurenboUIAction::ResetKeys, 0, false, 22);
	DrawButton(X + W - 440, Y + H - 84, 380, 60, TEXT("もどる [Esc]"), EKakurenboUIAction::Back, 0, false, 22, Gold);
}

void AKakurenboHUD::DrawHelp(EKakurenboPhase Phase)
{
	const float CanvasW = Canvas->ClipX / UIScale;
	const TArray<FString> Lines = GetHelpLines(Phase);
	const float W = 1240, H = 170 + Lines.Num() * 64, X = (CanvasW - W) * 0.5f, Y = FMath::Max(80.f, 540.f - H * 0.5f);
	Panel(X, Y, W, H, FLinearColor(0.06f, 0.02f, 0.11f, 0.96f));
	Frame(X, Y, W, H, FLinearColor(0.75f, 0.55f, 1.f, 0.6f), 2.f);
	static const TCHAR* Titles[] = { TEXT("操作説明（購入）"), TEXT("操作説明（設置）"), TEXT("操作説明（かくれんぼ）"), TEXT("操作説明（結果）"), TEXT("操作説明") };
	Text(Titles[FMath::Clamp(static_cast<int32>(Phase), 0, UE_ARRAY_COUNT(Titles) - 1)], 0, Y + 22, 32, FLinearColor::White, true);
	for (int32 i = 0; i < Lines.Num(); ++i)
	{
		RichText(Lines[i], X + 50, Y + 90 + i * 64, 22, FLinearColor::White);
	}
	DrawButton(X + W - 340, Y + H - 72, 300, 54, TEXT("もどる [Esc]"), EKakurenboUIAction::Back, 0, false, 22, Gold);
}

void AKakurenboHUD::DrawConfirm(const FString& Title, const FString& Message, const FString& YesLabel, const FString& NoLabel)
{
	const float CanvasW = Canvas->ClipX / UIScale;
	const float W = 900, H = 280, X = (CanvasW - W) * 0.5f, Y = 380;
	Panel(X, Y, W, H, FLinearColor(0.08f, 0.02f, 0.08f, 0.97f));
	Frame(X, Y, W, H, FLinearColor(1.f, 0.5f, 0.5f, 0.7f), 2.f);
	Text(Title, 0, Y + 24, 32, FLinearColor::White, true);
	Text(Message, 0, Y + 96, 22, PaleText, true);
	DrawButton(X + 60, Y + H - 90, 360, 60, YesLabel, EKakurenboUIAction::ConfirmYes, 0, false, 22, Bad);
	DrawButton(X + W - 420, Y + H - 90, 360, 60, NoLabel, EKakurenboUIAction::ConfirmNo, 0, false, 22, FLinearColor::White);
}

// ---------------------------------------------------------------- 購入

void AKakurenboHUD::DrawShop(AKakurenboGameState* State, AKakurenboGameMode* GM)
{
	const AKakurenboPlayerController* PC = Cast<AKakurenboPlayerController>(GetOwningPlayerController());
	const bool bPrestigeTab = PC && PC->bPrestigeShopTab;
	const TArray<FShopItemView> Items = bPrestigeTab ? GM->GetPrestigeShopItems() : GM->GetShopItems();

	// 配置は固定（お店を切り替えても、転生できるようになっても、ボタンの位置が動かないように）。
	// 上端は画面上部のお知らせ（y 116〜160）と重ならない高さ。1 商品 = 名前・説明・壊れた数の 3 行
	constexpr int32 MaxRows = 8;
	const float X = 520, Y = 168, W = 880;
	const float RowH = 70.f * FMath::Min(1.f, static_cast<float>(MaxRows) / FMath::Max(MaxRows, Items.Num()));
	const float RowsTop = Y + 70;
	const float ButtonsY = RowsTop + MaxRows * 70.f + 8;
	const float ButtonH = 50;
	Panel(X, Y, W, ButtonsY + ButtonH + 12 - Y);

	// お店の切り替え（クリックか Tab）
	DrawButton(X + 16, Y + 12, 300, 44, TEXT("コインのお店"), EKakurenboUIAction::ShopTab, 0, !bPrestigeTab, 22, !bPrestigeTab ? FLinearColor::Black : FLinearColor::White);
	DrawButton(X + 330, Y + 12, 340, 44, FString::Printf(TEXT("転生のお店（%d pt）"), State->PrestigePoints), EKakurenboUIAction::ShopTab, 1, bPrestigeTab, 22,
		bPrestigeTab ? FLinearColor::Black : Purple);
	RichText(FString::Printf(TEXT("%s 切り替え"), *KeyOf(PC, EKakurenboAction::SwitchShop)), X + 690, Y + 24, 16, Gray);

	for (int32 i = 0; i < Items.Num(); ++i)
	{
		const FShopItemView& Item = Items[i];
		const bool bAffordable = Item.bPrestigeItem ? (!Item.bMaxed && State->PrestigePoints >= Item.Cost) : State->Coins >= Item.Cost;
		const float RowY = RowsTop + i * RowH;

		// 行全体がボタン（カーソルを乗せると明るくなる）
		AddButton(X + 8, RowY - 4, W - 16, RowH - 2, EKakurenboUIAction::ShopItem, i);
		const bool bHovered = IsCursorOver(X + 8, RowY - 4, W - 16, RowH - 2);
		if (bHovered)
		{
			Panel(X + 8, RowY - 4, W - 16, RowH - 2, bAffordable ? FLinearColor(0.35f, 0.35f, 0.45f, 0.6f) : FLinearColor(0.3f, 0.15f, 0.15f, 0.5f));
		}

		RichText(FString::Printf(TEXT("[%d] %s  %s"), i + 1, *Item.DisplayName.ToString(), *Item.OwnedText.ToString()), X + 24, RowY, 22, bAffordable ? FLinearColor::White : Gray);
		const FString CostText = Item.bMaxed ? FString(TEXT("最大"))
			: Item.bPrestigeItem ? FString::Printf(TEXT("%d pt"), FMath::RoundToInt(Item.Cost))
			: FString::Printf(TEXT("%s コイン"), *Big(Item.Cost));
		Text(CostText, X + 680, RowY, 22, bAffordable ? (Item.bPrestigeItem ? Purple : Gold) : Gray);
		if (bHovered && !Item.bMaxed)
		{
			Text(bAffordable ? TEXT("クリックで買う") : TEXT("足りない"), X + 680, RowY + RowH * 0.42f, 15, bAffordable ? Good : Bad);
		}
		Text(Item.Description.ToString(), X + 64, RowY + RowH * 0.42f, 15, Gray);
		if (!Item.RepairText.IsEmpty())
		{
			// 壊れた壁・使った罠の数（足りなければ橙、在庫で直せるなら緑）
			Text(Item.RepairText.ToString(), X + 64, RowY + RowH * 0.68f, 15, Item.bNeedsMoreForRepair ? Warn : Good);
		}
	}

	// 下のボタン 3 つ（いつも同じ位置）：まとめて補充 / 転生 / 次へ
	const float ButtonW = (W - 32 - 24) / 3.f;
	int32 RefillCount = 0;
	double RefillCost = 0.0;
	GM->GetRefillPlan(RefillCount, RefillCost);
	DrawButton(X + 16, ButtonsY, ButtonW, ButtonH, FString::Printf(TEXT("まとめて補充 %s"), *KeyOf(PC, EKakurenboAction::Refill)), EKakurenboUIAction::RefillAll, 0, false, 20,
		RefillCount > 0 ? FLinearColor::White : Gray);
	const bool bCanPrestige = GM->CanPrestige();
	const bool bConfirm = bCanPrestige && PC && PC->IsPrestigeConfirmPending();
	const FString PrestigeLabel = !bCanPrestige ? FString::Printf(TEXT("転生（ステージ %d から）"), GM->PrestigeSettings.MinStage)
		: bConfirm ? FString(TEXT("もう一度押すと転生"))
		: FString::Printf(TEXT("転生 +%d pt %s"), GM->GetPrestigePointsOnReset(), *KeyOf(PC, EKakurenboAction::Prestige));
	DrawButton(X + 16 + (ButtonW + 12), ButtonsY, ButtonW, ButtonH, PrestigeLabel, EKakurenboUIAction::Prestige, 0, false, 20,
		!bCanPrestige ? Gray : bConfirm ? Bad : Purple);
	DrawButton(X + 16 + (ButtonW + 12) * 2, ButtonsY, ButtonW, ButtonH, FString::Printf(TEXT("設置へ ▶ %s"), *KeyOf(PC, EKakurenboAction::Next)), EKakurenboUIAction::NextPhase, 0, false, 20, Gold);

	// 説明（パネルの下・いつも同じ位置）
	float InfoY = ButtonsY + ButtonH + 24;
	if (RefillCount > 0)
	{
		Text(FString::Printf(TEXT("壊れた壁・使った罠を直すには、あと %d 個（%s コイン）。まとめて補充は高いものから買えるだけ買う"), RefillCount, *Big(RefillCost)),
			0, InfoY, 18, Warn, true);
	}
	else if (const UKakurenboGridSubsystem* Grid = GetWorld()->GetSubsystem<UKakurenboGridSubsystem>(); Grid && (Grid->GetTotalMissing() > 0 || Grid->GetMissingTrapCount() > 0))
	{
		Text(TEXT("壊れた壁・使った罠は、在庫から設置の始めに自動で直る"), 0, InfoY, 18, Good, true);
	}
	InfoY += 28;
	const FString StartCoins = Big(GM->GetPrestigeStartCoins(State->PrestigeCount + 1));
	const FString CoinsPerPrestige = Big(GM->PrestigeSettings.StartCoinsPerPrestige);
	if (bPrestigeTab)
	{
		Text(FString::Printf(TEXT("転生ポイントは転生するともらえる。ここで買った強化は、転生してもずっと残る（転生するたびに、はじめのコインも +%s）"), *CoinsPerPrestige),
			0, InfoY, 16, Purple, true);
	}
	else if (bCanPrestige)
	{
		Text(FString::Printf(TEXT("転生すると、コイン・ステージ・強化・在庫・置いた壁と罠はなくなり、%s コインから始まる（設計図と転生のお店の強化は残る）"), *StartCoins),
			0, InfoY, 16, Gray, true);
	}
	else
	{
		Text(FString::Printf(TEXT("転生はステージ %d から（転生ポイントで永続強化が買える・転生するたびに、はじめのコイン +%s）"), GM->PrestigeSettings.MinStage, *CoinsPerPrestige),
			0, InfoY, 16, Gray, true);
	}
	const UKakurenboSettings* S = UKakurenboSettings::Get(this);
	if (!S || S->bShowHints)
	{
		RichText(FString::Printf(TEXT("クリック・[1]〜[%d] 買う　%s%s カメラを回す　ホイール ズーム"), FMath::Min(Items.Num(), 9),
			*KeyOf(PC, EKakurenboAction::CameraLeft), *KeyOf(PC, EKakurenboAction::CameraRight)), -1.f, InfoY + 30, 18, PaleText, true);
	}
}

// ---------------------------------------------------------------- 設置

void AKakurenboHUD::DrawBuild(AKakurenboGameState* State, AKakurenboGameMode* GM)
{
	const AKakurenboPlayerController* PC = Cast<AKakurenboPlayerController>(GetOwningPlayerController());
	const UKakurenboSettings* S = UKakurenboSettings::Get(this);
	const float CanvasW = Canvas->ClipX / UIScale;
	const float CanvasH = Canvas->ClipY / UIScale;

	// 操作の案内（キーの形で。設定で消せる）
	if (!S || S->bShowHints)
	{
		const float HelpY = BelowStatusY;
		Panel(20, HelpY, 500, 178, FLinearColor(0.03f, 0.01f, 0.06f, 0.6f));
		RichText(TEXT("[左クリック] 置く　[右クリック] 回収"), 36, HelpY + 14, 18);
		RichText(FString::Printf(TEXT("%s%s%s%s カメラを動かす　ホイール ズーム"), *KeyOf(PC, EKakurenboAction::MoveForward), *KeyOf(PC, EKakurenboAction::MoveLeft),
			*KeyOf(PC, EKakurenboAction::MoveBack), *KeyOf(PC, EKakurenboAction::MoveRight)), 36, HelpY + 54, 18);
		RichText(FString::Printf(TEXT("%s%s 回す　%s 傾ける　%s 真上"), *KeyOf(PC, EKakurenboAction::CameraLeft), *KeyOf(PC, EKakurenboAction::CameraRight),
			*KeyOf(PC, EKakurenboAction::TiltDown), *KeyOf(PC, EKakurenboAction::TiltUp)), 36, HelpY + 94, 18);
		RichText(FString::Printf(TEXT("%s スタート位置（青い印）をカーソルへ"), *KeyOf(PC, EKakurenboAction::SetStart)), 36, HelpY + 134, 18, Blue);
	}

	// スタート位置と鬼の出入り口に名前を付ける
	if (const APawn* Pawn = GetOwningPawn())
	{
		DrawWorldLabel(Pawn->GetActorLocation() - FVector(0.f, 0.f, Pawn->GetSimpleCollisionHalfHeight()), TEXT("スタート"), Blue, 34.f);
	}
	if (const AKakurenboArena* Arena = GM->GetArena())
	{
		DrawWorldLabel(Arena->GetOniGateLocation() + FVector(0.f, 0.f, Arena->BorderHeight + 120.f), TEXT("鬼の門"), Bad);
	}

	// 壊れた壁の自動修復・使った罠の置き直しの結果（置く物の欄のすぐ上）
	float NoticeY = CanvasH - 214;
	if (const UKakurenboGridSubsystem* Grid = GetWorld()->GetSubsystem<UKakurenboGridSubsystem>())
	{
		const int32 MissingWalls = Grid->GetTotalMissing();
		const int32 MissingTraps = Grid->GetMissingTrapCount();
		if (MissingWalls > 0 || MissingTraps > 0)
		{
			Text(FString::Printf(TEXT("直せていない壁 %d・罠 %d（赤い枠：在庫を買う／右クリックで設計図から消す）"), MissingWalls, MissingTraps), 0, NoticeY, 19, Bad, true);
			NoticeY -= 32;
		}
	}
	if (State->LastRepairedWalls > 0 || State->LastRefilledTraps > 0)
	{
		Text(FString::Printf(TEXT("在庫から直した：壁 %d・罠 %d"), State->LastRepairedWalls, State->LastRefilledTraps), 0, NoticeY, 19, Good, true);
	}

	// 壁と罠の在庫（下部中央に並べる）
	const TArray<FWallTypeDef> Walls = GM->GetEffectiveWallTypes();
	const int32 NumWalls = Walls.Num();
	const int32 Num = NumWalls + GM->TrapTypes.Num();
	const float SlotW = FMath::Min(220.f, (CanvasW - 80.f) / FMath::Max(1, Num)), SlotH = 70;
	const float StartX = (CanvasW - SlotW * Num) * 0.5f;
	const float SlotY = CanvasH - 170;
	for (int32 i = 0; i < Num; ++i)
	{
		const bool bTrap = i >= NumWalls;
		const bool bSelected = PC && PC->SelectedBuildSlot == i;
		const int32 TrapIndex = i - NumWalls;
		const int32 Stock = bTrap
			? (State->TrapStock.IsValidIndex(TrapIndex) ? State->TrapStock[TrapIndex] : 0)
			: (State->WallStock.IsValidIndex(i) ? State->WallStock[i] : 0);
		const FText& Name = bTrap ? GM->TrapTypes[TrapIndex].DisplayName : Walls[i].DisplayName;
		const FLinearColor& Color = bTrap ? GM->TrapTypes[TrapIndex].Color : Walls[i].Color;

		const float X = StartX + i * SlotW;
		// 欄全体がボタン（クリックで選ぶ）
		AddButton(X + 4, SlotY, SlotW - 8, SlotH, EKakurenboUIAction::BuildSlot, i);
		const bool bHovered = IsCursorOver(X + 4, SlotY, SlotW - 8, SlotH);
		Panel(X + 4, SlotY, SlotW - 8, SlotH, bSelected ? FLinearColor(0.9f, 0.75f, 0.2f, 0.6f)
			: bHovered ? FLinearColor(0.35f, 0.35f, 0.45f, 0.75f) : FLinearColor(0.f, 0.f, 0.f, 0.55f));
		// 壁は縦長の四角、罠は平たい四角で見分ける
		if (bTrap)
		{
			Panel(X + 10, SlotY + 26, 20, 16, Color);
		}
		else
		{
			Panel(X + 12, SlotY + 10, 14, SlotH - 20, Color);
		}
		RichText(FString::Printf(TEXT("[%d] %s"), i + 1, *Name.ToString()), X + 36, SlotY + 8, 19, Stock > 0 ? FLinearColor::White : Gray);
		const FString Detail = bTrap
			? FString::Printf(TEXT("在庫 %d"), Stock)
			: FString::Printf(TEXT("在庫 %d　耐久 %s"), Stock, *Stat(Walls[i].MaxHP));
		Text(Detail, X + 36, SlotY + 40, 16, Gray);
	}

	// かくれんぼを始めるボタン（Enter と同じ）と、購入パートへ戻るボタン（B と同じ）
	DrawButton(CanvasW - 420, CanvasH - 260, 390, 60, FString::Printf(TEXT("かくれんぼ開始 ▶ %s"), *KeyOf(PC, EKakurenboAction::Next)), EKakurenboUIAction::NextPhase, 0, false, 22, Gold);
	DrawButton(CanvasW - 420, CanvasH - 330, 390, 54, FString::Printf(TEXT("◀ 購入へ戻る %s"), *KeyOf(PC, EKakurenboAction::BackToShop)), EKakurenboUIAction::BackToShop, 0, false, 20, FLinearColor::White);
	// 置いた壁・罠を全部回収（Delete と同じ。取り消せないので 2 回押す）
	const bool bClearPending = PC && PC->IsClearAllConfirmPending();
	DrawButton(CanvasW - 420, CanvasH - 400, 390, 54, bClearPending ? FString(TEXT("もう一度押すと全部回収")) : FString::Printf(TEXT("全部回収 %s"), *KeyOf(PC, EKakurenboAction::ClearAll)),
		EKakurenboUIAction::ClearAll, 0, false, 20, bClearPending ? Bad : FLinearColor::White);

	// カーソルの下に、そこでできること（置ける・回収できる・置けない理由）
	FVector2D Cursor;
	FKakurenboUIButton UnderCursor;
	if (PC && PC->GetUICursorPosition(Cursor) && !FindButtonAt(Cursor, UnderCursor))
	{
		const float CX = Cursor.X / UIScale;
		const float CY = Cursor.Y / UIScale + 26.f;
		if (PC->PickUpTarget || PC->bHasTrapPickUpTarget)
		{
			RichText(TEXT("[右クリック] 回収"), CX, CY, 17, Gold, true);
		}
		else if (PC->bHasBuildTarget && PC->bCanPlaceAtTarget)
		{
			RichText(TEXT("[左クリック] 置く"), CX, CY, 17, Good, true);
		}
		else if (PC->bHasBuildTarget && !PC->BuildTargetReason.IsEmpty())
		{
			TextPx(PC->BuildTargetReason.ToString(), Cursor.X, CY * UIScale, 18, Bad, true);
		}
	}

	// 鬼の種類ごとの攻撃力（このステージに出てくる種類だけ）
	FString Damage;
	TArray<EOniType> ShownTypes;
	for (const EOniType Type : GM->GetStageSettings().OniTypes)
	{
		if (!ShownTypes.Contains(Type))
		{
			ShownTypes.Add(Type);
			const FKakurenboOniTypeRow Row = GM->GetOniTypeRow(Type);
			Damage += FString::Printf(TEXT("%s%s %s"), Damage.IsEmpty() ? TEXT("") : TEXT("　"), *Row.DisplayName.ToString(), *Stat(GM->GetOniAttackDamage() * Row.DamageScale));
		}
	}
	Text(FString::Printf(TEXT("このステージの鬼の攻撃力: %s"), *Damage), 0, CanvasH - 60, 18, Gray, true);
}

// ---------------------------------------------------------------- かくれんぼ

void AKakurenboHUD::DrawHide(AKakurenboGameState* State, AKakurenboGameMode* GM)
{
	const float CanvasH = Canvas->ClipY / UIScale;
	const float CanvasW = Canvas->ClipX / UIScale;
	const APawn* Pawn = GetOwningPawn();
	const AKakurenboPlayerController* PC = Cast<AKakurenboPlayerController>(GetOwningPlayerController());
	const UKakurenboSettings* S = UKakurenboSettings::Get(this);
	const bool bHints = !S || S->bShowHints;

	// 鬼とお宝の場所は矢印では出さない（音で見当をつける）。見えている鬼にだけ、追いかけ始めた直後の「！」を頭の上に出す
	for (const AOniCharacter* Oni : GM->GetOnis())
	{
		const bool bJustSpotted = Oni && Oni->GetChaseStartTime() >= 0.f && GetWorld()->GetTimeSeconds() - Oni->GetChaseStartTime() < 1.5f;
		if (bJustSpotted)
		{
			DrawWorldLabel(Oni->GetActorLocation() + FVector(0.f, 0.f, 150.f), TEXT("！"), Bad);
		}
	}

	DrawMashPopups(State, GM);

	// 残り時間（上部中央。細い線と数字だけ）
	const float Ratio = State->HideTimeLimit > 0.f ? State->HideTimeRemaining / State->HideTimeLimit : 0.f;
	const float BarW = 520;
	const float BarX = (CanvasW - BarW) * 0.5f;
	Panel(BarX, 26, BarW, 10, FLinearColor(0.f, 0.f, 0.f, 0.55f));
	Panel(BarX, 26, BarW * Ratio, 10, Ratio < 0.2f ? FLinearColor(1.f, 0.5f, 0.3f, 0.9f) : FLinearColor(0.55f, 0.75f, 1.f, 0.85f));
	Text(FString::Printf(TEXT("%d"), FMath::CeilToInt(State->HideTimeRemaining)), 0, 42, 34, FLinearColor::White, true);
	Text(FString::Printf(TEXT("お宝 %d / %d"), State->TreasuresCollectedThisRound, State->TreasuresThisRound), BarX + BarW + 20, 20, 18, Gold);

	if (State->HideStartCountdown > 0.f)
	{
		// 鬼が数えている：「もういいかい」
		Text(TEXT("もういいかい…"), 0, 330, 40, FLinearColor(1.f, 0.82f, 0.9f), true);
		Text(FString::Printf(TEXT("%d"), FMath::CeilToInt(State->HideStartCountdown)), 0, 390, 120, FLinearColor::White, true);
		// 鬼が出てくる門の方向
		if (const AKakurenboArena* Arena = GM->GetArena())
		{
			DrawWorldIndicator(Arena->GetOniGateLocation() + FVector(0.f, 0.f, Arena->BorderHeight + 60.f), TEXT("鬼の門"), Bad, 0.33f, 1.f);
		}
	}

	// 鬼の様子（色・名前・ひとこと）
	const TArray<TObjectPtr<AOniCharacter>>& Onis = GM->GetOnis();
	if (Onis.Num() > 0 && Pawn)
	{
		Panel(20, 148, 400, 16 + Onis.Num() * 32, FLinearColor(0.03f, 0.01f, 0.06f, 0.5f));
		for (int32 i = 0; i < Onis.Num(); ++i)
		{
			if (const AOniCharacter* Oni = Onis[i])
			{
				FString StateText;
				FLinearColor Color;
				DescribeOni(Oni, StateText, Color);
				const float RowY = 156 + i * 32;
				Panel(32, RowY + 5, 14, 18, Oni->BodyColor); // 体の色（どの鬼か見分ける）
				Text(OniName(Oni), 56, RowY, 18, FLinearColor(0.85f, 0.85f, 0.9f));
				Text(StateText, 186, RowY, 18, Color);
			}
		}
	}

	// 煙幕ダッシュの残りの回数（下部中央の少し上）。1 回ぶんを 1 つの四角で表す
	const AHiderCharacter* Hider = Cast<AHiderCharacter>(Pawn);
	if (Hider && Hider->bDashUnlocked)
	{
		const int32 Uses = FMath::Max(1, Hider->DashUsesPerRound);
		const int32 Left = Hider->GetDashUsesLeft();
		const float BoxW = 28, Gap = 8, RowW = Uses * BoxW + (Uses - 1) * Gap;
		const float RowX = (CanvasW - RowW) * 0.5f, RowY = CanvasH - 140;
		for (int32 i = 0; i < Uses; ++i)
		{
			Panel(RowX + i * (BoxW + Gap), RowY, BoxW, 12, i < Left ? FLinearColor(0.75f, 0.75f, 0.8f, 0.95f) : FLinearColor(0.f, 0.f, 0.f, 0.6f));
		}
		RichText(FString::Printf(TEXT("%s 煙幕"), *KeyOf(PC, EKakurenboAction::Dash)), RowX + RowW + 14, RowY - 7, 15, Left > 0 ? PaleText : Gray, false, Left > 0 ? 0.9f : 0.5f);
	}

	// 自分の様子（左下に小さく）：しゃがんでいる・壁に囲まれて足音が小さい・消音壁
	float ChipY = CanvasH - 150;
	auto Chip = [&](const FString& Str, const FLinearColor& Color)
	{
		const float W = MeasureText(Str, 17) + 28;
		Panel(20, ChipY, W, 30, FLinearColor(0.03f, 0.01f, 0.06f, 0.6f));
		Text(Str, 34, ChipY + 4, 17, Color);
		ChipY -= 38;
	};
	const float NoiseMultiplier = GM->GetPlayerNoiseMultiplier();
	if (NoiseMultiplier < 0.999f)
	{
		const int32 Remaining = GM->GetPlayerQuietWallRemaining();
		FString QuietText = FString::Printf(TEXT("消音壁の中　音 −%d%%"), FMath::RoundToInt((1.f - NoiseMultiplier) * 100.f));
		if (Remaining >= 0)
		{
			QuietText += FString::Printf(TEXT("（あと %d 回）"), Remaining);
		}
		Chip(QuietText, Remaining >= 0 && Remaining <= 10 ? Warn : Purple);
	}
	const float StepMultiplier = GM->GetStepNoiseMultiplier();
	if (StepMultiplier < 0.999f && NoiseMultiplier >= 0.999f)
	{
		Chip(FString::Printf(TEXT("壁の中　足音 −%d%%"), FMath::RoundToInt((1.f - StepMultiplier) * 100.f)), Purple);
	}
	if (Hider && Hider->IsSneaking())
	{
		Chip(TEXT("しゃがんでいる"), Blue);
	}

	// 使える操作（キーの形で。設定で消せる）
	if (bHints)
	{
		FString Controls = FString::Printf(TEXT("%s%s%s%s 移動　%s しゃがむ　%s 連打"), *KeyOf(PC, EKakurenboAction::MoveForward), *KeyOf(PC, EKakurenboAction::MoveLeft),
			*KeyOf(PC, EKakurenboAction::MoveBack), *KeyOf(PC, EKakurenboAction::MoveRight), *KeyOf(PC, EKakurenboAction::Sneak), *KeyOf(PC, EKakurenboAction::Mash));
		if (GM->IsDashUnlocked())
		{
			Controls += FString::Printf(TEXT("　%s 煙幕"), *KeyOf(PC, EKakurenboAction::Dash));
		}
		if (GM->IsJumpUnlocked())
		{
			Controls += FString::Printf(TEXT("　%s ジャンプ"), *KeyOf(PC, EKakurenboAction::Jump));
		}
		RichText(Controls, -1.f, CanvasH - 64, 17, PaleText, true, 0.7f);
	}
}

void AKakurenboHUD::DrawMashPopups(const AKakurenboGameState* State, AKakurenboGameMode* GM)
{
	constexpr float Lifetime = 0.8f;
	constexpr int32 MaxPopups = 12;

	// 連打回数が増えた分だけ「+〇」を出す（新しいラウンドで回数が 0 に戻ったら追従する）
	if (State->MashCountThisRound < LastSeenMashCount)
	{
		LastSeenMashCount = 0;
		MashPopups.Reset();
	}
	const int32 NewMashes = State->MashCountThisRound - LastSeenMashCount;
	if (NewMashes > 0)
	{
		FMashPopup& Popup = MashPopups.AddDefaulted_GetRef();
		Popup.Text = FString::Printf(TEXT("+%s"), *Stat(GM->GetMashIncome() * NewMashes));
		Popup.OffsetX = FMath::FRandRange(-70.f, 70.f);
		LastSeenMashCount = State->MashCountThisRound;
		if (MashPopups.Num() > MaxPopups)
		{
			MashPopups.RemoveAt(0);
		}
	}

	// 上に浮かびながら消えていく
	const float CenterX = Canvas->ClipX * 0.5f;
	const float BaseY = Canvas->ClipY * 0.5f + 160.f * UIScale; // カウントダウンの数字より下
	for (int32 i = MashPopups.Num() - 1; i >= 0; --i)
	{
		FMashPopup& Popup = MashPopups[i];
		Popup.Age += RenderDelta;
		if (Popup.Age >= Lifetime)
		{
			MashPopups.RemoveAt(i);
			continue;
		}
		const float T = Popup.Age / Lifetime;
		const FLinearColor Color(Gold.R, Gold.G, Gold.B, 1.f - T);
		FCanvasTextItem Item(FVector2D(CenterX + Popup.OffsetX * UIScale, BaseY - 70.f * T * UIScale), FText::FromString(Popup.Text), MakeFont(26), Color);
		Item.bCentreX = true;
		Item.EnableShadow(FLinearColor(0.f, 0.f, 0.f, 0.7f * (1.f - T)));
		Canvas->DrawItem(Item);
	}
}

// ---------------------------------------------------------------- リザルト

void AKakurenboHUD::DrawResult(AKakurenboGameState* State, AKakurenboGameMode* GM)
{
	const AKakurenboPlayerController* PC = Cast<AKakurenboPlayerController>(GetOwningPlayerController());
	const float CanvasW = Canvas->ClipX / UIScale;
	const float W = 760, X = (CanvasW - W) * 0.5f, Y = 230;
	Panel(X, Y, W, 440);

	if (State->bLastRoundCleared)
	{
		Text(TEXT("逃げきった！"), 0, Y + 24, 54, Good, true);
		Text(FString::Printf(TEXT("逃げ切り報酬 +%s コイン"), *Big(State->LastClearReward)), 0, Y + 110, 26, Gold, true);
		Text(FString::Printf(TEXT("つぎは ステージ %d"), State->Stage), 0, Y + 150, 22, FLinearColor::White, true);
	}
	else
	{
		Text(TEXT("みーつけた"), 0, Y + 20, 64, Bad, true);
		Text(TEXT("稼いだコインは、そのまま持ち帰れる"), 0, Y + 116, 22, FLinearColor::White, true);
	}

	Text(FString::Printf(TEXT("今回の獲得: %s コイン（連打 %d 回）"), *Big(State->CoinsEarnedThisRound), State->MashCountThisRound), 0, Y + 194, 22, FLinearColor::White, true);
	// 内訳（時間だけ・連打・お宝・逃げ切り報酬。お金のバランスを見るため）
	Text(FString::Printf(TEXT("内訳：時間 %s ／ 連打 %s ／ お宝 %s ／ 逃げ切り報酬 %s"), *Big(State->TimeCoinsThisRound), *Big(State->MashCoinsThisRound),
		*Big(State->TreasureCoinsThisRound), *Big(State->LastClearReward)), 0, Y + 222, 16, Gray, true);
	Text(FString::Printf(TEXT("お宝: %d / %d 個（+%s コイン）"), State->TreasuresCollectedThisRound, State->TreasuresThisRound, *Big(State->TreasureCoinsThisRound)), 0, Y + 250, 22, Gold, true);
	if (State->LastRoundWallsDestroyed > 0 || State->TrapsTriggeredThisRound > 0)
	{
		Text(FString::Printf(TEXT("壊された壁 %d 個・使った罠 %d 個（在庫があれば次の設置で自動で直る）"), State->LastRoundWallsDestroyed, State->TrapsTriggeredThisRound), 0, Y + 284, 18, Warn, true);
	}
	DrawButton(X + (W - 360) * 0.5f, Y + 340, 360, 64, FString::Printf(TEXT("つぎへ ▶ %s"), *KeyOf(PC, EKakurenboAction::Next)), EKakurenboUIAction::NextPhase, 0, false, 24, Gold);
}
