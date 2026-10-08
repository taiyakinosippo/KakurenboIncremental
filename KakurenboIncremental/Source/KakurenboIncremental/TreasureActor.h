// お宝。かくれんぼパートの開始時にランダムな空きマスに出現し、プレイヤーが触れると取得できる。
// ときどきキラキラと鳴る（その場所から聞こえる）ので、画面に映っていなくても場所の見当がつく。

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "TreasureActor.generated.h"

class UMaterialInterface;
class UPointLightComponent;
class UStaticMesh;
class UStaticMeshComponent;

UCLASS()
class KAKURENBOINCREMENTAL_API ATreasureActor : public AActor
{
	GENERATED_BODY()

public:
	ATreasureActor();

	/** ふわふわ浮かびながら回る軸（メッシュとライトはこの子） */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Treasure")
	TObjectPtr<USceneComponent> Spin;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Treasure")
	TObjectPtr<UStaticMeshComponent> Mesh;

	/**
	 * 宝石の見た目（Fab の宝石。GameMode が出現時に設定する）。無ければ斜めにした立方体。
	 * LookSize は一番長い辺の長さ（cm）
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Treasure")
	TObjectPtr<UStaticMesh> LookMesh;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Treasure")
	TObjectPtr<UMaterialInterface> LookMaterial;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Treasure")
	float LookSize = 55.f;

	/** 宝石のメッシュで表示しているか（テスト用） */
	bool HasGemLook() const { return bGemLook; }

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
	float SparkleInterval = 1.2f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Treasure")
	float SparkleVolume = 1.3f;

	/** 鳴らしたキラキラの数（テスト用） */
	int32 GetSparkleCount() const { return SparkleCount; }

protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

private:
	FVector BaseSpinLocation = FVector::ZeroVector;
	bool bGemLook = false;
	float Time = 0.f;
	float SparkleTimer = 0.f;
	int32 SparkleCount = 0;
};
