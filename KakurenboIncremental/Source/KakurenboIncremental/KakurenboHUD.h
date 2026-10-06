// 仮の画面表示。Canvas に直接文字を描く（UMG のアセット不要）。
// 見た目を作り込む段階で UMG ウィジェットに置き換える想定。

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "KakurenboHUD.generated.h"

class AKakurenboGameMode;
class AKakurenboGameState;
class AOniCharacter;
class UFont;
struct FSlateFontInfo;

UCLASS()
class KAKURENBOINCREMENTAL_API AKakurenboHUD : public AHUD
{
	GENERATED_BODY()

public:
	virtual void DrawHUD() override;

	/** かくれんぼ中に鬼の位置を表示する（画面内: 頭上のマーカー / 画面外: 画面の縁の矢印） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HUD")
	bool bShowOniIndicator = true;

protected:
	void DrawStatusPanel(AKakurenboGameState* State, AKakurenboGameMode* GM);
	void DrawShop(AKakurenboGameState* State, AKakurenboGameMode* GM);
	void DrawBuild(AKakurenboGameState* State, AKakurenboGameMode* GM);
	void DrawHide(AKakurenboGameState* State, AKakurenboGameMode* GM);
	void DrawResult(AKakurenboGameState* State, AKakurenboGameMode* GM);
	void DrawOniIndicator(const AOniCharacter* Oni, const AKakurenboGameState* State);

	// ---- 描画の補助（X/Y は 1080p 基準の座標。Px が付くものは実際のピクセル座標） ----

	/** 日本語が表示できるフォントで文字を描く。bCenterX なら画面の横方向の中央に揃える */
	void Text(const FString& Str, float X, float Y, int32 Size, const FLinearColor& Color = FLinearColor::White, bool bCenterX = false);
	void TextPx(const FString& Str, float Px, float Py, int32 Size, const FLinearColor& Color, bool bCenterX);
	void Panel(float X, float Y, float W, float H, const FLinearColor& Color = FLinearColor(0.f, 0.f, 0.f, 0.55f));
	void TrianglePx(const FVector2D& A, const FVector2D& B, const FVector2D& C, const FLinearColor& Color);
	FSlateFontInfo MakeFont(int32 Size);

	/** 画面の大きさに合わせた UI 倍率（1080p 基準） */
	float UIScale = 1.f;

	UPROPERTY()
	TObjectPtr<UFont> HUDFont;
};
