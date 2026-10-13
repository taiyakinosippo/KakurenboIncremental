// プレイヤーの入力を処理するクラス。
// 入力アセットを作らなくても動くよう、毎フレームキーの状態を直接調べる方式にしている。
// キーは「操作（EKakurenboAction）→ キー」の表（UKakurenboSettings。設定画面のキー設定で変えられる）で調べる。
//
// 操作（初期設定）:
//   共通                 Esc: メニュー（一時停止・設定・タイトルへ） / F1・H: 操作説明 / Enter: 次のパートへ
//   タイトル              クリック・↑↓ と Enter: つづきから / はじめから / 設定 / やめる
//   購入（俯瞰）          クリック・数字キー: 購入 / Tab・タブをクリック: コインのお店 ↔ 転生のお店 / P を 2 回・ボタン: 転生
//                         Q・E / ホイールを押してドラッグ: カメラ回転 / ホイール: ズーム
//   リザルト（俯瞰）      クリック・Enter: 次へ / Q・E: カメラ回転 / ホイール: ズーム
//   設置（真上から）       WASD: カメラを動かす / カーソル+左クリック: 壁・罠を置く / 右クリック: 回収 / T: スタート位置をカーソルのマスへ
//                         数字キー・下の欄をクリック: 置く物（壁の種類 → 罠の種類の順） / Q・E / ホイールを押してドラッグ: カメラ回転 / ホイール: ズーム
//   かくれんぼ（三人称）   マウス: カメラ回転 / WASD: 移動 / Ctrl: しゃがむ / Space: よじ登る / Shift: 煙幕ダッシュ（回数制） / 左クリック・F: 連打 / ホイール: カメラの距離
//
// コントローラー（Xbox の並び。ボタンは決まっている。UKakurenboSettings::GamepadKey）:
//   共通                 START: メニュー / BACK: 操作説明 / 十字キー・左スティック: ボタンを選ぶ / A: 押す / B: もどる / Y: 次へ
//   購入                 LB・RB: お店の切り替え / X: まとめて補充 / 右スティック: カメラ回転 / LT・RT: ズーム
//   設置                 左スティック・十字キー: カーソル（カメラが付いてくる） / A: 置く / X: 回収 / LB・RB: 置く物 / B: 購入へ戻る
//                        L 押し込み: スタート位置 / R 押し込み 2 回: 全部回収 / 右スティック: 回す・傾ける / LT・RT: ズーム
//   かくれんぼ            左スティック: 移動 / 右スティック: カメラ / RT: 連打 / LT: しゃがむ / A: よじ登る / RB: 煙幕ダッシュ

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "KakurenboHUD.h"
#include "KakurenboTypes.h"
#include "KakurenboPlayerController.generated.h"

class AKakurenboGameMode;
class AHiderCharacter;
class APlaceableBlock;
class UKakurenboSettings;

/** 画面下に出す操作のヒント（はじめてのときだけ） */
struct FKakurenboTip
{
	FName Id;
	/** 文。[ ] で囲んだところはキーの形で描く */
	FString Text;
	/** 出してからの秒数（実時間） */
	float Age = 0.f;
};

