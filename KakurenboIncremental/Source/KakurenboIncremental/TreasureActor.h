// お宝。かくれんぼパートの開始時にランダムな空きマスに出現し、プレイヤーが触れると取得できる。
// ときどきキラキラと鳴る（その場所から聞こえる）ので、画面に映っていなくても場所の見当がつく。

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "TreasureActor.generated.h"

class UMaterialInterface;
class UPointLightComponent;
class UMaterialInstanceDynamic;
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

	/**
	 * M15：館の色（暖かい明かり・木の床）に宝石が埋もれて見えにくかったので、
	 * 足元に光る輪・宝石のまわりを回る白い光の粒・宝石そのものの明滅（自分で光る強さ）を足した
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Treasure")
	TObjectPtr<UStaticMeshComponent> GroundGlow;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Treasure")
	TArray<TObjectPtr<UStaticMeshComponent>> Twinkles;

	/** 宝石が自分で光る強さ（明滅の弱いとき・強いとき） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Treasure")
	float GemGlowMin = 1.2f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Treasure")
	float GemGlowMax = 3.5f;

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
	/** 宝石・足元の輪の光の強さを変えるためのマテリアル */
	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> GemMID;

	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> GroundMID;
	bool bGemLook = false;
	float Time = 0.f;
	float SparkleTimer = 0.f;
	int32 SparkleCount = 0;
};
