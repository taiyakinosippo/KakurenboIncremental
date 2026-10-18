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
#include "KakurenboOniStudio.h"
#include "Engine/Texture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Misc/PackageName.h"
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
	const FLinearColor GemTextColor(0.45f, 0.95f, 1.f);

	/** 左上の状態表示の下に出すパネルの上端（1080p 基準） */
	constexpr float BelowStatusY = 192.f;

	/** 鬼の様子（短く、雰囲気に合う言葉で）。壁を壊している最中はそれを優先して出す */
	void DescribeOni(const AOniCharacter* Oni, FString& OutText, FLinearColor& OutColor)
	{
		if (Oni->IsDarumaFrozen())
		{
			OutText = TEXT("ピタッ…（止まっている）");
			OutColor = FLinearColor(1.f, 0.55f, 0.5f);
			return;
		}
		if (Oni->IsPettingCat())
		{
			OutText = TEXT("猫をなでている");
			OutColor = FLinearColor(1.f, 0.75f, 0.45f);
			return;
		}
		if (Oni->GetIntent() == EOniState::Chase && !Oni->IsChasingPlayer())
		{
			OutText = TEXT("人形を追っている");
			OutColor = Blue;
			return;
		}
		if (Oni->GetOniState() == EOniState::Attack)
		{
			OutText = TEXT("壁をこわしている");
			OutColor = Warn;
			return;
		}
		if (Oni->OniType == EOniType::Detector && Oni->GetIntent() == EOniState::Wander)
		{
			// 身代わり人形を見つけて呼んだときは「仲間を呼んだ！」を出さない。おとり・猫の音で呼んだときは「音へ仲間を呼んだ」
			const bool bRecent = Oni->GetLastSummonTime() >= 0.f && Oni->GetWorld()->GetTimeSeconds() - Oni->GetLastSummonTime() < 3.f;
			if (bRecent && Oni->WasLastSummonForSound())
			{
				OutText = TEXT("音へ仲間を呼んだ");
				OutColor = Purple;
				return;
			}
			const bool bJustCalled = bRecent && !Oni->WasLastSummonForDoll();
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

bool AKakurenboHUD::bRecordTextLayout = false;

void AKakurenboHUD::RecordText(const FString& Str, float LeftPx, float TopPx, float WidthPx, int32 Size)
{
	if (!bRecordTextLayout || Str.IsEmpty())
	{
		return;
	}
	FKakurenboDrawnText& Item = DrawnLayout.AddDefaulted_GetRef();
	Item.Size = Size;
	Item.bWorld = bDrawingWorldLabel;
	Item.bMenu = bDrawingMenu;
	Item.bCursor = bDrawingCursorHint;
	Item.Rect = FBox2D(FVector2D(LeftPx, TopPx), FVector2D(LeftPx + WidthPx, TopPx + Size * 1.2f * UIScale));
	if (!PendingTruncatedText.IsEmpty())
	{
		Item.Text = MoveTemp(PendingTruncatedText);
		Item.bTruncated = true;
		PendingTruncatedText.Reset();
	}
	else
	{
		Item.Text = Str;
	}
}

void AKakurenboHUD::DrawHUD()
{
	Super::DrawHUD();

	LastFrameTexts = MoveTemp(DrawnTexts);
	DrawnTexts.Reset();
	LastFrameLayout = MoveTemp(DrawnLayout);
	DrawnLayout.Reset();
	LastFrameWorldLabels = MoveTemp(WorldLabels);
	WorldLabels.Reset();
	RichTextDepth = 0;
	PendingTruncatedText.Reset();
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
	ButtonsPhase = State->Phase;
	ButtonsMenu = PC ? PC->GetMenu() : EKakurenboMenu::None;

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
		bDrawingMenu = true;
		DrawMenu(GM);
		bDrawingMenu = false;
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
	// 大きな文字：ふわっと上がりながら消える。新しいものほど上に出し、古いものは下へずらす。
	// かくれんぼ中は上の残り時間・だるまさんがころんだの表示（〜150）の下から、結果画面は結果のパネルの下から並べる（文が重ならないように）
	constexpr float Lifetime = 1.4f;
	const AKakurenboGameState* State = GetWorld()->GetGameState<AKakurenboGameState>();
	const EKakurenboPhase Phase = State ? State->Phase : EKakurenboPhase::Hide;
	const float TopY = Phase == EKakurenboPhase::Result ? 690.f : Phase == EKakurenboPhase::Hide ? 172.f : 210.f;
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
		const float Y = TopY + 40.f * (1.f - T) + (Popups.Num() - 1 - i) * 46.f;
		FCanvasTextItem Item(FVector2D(Canvas->ClipX * 0.5f, Y * UIScale), FText::FromString(Popup.Text), MakeFont(36),
			FLinearColor(Popup.Color.R, Popup.Color.G, Popup.Color.B, Alpha));
		Item.bCentreX = true;
		Item.EnableShadow(FLinearColor(0.f, 0.f, 0.f, 0.8f * Alpha));
		Canvas->DrawItem(Item);
		DrawnTexts.Add(Popup.Text);
		if (bRecordTextLayout)
		{
			const float WidthPx = MeasureText(Popup.Text, 36) * UIScale;
			RecordText(Popup.Text, Canvas->ClipX * 0.5f - WidthPx * 0.5f, Y * UIScale, WidthPx, 36);
		}
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
	if (bRecordTextLayout && RichTextDepth == 0)
	{
		const float WidthPx = MeasureText(Str, Size) * UIScale;
		RecordText(Str, bCenterX ? Px - WidthPx * 0.5f : Px, Py, WidthPx, Size);
	}
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
	// 濃い紫の小さな板に明るい縁：キーボードのキーに見える形。
	// コントローラーの A・B・X・Y は、ボタンの色（緑・赤・青・黄）の板にする（Xbox の配置のとき。任天堂のボタンは色が無いので、キーと同じ板）
	const float Pad = Size * 0.4f;
	const float W = MeasureText(Label, Size) + Pad * 2.f;
	const float H = Size * 1.45f;
	const float Top = Y - Size * 0.12f;
	const AKakurenboPlayerController* PC = Cast<AKakurenboPlayerController>(GetOwningPlayerController());
	FLinearColor Back(0.16f, 0.1f, 0.24f);
	if (PC && PC->IsUsingGamepad() && Label.Len() == 1 && !UKakurenboSettings::IsNintendoLayout())
	{
		switch (Label[0])
		{
		case TEXT('A'): Back = FLinearColor(0.1f, 0.45f, 0.12f); break;
		case TEXT('B'): Back = FLinearColor(0.55f, 0.1f, 0.1f); break;
		case TEXT('X'): Back = FLinearColor(0.1f, 0.25f, 0.6f); break;
		case TEXT('Y'): Back = FLinearColor(0.6f, 0.48f, 0.05f); break;
		default: break;
		}
	}
	Panel(X, Top, W, H, FLinearColor(Back.R, Back.G, Back.B, 0.95f * Alpha));
	Frame(X, Top, W, H, FLinearColor(0.85f, 0.8f, 1.f, 0.75f * Alpha), FMath::Max(1.f, Size * 0.08f));
	Panel(X + 2.f, Top + H - Size * 0.16f, W - 4.f, Size * 0.1f, FLinearColor(0.f, 0.f, 0.f, 0.35f * Alpha)); // 下の影
	TextPx(Label, (X + Pad) * UIScale, Y * UIScale, Size, FLinearColor(1.f, 1.f, 1.f, Alpha), false);
	return W;
}

float AKakurenboHUD::MeasureRich(const FString& Str, int32 Size)
{
	// RichText と同じ分け方で、キーの形の分（左右の余白とすき間）も入れて測る
	float Total = 0.f;
	FString Current;
	bool bInKey = false;
	const float KeyExtra = Size * 0.4f * 2.f + Size * 0.15f;
	for (const TCHAR C : Str)
	{
		if (C == TEXT('[') && !bInKey)
		{
			Total += Current.IsEmpty() ? 0.f : MeasureText(Current, Size);
			Current.Reset();
			bInKey = true;
		}
		else if (C == TEXT(']') && bInKey)
		{
			Total += MeasureText(Current, Size) + KeyExtra;
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
		Total += MeasureText(bInKey ? TEXT("[") + Current : Current, Size);
	}
	return Total;
}

int32 AKakurenboHUD::FitSize(const FString& Str, int32 Size, float MaxW, bool bRich)
{
	const int32 MinSize = FMath::Max(10, FMath::RoundToInt(Size * 0.6f));
	int32 Fit = Size;
	while (Fit > MinSize && (bRich ? MeasureRich(Str, Fit) : MeasureText(Str, Fit)) > MaxW)
	{
		--Fit;
	}
	return Fit;
}

TArray<FString> AKakurenboHUD::WrapLines(const FString& Str, int32 Size, float MaxW, int32 MaxLines, bool bRich)
{
	TArray<FString> Lines;
	FString Rest = Str;
	auto Measure = [this, Size, bRich](const FString& S) { return bRich ? MeasureRich(S, Size) : MeasureText(S, Size); };
	while (Lines.Num() < MaxLines - 1 && Rest.Len() > 1 && Measure(Rest) > MaxW)
	{
		// [キー] の途中で切らない：先頭から i 文字で切ってよいか（キーの外か）
		TArray<bool> CanCut;
		CanCut.SetNum(Rest.Len() + 1);
		bool bInKey = false;
		CanCut[0] = true;
		for (int32 i = 0; i < Rest.Len(); ++i)
		{
			if (Rest[i] == TEXT('[')) { bInKey = true; }
			else if (Rest[i] == TEXT(']')) { bInKey = false; }
			CanCut[i + 1] = !bInKey;
		}
		// 入る長さを二分探索で（文の幅は長いほど広い）
		int32 Lo = 1, Hi = Rest.Len();
		while (Lo < Hi)
		{
			const int32 Mid = (Lo + Hi + 1) / 2;
			if (Measure(Rest.Left(Mid)) <= MaxW) { Lo = Mid; } else { Hi = Mid - 1; }
		}
		int32 Cut = Lo;
		while (Cut > 1 && !CanCut[Cut])
		{
			--Cut;
		}
		// 句読点・括弧・空白の後ろで切れるなら、そこで切る（読みやすく）
		for (int32 Pos = Cut; Pos > Cut * 2 / 3; --Pos)
		{
			const TCHAR C = Rest[Pos - 1];
			if (CanCut[Pos] && (C == TEXT('、') || C == TEXT('。') || C == TEXT('）') || C == TEXT('」') || C == TEXT('・') || C == TEXT('！') || C == TEXT(' ') || C == TEXT('　')))
			{
				Cut = Pos;
				break;
			}
		}
		Lines.Add(Rest.Left(Cut).TrimEnd());
		Rest = Rest.Mid(Cut).TrimStart();
		// 全角の空白も行の頭には残さない
		while (Rest.StartsWith(TEXT("　")))
		{
			Rest.RightChopInline(1);
		}
	}
	if (!Rest.IsEmpty() || Lines.Num() == 0)
	{
		Lines.Add(Rest);
	}
	return Lines;
}

void AKakurenboHUD::TextFit(const FString& Str, float X, float Y, int32 Size, float MaxW, const FLinearColor& Color, bool bCenterX)
{
	const int32 Fit = FitSize(Str, Size, MaxW, false);
	FString Shown = Str;
	if (MeasureText(Shown, Fit) > MaxW)
	{
		// 小さくしても入らない：後ろを削って「…」
		while (Shown.Len() > 1 && MeasureText(Shown + TEXT("…"), Fit) > MaxW)
		{
			Shown.LeftChopInline(1);
		}
		Shown += TEXT("…");
		if (bRecordTextLayout)
		{
			PendingTruncatedText = Str; // テキストの窓口には削る前の文と「切った」ことを伝える
		}
	}
	// 小さくした分だけ下げて、行の高さの真ん中にそろえる
	Text(Shown, X, Y + (Size - Fit) * 0.5f, Fit, Color, bCenterX);
	PendingTruncatedText.Reset();
}

float AKakurenboHUD::RichTextFit(const FString& Str, float X, float Y, int32 Size, float MaxW, const FLinearColor& Color, bool bCenterX, float Alpha)
{
	const int32 Fit = FitSize(Str, Size, MaxW, true);
	return RichText(Str, X, Y + (Size - Fit) * 0.5f, Fit, Color, bCenterX, Alpha);
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
	const float Left = Cursor;
	++RichTextDepth; // 部品（キーの形・文の切れ端）は記録しない。下で文全体を記録する
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
	--RichTextDepth;
	DrawnTexts.Add(Str);
	if (RichTextDepth == 0)
	{
		RecordText(Str, Left * UIScale, Y * UIScale, Total * UIScale, Size);
	}
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
	Buttons.Last().Label = Label;
	// カーソルを乗せている・コントローラーで選んでいるボタンは明るく（選んでいるボタンは金色の枠も）
	const AKakurenboPlayerController* PC = Cast<AKakurenboPlayerController>(GetOwningPlayerController());
	const bool bFocused = PC && PC->IsUIFocused(Action, Index);
	const bool bHovered = IsCursorOver(X, Y, W, H) || bFocused;
	const FLinearColor Back = bSelected ? FLinearColor(1.f, 0.7f, 0.2f, 0.8f)
		: bHovered ? FLinearColor(0.45f, 0.25f, 0.6f, 0.9f) : FLinearColor(0.12f, 0.05f, 0.2f, 0.82f);
	Panel(X, Y, W, H, Back);
	Frame(X, Y, W, H, bHovered ? FLinearColor(1.f, 1.f, 1.f, 0.7f) : FLinearColor(0.7f, 0.55f, 1.f, 0.35f), bHovered ? 2.f : 1.f);
	if (bFocused)
	{
		Frame(X - 4, Y - 4, W + 8, H + 8, FLinearColor(Gold.R, Gold.G, Gold.B, 0.95f), 3.f);
	}
	RichTextFit(Label, X + W * 0.5f, Y + (H - Size * 1.2f) * 0.5f, Size, W - 16.f, TextColor, true);
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
	// 押せる範囲は線の幅ちょうど（押した横の位置が値になる）。コントローラーで選んでいるときは金色の枠（左右で値が変わる）
	AddButton(X, Y, W, 46, EKakurenboUIAction::Slider, Index);
	const AKakurenboPlayerController* PC = Cast<AKakurenboPlayerController>(GetOwningPlayerController());
	const bool bFocused = PC && PC->IsUIFocused(EKakurenboUIAction::Slider, Index);
	const bool bHovered = IsCursorOver(X, Y, W, 46) || bFocused;
	if (bFocused)
	{
		Frame(X - 16, Y - 2, W + 32, 50, FLinearColor(Gold.R, Gold.G, Gold.B, 0.9f), 2.f);
	}
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
		bDrawingWorldLabel = true;
		TextPx(Label, Screen.X, Screen.Y - 62.f * S, 18, Color, true);
		bDrawingWorldLabel = false;
		WorldLabels.Add({ Label, WorldLocation });
		return;
	}
	WorldLabels.Add({ Label, WorldLocation }); // 画面の外：矢印で方向が出ている
	bDrawingWorldLabel = true; // 下の矢印の名前も、カメラで動く

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
	bDrawingWorldLabel = false;
}

void AKakurenboHUD::DrawWorldLabel(const FVector& WorldLocation, const FString& Label, const FLinearColor& Color, float OffsetY)
{
	APlayerController* PC = GetOwningPlayerController();
	FVector2D Screen;
	if (PC && PC->ProjectWorldLocationToScreen(WorldLocation, Screen, true))
	{
		bDrawingWorldLabel = true;
		TextPx(Label, Screen.X, Screen.Y + (OffsetY - 12.f) * UIScale, 18, Color, true);
		bDrawingWorldLabel = false;
		WorldLabels.Add({ Label, WorldLocation });
	}
}

void AKakurenboHUD::NoteWorldLabel(const FVector& WorldLocation, const FString& Label)
{
	APlayerController* PC = GetOwningPlayerController();
	FVector2D Screen;
	if (PC && PC->ProjectWorldLocationToScreen(WorldLocation, Screen, true))
	{
		WorldLabels.Add({ Label, WorldLocation });
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
		Text(FString::Printf(TEXT("宝石 %d"), State->Gems), 260, 96, 16, GemTextColor);
		return;
	}

	static const TCHAR* PhaseNames[] = { TEXT("購入"), TEXT("設置"), TEXT("かくれんぼ"), TEXT("結果"), TEXT("") };
	const int32 PhaseIndex = FMath::Clamp(static_cast<int32>(State->Phase), 0, UE_ARRAY_COUNT(PhaseNames) - 1);
	const bool bPrestiged = State->PrestigeCount > 0 || State->TotalPrestigePoints > 0;
	Panel(20, 20, 400, bPrestiged ? 158 : 130);
	Text(FString::Printf(TEXT("ステージ %d ・ %s"), State->Stage, PhaseNames[PhaseIndex]), 36, 28, 22);
	if (!MapName.IsEmpty())
	{
		TextFit(FString::Printf(TEXT("館：%s"), *MapName.ToString()), 290, 104, 16, 120.f, FLinearColor(1.f, 0.7f, 0.9f));
	}
	TextFit(FString::Printf(TEXT("コイン  %s"), *Big(State->Coins)), 36, 64, 30, 236.f, Gold);
	Text(FString::Printf(TEXT("宝石 %d"), State->Gems), 290, 74, 18, GemTextColor);
	TextFit(FString::Printf(TEXT("連打 +%s / 時間 +%s/秒"), *Stat(GM->GetMashIncome()), *Stat(GM->GetTimeIncomePerSecond())), 36, 108, 18, 244.f, Gray);
	if (bPrestiged)
	{
		Text(FString::Printf(TEXT("転生 %d 回・転生ポイント %d pt"), State->PrestigeCount, State->PrestigePoints), 36, 134, 18, Purple);
	}
	if (State->Phase == EKakurenboPhase::Shop || State->Phase == EKakurenboPhase::Build)
	{
		DrawStageOnis(GM);
	}
}

void AKakurenboHUD::DrawStageOnis(AKakurenboGameMode* GM)
{
	// 種類ごとに数える（Stages.csv に書いた順）
	TArray<TPair<EOniType, int32>> Counts;
	for (const EOniType Type : GM->GetStageSettings().OniTypes)
	{
		TPair<EOniType, int32>* Found = Counts.FindByPredicate([Type](const TPair<EOniType, int32>& P) { return P.Key == Type; });
		if (Found)
		{
			++Found->Value;
		}
		else
		{
			Counts.Add(TPair<EOniType, int32>(Type, 1));
		}
	}
	if (Counts.Num() == 0)
	{
		return;
	}
	constexpr int32 Size = 18;
	constexpr float Swatch = 14.f;
	const FString Title = TEXT("このステージの鬼");
	TArray<FString> Labels;
	float W = MeasureText(Title, Size) + 24.f;
	for (const TPair<EOniType, int32>& P : Counts)
	{
		const FString Name = GM->GetOniTypeRow(P.Key).DisplayName.ToString();
		Labels.Add(P.Value > 1 ? FString::Printf(TEXT("%s ×%d"), *Name, P.Value) : Name);
		W += Swatch + 6.f + MeasureText(Labels.Last(), Size) + 18.f;
	}
	const float CanvasW = Canvas->ClipX / UIScale;
	float X = (CanvasW - W) * 0.5f;
	const float Y = 20.f;
	Panel(X, Y, W, 34.f);
	Text(Title, X + 12.f, Y + 6.f, Size, Gray);
	X += MeasureText(Title, Size) + 24.f;
	for (int32 i = 0; i < Counts.Num(); ++i)
	{
		// 色の四角は鬼の種類の色（OniTypes.csv の Color）
		Panel(X, Y + 10.f, Swatch, Swatch, GM->GetOniTypeRow(Counts[i].Key).Color);
		X += Swatch + 6.f;
		Text(Labels[i], X, Y + 6.f, Size, FLinearColor::White);
		X += MeasureText(Labels[i], Size) + 18.f;
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
		if (PC && (!S || S->bShowHints))
		{
			RichText(FString::Printf(TEXT("%s メニュー　%s 操作説明"), *PC->UIKeyLabel(EKakurenboUIKey::Menu), *KeyOf(PC, EKakurenboAction::Help)), CanvasW - 360, 24, 16, PaleText, false, 0.75f);
		}
		return;
	}
	// 右上：操作説明・メニュー（クリックでも開ける）
	DrawButton(CanvasW - 470, 20, 220, 46, FString::Printf(TEXT("操作説明 %s"), *KeyOf(PC, EKakurenboAction::Help)), EKakurenboUIAction::OpenHelp, 0, false, 19);
	DrawButton(CanvasW - 240, 20, 220, 46, FString::Printf(TEXT("メニュー %s"), PC ? *PC->UIKeyLabel(EKakurenboUIKey::Menu) : TEXT("")), EKakurenboUIAction::OpenMenu, 0, false, 19);
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
	const float Slide = (1.f - FadeIn) * 12.f;
	const FLinearColor Back(0.06f, 0.03f, 0.12f, 0.85f * Alpha);
	const FLinearColor Accent(1.f, 0.8f, 0.3f, 0.9f * Alpha);
	const AKakurenboGameState* State = GetWorld()->GetGameState<AKakurenboGameState>();
	const EKakurenboPhase Phase = State ? State->Phase : EKakurenboPhase::Hide;

	// 購入パート：左の状態表示の下（お店のパネルの左の空き）。上のお知らせ・お店の切り替えボタンと重ならない
	if (Phase == EKakurenboPhase::Shop)
	{
		constexpr float X = 20.f, W = 430.f, LineH = 30.f;
		const TArray<FString> Lines = WrapLines(Tip->Text, 20, W - 40.f, 5, true);
		const float Y = BelowStatusY + Slide;
		const float H = 44.f + Lines.Num() * LineH;
		Panel(X, Y, W, H, Back);
		Panel(X, Y, 6, H, Accent);
		Text(TEXT("ヒント"), X + 20, Y + 8, 16, FLinearColor(1.f, 0.8f, 0.3f, Alpha));
		for (int32 i = 0; i < Lines.Num(); ++i)
		{
			RichTextFit(Lines[i], X + 20, Y + 36 + i * LineH, 20, W - 40.f, FLinearColor::White, false, Alpha);
		}
		return;
	}

	// ほかのパート：画面の真ん中の列。左の状態表示・鬼の様子・自分の様子（左）と右のボタンにかからない幅。長ければ 2 行
	// 設置パートは画面の下が混んでいる（置く物の欄とボタン）ので上に出す。お知らせが出ていればその下
	const bool bNotice = State && !State->NoticeText.IsEmpty() && GetWorld()->GetTimeSeconds() < State->NoticeUntilTime;
	const float BaseY = Phase == EKakurenboPhase::Build ? (bNotice ? 182.f : 112.f) : CanvasH - 236.f;
	const float Y = BaseY + Slide;
	const float MaxW = FMath::Max(520.f, CanvasW - 880.f);
	constexpr float LabelW = 104.f; // 左の「ヒント」の分
	const TArray<FString> Lines = WrapLines(Tip->Text, 22, MaxW - LabelW - 30.f, 2, true);
	float TextW = 0.f;
	for (const FString& Line : Lines)
	{
		TextW = FMath::Max(TextW, MeasureRich(Line, 22));
	}
	const float W = FMath::Min(MaxW, TextW + LabelW + 30.f);
	const float Left = (CanvasW - W) * 0.5f;
	const float H = 56.f + (Lines.Num() - 1) * 30.f;
	Panel(Left, Y - 12, W, H, Back);
	Panel(Left, Y - 12, 6, H, Accent);
	Text(TEXT("ヒント"), Left + 20, Y - 2, 16, FLinearColor(1.f, 0.8f, 0.3f, Alpha));
	for (int32 i = 0; i < Lines.Num(); ++i)
	{
		RichTextFit(Lines[i], Left + LabelW, Y + i * 30.f, 22, W - LabelW - 20.f, FLinearColor::White, false, Alpha);
	}
}

TArray<FString> AKakurenboHUD::GetHelpLines(EKakurenboPhase Phase) const
{
	const AKakurenboPlayerController* PC = Cast<AKakurenboPlayerController>(GetOwningPlayerController());
	const AKakurenboGameMode* GM = GetWorld()->GetAuthGameMode<AKakurenboGameMode>();
	auto K = [PC](EKakurenboAction A) { return KeyOf(PC, A); };
	auto U = [PC](EKakurenboUIKey Key) { return PC ? PC->UIKeyLabel(Key) : FString(); };
	const bool bPad = PC && PC->IsUsingGamepad();
	const int32 NumSlots = GM ? GM->WallTypes.Num() + GM->TrapTypes.Num() : 5;
	TArray<FString> Lines;
	switch (Phase)
	{
	case EKakurenboPhase::Hide:
		Lines.Add(FString::Printf(TEXT("%s 歩く（カメラの向きに）　%s 見回す%s"), *U(EKakurenboUIKey::Move), *U(EKakurenboUIKey::Look), bPad ? TEXT("") : TEXT("　ホイール カメラの距離")));
		Lines.Add(FString::Printf(TEXT("%s 連打：コインを稼ぐ（音が出て、鬼が寄ってくる）"), *K(EKakurenboAction::Mash)));
		Lines.Add(FString::Printf(TEXT("%s %s：足音が小さく、低い家具の陰に隠れられる"), *K(EKakurenboAction::Sneak),
			(!UKakurenboSettings::Get(this) || UKakurenboSettings::Get(this)->bCrouchToggle) ? TEXT("しゃがむ・立つ（1 回押すごと）") : TEXT("押している間しゃがむ")));
		if (GM && GM->GetTrapTypesInShop(true).Num() > 0)
		{
			Lines.Add(FString::Printf(TEXT("%s だるまさんがころんだ：見ている鬼が止まる（宝石のお店で買う）"), *K(EKakurenboAction::UseItem)));
		}
		Lines.Add(FString::Printf(TEXT("%s 煙幕ダッシュ：煙の向こうは鬼から見えない%s"), *K(EKakurenboAction::Dash),
			GM && GM->IsDashUnlocked() ? TEXT("") : TEXT("（転生のお店で解放）")));
		Lines.Add(FString::Printf(TEXT("%s よじ登る：前の壁・低い家具に 1 段ずつ（隙間は越えられない）%s"), *K(EKakurenboAction::Climb), GM && GM->IsClimbUnlocked() ? TEXT("") : TEXT("（転生のお店で解放）")));
		Lines.Add(TEXT("鬼にさわられたら見つかる。時間まで逃げきれば大きな報酬"));
		Lines.Add(TEXT("鬼の足音（どたどた）とお宝のキラキラは、音の左右と大きさで場所がわかる"));
		break;
	case EKakurenboPhase::Shop:
		Lines.Add(bPad ? FString::Printf(TEXT("%s 商品を選ぶ　[A] 買う（壁と罠は、次の設置で置く）"), *U(EKakurenboUIKey::Navigate))
			: FString::Printf(TEXT("クリック・[1]〜[%d] 商品を買う（壁と罠は、次の設置で置く）"), FMath::Min(9, 3 + NumSlots)));
		Lines.Add(FString::Printf(TEXT("%s お店の切り替え（コイン → 宝石 → 転生）　上の［×1］［×10］［最大］でまとめ買い"), bPad ? *U(EKakurenboUIKey::SelectSlot) : *K(EKakurenboAction::SwitchShop)));
		Lines.Add(FString::Printf(TEXT("%s 壊れた壁・使った罠をまとめて補充"), *K(EKakurenboAction::Refill)));
		Lines.Add(bPad ? FString::Printf(TEXT("「転生」のボタンを 2 回 転生（ステージ %d から）"), GM ? GM->PrestigeSettings.MinStage : 5)
			: FString::Printf(TEXT("%s を 2 回 転生（ステージ %d から）"), *K(EKakurenboAction::Prestige), GM ? GM->PrestigeSettings.MinStage : 5));
		Lines.Add(FString::Printf(TEXT("%s 設置へ"), *K(EKakurenboAction::Next)));
		Lines.Add(FString::Printf(TEXT("%s カメラを回す　%s ズーム%s"), *U(EKakurenboUIKey::Rotate), *U(EKakurenboUIKey::Zoom), bPad ? TEXT("") : TEXT("　ホイールを押してドラッグ 回す")));
		break;
	case EKakurenboPhase::Build:
		Lines.Add(FString::Printf(TEXT("%s 置く　%s 回収　%s 置く物を選ぶ"), *U(EKakurenboUIKey::Place), *U(EKakurenboUIKey::PickUp), *U(EKakurenboUIKey::SelectSlot)));
		Lines.Add(FString::Printf(TEXT("%s スタート位置（青い印）をカーソルのマスへ"), *K(EKakurenboAction::SetStart)));
		Lines.Add(bPad ? FString::Printf(TEXT("%s・[十字キー] カーソル（カメラが付いてくる）　%s ズーム"), *U(EKakurenboUIKey::Cursor), *U(EKakurenboUIKey::Zoom))
			: FString::Printf(TEXT("%s カメラを動かす　%s ズーム"), *U(EKakurenboUIKey::Move), *U(EKakurenboUIKey::Zoom)));
		Lines.Add(bPad ? FString::Printf(TEXT("%s 回す・傾ける"), *U(EKakurenboUIKey::Rotate))
			: FString::Printf(TEXT("%s 回す　%s 傾ける　%s 真上に戻す（ホイールを押してドラッグでも）"),
				*U(EKakurenboUIKey::Rotate), *K(EKakurenboAction::TiltDown), *K(EKakurenboAction::TiltUp)));
		Lines.Add(FString::Printf(TEXT("%s を 2 回 全部回収　%s 購入へ戻る　%s かくれんぼ開始"),
			*K(EKakurenboAction::ClearAll), *K(EKakurenboAction::BackToShop), *K(EKakurenboAction::Next)));
		Lines.Add(FString::Printf(TEXT("一方通行ののれん：置いたのれんを %s で向きを変える（矢印の向きにだけ鬼が通れる）"), *U(EKakurenboUIKey::Place)));
		break;
	case EKakurenboPhase::Result:
		Lines.Add(FString::Printf(TEXT("%s・ボタン 次へ"), *K(EKakurenboAction::Next)));
		Lines.Add(FString::Printf(TEXT("%s カメラを回す　%s ズーム"), *U(EKakurenboUIKey::Rotate), *U(EKakurenboUIKey::Zoom)));
		break;
	default:
		break;
	}
	Lines.Add(FString::Printf(TEXT("%s メニュー（設定・キー設定・タイトルへ）　%s 操作説明"), *U(EKakurenboUIKey::Menu), *K(EKakurenboAction::Help)));
	if (bPad)
	{
		Lines.Add(TEXT("ボタンは [十字キー] で選んで [A] で押す　[B] もどる"));
	}
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

	// 題名「かせぐんぼ」のロゴ。アセットが無ければ（別のパソコンで取り込む前など）文字で描く
	if (!bTitleLogoSearched)
	{
		bTitleLogoSearched = true;
		// 無いパッケージを LoadObject すると警告が出るので、あるときだけ読む
		if (!TitleLogoAsset.IsNull() && FPackageName::DoesPackageExist(TitleLogoAsset.ToSoftObjectPath().GetLongPackageName()))
		{
			TitleLogo = TitleLogoAsset.LoadSynchronous();
		}
	}
	if (TitleLogo && TitleLogo->GetResource())
	{
		// 幅 760（1080p 基準）。高さは絵の縦横比から
		const float LogoW = 760.f;
		const float LogoH = LogoW * TitleLogo->GetSizeY() / FMath::Max(1, TitleLogo->GetSizeX());
		FCanvasTileItem Tile(FVector2D(80.f, 116.f) * UIScale, TitleLogo->GetResource(), FVector2D(LogoW, LogoH) * UIScale, FLinearColor::White);
		Tile.BlendMode = SE_BLEND_Translucent; // 背景が透明な PNG
		Canvas->DrawItem(Tile);
	}
	else
	{
		Text(TEXT("かせぐんぼ"), 110, 170, 110, Gold);
	}
	Text(TEXT("夜の館で、鬼から隠れて稼ぐ"), 120, 362, 22, PaleText);

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
				: FString(TEXT("セーブなし"));
			// 右寄せ。「つづきから」の文字の右に残っている幅に収める（入らなければ小さく・「…」）。前は 720p で文字が重なっていた
			const float InfoRight = 110 + 460 - 20;
			const float InfoMaxW = InfoRight - (146 + MeasureText(Item.Label, 30) + 16);
			const float InfoW = FMath::Min(MeasureText(Info, FitSize(Info, 18, InfoMaxW, false)), InfoMaxW);
			TextFit(Info, InfoRight - InfoW, Y + 24, 18, InfoMaxW, bEnabled ? Gold : FLinearColor(0.45f, 0.42f, 0.5f));
		}
		Y += 82.f;
	}
	RichText(PC && PC->IsUsingGamepad() ? TEXT("[十字キー] と [A] で選ぶ") : TEXT("クリック・[↑][↓] と [Enter] で選ぶ"), 116, CanvasH - 70, 18, Gray);
}

