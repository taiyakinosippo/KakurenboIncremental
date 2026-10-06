// プレイヤーの入力を処理するクラス。
// 入力アセットを作らなくても動くよう、毎フレームキーの状態を直接調べる方式にしている。
// （後で Enhanced Input のアセットに置き換えてもよい）
//
// 操作:
//   共通        マウス: 視点移動 / Enter: 次のパートへ
//   購入パート  1〜: 商品を買う
//   設置パート  WASD: 移動 / Space: ジャンプ / 左クリック: 壁を置く / 右クリック: 壁を回収
//               数字キー・ホイール: 壁の種類を選ぶ
//   かくれんぼ  Space または 左クリック: 連打

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
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

	/** マウス感度（1 カウントあたりの回転角度） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input")
	float MouseSensitivity = 0.15f;

	/** 壁を置ける距離（カメラから、cm） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Build")
	float BuildReach = 1200.f;

	// ===== 設置パートの状態（HUD が読む） =====

	/** 選択中の壁の種類（GameMode の WallTypes のインデックス） */
	UPROPERTY(BlueprintReadOnly, Category = "Build")
	int32 SelectedWallType = 0;

	/** 照準の先に置き場所があるか */
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

	/** 照準の先にある、回収できる壁 */
	UPROPERTY(BlueprintReadOnly, Category = "Build")
	TObjectPtr<APlaceableBlock> PickUpTarget;

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

	/** 連打を 1 回行う（テスト用） */
	UFUNCTION(Exec)
	void KakuMash(int32 Count = 1);

	/** 購入パートで商品を買う（数字キーと同じ。1 始まり） */
	UFUNCTION(Exec)
	void KakuBuy(int32 ItemNumber);

	/** 設置パートで壁を置く（テスト用）。例: KakuPlaceWall 10 12 0 */
	UFUNCTION(Exec)
	void KakuPlaceWall(int32 X, int32 Y, int32 WallType);

	/**
	 * 自動テスト：一連の操作を時間差で実行し、スクリーンショットを Saved/AutoTest に保存して終了する。
	 * Scenario: Loop（基本ループ） / Oni（鬼の追跡） / Build（壁の設置と破壊）
	 * 起動例: UnrealEditor.exe <uproject> -game -ExecCmds="KakuAutoTest Oni"
	 * 実装は KakurenboAutoTest.cpp
	 */
	UFUNCTION(Exec)
	void KakuAutoTest(const FString& Scenario);

protected:
	virtual void PlayerTick(float DeltaTime) override;

	void TickLook();
	void TickShop();
	void TickBuild();
	void TickHide();

	void DoMash();

	/** カメラの照準から置き場所・回収対象を求める */
	void UpdateBuildTarget();
	void DrawBuildPreview() const;

	/** 購入パートの商品を番号で買う（1 始まり） */
	bool BuyItem(int32 ItemNumber);

	AKakurenboGameMode* GetKakurenboGameMode() const;
	AHiderCharacter* GetHider() const;

	/** 数字キー 1〜9 のうち、このフレームに押されたものを返す（無ければ 0） */
	int32 GetPressedNumberKey() const;
};
