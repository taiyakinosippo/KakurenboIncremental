// プレイヤーの入力を処理するクラス。
// 入力アセットを作らなくても動くよう、毎フレームキーの状態を直接調べる方式にしている。
// （後で Enhanced Input のアセットに置き換えてもよい）
//
// 操作:
//   共通        マウス: 視点移動 / Enter: 次のパートへ
//   購入パート  1〜: 商品を買う
//   設置パート  WASD: 移動 / Space: ジャンプ
//   かくれんぼ  Space または 左クリック: 連打

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "KakurenboPlayerController.generated.h"

class AKakurenboGameMode;
class AHiderCharacter;

UCLASS()
class KAKURENBOINCREMENTAL_API AKakurenboPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	AKakurenboPlayerController();

	/** マウス感度（1 カウントあたりの回転角度） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input")
	float MouseSensitivity = 0.15f;

	// ===== デバッグ用コンソールコマンド（@ キーでコンソールを開いて入力） =====

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

	/**
	 * 自動テスト：一連の操作を時間差で実行し、スクリーンショットを Saved/AutoTest に保存して終了する。
	 * 起動例: UnrealEditor.exe <uproject> -game -ExecCmds="KakuAutoTest"
	 */
	UFUNCTION(Exec)
	void KakuAutoTest();

protected:
	virtual void PlayerTick(float DeltaTime) override;

	void TickLook();
	void TickShop();
	void TickBuild();
	void TickHide();

	void DoMash();

	/** 購入パートの商品を番号で買う（1 始まり） */
	bool BuyItem(int32 ItemNumber);

	AKakurenboGameMode* GetKakurenboGameMode() const;
	AHiderCharacter* GetHider() const;

	/** 数字キー 1〜9 のうち、このフレームに押されたものを返す（無ければ 0） */
	int32 GetPressedNumberKey() const;
};