// ---------------------------------------------------------------- メニュー

void AKakurenboHUD::DrawMenu(AKakurenboGameMode* GM)
{
	const AKakurenboPlayerController* PC = Cast<AKakurenboPlayerController>(GetOwningPlayerController());
	if (!PC)
	{
		return;
	}
	// 確認の「はい」の Enter はキーボードだけ（コントローラーはボタンを選んで A）
	const bool bPad = PC->IsUsingGamepad();
	switch (PC->GetMenu())
	{
	case EKakurenboMenu::Pause:    DrawPauseMenu(GM); break;
	case EKakurenboMenu::Settings: DrawSettings(); break;
	case EKakurenboMenu::Keys:     DrawKeyConfig(); break;
	case EKakurenboMenu::Help:     DrawHelp(GM->GetPhase()); break;
	case EKakurenboMenu::OniIntro: DrawOniIntro(GM); break;
	case EKakurenboMenu::ConfirmNewGame:
		DrawConfirm(TEXT("はじめから"), TEXT("セーブを消して、ステージ 1 からはじめます。よろしいですか？"), bPad ? TEXT("はじめから") : TEXT("はじめから [Enter]"),
			FString::Printf(TEXT("やめる %s"), *PC->UIKeyLabel(EKakurenboUIKey::Cancel)));
		break;
	case EKakurenboMenu::ConfirmTitle:
		DrawConfirm(TEXT("タイトルへ"), TEXT("このかくれんぼで稼いだコインはなくなります（始める前の状態から、つづきを遊べます）"), bPad ? TEXT("タイトルへ") : TEXT("タイトルへ [Enter]"),
			FString::Printf(TEXT("つづける %s"), *PC->UIKeyLabel(EKakurenboUIKey::Cancel)));
		break;
	default:
		break;
	}
	if (bPad)
	{
		const float CanvasH = Canvas->ClipY / UIScale;
		RichText(TEXT("[十字キー] 選ぶ　[A] 決める　[B] もどる"), -1.f, CanvasH - 54, 18, PaleText, true, 0.85f);
	}
}

