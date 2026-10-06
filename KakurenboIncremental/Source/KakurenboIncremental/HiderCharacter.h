// プレイヤー（隠れる側）のキャラクター。
// 入力の処理は KakurenboPlayerController が行い、ここは見た目・カメラ・移動能力を持つ。
//
// カメラは 2 種類あり、パートによって切り替える：
//   俯瞰（Overhead）    : 購入・設置・リザルト。斜め上から見下ろす。Q/E で回転、ホイールでズーム
//   一人称（FirstPerson）: かくれんぼ。自分の目の高さから、マウスで自由に見回す

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "KakurenboTypes.h"
#include "HiderCharacter.generated.h"

class UCameraComponent;
class UPointLightComponent;
class USpringArmComponent;
class UStaticMeshComponent;

UCLASS()
class KAKURENBOINCREMENTAL_API AHiderCharacter : public ACharacter
{
	GENERATED_BODY()

public:
	AHiderCharacter();

	/** 俯瞰カメラを支えるアーム（向きはワールド固定。プレイヤーの向きには追従しない） */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Hider|Camera")
	TObjectPtr<USpringArmComponent> CameraBoom;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Hider|Camera")
	TObjectPtr<UCameraComponent> OverheadCamera;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Hider|Camera")
	TObjectPtr<UCameraComponent> FirstPersonCamera;

	/** 仮の見た目（円柱）。BP でメッシュを差し替えてよい */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Hider")
	TObjectPtr<UStaticMeshComponent> BodyMesh;

	/** 一人称のときだけ点く、身の回りを照らす弱い明かり（壁で囲まれた中でも壁の色が分かるように） */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Hider|Camera")
	TObjectPtr<UPointLightComponent> HideLight;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hider")
	FLinearColor BodyColor = FLinearColor(0.1f, 0.4f, 1.f);

	/** 俯瞰カメラの見下ろす角度（度。-90 で真上から） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hider|Camera")
	float OverheadPitch = -60.f;

	/** 俯瞰カメラの距離（cm）。ホイールで変わる */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hider|Camera")
	float OverheadDistance = 1800.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hider|Camera")
	float OverheadMinDistance = 700.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hider|Camera")
	float OverheadMaxDistance = 3500.f;

	/** 一人称カメラの高さ（カプセルの中心から、cm） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hider|Camera")
	float EyeHeight = 64.f;

	/** 視点を切り替える（カメラ・体の表示・向きの制御をまとめて変える） */
	void SetViewMode(EHiderViewMode NewMode);
	EHiderViewMode GetViewMode() const { return ViewMode; }

	/** 俯瞰カメラの水平方向の向き（度） */
	float GetOverheadYaw() const { return OverheadYaw; }
	void SetOverheadYaw(float Yaw);
	void AddOverheadYaw(float DeltaDegrees);
	void AddOverheadZoom(float DeltaCm);

	/** 連打したときの見た目の反応（体が縮む。一人称のカメラは動かさない） */
	void PlayMashFeedback();

	/** 鬼の視線チェックで狙う点（頭・体・足） */
	void GetSightTargetPoints(TArray<FVector>& OutPoints) const;

protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

private:
	EHiderViewMode ViewMode = EHiderViewMode::FirstPerson;
	float OverheadYaw = 0.f;
	float MashPulse = 0.f;
	FVector BodyBaseScale = FVector::OneVector;
};
