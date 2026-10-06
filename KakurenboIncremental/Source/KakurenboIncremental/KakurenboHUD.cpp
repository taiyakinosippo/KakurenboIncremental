#include "KakurenboHUD.h"

#include "CanvasItem.h"
#include "Engine/Canvas.h"
#include "Engine/Font.h"
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

void AKakurenboHUD::Text(const FString& Str, float X, float Y, int32 Size, const FLinearColor& Color, bool bCenterX)
{
	// エンジン付属の Roboto は日本語用のフォールバック（DroidSansFallback）を持っているので文字化けしない。
	// Canvas は UFont オブジェクトが必須なので、FontObject を指定した FSlateFontInfo を使う
	if (!HUDFont)
	{
		HUDFont = LoadObject<UFont>(nullptr, TEXT("/Engine/EngineFonts/Roboto.Roboto"));
	}
	const FSlateFontInfo Font(HUDFont, FMath::RoundToInt(Size * UIScale));
	FCanvasTextItem Item(FVector2D(X * UIScale, Y * UIScale), FText::FromString(Str), Font, Color);
	Item.EnableShadow(FLinearColor(0.f, 0.f, 0.f, 0.8f));
	if (bCenterX)
	{
		Item.Position.X = Canvas->ClipX * 0.5f;
		Item.bCentreX = true;
	}
	Canvas->DrawItem(Item);
}

void AKakurenboHUD::Panel(float X, float Y, float W, float H, const FLinearColor& Color)
{
	FCanvasTileItem Tile(FVector2D(X * UIScale, Y * UIScale), FVector2D(W * UIScale, H * UIScale), Color);
	Tile.BlendMode = SE_BLEND_Translucent;
	Canvas->DrawItem(Tile);
}

void AKakurenboHUD::DrawStatusPanel(AKakurenboGameState* State, AKakurenboGameMode* GM)
{
	static const TCHAR* PhaseNames[] = { TEXT("購入パート"), TEXT("設置パート"), TEXT("かくれんぼ"), TEXT("リザルト") };

	Panel(20, 20, 380, 130);
	Text(FString::Printf(TEXT("ステージ %d   [%s]"), State->Stage, PhaseNames[static_cast<int32>(State->Phase)]), 36, 30, 22);
	Text(FString::Printf(TEXT("コイン  %s"), *Big(State->Coins)), 36, 64, 30, Gold);
	Text(FString::Printf(TEXT("連打 +%s / 時間 +%s/秒"), *Big(GM->GetMashIncome()), *Big(GM->GetTimeIncomePerSecond())), 36, 108, 18, Gray);
}

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

	Text(TEXT("数字キー: 購入　　Enter: 設置パートへ"), 0, Y + 110 + Items.Num() * 80 + 16, 22, FLinearColor::White, true);
}

void AKakurenboHUD::DrawBuild(AKakurenboGameState* State, AKakurenboGameMode* GM)
{
	const AKakurenboPlayerController* PC = Cast<AKakurenboPlayerController>(GetOwningPlayerController());
	const float CanvasW = Canvas->ClipX / UIScale;
	const float CanvasH = Canvas->ClipY / UIScale;

	// 操作説明
	Panel(20, 170, 420, 150);
	Text(TEXT("WASD: 移動　Space: ジャンプ"), 36, 182, 18);
	Text(TEXT("左クリック: 置く　右クリック: 回収"), 36, 210, 18);
	Text(TEXT("数字キー / ホイール: 壁の種類"), 36, 238, 18);
	Text(TEXT("Enter: かくれんぼ開始"), 36, 274, 22, Gold);

	// 照準
	Text(TEXT("+"), 0, CanvasH * 0.5f - 18, 28, FLinearColor::White, true);

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

	// 置けない理由
	if (PC && PC->bHasBuildTarget && !PC->bCanPlaceAtTarget && !PC->BuildTargetReason.IsEmpty())
	{
		Text(PC->BuildTargetReason.ToString(), 0, CanvasH * 0.5f + 30, 20, Bad, true);
	}

	Text(FString::Printf(TEXT("このステージの鬼の攻撃力: %s"), *Big(GM->GetOniAttackDamage())), 0, CanvasH - 60, 18, Gray, true);
}

void AKakurenboHUD::DrawHide(AKakurenboGameState* State, AKakurenboGameMode* GM)
{
	// 残り時間（上部中央）
	const float Ratio = State->HideTimeLimit > 0.f ? State->HideTimeRemaining / State->HideTimeLimit : 0.f;
	const float BarW = 600;
	const float CanvasW = Canvas->ClipX / UIScale;
	const float CenteredBarX = (CanvasW - BarW) * 0.5f;
	Panel(CenteredBarX, 24, BarW, 18, FLinearColor(0.f, 0.f, 0.f, 0.6f));
	Panel(CenteredBarX, 24, BarW * Ratio, 18, FLinearColor(0.3f, 0.8f, 1.f, 0.9f));
	Text(FString::Printf(TEXT("残り %.1f 秒"), State->HideTimeRemaining), 0, 48, 28, FLinearColor::White, true);
	Text(FString::Printf(TEXT("逃げ切り報酬 %s コイン"), *Big(GM->GetClearReward())), 0, 86, 18, Gold, true);

	if (State->HideStartCountdown > 0.f)
	{
		Text(FString::Printf(TEXT("%d"), FMath::CeilToInt(State->HideStartCountdown)), 0, 380, 120, FLinearColor::White, true);
		Text(TEXT("もうすぐ鬼が来る…"), 0, 520, 28, FLinearColor::White, true);
	}

	// 鬼の様子
	if (const AOniCharacter* Oni = GM->GetOni())
	{
		FString StateText;
		FLinearColor StateColor = FLinearColor::White;
		switch (State->OniState)
		{
		case EOniState::Wander:      StateText = TEXT("鬼: うろうろしている"); break;
		case EOniState::Investigate: StateText = TEXT("鬼: 音に気づいた！"); StateColor = Bad; break;
		case EOniState::Attack:      StateText = TEXT("鬼: 壁を壊している！"); StateColor = FLinearColor(1.f, 0.6f, 0.2f); break;
		}
		const APawn* Pawn = GetOwningPawn();
		const float DistM = Pawn ? FVector::Dist2D(Pawn->GetActorLocation(), Oni->GetActorLocation()) / 100.f : 0.f;
		Panel(20, 170, 380, 76);
		Text(StateText, 36, 180, 22, StateColor);
		Text(FString::Printf(TEXT("距離 %.1f m"), DistM), 36, 214, 18, Gray);
	}

	// 操作説明（下部中央）
	const float CanvasH = Canvas->ClipY / UIScale;
	Text(FString::Printf(TEXT("Space / 左クリック: 連打 (+%s コイン・音が出る)"), *Big(GM->GetMashIncome())), 0, CanvasH - 90, 24, FLinearColor::White, true);
	Text(FString::Printf(TEXT("今回の獲得: %s コイン　連打 %d 回"), *Big(State->CoinsEarnedThisRound), State->MashCountThisRound), 0, CanvasH - 54, 18, Gray, true);
}

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
}
