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

class UAnimationAsset;
class UCameraComponent;
class UMaterialInterface;
class UPoseableMeshComponent;
class USkeletalMesh;
class USkeletalMeshComponent;
class USpringArmComponent;
class USpotLightComponent;
class UPointLightComponent;
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

	/** 懐中電灯（真っ暗ブロックで館が真っ暗なときだけ点く。カメラの向いている前を照らして、どっちが前か分かるようにする。M15） */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Hider")
	TObjectPtr<USpotLightComponent> Flashlight;

	/** 懐中電灯の明るさ・届く距離（cm）・広がり（片側の角度） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hider|Flashlight")
	float FlashlightIntensity = 100000.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hider|Flashlight")
	float FlashlightRange = 3000.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hider|Flashlight")
	float FlashlightAngle = 42.f;

	void SetFlashlightOn(bool bOn);
	bool IsFlashlightOn() const;

	/** 真っ暗のときに体がほんのり光る（ユーザーの希望。自分の場所が分かるように。鬼に見つかりやすくはならない）。カメラ側から体を照らす弱い光 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Hider")
	TObjectPtr<UPointLightComponent> BodyGlow;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hider|Flashlight")
	float BodyGlowIntensity = 6000.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hider|Flashlight")
	float BodyGlowRadius = 260.f;

	void SetBodyGlowOn(bool bOn);
	bool IsBodyGlowOn() const;

	// ===== 見た目（Fab のキャラクター。GameMode が ApplyLook で設定する。無ければ円柱） =====

	/**
	 * キャラクターのモデルを使う。体の高さ（カプセル）に合わせて大きさを変え、足を床に置き、円柱は隠す。
	 * モデルにはアニメーションが無いので、歩くと手足を振る動きをプログラムで付ける（骨の名前から脚・腕を探す）
	 */
	void ApplyLook(USkeletalMesh* Model, UMaterialInterface* Material, float MeshYaw);

	/** キャラクターのモデル（骨を直接動かせるメッシュ） */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Hider")
	TObjectPtr<UPoseableMeshComponent> LookMesh;

	/** 歩くときに脚・腕を振る角度（度） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hider|Look")
	float LegSwingDegrees = 32.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hider|Look")
	float ArmSwingDegrees = 28.f;

	/** 腕を下ろす角度（度。T ポーズのモデルの腕を体の横へ） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hider|Look")
	float ArmDownDegrees = 72.f;

	/**
	 * 公式のアニメーション（UE のマネキンの待機・歩く・走る・落ちる）で動かす。見えないマネキン（SourceMesh）にアニメーションを再生させ、
	 * 同じ名前の骨の回転を毎フレーム、キャラクターのモデルへ写す（骨の名前がマネキンと同じモデルなら、そのまま動く）。
	 * Move は待機〜走るのブレンドスペース（横軸が速さ）、Fall は空中のアニメーション。ApplyLook の後に呼ぶ
	 */
	void ApplyOfficialAnimation(USkeletalMesh* SourceMesh, UAnimationAsset* Move, UAnimationAsset* Fall);

	/** 公式のアニメーションで動いているか（テスト用） */
	bool HasOfficialAnimation() const { return AnimDriver != nullptr && DrivenBoneCount > 0; }
	int32 GetDrivenBoneCount() const { return DrivenBoneCount; }

	/** モデルを使っていて、手足を動かせる骨が見つかったか（テスト用） */
	bool HasAnimatedLook() const { return LookMesh != nullptr && (HasOfficialAnimation() || (LeftThigh != NAME_None && RightThigh != NAME_None)); }

	/** 公式のアニメーションを再生する見えないマネキン */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Hider")
	TObjectPtr<USkeletalMeshComponent> AnimDriver;

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

	// ===== よじ登る（転生のお店で解放。前の壁・低い家具の上へ 1 段ずつ。M14 でジャンプの代わりに） =====
	// 横への勢いは無いので、隙間は越えられない（壁の上どうしは、つながっている所だけ歩ける）。
	// 登っている間は動けず、音も出る。2 段以上に積んだ壁の上は鬼が飛び乗れない（下から壊すしかない）＝籠城

	/** よじ登れるか（最初は使えない。GameMode が転生のお店のレベルから設定する） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hider|Climb")
	bool bClimbUnlocked = false;

	/** よじ登れる高さ（cm）。壁 1 段（160cm）と少し */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hider|Climb")
	float ClimbMaxHeight = 175.f;

	/** これより低い段差はよじ登らない（歩いて上る・上れない） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hider|Climb")
	float ClimbMinHeight = 40.f;

	/** 体の縁から前へ何 cm までの壁・家具によじ登れるか */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hider|Climb")
	float ClimbReach = 45.f;

	/**
	 * Dir の方向（水平）の壁・家具によじ登り始める。登れる物が無い・解放していない・空中・ダッシュ中なら false。
	 * 登る秒数は壁の種類（Walls.csv の ClimbSeconds）・家具（GameMode の FurnitureClimbSeconds）で決まる
	 */
	bool TryStartClimb(const FVector& Dir);

	/** Dir の方向によじ登れる物があるか（あれば上に立つ位置と、登るのにかかる秒数） */
	bool FindClimbTarget(const FVector& Dir, FVector& OutStandLocation, float& OutSeconds) const;

	/** よじ登っている途中か（その間は動けない） */
	bool IsClimbing() const { return bClimbing; }

	/** よじ登るのをやめる（パートが変わったとき） */
	void CancelClimb();

	/** よじ登った回数・最後にかかった秒数（テスト用） */
	int32 GetClimbCount() const { return ClimbCount; }
	float GetLastClimbSeconds() const { return ClimbDuration; }

	// ===== 足音・しのび足 =====

	/** 1 歩の長さ（cm）。これだけ歩くたびに足音が出る（鬼に聞こえる） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hider|Step")
	float StepStride = 120.f;

	/**
	 * しのび足のときはしゃがむ（体の高さがこの 2 倍＝70cm になる）。テーブル・椅子・つぼなど、体の半分くらいの高さの家具の陰に隠れられる。
	 * 鬼が狙う頭・体・足の点も下がる
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hider|Step")
	float CrouchedHalfHeight = 35.f;

	/** しゃがんだときの見た目：太もも・すね・背中を曲げる角度（度） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hider|Look")
	float CrouchThighDegrees = 80.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hider|Look")
	float CrouchCalfDegrees = 120.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hider|Look")
	float CrouchSpineDegrees = 20.f;

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

	/**
	 * M14：壁に当たって寄ったカメラが、壁から離れたときに元の距離へ戻る速さ（FInterpTo の速さ。大きいほど速い）。
	 * 寄るときはすぐ（壁の中に入らない）、戻るときはゆっくり。すぐ戻すと、部屋の壁ぎわ・廊下で寄ったり離れたりを繰り返して揺れた
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hider|Camera")
	float CameraReturnSpeed = 2.5f;

	/** 今のカメラのアームの長さ（壁に当たって寄った分を含む。テスト用） */
	float GetCameraArmLength() const;

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

	/** タイトル画面の俯瞰カメラ（低い角度・遠く）にする / 元の俯瞰カメラに戻す */
	void SetTitleCamera(bool bTitle);

	/** タイトル画面のカメラの角度（度）と距離（cm） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hider|Camera")
	float TitlePitch = -24.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hider|Camera")
	float TitleDistance = 1250.f;

	/** 真上からのカメラが見ている場所（床の高さ） */
	FVector GetTopDownFocus() const { return TopDownFocus; }
	void SetTopDownFocus(const FVector& Focus) { TopDownFocus = Focus; }

	/** 連打したときの見た目の反応（体が縮む。カメラは動かさない） */
	void PlayMashFeedback();

	/** 鬼の視線チェックで狙う点（頭・体・足） */
	void GetSightTargetPoints(TArray<FVector>& OutPoints) const;

	/** 身代わり人形（AFakeHiderCharacter）か。人形の足音はプレイヤーの足音として鬼に聞かせない */
	bool bIsDecoyDoll = false;

protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	/** 着地した（1 歩ぶんの足音） */
	virtual void Landed(const FHitResult& Hit) override;

private:
	/** 三人称カメラ：ホイールの距離へなめらかに近づけている長さ / 壁で縮めた長さ（-1 はまだ決めていない） */
	float ZoomArmLength = -1.f;
	float CollisionArmLength = -1.f;

	EHiderViewMode ViewMode = EHiderViewMode::ThirdPerson;
	float OverheadYaw = 0.f;
	float MashPulse = 0.f;
	FVector BodyBaseScale = FVector::OneVector;
	FVector TopDownFocus = FVector::ZeroVector;

	float WalkSpeed = 420.f;
	float DashTimeRemaining = 0.f;
	int32 DashUsesLeft = 0;

	/** よじ登っている途中：前半で真上へ上がり、後半で前へ出て上に立つ */
	bool bClimbing = false;
	float ClimbElapsed = 0.f;
	float ClimbDuration = 1.f;
	FVector ClimbFrom = FVector::ZeroVector;
	FVector ClimbTo = FVector::ZeroVector;
	int32 ClimbCount = 0;
	void TickClimb(float DeltaSeconds);

	bool bSneaking = false;
	float StepDistance = 0.f;
	FVector LastStepLocation = FVector::ZeroVector;
	int32 StepCount = 0;
	/** しのび足でダッシュしていなければしゃがむ・それ以外は立つ */
	void UpdateCrouch();
	virtual void OnStartCrouch(float HalfHeightAdjust, float ScaledHalfHeightAdjust) override;
	virtual void OnEndCrouch(float HalfHeightAdjust, float ScaledHalfHeightAdjust) override;
	/** しゃがんだ姿勢（ひざ・腰を曲げ、足が床に付くようにモデルを下げる） */
	void ApplyCrouchPose(float DeltaSeconds, float Bob);
	/** しゃがんでカプセルが低くなった分、見た目を上げて足を床に置く（cm） */
	float CrouchMeshOffset = 0.f;
	float CrouchBlend = 0.f;
	float SmoothedCrouchLift = 0.f;
	/** しゃがみきったときに足が浮く量（cm。元の姿勢で一度だけ測る） */
	float FullCrouchLift = 0.f;
	bool bCrouchLiftMeasured = false;
	/** 公式のアニメーションに渡す速さ（なめらかにしたもの） */
	float SmoothedAnimSpeed = 0.f;
	/** タイトル画面のカメラにしているか・その前の俯瞰カメラの角度と距離 */
	bool bTitleCamera = false;
	float SavedOverheadPitch = -60.f;
	float SavedOverheadDistance = 1800.f;
	FVector StandingBodyScale = FVector::OneVector;
	FName LeftFoot, RightFoot;

	/** 足音を GameMode に知らせる */
	void NotifyStep();
	/** 今の歩く速さ（しのび足・ダッシュを反映）にする */
	void UpdateWalkSpeed();

	/** モデルの手足を動かす（歩く速さに合わせて振る・止まっているときは軽く揺れる） */
	void UpdateLookPose(float DeltaSeconds);
	/** 骨を部品の座標で回す（親から順に呼ぶ。子の骨は付いてくる） */
	void RotateBone(FName Bone, const FQuat& DeltaComponentSpace);

	/** 動かす骨（見つからなければ NAME_None） */
	FName LeftThigh, RightThigh, LeftCalf, RightCalf, LeftUpperArm, RightUpperArm, LeftForearm, RightForearm, Spine;
	/** モデルの元の位置と大きさ（体を縮める・弾ませるときの基準） */
	FVector LookBaseLocation = FVector::ZeroVector;
	FVector LookBaseScale = FVector::OneVector;
	float WalkPhase = 0.f;
	float WalkBlend = 0.f;
	float IdleTime = 0.f;

	/** 公式のアニメーション：再生するアセットと、モデルの骨ごとのマネキンの骨の番号（無ければ INDEX_NONE） */
	UPROPERTY()
	TObjectPtr<UAnimationAsset> MoveAnim;

	UPROPERTY()
	TObjectPtr<UAnimationAsset> FallAnim;

	UPROPERTY()
	TObjectPtr<UAnimationAsset> PlayingAnim;

	TArray<int32> DriverBoneIndex;
	int32 DrivenBoneCount = 0;
	int32 PelvisBoneIndex = INDEX_NONE;
	float DriverHeightRatio = 1.f;
	/** マネキンの今のポーズを、モデルへ写す */
	void CopyDriverPose();
};