UCLASS()
class KAKURENBOINCREMENTAL_API AKakurenboPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	AKakurenboPlayerController();

	/** マウス感度（マウスの移動 1 カウントあたりの回転角度・度）。設定画面で変えられる */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input")
	float MouseSensitivity = 0.12f;

	/** マウス（とスティック）の上下を反転する */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input")
	bool bInvertMouseY = false;

	/** コントローラーのスティックでカメラを回す速さの倍率（設定画面で変えられる） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input|Gamepad")
	float GamepadSensitivity = 1.f;

	/** 右スティックを倒しきったときにカメラが回る速さ（度/秒。倍率 1 のとき）：左右・上下 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input|Gamepad")
	float GamepadLookYawSpeed = 170.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input|Gamepad")
	float GamepadLookPitchSpeed = 110.f;

	/** スティックの遊び（中心からこの割合までは 0） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input|Gamepad")
	float GamepadDeadZone = 0.22f;

	/** 設置パートで左スティックがカーソルを動かす速さ（cm/秒。高さ 2600 のとき。ズームすると遅くなる） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input|Gamepad")
	float GamepadCursorSpeed = 1000.f;

	/** LT・RT でズームする速さ（cm/秒） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input|Gamepad")
	float GamepadZoomSpeed = 1600.f;

	/** コントローラーで遊んでいるか（最後に触ったのがコントローラー）。画面の案内とカーソルの出し方が変わる */
	bool IsUsingGamepad() const { return bUsingGamepad; }

	/** コントローラーで選んでいるボタンか（HUD が明るく描く。マウスで遊んでいるときは false） */
	bool IsUIFocused(EKakurenboUIAction Action, int32 Index) const;

	/** 決まった操作（キー設定に無いもの）の案内。キーボード・マウスかコントローラーかで変わる。[ ] で囲んだところはキーの形 */
	FString UIKeyLabel(EKakurenboUIKey Key) const;

	/** 設置パートでコントローラーのカーソルが指している床の位置（コントローラーで遊んでいなければ false） */
	bool GetGamepadBuildCursor(FVector& OutWorld) const;

	/** すべての入力（本物のキー・マウス・コントローラー）。最後に触った機器を覚える */
	virtual bool InputKey(const FInputKeyEventArgs& Params) override;

	/** 俯瞰カメラの Q/E 回転速度（度/秒） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera")
	float OverheadRotateSpeed = 120.f;

	/** ホイールを押しながらドラッグしたときの回転量（度/ピクセル） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera")
	float OverheadDragSensitivity = 0.3f;

	/** ホイール 1 目盛りのズーム量（cm） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera")
	float ZoomStep = 250.f;

	/** 設置パートで WASD がカメラを動かす速さ（cm/秒。高さ 2600 のとき。ズームすると遅くなる） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera")
	float TopDownPanSpeed = 1400.f;

	/** タイトル画面でカメラがゆっくり回る速さ（度/秒） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera")
	float TitleOrbitSpeed = 4.f;

	/** 転生の確認：1 回目の P からこの秒数以内にもう一度 P を押すと転生する */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input")
	float PrestigeConfirmSeconds = 3.f;

	/** 操作のヒントを出しておく秒数 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hint")
	float TipSeconds = 7.f;

	/** 転生の確認待ち（もう一度 P を押すと転生する）か */
	bool IsPrestigeConfirmPending() const;

	/** 設置パートの「全部回収」も 2 回押して決める（1 回目から 3 秒以内） */
	bool IsClearAllConfirmPending() const { return ClearAllConfirmUntil > 0.f && GetWorld() && GetWorld()->GetTimeSeconds() < ClearAllConfirmUntil; }

	/** 購入パートで開いているお店（コイン・宝石・転生） */
	EKakurenboShopTab ShopTab = EKakurenboShopTab::Coin;
	bool IsPrestigeShopTab() const { return ShopTab == EKakurenboShopTab::Prestige; }
	bool IsGemShopTab() const { return ShopTab == EKakurenboShopTab::Gem; }

	/** お店を切り替える（Delta = +1 で コイン → 宝石 → 転生 → コイン…） */
	void CycleShopTab(int32 Delta);

	/** M15：まとめ買い（商品をクリックしたときに 1 個・10 個・買えるだけ） */
	EKakurenboBuyAmount BuyAmount = EKakurenboBuyAmount::One;

	/** まとめ買いの数（買えるだけは 9999 までにしておく） */
	int32 GetBuyCount() const { return BuyAmount == EKakurenboBuyAmount::Ten ? 10 : BuyAmount == EKakurenboBuyAmount::Max ? 9999 : 1; }

	/** UI を指すカーソルの位置（ピクセル。自動テスト中はテスト用の位置） */
	bool GetUICursorPosition(FVector2D& OutPosition) const;

	/** 三人称カメラの上下の角度の範囲（度） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera")
	float ThirdPersonPitchMin = -75.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera")
	float ThirdPersonPitchMax = 15.f;

	// ===== メニュー（HUD が読む） =====

	/** 開いているメニュー（無ければ None） */
	EKakurenboMenu GetMenu() const { return MenuStack.Num() > 0 ? MenuStack.Last() : EKakurenboMenu::None; }
	bool IsMenuOpen() const { return MenuStack.Num() > 0; }

	/** メニューを開く（重ねる）・1 つ前に戻る・全部閉じる */
	void OpenMenu(EKakurenboMenu Menu);
	void CloseMenu();
	void CloseAllMenus();

	/**
	 * M15：鬼のチュートリアルを開く（ゲームは止まる）。bBook なら一時停止メニューからの図鑑（もどると一時停止メニュー）。
	 * 写真はスタジオで撮り直す
	 */
	void OpenOniIntro(const TArray<EOniType>& Types, bool bBook);

	/** チュートリアル・図鑑のページ（鬼の種類）と、今のページ */
	const TArray<EOniType>& GetOniIntroPages() const { return OniIntroPages; }
	int32 GetOniIntroPage() const { return OniIntroPage; }
	bool IsOniBook() const { return bOniBook; }

	/** ページを進める・戻す（最後のページで進めると閉じる） */
	void TurnOniIntroPage(int32 Delta);

	/** 今のステージの鬼のうち、まだ紹介していない種類があればチュートリアルを出す（かくれんぼの始め） */
	void ShowNewOniIntro();

	/** 今まで紹介した鬼の種類（図鑑に出す） */
	TArray<EOniType> GetIntroducedOniTypes() const;

	/** 鬼の種類の紹介を見たことにする名前（ヒントと同じく設定に保存。「ヒントをもう一度見る」で消える） */
	static FName OniIntroTipId(EOniType Type);

	/** タイトルのメニューで、キーボードで選んでいる項目 */
	int32 GetTitleSelection() const { return TitleSelection; }

	/** タイトルのメニューの項目が押せるか（「つづきから」はセーブが無いと押せない） */
	bool IsTitleItemEnabled(EKakurenboTitleItem Item) const;

	/** キー設定で、キーが押されるのを待っているか（待っている操作と、2 つ目か） */
	bool IsWaitingForKey(EKakurenboAction& OutAction, bool& bOutSecondary) const;

	/** 今出している操作のヒント（無ければ null） */
	const FKakurenboTip* GetCurrentTip() const { return Tips.Num() > 0 ? &Tips[0] : nullptr; }

	/**
	 * 操作のヒントを出す（設定でヒントを切っている・もう見た・並んでいるなら出さない）。
	 * Text の [ ] で囲んだところはキーの形で描く
	 */
	void QueueTip(FName Id, const FString& Text);

	/** キーの名前（キー設定を反映。「W」「Ctrl」など。bBoth なら 2 つ目も「Ctrl / C」） */
	FString KeyName(EKakurenboAction Action, bool bBoth = false) const;

	// ===== 設置パートの状態（HUD が読む） =====

	/**
	 * 選択中の置く物。0 〜 壁の種類数-1 は壁（GameMode の WallTypes のインデックス）、
	 * それ以降は罠（TrapTypes のインデックス + 壁の種類数）
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Build")
	int32 SelectedBuildSlot = 0;

	/** 罠を選んでいるか */
	bool IsTrapSlotSelected() const;

	/** 下の欄に並べる置く物（SelectedBuildSlot と同じ番号の並び。宝石の道具は持っているものだけ） */
	TArray<int32> GetBuildSlots() const;

	/** 置く物を欄の並びで前後に選ぶ（LB・RB） */
	void SelectBuildSlotByOffset(int32 Delta);

	/** そのマスに一方通行ののれんがあるか（左クリック・A で向きを変える） */
	bool IsOneWayAt(const FIntPoint& Cell) const;

	/** 選んでいる壁の種類（罠を選んでいたら INDEX_NONE） */
	int32 GetSelectedWallType() const;

	/** 選んでいる罠の種類（壁を選んでいたら INDEX_NONE） */
	int32 GetSelectedTrapType() const;

	/** カーソルの先に置き場所があるか */
	UPROPERTY(BlueprintReadOnly, Category = "Build")
	bool bHasBuildTarget = false;

	/** 置き場所に置けるか */
	UPROPERTY(BlueprintReadOnly, Category = "Build")
	bool bCanPlaceAtTarget = false;

	/** 置けない理由 */
	UPROPERTY(BlueprintReadOnly, Category = "Build")
	FText BuildTargetReason;

	UPROPERTY(BlueprintReadOnly, Category = "Build")
	FIntPoint BuildTargetCell = FIntPoint::ZeroValue;

	/** カーソルの先にある、回収できる壁 */
	UPROPERTY(BlueprintReadOnly, Category = "Build")
	TObjectPtr<APlaceableBlock> PickUpTarget;

	/** カーソルの先に、回収できる罠（または発動して消えた罠の設計図）があるか */
	UPROPERTY(BlueprintReadOnly, Category = "Build")
	bool bHasTrapPickUpTarget = false;

	UPROPERTY(BlueprintReadOnly, Category = "Build")
	FIntPoint TrapPickUpCell = FIntPoint::ZeroValue;

	/** 画面上の位置（ピクセル）から置き場所と回収対象を求める。設置パートではカーソル位置で毎フレーム呼ばれる */
	void UpdateBuildTargetAt(const FVector2D& ScreenPosition);

	AHiderCharacter* GetHider() const;

	// ===== デバッグ用コンソールコマンド（プレイ中にコンソールを開いて入力） =====

	/** 例: KakuAddCoins 1000 */
	UFUNCTION(Exec)
	void KakuAddCoins(double Amount);

	/** 例: KakuSkipTime 10  （かくれんぼの残り時間を減らす） */
	UFUNCTION(Exec)
	void KakuSkipTime(float Seconds);

	/** 次のパートへ進める（Enter キーと同じ） */
	UFUNCTION(Exec)
	void KakuNext();

	/** 連打を行う（テスト用） */
	UFUNCTION(Exec)
	void KakuMash(int32 Count = 1);

	/** 購入パートで商品を買う（数字キーと同じ。1 始まり。開いているお店の商品） */
	UFUNCTION(Exec)
	void KakuBuy(int32 ItemNumber);

	/** 転生のお店で買う（1 始まり。開いているお店に関係なく） */
	UFUNCTION(Exec)
	void KakuBuyPrestige(int32 ItemNumber);

	/** 設置パートで壁を置く（テスト用）。例: KakuPlaceWall 10 12 0 */
	UFUNCTION(Exec)
	void KakuPlaceWall(int32 X, int32 Y, int32 WallType);

	/** 設置パートで罠を置く（テスト用）。例: KakuPlaceTrap 10 12 0 */
	UFUNCTION(Exec)
	void KakuPlaceTrap(int32 X, int32 Y, int32 TrapType);

	/** 全体の音量を変える（0〜1）。例: KakuVolume 0.3 */
	UFUNCTION(Exec)
	void KakuVolume(float Volume);

	/** マウス感度を変える。例: KakuSens 0.2 */
	UFUNCTION(Exec)
	void KakuSens(float Sensitivity);

	/** セーブを消して最初からやり直す */
	UFUNCTION(Exec)
	void KakuResetSave();

	/** 転生する（購入パートで P を 2 回押すのと同じ。確認なし） */
	UFUNCTION(Exec)
	void KakuPrestige();

	/** 煙幕ダッシュする（かくれんぼ中に Shift を押すのと同じ） */
	UFUNCTION(Exec)
	void KakuDash();

	/** 設置パートでスタート位置を動かす（T キーと同じ）。例: KakuSetStart 5 5 */
	UFUNCTION(Exec)
	void KakuSetStart(int32 X, int32 Y);

	/** 設置パートから購入パートへ戻る（B キーと同じ） */
	UFUNCTION(Exec)
	void KakuShop();

	/** 宝石を増やす。例: KakuAddGems 20 */
	UFUNCTION(Exec)
	void KakuAddGems(int32 Amount);

	/** 宝石のお店で買う（1 始まり。開いているお店に関係なく） */
	UFUNCTION(Exec)
	void KakuBuyGem(int32 ItemNumber);

	/** だるまさんがころんだを使う（G キーと同じ） */
	UFUNCTION(Exec)
	void KakuDaruma();

	/** BGM の音量（0〜1）。例: KakuMusicVolume 0.2 */
	UFUNCTION(Exec)
	void KakuMusicVolume(float Volume);

	/**
	 * 自動テスト：一連の操作を時間差で実行し、スクリーンショットを Saved/AutoTest に保存して終了する。
	 * 最初にタイトルを抜ける（セーブがあればつづきから、無ければはじめから）。シナリオの一覧は CLAUDE.md
	 * 起動例: UnrealEditor.exe <uproject> -game -ExecCmds="KakuAutoTest Senses"
	 * 実装は KakurenboAutoTest.cpp
	 */
	UFUNCTION(Exec)
	void KakuAutoTest(const FString& Scenario);