void AKakurenboHUD::DrawPauseMenu(AKakurenboGameMode* GM)
{
	const float CanvasW = Canvas->ClipX / UIScale;
	const float W = 560, H = 560, X = (CanvasW - W) * 0.5f, Y = 220;
	Panel(X, Y, W, H, FLinearColor(0.06f, 0.02f, 0.11f, 0.95f));
	Frame(X, Y, W, H, FLinearColor(0.75f, 0.55f, 1.f, 0.6f), 2.f);
	Text(GM->GetPhase() == EKakurenboPhase::Hide ? TEXT("ひと休み") : TEXT("メニュー"), 0, Y + 28, 36, FLinearColor::White, true);
	const AKakurenboPlayerController* PC = Cast<AKakurenboPlayerController>(GetOwningPlayerController());
	const FString Labels[] = { FString::Printf(TEXT("つづける %s"), PC ? *PC->UIKeyLabel(EKakurenboUIKey::Cancel) : TEXT("")), TEXT("操作説明"),
		TEXT("設定（音量・感度・キー）"), TEXT("鬼の図鑑"), TEXT("タイトルへ") };
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
	const AKakurenboPlayerController* PC = Cast<AKakurenboPlayerController>(GetOwningPlayerController());
	const float CanvasW = Canvas->ClipX / UIScale;
	const float W = 960, H = 900, X = (CanvasW - W) * 0.5f, Y = 96;
	Panel(X, Y, W, H, FLinearColor(0.06f, 0.02f, 0.11f, 0.96f));
	Frame(X, Y, W, H, FLinearColor(0.75f, 0.55f, 1.f, 0.6f), 2.f);
	Text(TEXT("設定"), 0, Y + 24, 36, FLinearColor::White, true);

	const float LabelX = X + 60, ControlX = X + 360, SliderW = 400;
	float RowY = Y + 100;
	auto Label = [&](const TCHAR* Str) { Text(Str, LabelX, RowY + 8, 24, FLinearColor::White); };
	auto Percent = [](float V) { return FString::Printf(TEXT("%d%%"), FMath::RoundToInt(V * 100.f)); };

	Label(TEXT("全体の音量"));
	DrawSlider(ControlX, RowY, SliderW, S->MasterVolume, static_cast<int32>(EKakurenboSettingSlider::Master), Percent(S->MasterVolume));
	RowY += 64;
	Label(TEXT("BGM"));
	DrawSlider(ControlX, RowY, SliderW, S->MusicVolume, static_cast<int32>(EKakurenboSettingSlider::Music), Percent(S->MusicVolume));
	RowY += 64;
	Label(TEXT("効果音"));
	DrawSlider(ControlX, RowY, SliderW, S->SfxVolume, static_cast<int32>(EKakurenboSettingSlider::Sfx), Percent(S->SfxVolume));
	RowY += 64;
	Label(TEXT("マウス感度"));
	const float Sens01 = (S->MouseSensitivity - UKakurenboSettings::MinMouseSensitivity) / (UKakurenboSettings::MaxMouseSensitivity - UKakurenboSettings::MinMouseSensitivity);
	DrawSlider(ControlX, RowY, SliderW, Sens01, static_cast<int32>(EKakurenboSettingSlider::Mouse),
		FString::Printf(TEXT("×%.2f"), S->MouseSensitivity / UKakurenboSettings::DefaultMouseSensitivity));
	RowY += 64;
	Label(TEXT("スティック感度"));
	const float Pad01 = (S->GamepadSensitivity - UKakurenboSettings::MinGamepadSensitivity) / (UKakurenboSettings::MaxGamepadSensitivity - UKakurenboSettings::MinGamepadSensitivity);
	DrawSlider(ControlX, RowY, SliderW, Pad01, static_cast<int32>(EKakurenboSettingSlider::Gamepad), FString::Printf(TEXT("×%.2f"), S->GamepadSensitivity));
	RowY += 64;
	Label(TEXT("上下の向き"));
	DrawToggle(ControlX, RowY, 260, S->bInvertMouseY, static_cast<int32>(EKakurenboSettingToggle::InvertY), TEXT("反転する"), TEXT("そのまま"));
	RowY += 58;
	Label(TEXT("しゃがむ"));
	DrawToggle(ControlX, RowY, 260, S->bCrouchToggle, static_cast<int32>(EKakurenboSettingToggle::CrouchToggle), TEXT("1 回押して切り替え"), TEXT("押している間"));
	RowY += 58;
	Label(TEXT("操作のヒント"));
	DrawToggle(ControlX, RowY, 260, S->bShowHints, static_cast<int32>(EKakurenboSettingToggle::Hints), TEXT("表示する"), TEXT("表示しない"));
	DrawButton(ControlX + 280, RowY, 280, 44, TEXT("ヒントをもう一度見る"), EKakurenboUIAction::ResetTips, 0, false, 18);
	RowY += 58;
	Label(TEXT("画面"));
	DrawToggle(ControlX, RowY, 260, UKakurenboSettings::IsFullscreen(), static_cast<int32>(EKakurenboSettingToggle::Fullscreen), TEXT("フルスクリーン"), TEXT("ウィンドウ"));
	RowY += 58;
	Label(TEXT("コントローラーの配置"));
	DrawToggle(ControlX, RowY, 260, UKakurenboSettings::IsNintendoLayout(), static_cast<int32>(EKakurenboSettingToggle::PadLayout), TEXT("任天堂"), TEXT("Xbox"));
	TextFit(UKakurenboSettings::IsNintendoLayout() ? TEXT("右の A で決定・下の B で戻る") : TEXT("下の A で決定・右の B で戻る"), ControlX + 280, RowY + 10, 18, X + W - (ControlX + 280) - 30.f, FLinearColor(0.75f, 0.7f, 0.85f));
	RowY += 84;

	DrawButton(X + 60, RowY, 400, 60, TEXT("キー設定…"), EKakurenboUIAction::OpenKeys, 0, false, 24);
	DrawButton(X + W - 460, RowY, 400, 60, FString::Printf(TEXT("もどる %s"), PC ? *PC->UIKeyLabel(EKakurenboUIKey::Cancel) : TEXT("")), EKakurenboUIAction::Back, 0, false, 24, Gold);
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
	const float W = 1420, H = 900, X = (CanvasW - W) * 0.5f, Y = 90;
	Panel(X, Y, W, H, FLinearColor(0.06f, 0.02f, 0.11f, 0.97f));
	Frame(X, Y, W, H, FLinearColor(0.75f, 0.55f, 1.f, 0.6f), 2.f);
	Text(TEXT("キー設定"), 0, Y + 20, 34, FLinearColor::White, true);

	EKakurenboAction WaitingAction = EKakurenboAction::Count;
	bool bWaitingSecondary = false;
	const bool bWaiting = PC->IsWaitingForKey(WaitingAction, bWaitingSecondary);

	// 1 行 = 操作の名前・キー 2 つ（クリックで変える）・コントローラーのボタン（決まっていて変えられない）
	constexpr int32 ActionCount = static_cast<int32>(EKakurenboAction::Count);
	const int32 PerColumn = (ActionCount + 1) / 2;
	for (int32 Col = 0; Col < 2; ++Col)
	{
		const float ColX = X + 40 + Col * 690;
		Text(TEXT("キーボード・マウス"), ColX + 260, Y + 66, 15, Gray);
		Text(TEXT("コントローラー"), ColX + 540, Y + 66, 15, Gray);
	}
	for (int32 i = 0; i < ActionCount; ++i)
	{
		const EKakurenboAction Action = static_cast<EKakurenboAction>(i);
		const float ColX = X + 40 + (i / PerColumn) * 690;
		const float RowY = Y + 90 + (i % PerColumn) * 56;
		TextFit(UKakurenboSettings::ActionDisplayName(Action).ToString(), ColX, RowY + 10, 20, 250.f, FLinearColor::White);
		for (int32 Slot = 0; Slot < 2; ++Slot)
		{
			const bool bThisWaiting = bWaiting && WaitingAction == Action && bWaitingSecondary == (Slot == 1);
			const FKey Key = S->GetKey(Action, Slot == 1);
			const FString Label = bThisWaiting ? FString(TEXT("キーを押す…")) : UKakurenboSettings::KeyLabel(Key);
			DrawButton(ColX + 260 + Slot * 140, RowY, 132, 46, Label, EKakurenboUIAction::KeySlot, i * 2 + Slot, bThisWaiting, 17,
				bThisWaiting ? FLinearColor::Black : (Key.IsValid() ? FLinearColor::White : Gray));
		}
		const FKey Pad = UKakurenboSettings::GamepadKey(Action);
		Panel(ColX + 540, RowY, 110, 46, FLinearColor(0.f, 0.f, 0.f, 0.35f));
		if (Pad.IsValid())
		{
			RichText(FString::Printf(TEXT("[%s]"), *UKakurenboSettings::KeyLabel(Pad)), ColX + 595, RowY + 10, 16, FLinearColor::White, true, 0.9f);
		}
		else
		{
			Text(Action == EKakurenboAction::CameraLeft || Action == EKakurenboAction::CameraRight || Action == EKakurenboAction::TiltDown || Action == EKakurenboAction::TiltUp
				? TEXT("R スティック") : TEXT("ボタンで"), ColX + 556, RowY + 12, 15, Gray);
		}
	}
	const float FootY = Y + 90 + PerColumn * 56 + 10;
	RichTextFit(PC->IsUsingGamepad()
		? FString(TEXT("キーボードの欄を選んで [A]、使いたいキーを押す　[B] 取り消し　コントローラーのボタンは決まっている（右の列）"))
		: FString(TEXT("欄をクリックして、使いたいキーを押す　[Esc] 取り消し　右クリック：2 つ目を外す　ほかの操作と同じキーなら入れ替わる")), -1.f, FootY, 17, W - 60.f, PaleText, true);
	DrawButton(X + 60, Y + H - 84, 380, 60, TEXT("初期設定に戻す"), EKakurenboUIAction::ResetKeys, 0, false, 22);
	DrawButton(X + W - 440, Y + H - 84, 380, 60, FString::Printf(TEXT("もどる %s"), *PC->UIKeyLabel(EKakurenboUIKey::Cancel)), EKakurenboUIAction::Back, 0, false, 22, Gold);
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
	const AKakurenboPlayerController* PC = Cast<AKakurenboPlayerController>(GetOwningPlayerController());
	DrawButton(X + W - 340, Y + H - 72, 300, 54, FString::Printf(TEXT("もどる %s"), PC ? *PC->UIKeyLabel(EKakurenboUIKey::Cancel) : TEXT("")), EKakurenboUIAction::Back, 0, false, 22, Gold);
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

// ---------------------------------------------------------------- 鬼のチュートリアル・図鑑（M15）

FString AKakurenboHUD::GetOniCatchphrase(EOniType Type)
{
	switch (Type)
	{
	case EOniType::Balanced: return TEXT("いちばん普通の鬼。音に寄ってきて、見つけたら追いかけてくる");
	case EOniType::Scout:    return TEXT("足が速くて耳がいい。でも曲がり角は苦手");
	case EOniType::Breaker:  return TEXT("自分からは探さない力持ち。見えた壁を壊しに来る");
	case EOniType::Careful:  return TEXT("仲間と手分けして、広い場所から順にしらみつぶし");
	case EOniType::Treasure: return TEXT("お宝が大好き。お宝のまわりをぐるぐる見回る");
	case EOniType::Detector: return TEXT("動かない見張り。見つかると仲間を呼ばれる。おとりの音でも呼ぶ");
	default:                 return FString();
	}
}

TArray<FString> AKakurenboHUD::GetOniFeatureLines(EOniType Type, const FKakurenboOniTypeRow& Row)
{
	// 数字は OniTypes.csv から（CSV を変えても説明がずれないように）
	auto M = [](float Cm) { return UKakurenboLibrary::FormatStatNumber(Cm / 100.0); };
	const FString Sight = FString::Printf(TEXT("%sm・%d 度"), *M(Row.SightRadius), FMath::RoundToInt(Row.SightHalfAngle * 2.f));
	TArray<FString> Lines;
	switch (Type)
	{
	case EOniType::Balanced:
		Lines.Add(TEXT("連打・足音のした方へ調べに来る（遠い音ほど場所がずれる）"));
		Lines.Add(FString::Printf(TEXT("見つけると追いかけてくる（あなたの 2 倍の速さ）。視界 %s"), *Sight));
		Lines.Add(TEXT("壁はまわりごとまとめて壊す。壁で囲んだ場所も、ときどき壊して調べに来る"));
		Lines.Add(TEXT("対策：音を出しすぎない。壁・家具の陰に回りこんで見失わせる"));
		break;
	case EOniType::Scout:
		Lines.Add(TEXT("館を区画に分けて、しばらく行っていない所へ走る"));
		Lines.Add(FString::Printf(TEXT("視界が広い（%s）。連打は %s 倍・おとりは %s 倍遠くから聞こえる"), *Sight,
			*UKakurenboLibrary::FormatStatNumber(Row.HearingScale), *UKakurenboLibrary::FormatStatNumber(Row.DecoyHearingScale)));
		if (Row.CornerTurnAngle > 0.f && Row.CornerSlowScale < 1.f)
		{
			// 直線は速いが、曲がり角は苦手（ユーザーの決定 2026-10-18）
			Lines.Add(FString::Printf(TEXT("直線は速いが、曲がり角では %s 倍まで遅くなる"), *UKakurenboLibrary::FormatStatNumber(Row.CornerSlowScale)));
		}
		else
		{
			Lines.Add(TEXT("壁を壊す力は弱い"));
		}
		Lines.Add(FString::Printf(TEXT("対策：おとりで遠くへ呼ぶ。角を曲がって逃げる。トリモチに弱い（%s 倍の時間）"), *UKakurenboLibrary::FormatStatNumber(Row.StunScale)));
		break;
	case EOniType::Breaker:
		Lines.Add(FString::Printf(TEXT("連打の音は気にしない。おとりは聞こえにくい（%s 倍）。見えた壁を壊しに行く"), *UKakurenboLibrary::FormatStatNumber(Row.DecoyHearingScale)));
		Lines.Add(FString::Printf(TEXT("攻撃は前の %d×%d マス × %d 段をまとめて壊す（ためが長い：%s 秒）"), FMath::Max(1, Row.AttackBoxWidth), FMath::Max(1, Row.AttackBoxDepth),
			FMath::Max(1, Row.AttackBoxLevels), *UKakurenboLibrary::FormatStatNumber(Row.AttackWindup)));
		Lines.Add(FString::Printf(TEXT("目は近め（%s）。トリモチはすぐ抜け出し、直後に踏んだトリモチは壊す"), *Sight));
		Lines.Add(TEXT("対策：壁は離して置く（目が近い）。おとりはすぐそばに置く"));
		break;
	case EOniType::Careful:
		Lines.Add(TEXT("見たマスを「調べた」と仲間と分け合い、まだの場所へ行く"));
		Lines.Add(TEXT("細い道・行き止まり・壁で囲んだ場所は後回し（最後に壊して調べる）"));
		Lines.Add(TEXT("壁は目の前の 1 個ずつ壊す"));
		// ユーザーの指摘：何重の壁には強く、入り組んだ通路も得意。弱いのは「一度見た場所はしばらく見に来ない」こと
		Lines.Add(FString::Printf(TEXT("対策：一度見た場所は %d 秒ほど見に来ない。見終わった場所へこっそり移って隠れ直す"), FMath::RoundToInt(GetDefault<AOniCharacter>()->CarefulMemorySeconds)));
		break;
	case EOniType::Treasure:
		Lines.Add(TEXT("お宝（取られた場所も）へまっすぐ向かって、まわりを回る。じゃまな壁は壊す"));
		Lines.Add(TEXT("連打もおとりも聞かない。でも足音には敏感（ふつうの鬼の 2 倍くらい遠くから）"));
		Lines.Add(TEXT("壁を壊すのは苦手。壁で囲んだ場所は調べない"));
		Lines.Add(TEXT("対策：隠れたら動かずに連打。お宝はしのび足で取りに行く"));
		break;
	case EOniType::Detector:
		Lines.Add(FString::Printf(TEXT("決まった見張りの場所へ走り、首を回して遠くまで見張る（%s）"), *Sight));
		Lines.Add(FString::Printf(TEXT("見張りの場所のまわり（%sm）の壁は、すぐに壊してしまう"), *M(Row.AttackRadius)));
		Lines.Add(FString::Printf(TEXT("あなたを見つけると、%sm 以内の鬼をみんな呼び寄せる"), *M(Row.SummonRadius)));
		Lines.Add(FString::Printf(TEXT("自分は追いかけない。耳がとてもよく、連打の音がした方へ振り向く（%s 倍遠くから）"), *UKakurenboLibrary::FormatStatNumber(Row.HearingScale)));
		// 2026-10-18（ユーザーの指定）：おとり・迷い猫の音が聞こえると、その場所へ仲間を呼ぶ
		if (Row.DecoyHearingScale > 0.f)
		{
			Lines.Add(TEXT("おとり・迷い猫の音が聞こえると、その場所へまわりの鬼を呼び寄せる"));
			Lines.Add(TEXT("対策：見張りの近くでは連打しない。おとりを鳴らして、鬼をまとめて遠くへ集めさせる"));
		}
		else
		{
			Lines.Add(TEXT("対策：見張りから離れた壁・家具で視線を切る。見張りの近くでは連打しない"));
		}
		break;
	default:
		break;
	}
	return Lines;
}

void AKakurenboHUD::GetOniRatings(const FKakurenboOniTypeRow& Row, int32& OutSpeed, int32& OutSight, int32& OutHearing, int32& OutPower)
{
	// 星 1〜5（0 は「しない」）。標準鬼がだいたい 3。勘（Intuition）は隠しステータスなので出さない
	OutSpeed = FMath::Clamp(FMath::RoundToInt((Row.SpeedScale - 0.55f) / 0.15f), 1, 5);
	OutSight = FMath::Clamp(FMath::RoundToInt(Row.SightRadius / 300.f), 1, 5);
	OutHearing = Row.HearingScale <= 0.f ? 0 : FMath::Clamp(FMath::RoundToInt(1.f + Row.HearingScale * 2.f), 1, 5); // 標準 1.0 = 3・スピード 1.5 = 4・探知 2.0 = 5
	OutPower = FMath::Clamp(FMath::RoundToInt(Row.DamageScale * 2.f + 1.f), 1, 5);
}

void AKakurenboHUD::DrawOniIntro(AKakurenboGameMode* GM)
{
	const AKakurenboPlayerController* PC = Cast<AKakurenboPlayerController>(GetOwningPlayerController());
	if (!PC || !GM || PC->GetOniIntroPages().Num() == 0)
	{
		return;
	}
	const TArray<EOniType>& Pages = PC->GetOniIntroPages();
	const int32 Page = FMath::Clamp(PC->GetOniIntroPage(), 0, Pages.Num() - 1);
	const EOniType Type = Pages[Page];
	const FKakurenboOniTypeRow Row = GM->GetOniTypeRow(Type);
	const FLinearColor TypeColor = FLinearColor::LerpUsingHSV(Row.Color, FLinearColor::White, 0.15f);

	const float CanvasW = Canvas->ClipX / UIScale;
	const float W = 1560, H = 780, X = (CanvasW - W) * 0.5f, Y = 140;
	Panel(X, Y, W, H, FLinearColor(0.05f, 0.02f, 0.09f, 0.97f));
	Frame(X, Y, W, H, FLinearColor(TypeColor.R, TypeColor.G, TypeColor.B, 0.8f), 3.f);
	Text(PC->IsOniBook() ? TEXT("鬼の図鑑") : TEXT("新しい鬼が来る！"), X + 40, Y + 22, 30, PC->IsOniBook() ? PaleText : Bad);
	if (Pages.Num() > 1)
	{
		Text(FString::Printf(TEXT("%d / %d"), Page + 1, Pages.Num()), X + W - 120, Y + 28, 24, Gray);
	}

	// 写真（スタジオで撮った見本の鬼）。まだ撮れていなければ色の板
	const float PhotoX = X + 40, PhotoY = Y + 86, PhotoSize = 560;
	Panel(PhotoX - 6, PhotoY - 6, PhotoSize + 12, PhotoSize + 12, FLinearColor(TypeColor.R, TypeColor.G, TypeColor.B, 0.9f));
	const AKakurenboOniStudio* Studio = GM->GetOniStudio();
	UTextureRenderTarget2D* Photo = Studio ? Studio->GetPortrait(Type) : nullptr;
	if (Photo && Photo->GetResource())
	{
		FCanvasTileItem Tile(FVector2D(PhotoX, PhotoY) * UIScale, Photo->GetResource(), FVector2D(PhotoSize, PhotoSize) * UIScale, FLinearColor::White);
		Tile.BlendMode = SE_BLEND_Opaque;
		Canvas->DrawItem(Tile);
	}
	else
	{
		Panel(PhotoX, PhotoY, PhotoSize, PhotoSize, FLinearColor(Row.Color.R * 0.4f, Row.Color.G * 0.4f, Row.Color.B * 0.4f, 1.f));
		Text(TEXT("？"), PhotoX + PhotoSize * 0.5f - 30, PhotoY + PhotoSize * 0.4f, 90, FLinearColor::White);
	}

	// 名前・ひとこと
	const float TX = X + 650, TW = W - 690;
	TextFit(Row.DisplayName.IsEmpty() ? FString(TEXT("鬼")) : Row.DisplayName.ToString(), TX, Y + 84, 52, TW, TypeColor);
	TextFit(GetOniCatchphrase(Type), TX, Y + 156, 24, TW, FLinearColor::White);

	// 能力（星）
	int32 Speed = 0, Sight = 0, Hearing = 0, Power = 0;
	GetOniRatings(Row, Speed, Sight, Hearing, Power);
	auto Stars = [](int32 N) { return N <= 0 ? FString(TEXT("―（気にしない）")) : FString::ChrN(N, TEXT('★')) + FString::ChrN(5 - N, TEXT('☆')); };
	const TPair<const TCHAR*, int32> Ratings[] = { { TEXT("足の速さ"), Speed }, { TEXT("目のよさ"), Sight }, { TEXT("耳（連打の音）"), Hearing }, { TEXT("壁をこわす力"), Power } };
	for (int32 i = 0; i < UE_ARRAY_COUNT(Ratings); ++i)
	{
		const float RY = Y + 214 + (i / 2) * 44;
		const float RX = TX + (i % 2) * (TW * 0.5f);
		Text(Ratings[i].Key, RX, RY, 21, Gray);
		Text(Stars(Ratings[i].Value), RX + 190, RY - 2, 23, Ratings[i].Value <= 0 ? Gray : Gold);
	}

	// 特徴（最後の行は「対策」なので目立たせる）
	const TArray<FString> Lines = GetOniFeatureLines(Type, Row);
	float LY = Y + 324;
	for (int32 i = 0; i < Lines.Num(); ++i)
	{
		const bool bTip = Lines[i].StartsWith(TEXT("対策"));
		if (bTip)
		{
			Panel(TX - 12, LY - 8, TW + 12, 46, FLinearColor(0.15f, 0.35f, 0.15f, 0.45f));
		}
		else
		{
			Panel(TX - 2, LY + 10, 8, 8, TypeColor);
		}
		TextFit(Lines[i], TX + (bTip ? 0 : 18), LY, 22, TW - (bTip ? 0 : 18), bTip ? Good : FLinearColor::White);
		LY += bTip ? 60 : 52;
	}

	// ボタン：まえ / つぎ（最後は「わかった」）
	const bool bLast = Page == Pages.Num() - 1;
	const float BY = Y + H - 96;
	if (Page > 0)
	{
		DrawButton(TX, BY, 260, 64, TEXT("◀ まえ"), EKakurenboUIAction::OniPage, -1, false, 24);
	}
	const FString NextLabel = !bLast ? FString(TEXT("つぎの鬼 ▶")) : PC->IsOniBook() ? FString(TEXT("もどる")) : FString(TEXT("わかった！"));
	// 出てすぐ（1 秒）は押せないので、ボタンを薄く出す（読む前に消えないように）
	const bool bCanTurn = PC->CanTurnOniIntroPage();
	DrawButton(X + W - 420, BY, 380, 64, PC->IsUsingGamepad() ? NextLabel : NextLabel + TEXT(" [Enter]"), EKakurenboUIAction::OniPage, 1, false, 26,
		bCanTurn ? Gold : FLinearColor(0.55f, 0.5f, 0.45f, 0.6f));
}

// ---------------------------------------------------------------- 購入

void AKakurenboHUD::DrawShop(AKakurenboGameState* State, AKakurenboGameMode* GM)
{
	const AKakurenboPlayerController* PC = Cast<AKakurenboPlayerController>(GetOwningPlayerController());
	const EKakurenboShopTab Tab = PC ? PC->ShopTab : EKakurenboShopTab::Coin;
	const bool bPrestigeTab = Tab == EKakurenboShopTab::Prestige;
	const bool bGemTab = Tab == EKakurenboShopTab::Gem;
	const TArray<FShopItemView> Items = bPrestigeTab ? GM->GetPrestigeShopItems() : bGemTab ? GM->GetGemShopItems() : GM->GetShopItems();
	const int32 BuyCount = PC ? PC->GetBuyCount() : 1;

	// 配置は固定（お店を切り替えても、転生できるようになっても、ボタンの位置が動かないように）。
	// 上端は画面上部のお知らせ（y 116〜160）と重ならない高さ。1 商品 = 名前・説明・壊れた数の 3 行
	constexpr int32 MaxRows = 8;
	const float X = 470, Y = 168, W = 980;
	const float RowH = 70.f * FMath::Min(1.f, static_cast<float>(MaxRows) / FMath::Max(MaxRows, Items.Num()));
	const float RowsTop = Y + 70;
	const float ButtonsY = RowsTop + MaxRows * 70.f + 8;
	const float ButtonH = 50;
	// 列：名前（左）・値段と「クリックで買う」（右）。説明は値段の列の手前まで（かぶらないように）
	const float CostX = X + 730;
	const float CostW = W - (CostX - X) - 20;
	const float NameW = CostX - (X + 24) - 12;
	const float DescW = CostX - (X + 64) - 12;
	Panel(X, Y, W, ButtonsY + ButtonH + 12 - Y);

	// お店の切り替え（クリックか Tab）：コイン → 宝石 → 転生
	DrawButton(X + 16, Y + 12, 220, 44, TEXT("コインのお店"), EKakurenboUIAction::ShopTab, static_cast<int32>(EKakurenboShopTab::Coin), Tab == EKakurenboShopTab::Coin, 21,
		Tab == EKakurenboShopTab::Coin ? FLinearColor::Black : FLinearColor::White);
	DrawButton(X + 244, Y + 12, 220, 44, FString::Printf(TEXT("宝石のお店（%d）"), State->Gems), EKakurenboUIAction::ShopTab, static_cast<int32>(EKakurenboShopTab::Gem), bGemTab, 21,
		bGemTab ? FLinearColor::Black : GemTextColor);
	DrawButton(X + 472, Y + 12, 250, 44, FString::Printf(TEXT("転生のお店（%d pt）"), State->PrestigePoints), EKakurenboUIAction::ShopTab, static_cast<int32>(EKakurenboShopTab::Prestige), bPrestigeTab, 21,
		bPrestigeTab ? FLinearColor::Black : Purple);
	// まとめ買い（×1 / ×10 / 買えるだけ）。転生のお店は 1 つずつ
	if (!bPrestigeTab)
	{
		static const TCHAR* AmountLabels[] = { TEXT("×1"), TEXT("×10"), TEXT("最大") };
		for (int32 i = 0; i < 3; ++i)
		{
			const bool bOn = PC && static_cast<int32>(PC->BuyAmount) == i;
			DrawButton(X + 738 + i * 76, Y + 12, 70, 44, AmountLabels[i], EKakurenboUIAction::BuyAmount, i, bOn, 19, bOn ? FLinearColor::Black : FLinearColor::White);
		}
	}
	const bool bPad = PC && PC->IsUsingGamepad();

	for (int32 i = 0; i < Items.Num(); ++i)
	{
		const FShopItemView& Item = Items[i];
		const int32 Wallet = Item.bPrestigeItem ? State->PrestigePoints : Item.bGemItem ? State->Gems : 0;
		const bool bAffordable = !Item.bLocked && !Item.bMaxed && (Item.bPrestigeItem || Item.bGemItem ? Wallet >= Item.Cost : State->Coins >= Item.Cost);
		const float RowY = RowsTop + i * RowH;

		// 行全体がボタン（カーソルを乗せる・コントローラーで選ぶと明るくなる）
		AddButton(X + 8, RowY - 4, W - 16, RowH - 2, EKakurenboUIAction::ShopItem, i);
		const bool bFocused = PC && PC->IsUIFocused(EKakurenboUIAction::ShopItem, i);
		const bool bHovered = IsCursorOver(X + 8, RowY - 4, W - 16, RowH - 2) || bFocused;
		if (bHovered)
		{
			Panel(X + 8, RowY - 4, W - 16, RowH - 2, bAffordable ? FLinearColor(0.35f, 0.35f, 0.45f, 0.6f) : FLinearColor(0.3f, 0.15f, 0.15f, 0.5f));
		}
		if (bFocused)
		{
			Frame(X + 6, RowY - 6, W - 12, RowH + 2, FLinearColor(Gold.R, Gold.G, Gold.B, 0.9f), 2.f);
		}

		// 番号キーはキーボードのときだけ（コントローラーは選んで A）
		const FString Number = bPad ? FString() : FString::Printf(TEXT("[%d] "), i + 1);
		RichTextFit(FString::Printf(TEXT("%s%s  %s"), *Number, *Item.DisplayName.ToString(), *Item.OwnedText.ToString()), X + 24, RowY, 22, NameW, bAffordable ? FLinearColor::White : Gray);

		// 値段（まとめ買いなら、買える数と合計）
		FString CostText;
		if (Item.bLocked)
		{
			CostText = TEXT("まだ買えない");
		}
		else if (Item.bMaxed)
		{
			CostText = TEXT("最大");
		}
		else if (Item.bPrestigeItem)
		{
			CostText = FString::Printf(TEXT("%d pt"), FMath::RoundToInt(Item.Cost));
		}
		else if (Item.bGemItem)
		{
			CostText = FString::Printf(TEXT("宝石 %d"), FMath::RoundToInt(Item.Cost));
		}
		else if (BuyCount > 1)
		{
			int32 Count = 0;
			double Cost = 0.0;
			GM->GetShopItemBulkPrice(i, BuyCount, Count, Cost);
			CostText = Count > 0 ? FString::Printf(TEXT("×%d  %s コイン"), Count, *Big(Cost)) : FString::Printf(TEXT("%s コイン"), *Big(Cost));
		}
		else
		{
			CostText = FString::Printf(TEXT("%s コイン"), *Big(Item.Cost));
		}
		const FLinearColor CostColor = !bAffordable ? Gray : Item.bPrestigeItem ? Purple : Item.bGemItem ? GemTextColor : Gold;
		TextFit(CostText, CostX, RowY, 22, CostW, CostColor);
		if (bHovered && !Item.bMaxed && !Item.bLocked)
		{
			RichTextFit(bAffordable ? (bPad ? FString(TEXT("[A] 買う")) : FString(TEXT("クリックで買う"))) : FString(TEXT("足りない")), CostX, RowY + RowH * 0.42f, 15, CostW, bAffordable ? Good : Bad);
		}
		// 説明：長いときは 2 行に折り返す（説明は全部読めるように。入らなければ小さく）
		const FString Desc = Item.Description.ToString();
		if (MeasureText(Desc, 15) > DescW)
		{
			const TArray<FString> DescLines = WrapLines(Desc, 14, DescW, 2, false);
			for (int32 Line = 0; Line < DescLines.Num(); ++Line)
			{
				TextFit(DescLines[Line], X + 64, RowY + RowH * (Line == 0 ? 0.40f : 0.66f), 14, DescW, Gray);
			}
		}
		else
		{
			TextFit(Desc, X + 64, RowY + RowH * 0.42f, 15, DescW, Gray);
		}
		if (!Item.RepairText.IsEmpty())
		{
			// 壊れた壁・使った罠の数（足りなければ橙、在庫で直せるなら緑）。値段の列の下（説明の 2 行目とかぶらないように）
			TextFit(Item.RepairText.ToString(), CostX, RowY + RowH * 0.68f, 15, CostW, Item.bNeedsMoreForRepair ? Warn : Good);
		}
	}
	if (bGemTab && Items.Num() == 0)
	{
		Text(TEXT("宝石の道具がありません（Data/Traps.csv の bGemItem）"), 0, RowsTop + 20, 20, Gray, true);
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

	// 説明（パネルの下・いつも同じ位置）。画面の幅に収める
	const float CanvasW = Canvas->ClipX / UIScale;
	const float InfoW = CanvasW - 80.f;
	float InfoY = ButtonsY + ButtonH + 24;
	if (RefillCount > 0)
	{
		TextFit(FString::Printf(TEXT("壊れた壁・使った罠を直すには、あと %d 個（%s コイン）。まとめて補充は高いものから買えるだけ買う"), RefillCount, *Big(RefillCost)),
			0, InfoY, 18, InfoW, Warn, true);
	}
	else if (const UKakurenboGridSubsystem* Grid = GetWorld()->GetSubsystem<UKakurenboGridSubsystem>(); Grid && (Grid->GetTotalMissing() > 0 || Grid->GetMissingTrapCount() > 0))
	{
		TextFit(TEXT("壊れた壁・使った罠は、在庫から設置の始めに自動で直る"), 0, InfoY, 18, InfoW, Good, true);
	}
	InfoY += 28;
	const FString StartCoins = Big(GM->GetPrestigeStartCoins(State->PrestigeCount + 1));
	const FString CoinsPerPrestige = Big(GM->PrestigeSettings.StartCoinsPerPrestige);
	if (bGemTab)
	{
		TextFit(FString::Printf(TEXT("宝石はお宝 1 個で %d つ（ラウンドのお宝を全部取るとボーナス +%d）。宝石と宝石の道具は転生するとなくなる"), GM->GetGemsPerTreasure(), GM->GetAllTreasureBonusGems()),
			0, InfoY, 16, InfoW, GemTextColor, true);
	}
	else if (bPrestigeTab)
	{
		TextFit(FString::Printf(TEXT("転生ポイントは転生するともらえる。ここで買った強化は、転生してもずっと残る（転生するたびに、はじめのコインも +%s）"), *CoinsPerPrestige),
			0, InfoY, 16, InfoW, Purple, true);
	}
	else if (bCanPrestige)
	{
		TextFit(FString::Printf(TEXT("転生すると、コイン・ステージ・強化・在庫・置いた壁と罠はなくなり、%s コインから始まる（設計図・転生のお店の強化は残る。宝石と宝石の道具もなくなる）"), *StartCoins),
			0, InfoY, 16, InfoW, Gray, true);
	}
	else
	{
		TextFit(FString::Printf(TEXT("転生はステージ %d から（転生ポイントで永続強化が買える・転生するたびに、はじめのコイン +%s）"), GM->PrestigeSettings.MinStage, *CoinsPerPrestige),
			0, InfoY, 16, InfoW, Gray, true);
	}
	const UKakurenboSettings* S = UKakurenboSettings::Get(this);
	if (PC && (!S || S->bShowHints))
	{
		RichTextFit(bPad ? FString::Printf(TEXT("[十字キー] 選ぶ　[A] 買う・押す　%s お店　%s まとめて補充　%s 設置へ　%s カメラ"),
				*PC->UIKeyLabel(EKakurenboUIKey::SelectSlot), *KeyOf(PC, EKakurenboAction::Refill), *KeyOf(PC, EKakurenboAction::Next), *PC->UIKeyLabel(EKakurenboUIKey::Rotate))
			: FString::Printf(TEXT("クリック・[1]〜[%d] 買う　%s お店の切り替え　%s カメラを回す　ホイール ズーム"), FMath::Max(1, FMath::Min(Items.Num(), 9)),
				*KeyOf(PC, EKakurenboAction::SwitchShop), *PC->UIKeyLabel(EKakurenboUIKey::Rotate)),
			-1.f, InfoY + 30, 18, InfoW, PaleText, true);
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
	const bool bPad = PC && PC->IsUsingGamepad();
	if (PC && (!S || S->bShowHints))
	{
		// M15：枠は中の文の幅に合わせる（文が枠からはみ出さないように）
		const float HelpY = BelowStatusY;
		TArray<FString> HelpLines;
		HelpLines.Add(FString::Printf(TEXT("%s 置く　%s 回収　%s 置く物"), *PC->UIKeyLabel(EKakurenboUIKey::Place), *PC->UIKeyLabel(EKakurenboUIKey::PickUp),
			*PC->UIKeyLabel(EKakurenboUIKey::SelectSlot)));
		if (bPad)
		{
			HelpLines.Add(FString::Printf(TEXT("%s・[十字キー] カーソル　%s ズーム"), *PC->UIKeyLabel(EKakurenboUIKey::Cursor), *PC->UIKeyLabel(EKakurenboUIKey::Zoom)));
			HelpLines.Add(FString::Printf(TEXT("%s 回す・傾ける"), *PC->UIKeyLabel(EKakurenboUIKey::Rotate)));
		}
		else
		{
			HelpLines.Add(FString::Printf(TEXT("%s カメラを動かす　ホイール ズーム"), *PC->UIKeyLabel(EKakurenboUIKey::Move)));
			HelpLines.Add(FString::Printf(TEXT("%s 回す　%s 傾ける　%s 真上"), *PC->UIKeyLabel(EKakurenboUIKey::Rotate),
				*KeyOf(PC, EKakurenboAction::TiltDown), *KeyOf(PC, EKakurenboAction::TiltUp)));
		}
		HelpLines.Add(FString::Printf(TEXT("%s スタート位置（青い印）をカーソルへ"), *KeyOf(PC, EKakurenboAction::SetStart)));
		float HelpW = 0.f;
		for (const FString& Line : HelpLines)
		{
			HelpW = FMath::Max(HelpW, MeasureRich(Line, 18));
		}
		HelpW = FMath::Min(HelpW + 36.f, 620.f);
		Panel(20, HelpY, HelpW, 18 + HelpLines.Num() * 40, FLinearColor(0.03f, 0.01f, 0.06f, 0.6f));
		for (int32 i = 0; i < HelpLines.Num(); ++i)
		{
			RichTextFit(HelpLines[i], 36, HelpY + 14 + i * 40, 18, HelpW - 32.f, i == HelpLines.Num() - 1 ? Blue : FLinearColor::White);
		}
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

	// 壁と罠の在庫（下部中央に並べる。宝石の道具は持っているものだけ）。右下のボタンとかぶらない幅にする
	const TArray<FWallTypeDef> Walls = GM->GetEffectiveWallTypes();
	const int32 NumWalls = Walls.Num();
	const TArray<int32> Slots = PC ? PC->GetBuildSlots() : TArray<int32>();
	const int32 Num = Slots.Num();
	const float SlotW = FMath::Min(262.f, (CanvasW - 80.f) / FMath::Max(1, Num)), SlotH = 70;
	const float StartX = (CanvasW - SlotW * Num) * 0.5f;
	const float SlotY = CanvasH - 170;
	for (int32 Pos = 0; Pos < Num; ++Pos)
	{
		const int32 i = Slots[Pos];
		const bool bTrap = i >= NumWalls;
		const bool bSelected = PC && PC->SelectedBuildSlot == i;
		const int32 TrapIndex = i - NumWalls;
		const int32 Stock = bTrap
			? (State->TrapStock.IsValidIndex(TrapIndex) ? State->TrapStock[TrapIndex] : 0)
			: (State->WallStock.IsValidIndex(i) ? State->WallStock[i] : 0);
		const FText& Name = bTrap ? GM->TrapTypes[TrapIndex].DisplayName : Walls[i].DisplayName;
		const FLinearColor& Color = bTrap ? GM->TrapTypes[TrapIndex].Color : Walls[i].Color;

		const float X = StartX + Pos * SlotW;
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
		const FString Label = bPad || Pos >= 9 ? Name.ToString() : FString::Printf(TEXT("[%d] %s"), Pos + 1, *Name.ToString());
		RichTextFit(Label, X + 36, SlotY + 8, 19, SlotW - 48.f, Stock > 0 ? FLinearColor::White : Gray);
		// 欄がせまい（置く物が多い）ときは、耐久を「何発で壊れるか」だけにする
		const FString Detail = bTrap
			? FString::Printf(TEXT("在庫 %d"), Stock)
			: SlotW < 240.f ? FString::Printf(TEXT("在庫 %d・%d 発"), Stock, GM->GetHitsToBreak(Walls[i].MaxHP))
			: FString::Printf(TEXT("在庫 %d　耐久 %s（%d 発）"), Stock, *Stat(Walls[i].MaxHP), GM->GetHitsToBreak(Walls[i].MaxHP));
		TextFit(Detail, X + 36, SlotY + 40, 16, SlotW - 48.f, Gray);
	}

	// かくれんぼを始めるボタン（Enter と同じ）と、購入パートへ戻るボタン（B と同じ）
	DrawButton(CanvasW - 420, CanvasH - 260, 390, 60, FString::Printf(TEXT("かくれんぼ開始 ▶ %s"), *KeyOf(PC, EKakurenboAction::Next)), EKakurenboUIAction::NextPhase, 0, false, 22, Gold);
	DrawButton(CanvasW - 420, CanvasH - 330, 390, 54, FString::Printf(TEXT("◀ 購入へ戻る %s"), *KeyOf(PC, EKakurenboAction::BackToShop)), EKakurenboUIAction::BackToShop, 0, false, 20, FLinearColor::White);
	// 置いた壁・罠を全部回収（Delete と同じ。取り消せないので 2 回押す）
	const bool bClearPending = PC && PC->IsClearAllConfirmPending();
	DrawButton(CanvasW - 420, CanvasH - 400, 390, 54, bClearPending ? FString(TEXT("もう一度押すと全部回収")) : FString::Printf(TEXT("全部回収 %s"), *KeyOf(PC, EKakurenboAction::ClearAll)),
		EKakurenboUIAction::ClearAll, 0, false, 20, bClearPending ? Bad : FLinearColor::White);

	// カーソルの下に、そこでできること（置ける・回収できる・置けない理由）。
	// コントローラーのときは、カーソルのマス（画面の真ん中あたり）の下に出す
	FVector2D Cursor = FVector2D::ZeroVector;
	FKakurenboUIButton UnderCursor;
	bool bHasCursor = false;
	if (bPad)
	{
		FVector CursorWorld;
		bHasCursor = PC->GetGamepadBuildCursor(CursorWorld) && PC->ProjectWorldLocationToScreen(CursorWorld, Cursor, true);
	}
	else
	{
		bHasCursor = PC && PC->GetUICursorPosition(Cursor) && !FindButtonAt(Cursor, UnderCursor);
	}
	if (bHasCursor)
	{
		const float CX = Cursor.X / UIScale;
		const float CY = Cursor.Y / UIScale + 26.f;
		// 置ける・回収できるが両方あれば両方（コントローラーは A・X で選べる）。のれんの上なら「向きを変える」
		FString Hint;
		const bool bOneWayHere = PC->bHasTrapPickUpTarget && PC->IsRotatableTrapAt(PC->TrapPickUpCell);
		if (bOneWayHere)
		{
			Hint = FString::Printf(TEXT("%s 向きを変える"), *PC->UIKeyLabel(EKakurenboUIKey::Place));
		}
		else if (PC->bHasBuildTarget && PC->bCanPlaceAtTarget)
		{
			Hint = FString::Printf(TEXT("%s 置く"), *PC->UIKeyLabel(EKakurenboUIKey::Place));
		}
		if (PC->PickUpTarget || PC->bHasTrapPickUpTarget)
		{
			Hint += FString::Printf(TEXT("%s%s 回収"), Hint.IsEmpty() ? TEXT("") : TEXT("　"), *PC->UIKeyLabel(EKakurenboUIKey::PickUp));
		}
		// M15：画面の端で文が切れないよう、真ん中の位置を画面の中に収める
		auto ClampCenter = [CanvasW](float Center, float Width) { return FMath::Clamp(Center, Width * 0.5f + 12.f, CanvasW - Width * 0.5f - 12.f); };
		bDrawingCursorHint = true; // カーソルと一緒に動く案内（ほかの文と重なってもよい）
		if (!Hint.IsEmpty())
		{
			RichText(Hint, ClampCenter(CX, MeasureRich(Hint, 17)), CY, 17, PC->bCanPlaceAtTarget || bOneWayHere ? Good : Gold, true);
		}
		else if (PC->bHasBuildTarget && !PC->BuildTargetReason.IsEmpty())
		{
			const FString Reason = PC->BuildTargetReason.ToString();
			TextPx(Reason, ClampCenter(CX, MeasureText(Reason, 18)) * UIScale, CY * UIScale, 18, Bad, true);
		}
		bDrawingCursorHint = false;
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
	TextFit(FString::Printf(TEXT("このステージの鬼の攻撃力: %s"), *Damage), 0, CanvasH - 60, 18, CanvasW - 80.f, Gray, true);
	// 一方通行ののれん・身代わり人形を選んでいるときは、置く向きを教える
	static const TCHAR* DirNames[] = { TEXT("東（門の方）"), TEXT("南"), TEXT("西（門の反対）"), TEXT("北") };
	const ETrapKind SelectedKind = PC && PC->IsTrapSlotSelected() && GM->TrapTypes.IsValidIndex(PC->GetSelectedTrapType()) ? GM->TrapTypes[PC->GetSelectedTrapType()].Kind : ETrapKind::Sticky;
	if (SelectedKind == ETrapKind::OneWay)
	{
		TextFit(FString::Printf(TEXT("のれんの矢印の向きにだけ鬼が通れる（次に置く向き：%s）。置いたのれんをクリックすると向きが変わる"), DirNames[FMath::Clamp(GM->OneWayPlaceDir, 0, 3)]),
			0, CanvasH - 290, 18, FMath::Min(1000.f, CanvasW - 920.f), PaleText, true);
	}
	else if (SelectedKind == ETrapKind::FakeHider)
	{
		TextFit(FString::Printf(TEXT("人形は見つかると矢印の向きへ逃げる（次に置く向き：%s）。置いた人形をクリックすると向きが変わる"), DirNames[FMath::Clamp(GM->FakeHiderPlaceDir, 0, 3)]),
			0, CanvasH - 290, 18, FMath::Min(1000.f, CanvasW - 920.f), PaleText, true);
	}
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

	// 鬼とお宝の場所は矢印では出さない（音で見当をつける）。追いかけ始めた直後の「！」は、鬼の頭の上の玉が 3D の「!」に変わる（AOniCharacter::UpdateExclaim）。
	// 文字は描かないが、テキストの窓口には今までどおり「！」として渡す
	for (const AOniCharacter* Oni : GM->GetOnis())
	{
		const bool bJustSpotted = Oni && Oni->GetChaseStartTime() >= 0.f && GetWorld()->GetTimeSeconds() - Oni->GetChaseStartTime() < 1.5f;
		if (Oni && Oni->IsDarumaFrozen())
		{
			DrawWorldLabel(Oni->GetActorLocation() + FVector(0.f, 0.f, 150.f), TEXT("ピタッ"), FLinearColor(1.f, 0.55f, 0.5f));
		}
		else if (Oni && Oni->IsPettingCat())
		{
			DrawWorldLabel(Oni->GetActorLocation() + FVector(0.f, 0.f, 150.f), TEXT("なでなで♪"), FLinearColor(1.f, 0.75f, 0.45f));
		}
		else if (bJustSpotted)
		{
			NoteWorldLabel(Oni->GetActorLocation() + FVector(0.f, 0.f, 150.f), TEXT("！"));
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
	if (GM->IsDarumaActive())
	{
		Text(FString::Printf(TEXT("だるまさんが ころんだ　%d"), FMath::CeilToInt(GM->GetDarumaTimeLeft())), 0, 92, 26, FLinearColor(1.f, 0.5f, 0.45f), true);
		Text(GM->GetDarumaFrozenCount() > 0 ? FString::Printf(TEXT("鬼 %d 体が動けない　今のうちに逃げよう（ぶつかれば見つかる）"), GM->GetDarumaFrozenCount())
			: FString(TEXT("見えている鬼がいなかった")), 0, 128, 17, PaleText, true);
	}

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
	if (GM->IsMansionDark())
	{
		Chip(TEXT("館が真っ暗（鬼は近くしか見えない）"), FLinearColor(0.7f, 0.65f, 0.95f));
	}
	if (GM->IsGoldRushActive())
	{
		Chip(FString::Printf(TEXT("ゴールドラッシュ　連打のコイン ×%s"), *UKakurenboLibrary::FormatStatNumber(GM->GoldRushMashMultiplier)), FLinearColor(1.f, 0.82f, 0.25f));
	}
	// だるまさんがころんだ（持っている数）
	const int32 DarumaType = GM->FindTrapTypeOfKind(ETrapKind::Daruma);
	const int32 DarumaStock = State->TrapStock.IsValidIndex(DarumaType) ? State->TrapStock[DarumaType] : 0;
	if (DarumaStock > 0 && !GM->IsDarumaActive())
	{
		const FString DarumaText = FString::Printf(TEXT("%s だるまさんがころんだ ×%d"), *KeyOf(PC, EKakurenboAction::UseItem), DarumaStock);
		const float W = MeasureRich(DarumaText, 17) + 28;
		Panel(20, ChipY, W, 30, FLinearColor(0.12f, 0.02f, 0.03f, 0.6f));
		RichText(DarumaText, 34, ChipY + 4, 17, FLinearColor(1.f, 0.6f, 0.55f));
		ChipY -= 38;
	}

	// 使える操作（キーの形で。設定で消せる）
	if (bHints && PC)
	{
		// 準備の間（もういいかい）は連打できないので出さない
		FString Controls = FString::Printf(TEXT("%s 移動　%s しゃがむ"), *PC->UIKeyLabel(EKakurenboUIKey::Move), *KeyOf(PC, EKakurenboAction::Sneak));
		if (GM->CanMash())
		{
			Controls += FString::Printf(TEXT("　%s 連打"), *KeyOf(PC, EKakurenboAction::Mash));
		}
		if (GM->IsDashUnlocked())
		{
			Controls += FString::Printf(TEXT("　%s 煙幕"), *KeyOf(PC, EKakurenboAction::Dash));
		}
		if (GM->IsClimbUnlocked())
		{
			Controls += FString::Printf(TEXT("　%s よじ登る"), *KeyOf(PC, EKakurenboAction::Climb));
		}
		RichTextFit(Controls, -1.f, CanvasH - 64, 17, CanvasW - 80.f, PaleText, true, 0.7f);
	}
	// よじ登っている途中（動けない）
	if (Hider && Hider->IsClimbing())
	{
		Chip(TEXT("よじ登っている…"), Gold);
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
	TextFit(FString::Printf(TEXT("内訳：時間 %s ／ 連打 %s ／ お宝 %s ／ 逃げ切り報酬 %s"), *Big(State->TimeCoinsThisRound), *Big(State->MashCoinsThisRound),
		*Big(State->TreasureBonusCoinsThisRound), *Big(State->LastClearReward)), 0, Y + 222, 16, W - 40.f, Gray, true);
	TextFit(FString::Printf(TEXT("お宝: %d / %d 個（宝石 +%d）"), State->TreasuresCollectedThisRound, State->TreasuresThisRound, State->GemsThisRound), 0, Y + 246, 22, W - 40.f, Gold, true);
	if (State->GemsThisRound > 0)
	{
		const FString GemLine = State->bAllTreasuresThisRound
			? FString::Printf(TEXT("お宝コンプリート！ ボーナス 宝石 +%d（宝石のお店で道具と交換できる）"), GM->GetAllTreasureBonusGems())
			: FString(TEXT("全部取るとボーナスの宝石（宝石のお店で道具と交換できる）"));
		TextFit(GemLine, 0, Y + 276, 18, W - 40.f, GemTextColor, true);
	}
	if (State->LastRoundWallsDestroyed > 0 || State->TrapsTriggeredThisRound > 0)
	{
		TextFit(FString::Printf(TEXT("壊された壁 %d 個・使った罠 %d 個（在庫があれば次の設置で自動で直る）"), State->LastRoundWallsDestroyed, State->TrapsTriggeredThisRound), 0, Y + 304, 17, W - 40.f, Warn, true);
	}
	DrawButton(X + (W - 360) * 0.5f, Y + 340, 360, 64, FString::Printf(TEXT("つぎへ ▶ %s"), *KeyOf(PC, EKakurenboAction::Next)), EKakurenboUIAction::NextPhase, 0, false, 24, Gold);
}
