// 仮の画面表示。Canvas に直接文字を描く（UMG のアセット不要）。
// 見た目を作り込む段階で UMG ウィジェットに置き換える想定。

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "KakurenboHUD.generated.h"

class AKakurenboGameMode;
class AKakurenboGameState;
class UFont;

UCLASS()
class KAKURENBOINCREMENTAL_API AKakurenboHUD : public AHUD
{
	GENERATED_BODY()

public:
	virtual void DrawHUD() override;

protected:
	void DrawStatusPanel(AKakurenboGameState* State, AKakurenboGameMode* GM);
	void DrawShop(AKakurenboGameState* State, AKakurenboGameMode* GM);
	void DrawBuild(AKakurenboGameState* State, AKakurenboGameMode* GM);
	void DrawHide(AKakurenboGameState* State, AKakurenboGameMode* GM);
	void DrawResult(AKakurenboGameState* State, AKakurenboGameMode* GM);

	/** 日本語が表示できる Slate フォントで文字を描く */
	void Text(const FString& Str, float X, float Y, int32 Size, const FLinearColor& Color = FLinearColor::White, bool bCenterX = false);
	void Panel(float X, float Y, float W, float H, const FLinearColor& Color = FLinearColor(0.f, 0.f, 0.f, 0.55f));

	/** 画面の大きさに合わせた UI 倍率（1080p 基準） */
	float UIScale = 1.f;

	UPROPERTY()
	TObjectPtr<UFont> HUDFont;
};
