// 設定（音量・マウス感度・キー設定・操作のヒント）。ゲーム全体で 1 つ（GameInstanceSubsystem）。
// 進み具合のセーブとは別のスロット（Saved/SaveGames/KakurenboSettings.sav）に保存する。
// 起動オプション -KakuNoSave（自動テスト）のときは読み書きしない（いつも初期設定）。
//
// 入力は「操作（EKakurenboAction）→ キー」の表で調べる。PlayerController は EKeys を直接見ずに IsDown / WasPressed を使う。

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "KakurenboTypes.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "KakurenboSettings.generated.h"

class APlayerController;

/**
 * コントローラーのボタン配置（M14）。UE のボタンは場所で決まっている（下・右・左・上）ので、配置によって役割を入れ替える。
 *   Xbox：下が A（決定）・右が B（戻る）・左が X・上が Y。LB / RB / LT / RT / BACK / START
 *   任天堂：右が A（決定）・下が B（戻る）・上が X・左が Y。L / R / ZL / ZR / － / ＋
 */
enum class EKakurenboPadLayout : uint8
{
	Xbox,
	Nintendo,
};

/** ボタンに書いてある文字（役割）。A = 決定・B = 戻る */
enum class EKakurenboPadFace : uint8
{
	A,
	B,
	X,
	Y,
};

/**
 * 操作 → キーの表（UObject ではない普通の構造体。単体テストでそのまま使える）。
 * 同じキーが 2 つの操作に付かないよう、付け替えるときは入れ替える
 */
struct KAKURENBOINCREMENTAL_API FKakurenboKeyMap
{
	/** 初期設定に戻す */
	void Reset();

	/** 操作に割り当てたキー（bSecondary: 2 つ目） */
	FKey Get(EKakurenboAction Action, bool bSecondary = false) const;

	/** キーを割り当てる。ほかの操作で使っていたキーなら、その操作には今のキーを渡す（入れ替え）。割り当てられないキーなら false */
	bool Bind(EKakurenboAction Action, const FKey& Key, bool bSecondary);

	/** 2 つ目のキーを外す */
	void ClearSecondary(EKakurenboAction Action);

	/** 操作のキーの名前（2 つあれば「Ctrl / C」） */
	FString Label(EKakurenboAction Action) const;

	/** 1 つの操作のキー（読み込み・保存用） */
	FKakurenboKeyBinding& At(int32 Index) { return Bindings[Index]; }
	const FKakurenboKeyBinding& At(int32 Index) const { return Bindings[Index]; }

private:
	TArray<FKakurenboKeyBinding> Bindings;
};

/** 設定のセーブデータ */
UCLASS()
class KAKURENBOINCREMENTAL_API UKakurenboSettingsSave : public USaveGame
{
	GENERATED_BODY()

public:
	/** 1: 最初 / 2: コントローラーの感度・「ジャンプ」のキーを「よじ登る」へ / 3: コントローラーのボタン配置 */
	UPROPERTY()
	int32 Version = 4;

	/** コントローラーのボタン配置（EKakurenboPadLayout。版 3 から） */
	UPROPERTY()
	uint8 PadLayout = 0;

	UPROPERTY()
	float MasterVolume = 0.7f;

	UPROPERTY()
	float MusicVolume = 0.45f;

	UPROPERTY()
	float SfxVolume = 1.f;

	UPROPERTY()
	float MouseSensitivity = 0.12f;

	/** コントローラーのスティックでカメラを回す速さの倍率（版 2 から） */
	UPROPERTY()
	float GamepadSensitivity = 1.f;

	UPROPERTY()
	bool bInvertMouseY = false;

	UPROPERTY()
	bool bShowHints = true;

	/** しゃがむボタンを 1 回押すとしゃがむ・もう 1 回で立つ（版 4 から。false なら押している間だけ） */
	UPROPERTY()
	bool bCrouchToggle = true;

	/** 操作の名前（EKakurenboAction の名前）→ キー。並びを変えても困らないよう名前で持つ */
	UPROPERTY()
	TMap<FName, FKakurenboKeyBinding> Bindings;

	/** もう見せた操作のヒント（同じヒントは 1 回だけ出す） */
	UPROPERTY()
	TArray<FName> SeenTips;
};