protected:
	virtual void BeginPlay() override;
	virtual void PlayerTick(float DeltaTime) override;
	virtual void Tick(float DeltaSeconds) override;

	/**
	 * このフレームの入力が確定した直後・視点の更新（UpdateRotation）の直前に呼ばれる。
	 * 視点の回転入力はここで加えないと反映されない（PlayerTick の Super の後では捨てられる）。
	 * 一時停止中も呼ばれる（bGamePaused = true）ので、メニューの操作もここで行う
	 */
	virtual void PostProcessInput(const float DeltaTime, const bool bGamePaused) override;

	/** パートが変わったら視点（俯瞰／三人称）を切り替える */
	void ApplyViewForPhase(EKakurenboPhase Phase);

	/** カーソルを出すか（メニュー・購入・設置・リザルト・タイトル）・マウスでカメラを回すか（かくれんぼ）に合わせて入力モードを切り替える */
	void ApplyInputMode();

	void HandleMouseLook();
	/** かくれんぼ：右スティックでカメラを回す */
	void HandleGamepadLook(float DeltaTime);
	void HandleOverheadCamera(float DeltaTime);
	/** 設置パート：WASD で真上からのカメラを画面の向きに動かす */
	void HandleTopDownPan(float DeltaTime);
	void HandleCharacterMovement();
	void DoDash();
	/** よじ登る（Dir の方向。無ければ体の向き） */
	void DoClimb(const FVector& Dir);
	/** 設置パート：スタート位置をそのマスへ（できなければ理由をお知らせに出す） */
	bool SetStartCell(const FIntPoint& Cell);
	void HandleZoom();
	void HandleShopInput();
	void HandleBuildInput();
	void HandleHideInput();
	void HandleTitleInput();
	/** メニューを開いている間の操作（Esc・キー設定のキー待ち・つまみのドラッグ） */
	void HandleMenuInput();

	/** タイトルの項目を選んだ */
	void ActivateTitleItem(EKakurenboTitleItem Item);
	/** 一時停止メニューの項目を選んだ */
	void ActivatePauseItem(EKakurenboPauseItem Item);
	/** 設定のつまみの値（0〜1）を変える */
	void SetSliderValue(EKakurenboSettingSlider Slider, float Value01);
	void ToggleSetting(EKakurenboSettingToggle Toggle);

	/** パートや鬼の様子を見て、はじめての操作のヒントを出す */
	void UpdateTips(float RealDeltaSeconds);

	void DoMash();
	void DrawBuildPreview() const;
	void ClearBuildTarget();

	/** 購入パートの商品を番号で買う（1 始まり。開いているお店の商品） */
	bool BuyItem(int32 ItemNumber);

	/** 転生ボタン・P キー：1 回目は確認、確認中にもう一度で転生する */
	void RequestPrestige();
	void RequestClearAll();

	/**
	 * 左クリックが HUD のボタンの上なら、そのボタンの働きをする。
	 * @return ボタンを押した（クリックを使った）なら true
	 */
	bool HandleUIClick();

	/** ボタンを押したのと同じ働きをする（bFromMouse: マウスで押した。つまみはその位置の値にしてドラッグを始める） */
	void ActivateUIButton(const FKakurenboUIButton& Button, const FVector2D& Point, bool bFromMouse);

	// ---- コントローラー ----

	/** スティックの値（遊びを除いて 0〜1 に伸ばしたもの。上・右がプラス） */
	FVector2D GetStick(bool bRight) const;

	/** 十字キー（bWithStick なら左スティックも）を押した方向。押した瞬間と、押し続けたときのくり返しで返す（上が Y = -1）。無ければ (0, 0) */
	FIntPoint ReadNavigation(float RealDeltaSeconds, bool bWithStick);

	/**
	 * 画面のボタンを十字キー・左スティックで選び、A で押す（メニュー・購入・リザルト）。
	 * bSliders: 選んでいるのがつまみなら、左右で値を変える
	 */
	void HandleGamepadFocus(float RealDeltaSeconds, bool bSliders);

	/** 選んでいるボタンが画面に無ければ、その画面の最初のボタンを選ぶ */
	int32 EnsureFocus(const TArray<FKakurenboUIButton>& Buttons);

	/** 設置パート：左スティック・十字キーでカーソル、A で置く、X で回収、LB・RB で置く物を選ぶ */
	void HandleGamepadBuild(float DeltaTime);

	/** マスを指したときの置き場所と回収対象（コントローラーのカーソル用） */
	void UpdateBuildTargetAtCell(const FIntPoint& Cell);

	/** 設定のつまみの今の値（0〜1） */
	float GetSliderValue(EKakurenboSettingSlider Slider) const;

	/** メニューを開く・閉じるボタン（Esc / START）・もどるボタン（Esc / B）がこのフレームに押されたか */
	bool WasMenuKeyPressed() const;
	bool WasBackKeyPressed() const;

	AKakurenboGameMode* GetKakurenboGameMode() const;
	UKakurenboSettings* Settings() const;

	/** 操作のキーが押されているか / このフレームに押されたか（キー設定を反映） */
	bool IsActionDown(EKakurenboAction Action) const;
	bool WasActionPressed(EKakurenboAction Action) const;

	/** 数字キー 1〜9 のうち、このフレームに押されたものを返す（無ければ 0） */
	int32 GetPressedNumberKey() const;

