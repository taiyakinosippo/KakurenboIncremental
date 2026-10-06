#include "KakurenboHUD.h"

#include "CanvasItem.h"
#include "Engine/Canvas.h"
#include "Engine/Font.h"
#include "GlobalRenderResources.h"
#include "KakurenboGameMode.h"
#include "KakurenboGameState.h"
#include "KakurenboLibrary.h"
#include "KakurenboPlayerController.h"
#include "OniCharacter.h"

namespace
{
	FString Big(double V) { return UKakurenboLibrary::FormatBigNumber(V); }

	const FLinearColor Gold(1.f, 0.85f, 0.2f);
	const FLinearColor Gray(0.6f, 0.6f, 0.6f);
	const FLinearColor Good(0.4f, 1.f, 0.5f);
	const FLinearColor Bad(1.f, 0.35f, 0.3f);
	const FLinearColor Warn(1.f, 0.6f, 0.2f);

	FLinearColor OniStateColor(EOniState State)
	{
		switch (State)
		{
		case EOniState::Investigate: return Bad;
		case EOniState::Attack:      return Warn;
		default:                     return FLinearColor(0.95f, 0.95f, 0.95f);
		}
	}
}

void AKakurenboHUD::DrawHUD()
{
	Super::DrawHUD();

	if (!Canvas)
	{
		return;
	}
	UIScale = Canvas->ClipY / 1080.f;

	AKakurenboGameState* State = GetWorld()->GetGameState<AKakurenboGameState>();
	AKakurenboGameMode* GM = GetWorld()->GetAuthGameMode<AKakurenboGameMode>();
	if (!State || !GM)
	{
		return;
	}

	DrawStatusPanel(State, GM);

	switch (State->Phase)
	{
	case EKakurenboPhase::Shop:   DrawShop(State, GM); break;
	case EKakurenboPhase::Build:  DrawBuild(State, GM); break;
	case EKakurenboPhase::Hide:   DrawHide(State, GM); break;
	case EKakurenboPhase::Result: DrawResult(State, GM); break;
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

void AKakurenboHUD::Text(const FString& Str, float X, float Y, int32 Size, const FLinearColor& Color, bool bCenterX)
{
	TextPx(Str, bCenterX ? Canvas->ClipX * 0.5f : X * UIScale, Y * UIScale, Size, Color, bCenterX);
}

void AKakurenboHUD::TextPx(const FString& Str, float Px, float Py, int32 Size, const FLinearColor& Color, bool bCenterX)
{
	FCanvasTextItem Item(FVector2D(Px, Py), FText::FromString(Str), MakeFont(Size), Color);
	Item.EnableShadow(FLinearColor(0.f, 0.f, 0.f, 0.8f));
	Item.bCentreX = bCenterX;
	Canvas->DrawItem(Item);
}

void AKakurenboHUD::Panel(float X, float Y, float W, float H, const FLinearColor& Color)
{
	FCanvasTileItem Tile(FVector2D(X * UIScale, Y * UIScale), FVector2D(W * UIScale, H * UIScale), Color);
	Tile.BlendMode = SE_BLEND_Translucent;
	Canvas->DrawItem(Tile);
}

void AKakurenboHUD::TrianglePx(const FVector2D& A, const FVector2D& B, const FVector2D& C, const FLinearColor& Color)
{
	FCanvasTriangleItem Triangle(A, B, C, GWhiteTexture);
	Triangle.SetColor(Color);
	Triangle.BlendMode = SE_BLEND_Translucent;
	Canvas->DrawItem(Triangle);
}

// ---------------------------------------------------------------- 共通

void AKakurenboHUD::DrawStatusPanel(AKakurenboGameState* State, AKakurenboGameMode* GM)
{
	static const TCHAR* PhaseNames[] = { TEXT("購入パート"), TEXT("設置パート"), TEXT("かくれんぼ"), TEXT("リザルト") };

	Panel(20, 20, 380, 130);
	Text(FString::Printf(TEXT("ステージ %d   [%s]"), State->Stage, PhaseNames[static_cast<int32>(State->Phase)]), 36, 30, 22);
	Text(FString::Printf(TEXT("コイン  %s"), *Big(State->Coins)), 36, 64, 30, Gold);
	Text(FString::Printf(TEXT("連打 +%s / 時間 +%s/秒"), *Big(GM->GetMashIncome()), *Big(GM->GetTimeIncomePerSecond())), 36, 108, 18, Gray);
}

// ---------------------------------------------------------------- 購入

void AKakurenboHUD::DrawShop(AKakurenboGameState* State, AKakurenboGameMode* GM)
{
	const TArray<FShopItemView> Items = GM->GetShopItems();

	const float X = 560, Y = 160, W = 800;
	Panel(X, Y, W, 110 + Items.Num() * 80);
	Text(TEXT("購入パート"), 0, Y + 14, 34, FLinearColor::White, true);

	for (int32 i = 0; i < Items.Num(); ++i)
	{
		const FShopItemView& Item = Items[i];
		const bool bAffordable = State->Coins >= Item.Cost;
		const float RowY = Y + 80 + i * 80;
		Text(FString::Printf(TEXT("[%d] %s  %s"), i + 1, *Item.DisplayName.ToString(), *Item.OwnedText.ToString()), X + 24, RowY, 24, bAffordable ? FLinearColor::White : Gray);
		Text(FString::Printf(TEXT("%s コイン"), *Big(Item.Cost)), X + 560, RowY, 24, bAffordable ? Gold : Gray);
		Text(Item.Description.ToString(), X + 60, RowY + 38, 16, Gray);
	}

	const float BottomY = Y + 110 + Items.Num() * 80 + 16;
	Text(TEXT("数字キー: 購入　　Enter: 設置パートへ"), 0, BottomY, 22, FLinearColor::White, true);
	Text(TEXT("マウス・Q/E: カメラ回転　ホイール: ズーム"), 0, BottomY + 34, 16, Gray, true);
}

// ---------------------------------------------------------------- 設置

void AKakurenboHUD::DrawBuild(AKakurenboGameState* State, AKakurenboGameMode* GM)
{
	const AKakurenboPlayerController* PC = Cast<AKakurenboPlayerController>(GetOwningPlayerController());
	const float CanvasW = Canvas->ClipX / UIScale;
	const float CanvasH = Canvas->ClipY / UIScale;

	// 操作説明
	Panel(20, 170, 480, 196);
	Text(TEXT("WASD: 移動　Space: ジャンプ"), 36, 182, 18);
	Text(TEXT("左クリック: 置く　右クリック: 回収"), 36, 210, 18);
	Text(TEXT("数字キー: 壁の種類"), 36, 238, 18);
	Text(TEXT("Q/E・ホイールを押してドラッグ: 回転"), 36, 266, 18);
	Text(TEXT("ホイール: ズーム"), 36, 294, 18);
	Text(TEXT("Enter: かくれんぼ開始"), 36, 326, 22, Gold);

	// 壁の在庫（下部中央に並べる）
	const int32 Num = GM->WallTypes.Num();
	const float SlotW = 220, SlotH = 70;
	const float StartX = (CanvasW - SlotW * Num) * 0.5f;
	const float SlotY = CanvasH - 170;
	for (int32 i = 0; i < Num; ++i)
	{
		const FWallTypeDef& Def = GM->WallTypes[i];
		const bool bSelected = PC && PC->SelectedWallType == i;
		const int32 Stock = State->WallStock.IsValidIndex(i) ? State->WallStock[i] : 0;
		const float X = StartX + i * SlotW;
		Panel(X + 4, SlotY, SlotW - 8, SlotH, bSelected ? FLinearColor(0.9f, 0.75f, 0.2f, 0.6f) : FLinearColor(0.f, 0.f, 0.f, 0.55f));
		Panel(X + 12, SlotY + 10, 14, SlotH - 20, Def.Color);
		Text(FString::Printf(TEXT("[%d] %s"), i + 1, *Def.DisplayName.ToString()), X + 36, SlotY + 8, 20, Stock > 0 ? FLinearColor::White : Gray);
		Text(FString::Printf(TEXT("在庫 %d　耐久 %s"), Stock, *Big(Def.MaxHP)), X + 36, SlotY + 38, 16, Gray);
	}

	// 置けない理由をカーソルの下に出す
	if (PC && PC->bHasBuildTarget && !PC->bCanPlaceAtTarget && !PC->BuildTargetReason.IsEmpty())
	{
		float MouseX = 0.f, MouseY = 0.f;
		if (PC->GetMousePosition(MouseX, MouseY))
		{
			TextPx(PC->BuildTargetReason.ToString(), MouseX, MouseY + 26.f * UIScale, 18, Bad, true);
		}
	}

	Text(FString::Printf(TEXT("このステージの鬼の攻撃力: %s"), *Big(GM->GetOniAttackDamage())), 0, CanvasH - 60, 18, Gray, true);
}

// ---------------------------------------------------------------- かくれんぼ

void AKakurenboHUD::DrawHide(AKakurenboGameState* State, AKakurenboGameMode* GM)
{
	const float CanvasW = Canvas->ClipX / UIScale;
	const float CanvasH = Canvas->ClipY / UIScale;

	// 鬼の位置（一番下に描いて、他の表示に隠れないようにする）
	if (const AOniCharacter* Oni = GM->GetOni())
	{
		if (bShowOniIndicator)
		{
			DrawOniIndicator(Oni, State);
		}
	}

	// 画面中央の小さな点（向いている方向の目安）
	Panel(CanvasW * 0.5f - 3, CanvasH * 0.5f - 3, 6, 6, FLinearColor(1.f, 1.f, 1.f, 0.7f));

	DrawMashPopups(State, GM);

	// 残り時間（上部中央）
	const float Ratio = State->HideTimeLimit > 0.f ? State->HideTimeRemaining / State->HideTimeLimit : 0.f;
	const float BarW = 600;
	const float BarX = (CanvasW - BarW) * 0.5f;
	Panel(BarX, 24, BarW, 18, FLinearColor(0.f, 0.f, 0.f, 0.6f));
	Panel(BarX, 24, BarW * Ratio, 18, FLinearColor(0.3f, 0.8f, 1.f, 0.9f));
	Text(FString::Printf(TEXT("残り %.1f 秒"), State->HideTimeRemaining), 0, 48, 28, FLinearColor::White, true);
	Text(FString::Printf(TEXT("逃げ切り報酬 %s コイン"), *Big(GM->GetClearReward())), 0, 86, 18, Gold, true);

	if (State->HideStartCountdown > 0.f)
	{
		Text(FString::Printf(TEXT("%d"), FMath::CeilToInt(State->HideStartCountdown)), 0, 380, 120, FLinearColor::White, true);
		Text(TEXT("もうすぐ鬼が来る…　マウスで周りを見回せます"), 0, 580, 28, FLinearColor::White, true);
	}

	// 鬼の様子
	if (const AOniCharacter* Oni = GM->GetOni())
	{
		FString StateText;
		switch (State->OniState)
		{
		case EOniState::Wander:      StateText = TEXT("鬼: うろうろしている"); break;
		case EOniState::Investigate: StateText = TEXT("鬼: 音に気づいた！"); break;
		case EOniState::Attack:      StateText = TEXT("鬼: 壁を壊している！"); break;
		}
		const APawn* Pawn = GetOwningPawn();
		const float DistM = Pawn ? FVector::Dist2D(Pawn->GetActorLocation(), Oni->GetActorLocation()) / 100.f : 0.f;
		Panel(20, 170, 380, 76);
		Text(StateText, 36, 180, 22, OniStateColor(State->OniState));
		Text(FString::Printf(TEXT("距離 %.1f m"), DistM), 36, 214, 18, Gray);
	}

	// 操作説明（下部中央）
	Text(FString::Printf(TEXT("マウス: 見回す　Space / 左クリック: 連打 (+%s コイン・音が出る)"), *Big(GM->GetMashIncome())), 0, CanvasH - 90, 24, FLinearColor::White, true);
	Text(FString::Printf(TEXT("今回の獲得: %s コイン　連打 %d 回"), *Big(State->CoinsEarnedThisRound), State->MashCountThisRound), 0, CanvasH - 54, 18, Gray, true);
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
		Popup.Text = FString::Printf(TEXT("+%s"), *Big(GM->GetMashIncome() * NewMashes));
		Popup.OffsetX = FMath::FRandRange(-70.f, 70.f);
		LastSeenMashCount = State->MashCountThisRound;
		if (MashPopups.Num() > MaxPopups)
		{
			MashPopups.RemoveAt(0);
		}
	}

	// 上に浮かびながら消えていく
	const float CenterX = Canvas->ClipX * 0.5f;
	const float BaseY = Canvas->ClipY * 0.5f + 160.f * UIScale; // カウントダウンの説明文より下
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

void AKakurenboHUD::DrawOniIndicator(const AOniCharacter* Oni, const AKakurenboGameState* State)
{
	APlayerController* PC = GetOwningPlayerController();
	const APawn* Pawn = GetOwningPawn();
	if (!PC || !Pawn)
	{
		return;
	}

	const FLinearColor Color = OniStateColor(State->OniState);
	const float DistM = FVector::Dist2D(Pawn->GetActorLocation(), Oni->GetActorLocation()) / 100.f;
	const FString Label = FString::Printf(TEXT("鬼 %.0fm"), DistM);

	const float W = Canvas->ClipX;
	const float H = Canvas->ClipY;
	const float S = UIScale;
	const FVector Head = Oni->GetActorLocation() + FVector(0.f, 0.f, 130.f);

	// 画面内に見えていれば、頭上に ▼ を出す（壁の向こうにいても表示する）
	FVector2D Screen;
	const float Margin = 60.f * S;
	if (PC->ProjectWorldLocationToScreen(Head, Screen, true)
		&& Screen.X > Margin && Screen.X < W - Margin && Screen.Y > Margin && Screen.Y < H - Margin)
	{
		TrianglePx(Screen + FVector2D(-14.f, -30.f) * S, Screen + FVector2D(14.f, -30.f) * S, Screen + FVector2D(0.f, -6.f) * S, Color);
		TextPx(Label, Screen.X, Screen.Y - 62.f * S, 18, Color, true);
		return;
	}

	// 画面外なら、画面中央を囲む円の上に「鬼のいる方向」を指す矢印を出す（上＝正面、下＝背後）
	FVector CamLoc;
	FRotator CamRot;
	PC->GetPlayerViewPoint(CamLoc, CamRot);
	const FVector ToOni = Oni->GetActorLocation() - CamLoc;
	const float BearingDeg = FMath::RadiansToDegrees(FMath::Atan2(ToOni.Y, ToOni.X));
	const float RelRad = FMath::DegreesToRadians(FRotator::NormalizeAxis(BearingDeg - CamRot.Yaw));
	const FVector2D Dir(FMath::Sin(RelRad), -FMath::Cos(RelRad)); // 右＝時計回り
	const FVector2D Perp(-Dir.Y, Dir.X);
	const FVector2D Center(W * 0.5f, H * 0.5f);
	const float Radius = H * 0.33f;

	const FVector2D Base = Center + Dir * Radius;
	const FVector2D Tip = Center + Dir * (Radius + 34.f * S);
	TrianglePx(Tip, Base + Perp * 22.f * S, Base - Perp * 22.f * S, Color);
	const FVector2D LabelPos = Center + Dir * (Radius - 34.f * S);
	TextPx(Label, LabelPos.X, LabelPos.Y - 12.f * S, 18, Color, true);
}

// ---------------------------------------------------------------- リザルト

void AKakurenboHUD::DrawResult(AKakurenboGameState* State, AKakurenboGameMode* GM)
{
	const float CanvasW = Canvas->ClipX / UIScale;
	const float W = 700, X = (CanvasW - W) * 0.5f, Y = 260;
	Panel(X, Y, W, 340);

	if (State->bLastRoundCleared)
	{
		Text(TEXT("逃げ切り成功！"), 0, Y + 24, 48, Good, true);
		Text(FString::Printf(TEXT("逃げ切り報酬 +%s コイン"), *Big(State->LastClearReward)), 0, Y + 110, 26, Gold, true);
		Text(FString::Printf(TEXT("ステージ %d へ進みます"), State->Stage), 0, Y + 150, 22, FLinearColor::White, true);
	}
	else
	{
		Text(TEXT("見つかった…"), 0, Y + 24, 48, Bad, true);
		Text(TEXT("稼いだコインはそのまま持ち帰れます"), 0, Y + 110, 22, FLinearColor::White, true);
	}

	Text(FString::Printf(TEXT("今回の獲得: %s コイン（連打 %d 回）"), *Big(State->CoinsEarnedThisRound), State->MashCountThisRound), 0, Y + 200, 22, FLinearColor::White, true);
	if (State->LastRoundWallsDestroyed > 0)
	{
		Text(FString::Printf(TEXT("壊された壁: %d 個"), State->LastRoundWallsDestroyed), 0, Y + 236, 20, Bad, true);
	}
	Text(TEXT("Enter: 購入パートへ"), 0, Y + 284, 24, Gold, true);
	Text(TEXT("マウス・Q/E: カメラ回転　ホイール: ズーム"), 0, Y + 356, 16, Gray, true);
}
