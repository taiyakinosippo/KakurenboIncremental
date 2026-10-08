// プレイヤー（隠れる側）のキャラクター。
// 入力の処理は KakurenboPlayerController が行い、ここは見た目・カメラ・移動能力を持つ。
//
// カメラは 1 本のアーム（SpringArm）の先に付いていて、パートによって向きと長さを変える：
//   俯瞰（Overhead）    : 購入・リザルト。斜め上から見下ろす。Q/E で回転、ホイールでズーム
//   真上（TopDown）     : 設置。真上から見下ろす（Z/X で斜めに傾けられる）。プレイヤーは表示せず、アームの根元（注視点）を WASD で動かす
//   三人称（ThirdPerson）: かくれんぼ。自分の背後の少し上から見る。マウスで回す、ホイールで距離を変える
// 置いたブロックはカメラがすり抜けるので、壁で囲まれても周りが見える。
// 三人称では舞台の外周の壁には当たって手前に寄る（舞台の外へ出ない）。

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "KakurenboTypes.h"
#include "HiderCharacter.generated.h"

class UCameraComponent;
class USpringArmComponent;
class UStaticMeshComponent;

UCLASS()
class KAKURENBOINCREMENTAL_API AHiderCharacter : public ACharacter
{
	GENERATED_BODY()

public:
	AHiderCharacter();

	/** カメラを支えるアーム（向きはワールド固定で、Tick で設定する） */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Hider|Camera")
	TObjectPtr<USpringArmComponent> CameraBoom;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Hider|Camera")
	TObjectPtr<UCameraComponent> Camera;

	/** 仮の見た目（円柱）。BP でメッシュを差し替えてよい */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Hider")
	TObjectPtr<UStaticMeshComponent> BodyMesh;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hider")
	FLinearColor BodyColor = FLinearColor(0.1f, 0.4f, 1.f);

	// ===== 俯瞰カメラ =====

	/** 見下ろす角度（度。-90 で真上から） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hider|Camera")
	float OverheadPitch = -60.f;

	/** 距離（cm）。ホイールで変わる */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hider|Camera")
	float OverheadDistance = 1800.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hider|Camera")
	float OverheadMinDistance = 700.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hider|Camera")
	float OverheadMaxDistance = 3500.f;

	// ===== 真上からのカメラ（設置パート） =====

	/** 高さ（cm）。ホイールで変わる。2600 で 24×24 マスの舞台がほぼ全部入る */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hider|Camera")
	float TopDownDistance = 2600.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hider|Camera")
	float TopDownMinDistance = 900.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hider|Camera")
	float TopDownMaxDistance = 4000.f;

	/** 見下ろす角度（度）。-90 で真上から。Z/X・ホイールを押して上下にドラッグで傾けられる（積んだ壁が見やすい） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hider|Camera")
	float TopDownPitch = -90.f;

	/** 一番傾けたときの角度（度） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hider|Camera")
	float TopDownMaxTiltPitch = -25.f;

	/** 設置パートのカメラを傾ける（プラスで横から見る向き、マイナスで真上に戻る向き） */
	void AddTopDownTilt(float DeltaDegrees);

	// ===== 煙幕ダッシュ（かくれんぼ中に Shift。足元に煙幕を投げて走る。1 ラウンドに使える回数が決まっている。
	//       転生のお店で解放し、速さ・回数も強化される） =====

	/** 煙幕ダッシュが使えるか（最初は使えない。GameMode が転生のお店のレベルから設定する） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hider|Dash")
	bool bDashUnlocked = false;

	/** ダッシュ中の速さの倍率（歩く速さ 420 × これ。強化では変わらない。強化で伸びるのは煙幕が残る時間） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hider|Dash")
	float DashSpeedMultiplier = 1.4f;

	/** ダッシュが続く時間（秒） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hider|Dash")
	float DashDuration = 1.5f;

	/** 1 ラウンドに煙幕ダッシュを使える回数（GameMode が転生のお店の「煙幕の数」から設定する） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hider|Dash")
	int32 DashUsesPerRound = 1;

	/** ダッシュの音の大きさ（1 = 連打と同じ距離まで届く） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hider|Dash")
	float DashNoiseLoudness = 1.5f;

	/** 煙幕ダッシュを始める。解放していない・回数が残っていない・ダッシュ中なら false */
	bool TryStartDash();

