#include "KakurenboHUD.h"

#include "CanvasItem.h"
#include "Engine/Canvas.h"
#include "Engine/Font.h"
#include "GlobalRenderResources.h"
#include "KakurenboGameMode.h"
#include "KakurenboGameState.h"
#include "KakurenboGridSubsystem.h"
#include "KakurenboLibrary.h"
#include "KakurenboPlayerController.h"
#include "OniCharacter.h"
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

	/** 鬼の様子の文字と色（壁を壊している最中はそれを優先して出す） */
	void DescribeOni(const AOniCharacter* Oni, FString& OutText, FLinearColor& OutColor)
	{
		if (Oni->GetOniState() == EOniState::Attack)
		{
			OutText = TEXT("壁を壊している");
			OutColor = Warn;
			return;
		}
		switch (Oni->GetIntent())
		{
		case EOniState::Chase:       OutText = TEXT("追いかけてくる！"); OutColor = Bad; break;
		case EOniState::Investigate: OutText = TEXT("音に気づいた"); OutColor = Warn; break;
		case EOniState::Inspect:     OutText = TEXT("怪しい場所を調べている"); OutColor = Warn; break;
		case EOniState::Stunned:     OutText = TEXT("罠にかかって動けない"); OutColor = FLinearColor(0.6f, 1.f, 0.4f); break;
		default:                     OutText = TEXT("うろうろしている"); OutColor = FLinearColor(0.95f, 0.95f, 0.95f); break;
		}
	}

	/** 種類名（CSV の DisplayName。無ければ「鬼」） */
	FString OniName(const AOniCharacter* Oni)
	{
		return Oni->TypeDisplayName.IsEmpty() ? FString(TEXT("鬼")) : Oni->TypeDisplayName.ToString();
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

	// 画面上部のお知らせ（一番上に重ねて描く）
	if (!State->NoticeText.IsEmpty() && GetWorld()->GetTimeSeconds() < State->NoticeUntilTime)
	{
		const float CanvasW = Canvas->ClipX / UIScale;
		Panel(CanvasW * 0.5f - 330, 116, 660, 44, FLinearColor(0.05f, 0.25f, 0.1f, 0.8f));
		Text(State->NoticeText.ToString(), 0, 122, 22, FLinearColor::White, true);
	}

	DrawPopupsAndFlash();
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

// ---------------------------------------------------------------- 共通

void AKakurenboHUD::DrawStatusPanel(AKakurenboGameState* State, AKakurenboGameMode* GM)
{
	static const TCHAR* PhaseNames[] = { TEXT("購入パート"), TEXT("設置パート"), TEXT("かくれんぼ"), TEXT("リザルト") };

	Panel(20, 20, 380, 130);
	Text(FString::Printf(TEXT("ステージ %d   [%s]"), State->Stage, PhaseNames[static_cast<int32>(State->Phase)]), 36, 30, 22);
	Text(FString::Printf(TEXT("コイン  %s"), *Big(State->Coins)), 36, 64, 30, Gold);
	Text(FString::Printf(TEXT("連打 +%s / 時間 +%s/秒"), *Stat(GM->GetMashIncome()), *Stat(GM->GetTimeIncomePerSecond())), 36, 108, 18, Gray);
}

// ---------------------------------------------------------------- 購入

void AKakurenboHUD::DrawShop(AKakurenboGameState* State, AKakurenboGameMode* GM)
{
	const TArray<FShopItemView> Items = GM->GetShopItems();

	// 商品が増えても画面に収まるよう、1 行の高さを詰める
	const float X = 520, Y = 150, W = 880;
	const float RowH = FMath::Min(80.f, 680.f / FMath::Max(1, Items.Num()));
	Panel(X, Y, W, 100 + Items.Num() * RowH);
	Text(TEXT("購入パート"), 0, Y + 12, 34, FLinearColor::White, true);

	for (int32 i = 0; i < Items.Num(); ++i)
	{
		const FShopItemView& Item = Items[i];
		const bool bAffordable = State->Coins >= Item.Cost;
		const float RowY = Y + 72 + i * RowH;
		Text(FString::Printf(TEXT("[%d] %s  %s"), i + 1, *Item.DisplayName.ToString(), *Item.OwnedText.ToString()), X + 24, RowY, 23, bAffordable ? FLinearColor::White : Gray);
		Text(FString::Printf(TEXT("%s コイン"), *Big(Item.Cost)), X + 640, RowY, 23, bAffordable ? Gold : Gray);
		Text(Item.Description.ToString(), X + 60, RowY + RowH * 0.45f, 16, Gray);
	}

	float BottomY = Y + 100 + Items.Num() * RowH + 16;
	if (const UKakurenboGridSubsystem* Grid = GetWorld()->GetSubsystem<UKakurenboGridSubsystem>())
	{
		if (const int32 Missing = Grid->GetTotalMissing())
		{
			Text(FString::Printf(TEXT("壊れたままの壁: %d 個（在庫があれば設置パートの開始時に自動で直ります）"), Missing), 0, BottomY, 20, Warn, true);
			BottomY += 34;
		}
	}
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
	Text(TEXT("数字キー: 置く物（壁・罠）"), 36, 238, 18);
	Text(TEXT("Q/E・ホイールを押してドラッグ: 回転"), 36, 266, 18);
	Text(TEXT("ホイール: ズーム"), 36, 294, 18);
	Text(TEXT("Enter: かくれんぼ開始"), 36, 326, 22, Gold);

	// 壊れた壁の自動修復・使った罠の置き直しの結果
	float NoticeY = 24;
	if (State->LastRepairedWalls > 0 || State->LastRefilledTraps > 0)
	{
		Text(FString::Printf(TEXT("在庫から自動で直しました（壁 %d 個・罠 %d 個）"), State->LastRepairedWalls, State->LastRefilledTraps), 0, NoticeY, 22, Good, true);
		NoticeY += 34;
	}
	if (const UKakurenboGridSubsystem* Grid = GetWorld()->GetSubsystem<UKakurenboGridSubsystem>())
	{
		const int32 MissingWalls = Grid->GetTotalMissing();
		const int32 MissingTraps = Grid->GetMissingTrapCount();
		if (MissingWalls > 0 || MissingTraps > 0)
		{
			Text(FString::Printf(TEXT("直せていない壁 %d 個・罠 %d 個（赤い枠。在庫を買うか、右クリックで設計図から消せます）"), MissingWalls, MissingTraps), 0, NoticeY, 20, Bad, true);
		}
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
		Panel(X + 4, SlotY, SlotW - 8, SlotH, bSelected ? FLinearColor(0.9f, 0.75f, 0.2f, 0.6f) : FLinearColor(0.f, 0.f, 0.f, 0.55f));
		// 壁は縦長の四角、罠は平たい四角で見分ける
		if (bTrap)
		{
			Panel(X + 10, SlotY + 26, 20, 16, Color);
		}
		else
		{
			Panel(X + 12, SlotY + 10, 14, SlotH - 20, Color);
		}
		Text(FString::Printf(TEXT("[%d] %s"), i + 1, *Name.ToString()), X + 36, SlotY + 8, 20, Stock > 0 ? FLinearColor::White : Gray);
		const FString Detail = bTrap
			? FString::Printf(TEXT("在庫 %d"), Stock)
			: FString::Printf(TEXT("在庫 %d　耐久 %s"), Stock, *Stat(Walls[i].MaxHP));
		Text(Detail, X + 36, SlotY + 38, 16, Gray);
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
	const APawn* Pawn = GetOwningPawn();

	// 目印（一番下に描いて、他の表示に隠れないようにする）
	if (bShowTreasureIndicator && Pawn)
	{
		for (const ATreasureActor* Treasure : GM->GetTreasures())
		{
			if (Treasure)
			{
				const float DistM = FVector::Dist2D(Pawn->GetActorLocation(), Treasure->GetActorLocation()) / 100.f;
				DrawWorldIndicator(Treasure->GetActorLocation() + FVector(0.f, 0.f, 90.f), FString::Printf(TEXT("お宝 %.0fm"), DistM), Gold, 0.27f, 0.8f);
			}
		}
	}
	if (bShowOniIndicator && Pawn)
	{
		for (const AOniCharacter* Oni : GM->GetOnis())
		{
			if (Oni)
			{
				FString StateText;
				FLinearColor Color;
				DescribeOni(Oni, StateText, Color);
				const float DistM = FVector::Dist2D(Pawn->GetActorLocation(), Oni->GetActorLocation()) / 100.f;
				// 追いかけ始めた直後は「！」を付ける
				const bool bJustSpotted = Oni->GetChaseStartTime() >= 0.f && GetWorld()->GetTimeSeconds() - Oni->GetChaseStartTime() < 1.5f;
				const FString Label = FString::Printf(TEXT("%s%s %.0fm"), bJustSpotted ? TEXT("！ ") : TEXT(""), *OniName(Oni), DistM);
				DrawWorldIndicator(Oni->GetActorLocation() + FVector(0.f, 0.f, 130.f), Label, Color, 0.33f, 1.f);
			}
		}
	}

	DrawMashPopups(State, GM);

	// 残り時間（上部中央）
	const float CanvasW = Canvas->ClipX / UIScale;
	const float Ratio = State->HideTimeLimit > 0.f ? State->HideTimeRemaining / State->HideTimeLimit : 0.f;
	const float BarW = 600;
	const float BarX = (CanvasW - BarW) * 0.5f;
	Panel(BarX, 24, BarW, 18, FLinearColor(0.f, 0.f, 0.f, 0.6f));
	Panel(BarX, 24, BarW * Ratio, 18, FLinearColor(0.3f, 0.8f, 1.f, 0.9f));
	Text(FString::Printf(TEXT("残り %.1f 秒"), State->HideTimeRemaining), 0, 48, 28, FLinearColor::White, true);
	Text(FString::Printf(TEXT("逃げ切り報酬 %s コイン　　お宝 %d / %d（1個 %s）"),
		*Big(GM->GetClearReward()), State->TreasuresCollectedThisRound, State->TreasuresThisRound, *Big(GM->GetTreasureValue())), 0, 86, 18, Gold, true);

	if (State->HideStartCountdown > 0.f)
	{
		Text(FString::Printf(TEXT("%d"), FMath::CeilToInt(State->HideStartCountdown)), 0, 380, 120, FLinearColor::White, true);
		Text(TEXT("もうすぐ鬼が来る…　ぶつかったらアウト"), 0, 580, 28, FLinearColor::White, true);
	}

	// 鬼の様子
	const TArray<TObjectPtr<AOniCharacter>>& Onis = GM->GetOnis();
	if (Onis.Num() > 0 && Pawn)
	{
		Panel(20, 170, 520, 20 + Onis.Num() * 34);
		for (int32 i = 0; i < Onis.Num(); ++i)
		{
			if (const AOniCharacter* Oni = Onis[i])
			{
				FString StateText;
				FLinearColor Color;
				DescribeOni(Oni, StateText, Color);
				const float DistM = FVector::Dist2D(Pawn->GetActorLocation(), Oni->GetActorLocation()) / 100.f;
				const float RowY = 180 + i * 34;
				Panel(32, RowY + 4, 16, 20, Oni->BodyColor); // 体の色（どの鬼か見分ける）
				Text(FString::Printf(TEXT("%s: %s（%.1f m）"), *OniName(Oni), *StateText, DistM), 58, RowY, 20, Color);
			}
		}
	}

	// 操作説明（下部中央）
	Text(FString::Printf(TEXT("WASD: 移動　Space: ジャンプ　左クリック / F: 連打 (+%s・音が出る)　マウス: カメラ"), *Stat(GM->GetMashIncome())), 0, CanvasH - 90, 22, FLinearColor::White, true);
	FString Stats = FString::Printf(TEXT("今回の獲得: %s コイン　連打 %d 回"), *Big(State->CoinsEarnedThisRound), State->MashCountThisRound);
	if (State->TrapsTriggeredThisRound > 0)
	{
		Stats += FString::Printf(TEXT("　罠の発動 %d 回"), State->TrapsTriggeredThisRound);
	}
	Text(Stats, 0, CanvasH - 54, 18, Gray, true);
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

// ---------------------------------------------------------------- リザルト

void AKakurenboHUD::DrawResult(AKakurenboGameState* State, AKakurenboGameMode* GM)
{
	const float CanvasW = Canvas->ClipX / UIScale;
	const float W = 700, X = (CanvasW - W) * 0.5f, Y = 240;
	Panel(X, Y, W, 380);

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
	Text(FString::Printf(TEXT("お宝: %d / %d 個（+%s コイン）"), State->TreasuresCollectedThisRound, State->TreasuresThisRound, *Big(State->TreasureCoinsThisRound)), 0, Y + 236, 22, Gold, true);
	if (State->LastRoundWallsDestroyed > 0 || State->TrapsTriggeredThisRound > 0)
	{
		Text(FString::Printf(TEXT("壊された壁: %d 個・使った罠: %d 個（在庫があれば次の設置パートで自動で直ります）"), State->LastRoundWallsDestroyed, State->TrapsTriggeredThisRound), 0, Y + 272, 18, Warn, true);
	}
	Text(TEXT("Enter: 購入パートへ"), 0, Y + 320, 24, Gold, true);
	Text(TEXT("マウス・Q/E: カメラ回転　ホイール: ズーム"), 0, Y + 396, 16, Gray, true);
}
