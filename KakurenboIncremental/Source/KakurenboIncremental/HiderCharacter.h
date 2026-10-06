// プレイヤー（隠れる側）のキャラクター。
// 入力の処理は KakurenboPlayerController が行い、ここは見た目・カメラ・移動能力を持つ。
//
// カメラは 1 本のアーム（SpringArm）の先に付いていて、パートによって向きと長さを変える：
//   俯瞰（Overhead）    : 購入・リザルト。斜め上から見下ろす。Q/E で回転、ホイールでズーム
//   真上（TopDown）     : 設置。真上から見下ろす。プレイヤーは表示せず、アームの根元（注視点）を WASD で動かす
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

	// ===== ダッシュ（かくれんぼ中に Shift） =====

	/** ダッシュ中の速さの倍率（歩く速さ 420 × これ） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hider|Dash")
	float DashSpeedMultiplier = 1.8f;

	/** ダッシュが続く時間（秒） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hider|Dash")
	float DashDuration = 1.5f;

	/** ダッシュしてから次にダッシュできるまでの時間（秒。ダッシュ中の時間も含む） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hider|Dash")
	float DashCooldown = 6.f;

	/** ダッシュの音の大きさ（1 = 連打と同じ距離まで届く） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hider|Dash")
	float DashNoiseLoudness = 1.5f;

	/** ダッシュを始める。クールタイム中なら false */
	bool TryStartDash();

	/** ダッシュをやめてクールタイムも消す（ラウンドの始まり） */
	void ResetDash();

	bool IsDashing() const { return DashTimeRemaining > 0.f; }

	/** 次にダッシュできるまでの残り秒数（0 ならすぐできる） */
	float GetDashCooldownRemaining() const { return DashCooldownRemaining; }

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

private:
	EHiderViewMode ViewMode = EHiderViewMode::ThirdPerson;
	float OverheadYaw = 0.f;
	float MashPulse = 0.f;
	FVector BodyBaseScale = FVector::OneVector;
	FVector TopDownFocus = FVector::ZeroVector;

	float WalkSpeed = 420.f;
	float DashTimeRemaining = 0.f;
	float DashCooldownRemaining = 0.f;
};
