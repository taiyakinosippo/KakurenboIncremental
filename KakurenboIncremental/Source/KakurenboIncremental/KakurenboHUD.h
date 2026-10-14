// 仮の画面表示。Canvas に直接文字を描く（UMG のアセット不要）。
// 見た目を作り込む段階で UMG ウィジェットに置き換える想定。

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "KakurenboTypes.h"
#include "KakurenboHUD.generated.h"

class AKakurenboGameMode;
class AKakurenboGameState;
class UFont;
class UTexture2D;
struct FSlateFontInfo;

/** マウスでクリックできる UI の種類 */
enum class EKakurenboUIAction : uint8
{
	None,
	ShopTab,   // お店の切り替え（Index は EKakurenboShopTab：0 コインのお店 / 1 転生のお店 / 2 宝石のお店）
	BuyAmount, // まとめ買いの数（Index は EKakurenboBuyAmount：0 ×1 / 1 ×10 / 2 買えるだけ）
	ShopItem,  // 商品（Index は 0 始まりの番号）
	Prestige,  // 転生する
	NextPhase, // 次のパートへ（Enter と同じ）
	BuildSlot, // 置く物（Index は SelectedBuildSlot と同じ番号）
	RefillAll, // 壊れた壁・使った罠をまとめて補充（R と同じ）
	BackToShop, // 設置パートから購入パートへ戻る（B と同じ）
	ClearAll,   // 置いた壁・罠をすべて回収する（Delete と同じ。2 回押して決める）
	// ---- タイトル・メニュー・設定 ----
	TitleItem,  // タイトルのメニュー（Index は EKakurenboTitleItem）
	OpenMenu,   // 右上の「メニュー」（Esc と同じ）
	OpenHelp,   // 右上の「操作説明」（F1 と同じ）
	MenuItem,   // 一時停止メニュー（Index は EKakurenboPauseItem）
	Slider,     // 設定のつまみ（Index は EKakurenboSettingSlider）
	Toggle,     // 設定の切り替え（Index は EKakurenboSettingToggle）
	OpenKeys,   // キー設定を開く
	KeySlot,    // キー設定の欄（Index = 操作 × 2 + 2 つ目なら 1）
	ResetKeys,  // キー設定を初期設定に戻す
	ResetTips,  // 操作のヒントをもう一度出す
	Back,       // もどる（Esc と同じ）
	ConfirmYes, // 確認の「はい」
	ConfirmNo,  // 確認の「いいえ」
	OniPage,    // 鬼のチュートリアル・図鑑のページ（Index -1: まえ / +1: つぎ・閉じる）
};

/** 購入パートで開いているお店（番号はタブのボタンの Index。並びは コイン → 宝石 → 転生） */
enum class EKakurenboShopTab : uint8
{
	Coin = 0,     // コインのお店（強化・壁・罠）
	Prestige = 1, // 転生のお店（転生ポイントで永続強化）
	Gem = 2,      // 宝石のお店（お宝の宝石で、ふしぎな道具）
};

/** まとめ買い（M15）：商品をクリックしたときに買う数 */
enum class EKakurenboBuyAmount : uint8
{
	One = 0,
	Ten = 1,
	Max = 2, // 買えるだけ
};

/** 開いているメニュー（重ねて開ける。Esc・もどるで 1 つ前に戻る） */
enum class EKakurenboMenu : uint8
{
	None,
	Pause,          // 一時停止（Esc）
	Settings,       // 設定（音量・マウス・画面・ヒント）
	Keys,           // キー設定
	Help,           // 操作説明（F1）
	ConfirmNewGame, // タイトル：セーブを消してはじめから？
	ConfirmTitle,   // かくれんぼの途中でタイトルへ戻る？
	OniIntro,       // M15：新しい鬼のチュートリアル（初めて出てくるとき）・鬼の図鑑（一時停止メニューから）
};

/** タイトルのメニュー */
enum class EKakurenboTitleItem : uint8
{
	Continue,
	NewGame,
	Settings,
	Quit,
	Count
};

/** 一時停止メニュー */
enum class EKakurenboPauseItem : uint8
{
	Resume,
	Help,
	Settings,
	OniBook,    // 鬼の図鑑（今まで出てきた鬼）
	Title,
	Count
};

/** 設定のつまみ */
enum class EKakurenboSettingSlider : uint8
{
	Master,
	Music,
	Sfx,
	Mouse,
	Gamepad, // コントローラーのスティックでカメラを回す速さ
	Count
};