UCLASS()
class KAKURENBOINCREMENTAL_API UKakurenboSettings : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	/** ワールドから取り出す（無ければ null） */
	static UKakurenboSettings* Get(const UObject* WorldContext);

	// ===== 値（変えたら Apply と Save を呼ぶ） =====

	/** 全体・BGM・効果音の音量（0〜1） */
	float MasterVolume = 0.7f;
	float MusicVolume = 0.45f;
	float SfxVolume = 1.f;

	/** マウス感度（マウスの移動 1 カウントあたりの回転角度・度）と、その範囲 */
	float MouseSensitivity = 0.12f;
	static constexpr float MinMouseSensitivity = 0.03f;
	static constexpr float MaxMouseSensitivity = 0.42f;
	static constexpr float DefaultMouseSensitivity = 0.12f;

	bool bInvertMouseY = false;

	/** コントローラーのスティックでカメラを回す速さの倍率と、その範囲 */
	float GamepadSensitivity = 1.f;
	static constexpr float MinGamepadSensitivity = 0.3f;
	static constexpr float MaxGamepadSensitivity = 2.5f;

	/** 画面下のキーの案内と、はじめての操作のヒントを出す */
	bool bShowHints = true;

	/** M15：しゃがむボタンを 1 回押すとしゃがむ・もう 1 回で立つ（ユーザーの依頼）。false なら押している間だけ */
	bool bCrouchToggle = true;

	/** コントローラーのボタン配置（変えるときは SetPadLayout） */
	static EKakurenboPadLayout GetPadLayout() { return ActivePadLayout; }
	static void SetPadLayout(EKakurenboPadLayout NewLayout) { ActivePadLayout = NewLayout; }

	/** 今のボタン配置で、その文字（役割）のボタンはどれか（Xbox の A = 下、任天堂の A = 右） */
	static FKey PadFace(EKakurenboPadFace Face);
	static bool IsNintendoLayout() { return ActivePadLayout == EKakurenboPadLayout::Nintendo; }

	/** 音量・感度を今のワールド（効果音・BGM・プレイヤー）に反映する */
	void Apply(const UObject* WorldContext) const;

	/** 保存する（-KakuNoSave のときは何もしない） */
	void Save();

	/** 画面の表示（フルスクリーンか）。エンジンの GameUserSettings に保存される */
	static bool IsFullscreen();
	static void SetFullscreen(bool bFullscreen);

	// ===== キー設定 =====

	/** 操作に割り当てたキー（bSecondary: 2 つ目） */
	FKey GetKey(EKakurenboAction Action, bool bSecondary = false) const;

	/** その操作のキー（コントローラーのボタンも）が押されているか / このフレームに押されたか */
	bool IsDown(const APlayerController* PC, EKakurenboAction Action) const;
	bool WasPressed(const APlayerController* PC, EKakurenboAction Action) const;

	/**
	 * コントローラーのボタン（決まっていて変えられない。ボタン配置（Xbox / 任天堂）で A・B・X・Y の場所が変わる）。パートごとに使う操作が違うので、
	 * 別のパートの操作と同じボタンでもよい（例: RB はかくれんぼでは煙幕、購入ではお店の切り替え）。無ければ Invalid
	 */
	static FKey GamepadKey(EKakurenboAction Action);

	/** キーを割り当てる。ほかの操作で使っていたキーなら、その操作には今のキーを渡す（入れ替え）。割り当てられないキーなら false */
	bool Bind(EKakurenboAction Action, const FKey& Key, bool bSecondary);

	/** 2 つ目のキーを外す */
	void ClearSecondary(EKakurenboAction Action);

	/** キー設定を初期設定に戻す */
	void ResetBindings();

	/** 割り当てられるキーか（Esc・マウスの移動とホイール・コンソールのキー・コントローラーのボタンは使えない） */
	static bool CanBindKey(const FKey& Key);

	/** 画面に出すキーの名前（「W」「Ctrl」「左クリック」、コントローラーなら「A」「LB」「十字↑」など） */
	static FString KeyLabel(const FKey& Key);

	/** 操作のキーの名前（2 つあれば「Ctrl / C」） */
	FString ActionKeyLabel(EKakurenboAction Action) const;

	/** 操作の日本語の名前（キー設定の画面に出す） */
	static FText ActionDisplayName(EKakurenboAction Action);

	/** 初期設定のキー */
	static FKakurenboKeyBinding DefaultBinding(EKakurenboAction Action);

	// ===== 操作のヒント（はじめてのときだけ出す） =====

	bool HasSeenTip(FName Tip) const { return SeenTips.Contains(Tip); }
	void MarkTipSeen(FName Tip);
	/** ヒントをもう一度見られるようにする（設定画面から） */
	void ResetTips();

private:
	void Load();

	FKakurenboKeyMap Keys;
	TSet<FName> SeenTips;

	/** 今のボタン配置（GamepadKey・KeyLabel などの static な関数からも使うので static） */
	static EKakurenboPadLayout ActivePadLayout;

	/** ファイルに保存するか（-KakuNoSave のときは false） */
	bool bPersist = true;
	FString SlotName = TEXT("KakurenboSettings");
};
