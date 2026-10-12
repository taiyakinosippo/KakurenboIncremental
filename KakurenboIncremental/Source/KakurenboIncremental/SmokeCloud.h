// 煙幕。煙幕ダッシュ（Shift）でプレイヤーの足元に投げる。
// しばらくの間、煙の中・煙の向こう側は鬼から見えない（追いかけている鬼は見失う）。
// 見た目は半透明の灰色の球の集まり（M_KakuSmoke。無いパソコンでは不透明な球）。
// カメラのすぐ近くの球は消し、カメラとプレイヤーの間の球は薄くして、自分の姿と行く先が見えるようにする。
// 残る時間を過ぎても、見た目だけしばらく薄く漂う（鬼の目はもうさえぎらない）。

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SmokeCloud.generated.h"

class UMaterialInstanceDynamic;
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

	/** 煙が残る時間（秒）。この間は鬼の目をさえぎる */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Smoke")
	float Duration = 7.f;

	/** 残る時間を過ぎてから、見た目だけ薄く漂う時間（秒。半透明のときだけ） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Smoke")
	float LingerSeconds = 1.2f;

	/** 球の濃さ（0〜1）と、カメラとプレイヤーの間の球の濃さの倍率 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Smoke")
	float PuffOpacity = 0.7f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Smoke")
	float BetweenOpacityScale = 0.35f;

	/** 今、煙が視線をさえぎっているか（From → To の線が煙の球を通るか） */
	bool BlocksSight(const FVector& From, const FVector& To) const;

	/** ワールドにある煙幕のどれかが、From → To の視線をさえぎっているか */
	static bool IsSightBlocked(const UWorld* World, const FVector& From, const FVector& To);

	/** 今の煙の半径（広がる途中・消える途中は小さい） */
	float GetCurrentRadius() const;

	/** 煙の真ん中（床から 1m の高さ） */
	FVector GetCloudCenter() const { return GetActorLocation() + FVector(0.f, 0.f, 100.f); }

	/** 画面に見えている球の数（テスト用） */
	int32 GetVisiblePuffCount() const;

	/** 半透明のマテリアルを使っているか（テスト用） */
	bool IsTranslucent() const { return bTranslucent; }

protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

private:
	UPROPERTY()
	TObjectPtr<UStaticMesh> SphereMesh;

	UPROPERTY()
	TArray<TObjectPtr<UStaticMeshComponent>> Puffs;

	/** 球ごとの半透明のマテリアル（不透明なら null） */
	UPROPERTY()
	TArray<TObjectPtr<UMaterialInstanceDynamic>> PuffMaterials;

	/** 球ごとの、煙の真ん中からの位置・大きさ（cm）・揺れのずれ・今の濃さ */
	TArray<FVector> PuffOffsets;
	TArray<float> PuffSizes;
	TArray<float> PuffPhases;
	TArray<float> PuffVisibleOpacity;

	float Age = 0.f;
	bool bTranslucent = false;
};