private:
	/** 自動テスト中は、実際のキーボード・マウス入力を無視する（テストの疑似入力だけを通す） */
	void ApplyAutoTestInputIsolation();
	bool bIgnoreRealInputForAutoTest = false;

	/** 自動テスト用：設置パートのカーソル位置を本物のマウスの代わりに使う（本物のカーソルは動かさない） */
	bool bUseTestCursor = false;
	FVector2D TestCursorPosition = FVector2D::ZeroVector;

	/** 自動テスト用：Ctrl を押しているのと同じ（しのび足） */
	bool bTestSneak = false;

	/** しゃがむを「1 回押して切り替え」にしているときの、今しゃがんでいるか（かくれんぼの始めに立つ） */
	bool bSneakToggled = false;

	/** 自動テスト中も操作のヒントを出す（ふだんのテストでは、スクリーンショットが変わらないよう出さない） */
	bool bShowTipsInAutoTest = false;

	/** 自動テスト中も鬼のチュートリアルを出す（ふだんのテストでは止まってしまうので出さない） */
	bool bShowOniIntroInAutoTest = false;

	/** 鬼のチュートリアル・図鑑 */
	TArray<EOniType> OniIntroPages;
	int32 OniIntroPage = 0;
	bool bOniBook = false;

	/** いまのページを出した時刻（実時間）。出てすぐは「つぎ」を受け付けない（移動しようとしたキーで読む前に消えないように） */
	double OniIntroPageShownTime = 0.0;