	/** ダッシュをやめて回数を元に戻す（ラウンドの始まり） */
	void ResetDash();

	/** このラウンドで残っている煙幕ダッシュの回数 */
	int32 GetDashUsesLeft() const { return DashUsesLeft; }

	/** 残りの回数を増やす・減らす（転生のお店で回数が変わったとき） */
	void AddDashUses(int32 Delta) { DashUsesLeft = FMath::Max(0, DashUsesLeft + Delta); }

	// ===== 足音・しのび足 =====

	/** 1 歩の長さ（cm）。これだけ歩くたびに足音が出る（鬼に聞こえる） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hider|Step")
	float StepStride = 120.f;

	/** しのび足（Ctrl を押しながら歩く）の速さの倍率。足音が小さくなる */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hider|Step")
	float SneakSpeedMultiplier = 0.5f;

	/** しのび足にする・やめる（ダッシュ中は速さを変えない） */
	void SetSneaking(bool bInSneaking);
	bool IsSneaking() const { return bSneaking; }

	/** 歩いた足音の数（テスト用） */
	int32 GetStepCount() const { return StepCount; }

	bool IsDashing() const { return DashTimeRemaining > 0.f; }

	// ===== 三人称カメラ =====

	/** 距離（cm）。ホイールで変わる */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hider|Camera")
	float ThirdPersonDistance = 550.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hider|Camera")
	float ThirdPersonMinDistance = 250.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hider|Camera")
	float ThirdPersonMaxDistance = 1100.f;

	/** 注視点をプレイヤーの中心からどれだけ上げるか（壁越しに見渡しやすくする） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hider|Camera")
	float ThirdPersonLookHeight = 70.f;

	/** 視点を切り替える（アームの動かし方・体の向きの制御をまとめて変える） */
	void SetViewMode(EHiderViewMode NewMode);
	EHiderViewMode GetViewMode() const { return ViewMode; }

	/** 今のカメラが水平方向に向いている角度（WASD をカメラ基準にするのに使う） */
	float GetViewYaw() const;

	/** 俯瞰カメラの水平方向の向き（度） */
	float GetOverheadYaw() const { return OverheadYaw; }
	void SetOverheadYaw(float Yaw);
	void AddOverheadYaw(float DeltaDegrees);

	/** ホイールでのズーム（今の視点の距離を変える） */
	void AddZoom(float DeltaCm);

	/** 真上からのカメラが見ている場所（床の高さ） */
	FVector GetTopDownFocus() const { return TopDownFocus; }
	void SetTopDownFocus(const FVector& Focus) { TopDownFocus = Focus; }

	/** 連打したときの見た目の反応（体が縮む。カメラは動かさない） */
	void PlayMashFeedback();

	/** 鬼の視線チェックで狙う点（頭・体・足） */
	void GetSightTargetPoints(TArray<FVector>& OutPoints) const;

protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	/** ジャンプした（音が出る） */
	virtual void OnJumped_Implementation() override;
	/** 着地した（1 歩ぶんの足音） */
	virtual void Landed(const FHitResult& Hit) override;

private:
	EHiderViewMode ViewMode = EHiderViewMode::ThirdPerson;
	float OverheadYaw = 0.f;
	float MashPulse = 0.f;
	FVector BodyBaseScale = FVector::OneVector;
	FVector TopDownFocus = FVector::ZeroVector;

	float WalkSpeed = 420.f;
	float DashTimeRemaining = 0.f;
	int32 DashUsesLeft = 0;

	bool bSneaking = false;
	float StepDistance = 0.f;
	FVector LastStepLocation = FVector::ZeroVector;
	int32 StepCount = 0;
	/** 足音を GameMode に知らせる */
	void NotifyStep();
	/** 今の歩く速さ（しのび足・ダッシュを反映）にする */
	void UpdateWalkSpeed();
};
