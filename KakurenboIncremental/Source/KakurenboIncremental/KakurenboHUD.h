// 仮の画面表示。Canvas に直接文字を描く（UMG のアセット不要）。
// 見た目を作り込む段階で UMG ウィジェットに置き換える想定。

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "KakurenboHUD.generated.h"

class AKakurenboGameMode;
class AKakurenboGameState;
class UFont;
struct FSlateFontInfo;

/** マウスでクリックできる UI の種類（購入パート・設置パート） */
enum class EKakurenboUIAction : uint8
{
	None,
	ShopTab,   // お店の切り替え（Index 0: コインのお店 / 1: 転生のお店）
	ShopItem,  // 商品（Index は 0 始まりの番号）
	Prestige,  // 転生する
	NextPhase, // 次のパートへ（Enter と同じ）
	BuildSlot, // 置く物（Index は SelectedBuildSlot と同じ番号）
};

/** 画面上のボタン 1 つ（位置は実際のピクセル） */
struct FKakurenboUIButton
{
	FBox2D Rect = FBox2D(ForceInit);
	EKakurenboUIAction Action = EKakurenboUIAction::None;
	int32 Index = 0;
};

UCLASS()
class KAKURENBOINCREMENTAL_API AKakurenboHUD : public AHUD
{
	GENERATED_BODY()

public:
	virtual void DrawHUD() override;

	/** かくれんぼ中に鬼の位置を表示する（画面内: 頭上のマーカー / 画面外: 画面の縁の矢印） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HUD")
	bool bShowOniIndicator = true;

	/** かくれんぼ中にお宝の位置を表示する */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HUD")
	bool bShowTreasureIndicator = true;

	/** 画面全体を一瞬その色で光らせる（Color.A が最初の濃さ。Duration 秒で消える） */
	void Flash(const FLinearColor& Color, float Duration);

	/** 画面中央の上に大きな文字を浮かべる（「お宝 +30」「トリモチにかかった！」など） */
	void AddPopup(const FString& Text, const FLinearColor& Color);

	/** 今の点滅の濃さ（テスト用） */
	float GetFlashAlpha() const;

	/** 表示中の大きな文字の数（テスト用） */
	int32 GetPopupCount() const { return Popups.Num(); }

	/** 画面上の位置（ピクセル）にあるボタン。前のフレームで描いたボタンから探す */
	bool FindButtonAt(const FVector2D& ScreenPx, FKakurenboUIButton& OutButton) const;

	/** ボタンの真ん中の位置（テスト用。描いていなければ false） */
	bool GetButtonCenter(EKakurenboUIAction Action, int32 Index, FVector2D& OutScreenPx) const;

protected:
	void DrawStatusPanel(AKakurenboGameState* State, AKakurenboGameMode* GM);
	void DrawShop(AKakurenboGameState* State, AKakurenboGameMode* GM);
	void DrawBuild(AKakurenboGameState* State, AKakurenboGameMode* GM);
	void DrawHide(AKakurenboGameState* State, AKakurenboGameMode* GM);
	void DrawResult(AKakurenboGameState* State, AKakurenboGameMode* GM);

	/**
	 * ワールド上の位置を示す目印を描く。画面内なら ▼ とラベル、画面外なら画面の縁に方向を示す矢印。
	 * @param ArrowRadiusRatio 矢印を並べる円の半径（画面の高さに対する割合）
	 */
	void DrawWorldIndicator(const FVector& WorldLocation, const FString& Label, const FLinearColor& Color, float ArrowRadiusRatio, float Scale);

	/** ワールド上の位置に文字だけを出す（画面外なら出さない）。OffsetY は画面上で下へずらす量（1080p 基準） */
	void DrawWorldLabel(const FVector& WorldLocation, const FString& Label, const FLinearColor& Color, float OffsetY = 0.f);

	/** 連打したときに「+〇」を画面中央の下に浮かべる（カメラを揺らさない手応え） */
	void DrawMashPopups(const AKakurenboGameState* State, AKakurenboGameMode* GM);

	/** 大きな文字と画面の点滅（一番上に重ねる） */
	void DrawPopupsAndFlash();

	// ---- 描画の補助（X/Y は 1080p 基準の座標。Px が付くものは実際のピクセル座標） ----

	/** 日本語が表示できるフォントで文字を描く。bCenterX なら画面の横方向の中央に揃える */
	void Text(const FString& Str, float X, float Y, int32 Size, const FLinearColor& Color = FLinearColor::White, bool bCenterX = false);
	void TextPx(const FString& Str, float Px, float Py, int32 Size, const FLinearColor& Color, bool bCenterX);
	void Panel(float X, float Y, float W, float H, const FLinearColor& Color = FLinearColor(0.f, 0.f, 0.f, 0.55f));
	void TrianglePx(const FVector2D& A, const FVector2D& B, const FVector2D& C, const FLinearColor& Color);
	FSlateFontInfo MakeFont(int32 Size);

	/** クリックできる範囲を登録する（1080p 基準の座標） */
	void AddButton(float X, float Y, float W, float H, EKakurenboUIAction Action, int32 Index);

	/** カーソルがその範囲の上にあるか（1080p 基準の座標） */
	bool IsCursorOver(float X, float Y, float W, float H) const;

	/** ボタンを描いてクリックできるようにする（bSelected: 選ばれている見た目） */
	void DrawButton(float X, float Y, float W, float H, const FString& Label, EKakurenboUIAction Action, int32 Index,
		bool bSelected = false, int32 Size = 22, const FLinearColor& TextColor = FLinearColor::White);

	/** このフレームに描いたボタン（次のフレームの入力で使う） */
	TArray<FKakurenboUIButton> Buttons;

	/** 画面の大きさに合わせた UI 倍率（1080p 基準） */
	float UIScale = 1.f;

	UPROPERTY()
	TObjectPtr<UFont> HUDFont;

private:
	struct FMashPopup
	{
		FString Text;
		float Age = 0.f;     // 出てからの秒数
		float OffsetX = 0.f; // 横方向のばらつき（1080p 基準）
	};
	TArray<FMashPopup> MashPopups;
	int32 LastSeenMashCount = 0;

	struct FBigPopup
	{
		FString Text;
		FLinearColor Color;
		float Age = 0.f;
	};
	TArray<FBigPopup> Popups;

	FLinearColor FlashColor = FLinearColor::Transparent;
	float FlashDuration = 0.f;
	/** 点滅が始まったワールド時刻 */
	float FlashStartTime = -100.f;
};
