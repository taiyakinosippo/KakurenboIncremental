// プレイヤー（隠れる側）のキャラクター。
// 入力の処理は KakurenboPlayerController が行い、ここは見た目と移動能力だけを持つ。

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
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

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Hider")
	TObjectPtr<USpringArmComponent> CameraBoom;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Hider")
	TObjectPtr<UCameraComponent> Camera;

	/** 仮の見た目（円柱）。BP でメッシュを差し替えてよい */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Hider")
	TObjectPtr<UStaticMeshComponent> BodyMesh;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hider")
	FLinearColor BodyColor = FLinearColor(0.1f, 0.4f, 1.f);

	/** 連打したときの見た目の反応（少し縮む） */
	void PlayMashFeedback();

	/** 鬼の視線チェックで狙う点（頭と体の中心） */
	void GetSightTargetPoints(TArray<FVector>& OutPoints) const;

protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

private:
	float MashPulse = 0.f;
	FVector BodyBaseScale = FVector::OneVector;
};