public:
	/** 鬼のチュートリアルのページを出してから「つぎ」を受け付けるまでの秒数（図鑑は除く） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kakurenbo|Tips")
	float OniIntroMinSeconds = 1.0f;

	/** いまのページで「つぎ」を受け付けるか */
	bool CanTurnOniIntroPage() const;

private:

	/** 設置パートのカメラの高さを最後に合わせた館の大きさ */
	FIntPoint LastTopDownFitSize = FIntPoint::ZeroValue;

	bool bViewInitialized = false;
	EKakurenboPhase ViewPhase = EKakurenboPhase::Hide;

	/** ホイールを押しながらのドラッグ回転用 */
	bool bDraggingCamera = false;
	FVector2D LastDragMousePosition = FVector2D::ZeroVector;

	/** 転生の確認：このワールド時刻まで、もう一度 P を押すと転生する */
	float PrestigeConfirmUntil = -1.f;
	float ClearAllConfirmUntil = -1.f;

	/** 開いているメニュー（最後が一番上） */
	TArray<EKakurenboMenu> MenuStack;
	/** メニューを開いたときにゲームを止めたか（閉じたら動かす） */
	bool bPausedByMenu = false;
	/** カーソルを出しているか（入力モードを最後に設定した状態） */
	bool bCursorModeApplied = false;
	bool bCursorModeKnown = false;

	int32 TitleSelection = 0;

	/** キー設定：キーが押されるのを待っている欄（INDEX_NONE なら待っていない） */
	int32 WaitingKeySlot = INDEX_NONE;

	/** つまみをドラッグしている（INDEX_NONE ならしていない） */
	int32 DraggingSlider = INDEX_NONE;
	FBox2D DraggingSliderRect = FBox2D(ForceInit);

	/** 操作のヒント（先頭を出している） */
	TArray<FKakurenboTip> Tips;
	/** かくれんぼの経過（ヒントを出す時間の目安・実時間） */
	float HideTipClock = 0.f;

	// ---- コントローラー ----

	/** 最後に触ったのがコントローラーか（キーボード・マウスを触ると false に戻る） */
	bool bUsingGamepad = false;
	/** カーソルの位置（マウスが動いたらキーボード・マウスに戻すため） */
	FVector2D LastMousePosition = FVector2D(-1.f, -1.f);
	/** 入力モード（カーソルを出しているか・UI のパートか）を最後に設定した状態 */
	bool bUIModeApplied = false;

	/** コントローラーで選んでいるボタン（Action が None なら選んでいない） */
	EKakurenboUIAction FocusAction = EKakurenboUIAction::None;
	int32 FocusIndex = 0;

	/** 十字キー・スティックを押し続けたときのくり返し */
	FIntPoint HeldNavDir = FIntPoint::ZeroValue;
	float NavRepeatTimer = 0.f;

	/** 設置パートのコントローラーのカーソル（床の位置） */
	FVector GamepadCursor = FVector::ZeroVector;
	bool bGamepadCursorValid = false;
};
