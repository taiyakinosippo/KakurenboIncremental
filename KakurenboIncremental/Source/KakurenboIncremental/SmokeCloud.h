// 煙幕。煙幕ダッシュ（Shift）でプレイヤーの足元に投げる。
// しばらくの間、煙の中・煙の向こう側は鬼から見えない（追いかけている鬼は見失う）。
// 見た目は灰色の球の集まり（仮）。カメラとプレイヤーの間に入った球は隠して、自分の姿が見えなくならないようにする。

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SmokeCloud.generated.h"

class UStaticMesh;
class UStaticMeshComponent;

UCLASS()
class KAKURENBOINCREMENTAL_API ASmokeCloud : public AActor
{
	GENERATED_BODY()

public:
	ASmokeCloud();

	/** 煙の大きさ（半径 cm）。この球の中・向こう側は鬼から見えない */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Smoke")
	float Radius = 260.f;

	/** 煙が残る時間（秒） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Smoke")
	float Duration = 7.f;

	/** 今、煙が視線をさえぎっているか（From → To の線が煙の球を通るか） */
	bool BlocksSight(const FVector& From, const FVector& To) const;

	/** ワールドにある煙幕のどれかが、From → To の視線をさえぎっているか */
	static bool IsSightBlocked(const UWorld* World, const FVector& From, const FVector& To);

	/** 今の煙の半径（広がる途中・消える途中は小さい） */
	float GetCurrentRadius() const;

	/** 煙の真ん中（床から 1m の高さ） */
	FVector GetCloudCenter() const { return GetActorLocation() + FVector(0.f, 0.f, 100.f); }

protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

private:
	UPROPERTY()
	TObjectPtr<UStaticMesh> SphereMesh;

	UPROPERTY()
	TArray<TObjectPtr<UStaticMeshComponent>> Puffs;

	/** 球ごとの、煙の真ん中からの位置・大きさ（cm）・揺れのずれ */
	TArray<FVector> PuffOffsets;
	TArray<float> PuffSizes;
	TArray<float> PuffPhases;

	float Age = 0.f;
};
