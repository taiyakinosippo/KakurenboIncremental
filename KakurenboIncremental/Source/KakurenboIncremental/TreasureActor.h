// お宝。かくれんぼパートの開始時にランダムな空きマスに出現し、プレイヤーが触れると取得できる。
// ときどきキラキラと鳴る（その場所から聞こえる）ので、画面に映っていなくても場所の見当がつく。

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "TreasureActor.generated.h"

class UPointLightComponent;
class UStaticMeshComponent;

UCLASS()
class KAKURENBOINCREMENTAL_API ATreasureActor : public AActor
{
	GENERATED_BODY()

public:
	ATreasureActor();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Treasure")
	TObjectPtr<UStaticMeshComponent> Mesh;

	/** 遠くからでも見つけやすいように光らせる */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Treasure")
	TObjectPtr<UPointLightComponent> Glow;

	/** 取得したときにもらえるコイン（GameMode が出現時に設定する） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Treasure")
	double Value = 0.0;

	/** プレイヤーとの距離がこれ以下になったら取得（cm） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Treasure")
	float PickupRadius = 75.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Treasure")
	FLinearColor Color = FLinearColor(1.f, 0.72f, 0.08f);

	/** キラキラの音を鳴らす間隔（秒）と大きさ */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Treasure")
	float SparkleInterval = 1.6f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Treasure")
	float SparkleVolume = 0.8f;

	/** 鳴らしたキラキラの数（テスト用） */
	int32 GetSparkleCount() const { return SparkleCount; }

protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

private:
	FVector BaseMeshLocation = FVector::ZeroVector;
	float Time = 0.f;
	float SparkleTimer = 0.f;
	int32 SparkleCount = 0;
};