/** 決まった操作（キー設定に無いもの）の案内。キーボード・マウスかコントローラーかで出し分ける */
enum class EKakurenboUIKey : uint8
{
	Confirm,    // 選ぶ・押す（左クリック / A）
	Cancel,     // もどる（Esc / B）
	Menu,       // メニュー（Esc / START）
	Place,      // 設置：置く（左クリック / A）
	PickUp,     // 設置：回収（右クリック / X）
	SelectSlot, // 設置：置く物を選ぶ（下の欄・数字キー / LB・RB）
	Zoom,       // ズーム（ホイール / LT・RT）
	Look,       // 見回す（マウス / 右スティック）
	Move,       // 歩く（WASD / 左スティック）
	Cursor,     // 設置のカーソル（マウス / 左スティック）
	Rotate,     // カメラを回す（Q・E / 右スティック）
	Navigate,   // 項目を選ぶ（マウス / 十字キー）
	Buy,        // 買う（クリック・数字キー / A）
	Tilt,       // 設置のカメラを傾ける（Z・X / 右スティックの上下）
};

/** 設定の切り替え */
enum class EKakurenboSettingToggle : uint8
{
	InvertY,
	Hints,
	Fullscreen,
	PadLayout,  // コントローラーのボタン配置（Xbox / 任天堂）
	CrouchToggle, // しゃがむ：1 回押して切り替え / 押している間
	Count
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

	// 鬼とお宝の位置は矢印では出さない（鬼の足音・お宝のキラキラの音で、どこにいそうかわかる）

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

	/** 前のフレームで描いたボタン（コントローラーで選ぶのに使う） */
	const TArray<FKakurenboUIButton>& GetButtons() const { return Buttons; }

	/** 前のフレームのボタンが、そのパート・メニューの画面のものか（画面が変わった直後の 1 フレームは前の画面のボタンが残っている） */
	bool AreButtonsFor(EKakurenboPhase Phase, EKakurenboMenu Menu) const { return ButtonsPhase == Phase && ButtonsMenu == Menu; }

	/** ボタンの真ん中の位置（テスト用。描いていなければ false） */
	bool GetButtonCenter(EKakurenboUIAction Action, int32 Index, FVector2D& OutScreenPx) const;

	/** 今の画面に出している文（テスト用。文字が出ているかを確かめる） */
	const TArray<FString>& GetDrawnTexts() const { return LastFrameTexts; }
	bool HasDrawnText(const FString& Part) const;

protected:
	void DrawStatusPanel(AKakurenboGameState* State, AKakurenboGameMode* GM);

	/** 購入・設置パートで、このステージに出てくる鬼の種類と数を上の真ん中に出す（ユーザーの希望：始まる前に分かるように） */
	void DrawStageOnis(AKakurenboGameMode* GM);
	void DrawShop(AKakurenboGameState* State, AKakurenboGameMode* GM);
	void DrawBuild(AKakurenboGameState* State, AKakurenboGameMode* GM);
	void DrawHide(AKakurenboGameState* State, AKakurenboGameMode* GM);
	void DrawResult(AKakurenboGameState* State, AKakurenboGameMode* GM);

	/** タイトル画面（はじめから / つづきから / 設定 / やめる） */
	void DrawTitle(AKakurenboGameState* State, AKakurenboGameMode* GM);
	/** 右上の「操作説明」「メニュー」のボタン（カーソルが出ているパート） */
	void DrawCornerButtons(EKakurenboPhase Phase);
	/** 開いているメニュー（一時停止・設定・キー設定・操作説明・確認）を一番上に描く */
	void DrawMenu(AKakurenboGameMode* GM);
	void DrawPauseMenu(AKakurenboGameMode* GM);
	void DrawSettings();
	void DrawKeyConfig();
	void DrawHelp(EKakurenboPhase Phase);
	void DrawConfirm(const FString& Title, const FString& Message, const FString& YesLabel, const FString& NoLabel);
	/** 鬼のチュートリアル・図鑑（写真と能力） */
	void DrawOniIntro(AKakurenboGameMode* GM);

public:
	/** 鬼の種類ごとの、ひとことと特徴の行（チュートリアル・図鑑。[ ] はキーの形） */
	static FString GetOniCatchphrase(EOniType Type);
	static TArray<FString> GetOniFeatureLines(EOniType Type, const FKakurenboOniTypeRow& Row);

	/** 能力の星（1〜5。0 なら「しない」）：足の速さ・目・耳（連打の音）・壁を壊す力 */
	static void GetOniRatings(const FKakurenboOniTypeRow& Row, int32& OutSpeed, int32& OutSight, int32& OutHearing, int32& OutPower);

protected:
	/** 画面下の操作のヒント（はじめてのときだけ） */
	void DrawTip();

