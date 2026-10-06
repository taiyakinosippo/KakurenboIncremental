// プレイヤーの入力を処理するクラス。
// 入力アセットを作らなくても動くよう、毎フレームキーの状態を直接調べる方式にしている。
// （後で Enhanced Input のアセットに置き換えてもよい）
//
// 操作:
//   共通                Enter: 次のパートへ
//   購入・リザルト（俯瞰）マウス / Q・E: カメラ回転 / ホイール: ズーム / 数字キー: 購入
//   設置（俯瞰）         WASD: 移動 / Space: ジャンプ / カーソル+左クリック: 壁を置く / 右クリック: 回収
//                        数字キー: 壁の種類 / Q・E / ホイールクリックしながらドラッグ: カメラ回転 / ホイール: ズーム
//   かくれんぼ（一人称）  マウス: 見回す / Space・左クリック: 連打

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "KakurenboTypes.h"
#include "KakurenboPlayerController.generated.h"

class AKakurenboGameMode;
class AHiderCharacter;
class APlaceableBlock;

UCLASS()
class KAKURENBOINCREMENTAL_API AKakurenboPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	AKakurenboPlayerController();

	/** マウス感度（マウスの移動 1 カウントあたりの回転角度・度） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input")
	float MouseSensitivity = 0.12f;

	/** マウスの上下を反転する */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input")
	bool bInvertMouseY = false;

	/** 俯瞰カメラの Q/E 回転速度（度/秒） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera")
	float OverheadRotateSpeed = 120.f;

	/** ホイールクリックしながらドラッグしたときの回転量（度/ピクセル） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera")
	float OverheadDragSensitivity = 0.3f;

	/** ホイール 1 目盛りのズーム量（cm） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera")
	float OverheadZoomStep = 250.f;

	// ===== 設置パートの状態（HUD が読む） =====

	/** 選択中の壁の種類（GameMode の WallTypes のインデックス） */
	UPROPERTY(BlueprintReadOnly, Category = "Build")
	int32 SelectedWallType = 0;

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

	/** 購入パートで商品を買う（数字キーと同じ。1 始まり） */
	UFUNCTION(Exec)
	void KakuBuy(int32 ItemNumber);

	/** 設置パートで壁を置く（テスト用）。例: KakuPlaceWall 10 12 0 */
	UFUNCTION(Exec)
	void KakuPlaceWall(int32 X, int32 Y, int32 WallType);

	/** マウス感度を変える。例: KakuSens 0.2 */
	UFUNCTION(Exec)
	void KakuSens(float Sensitivity);

	/**
	 * 自動テスト：一連の操作を時間差で実行し、スクリーンショットを Saved/AutoTest に保存して終了する。
	 * Scenario: Loop（基本ループ） / Oni（鬼の追跡） / Touch（ぶつかったら発見） / Build（壁の設置と破壊） / Camera（視点と操作）
	 * 起動例: UnrealEditor.exe <uproject> -game -ExecCmds="KakuAutoTest Oni"
	 * 実装は KakurenboAutoTest.cpp
	 */
	UFUNCTION(Exec)
	void KakuAutoTest(const FString& Scenario);

protected:
	virtual void PlayerTick(float DeltaTime) override;

	/**
	 * このフレームの入力が確定した直後・視点の更新（UpdateRotation）の直前に呼ばれる。
	 * 視点の回転入力はここで加えないと反映されない（PlayerTick の Super の後では捨てられる）。
	 */
	virtual void PostProcessInput(const float DeltaTime, const bool bGamePaused) override;

	/** パートが変わったら視点（俯瞰／一人称）と入力モード（カーソル表示）を切り替える */
	void ApplyViewForPhase(EKakurenboPhase Phase);

	void HandleFirstPersonLook();
	void HandleOverheadCamera(float DeltaTime, bool bMouseOrbits);
	void HandleShopInput();
	void HandleBuildInput();
	void HandleHideInput();

	void DoMash();
	void DrawBuildPreview() const;
	void ClearBuildTarget();

	/** 購入パートの商品を番号で買う（1 始まり） */
	bool BuyItem(int32 ItemNumber);

	AKakurenboGameMode* GetKakurenboGameMode() const;

	/** 数字キー 1〜9 のうち、このフレームに押されたものを返す（無ければ 0） */
	int32 GetPressedNumberKey() const;

private:
	bool bViewInitialized = false;
	EKakurenboPhase ViewPhase = EKakurenboPhase::Hide;

	/** ホイールクリックでのドラッグ回転用 */
	bool bDraggingCamera = false;
	FVector2D LastDragMousePosition = FVector2D::ZeroVector;
};