	/** 操作の行（[ ] はキーの形）。パートごとの操作説明・画面下のキーの案内に使う */
	TArray<FString> GetHelpLines(EKakurenboPhase Phase) const;

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
	void Panel(float X, float Y, float W, float H, const FLinearColor& Color = FLinearColor(0.05f, 0.02f, 0.09f, 0.68f));
	void TrianglePx(const FVector2D& A, const FVector2D& B, const FVector2D& C, const FLinearColor& Color);
	FSlateFontInfo MakeFont(int32 Size);

	/** クリックできる範囲を登録する（1080p 基準の座標） */
	void AddButton(float X, float Y, float W, float H, EKakurenboUIAction Action, int32 Index);

	/** カーソルがその範囲の上にあるか（1080p 基準の座標） */
	bool IsCursorOver(float X, float Y, float W, float H) const;

	/** ボタンを描いてクリックできるようにする（bSelected: 選ばれている見た目）。ラベルの [ ] はキーの形で描く */
	void DrawButton(float X, float Y, float W, float H, const FString& Label, EKakurenboUIAction Action, int32 Index,
		bool bSelected = false, int32 Size = 22, const FLinearColor& TextColor = FLinearColor::White);

	/** 文の幅（1080p 基準） */
	float MeasureText(const FString& Str, int32 Size);

	/** [ ] をキーの形で描く文の幅（1080p 基準） */
	float MeasureRich(const FString& Str, int32 Size);

	/**
	 * M15：幅 MaxW（1080p 基準）に収まるように描く。入らなければ文字を小さくし（Size の 6 割まで）、それでも入らなければ後ろを「…」にする。
	 * 枠からはみ出す・隣の文字とかぶるのを防ぐ
	 */
	void TextFit(const FString& Str, float X, float Y, int32 Size, float MaxW, const FLinearColor& Color = FLinearColor::White, bool bCenterX = false);

	/** RichText の幅を MaxW に収める（文字を小さくする。Size の 6 割まで） */
	float RichTextFit(const FString& Str, float X, float Y, int32 Size, float MaxW, const FLinearColor& Color = FLinearColor::White, bool bCenterX = false, float Alpha = 1.f);

	/** その幅に収まる文字の大きさ */
	int32 FitSize(const FString& Str, int32 Size, float MaxW, bool bRich);

	/** キーの形（キーキャップ）を描く。幅を返す（1080p 基準） */
	float KeyCap(const FString& Label, float X, float Y, int32 Size, float Alpha = 1.f);

	/**
	 * [ ] で囲んだところをキーの形で描く文。bCenterX なら X を中心にする（X < 0 なら画面の中央）。描いた幅を返す
	 */
	float RichText(const FString& Str, float X, float Y, int32 Size, const FLinearColor& Color = FLinearColor::White, bool bCenterX = false, float Alpha = 1.f);

	/** 枠線 */
	void Frame(float X, float Y, float W, float H, const FLinearColor& Color, float Thickness = 2.f);

	/** 画面全体を暗くする（メニューの後ろ） */
	void DimScreen(float Alpha);

	/** 設定のつまみ（Value01: 0〜1）と切り替え */
	void DrawSlider(float X, float Y, float W, float Value01, int32 Index, const FString& ValueText);
	void DrawToggle(float X, float Y, float W, bool bOn, int32 Index, const FString& OnText, const FString& OffText);

	/** このフレームに描いたボタン（次のフレームの入力で使う）と、描いた画面 */
	TArray<FKakurenboUIButton> Buttons;
	EKakurenboPhase ButtonsPhase = EKakurenboPhase::Title;
	EKakurenboMenu ButtonsMenu = EKakurenboMenu::None;

	/** 画面の大きさに合わせた UI 倍率（1080p 基準） */
	float UIScale = 1.f;

	UPROPERTY()
	TObjectPtr<UFont> HUDFont;

	/**
	 * タイトルロゴ「かせぐんぼ」（Tools/MakeLogo.ps1 で作った PNG を ImportSourceArt.py --logo-only で取り込んだもの）。
	 * 無ければタイトル画面は文字で題名を描く
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Kakurenbo|UI")
	TSoftObjectPtr<UTexture2D> TitleLogoAsset = TSoftObjectPtr<UTexture2D>(FSoftObjectPath(TEXT("/Game/Kakurenbo/UI/T_TitleLogo.T_TitleLogo")));

	/** 読み込んだタイトルロゴ（1 回だけ探す） */
	UPROPERTY()
	TObjectPtr<UTexture2D> TitleLogo;
	bool bTitleLogoSearched = false;

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

	/** このフレームに描いた文（テスト用） */
	TArray<FString> DrawnTexts;
	TArray<FString> LastFrameTexts;
};
