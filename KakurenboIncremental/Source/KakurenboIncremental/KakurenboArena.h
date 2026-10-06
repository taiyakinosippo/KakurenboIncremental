// かくれんぼの舞台（床・外周の壁・ライト）を C++ だけで組み立てるアクター。
// レベルに何も置いていなくても遊べるよう、GameMode が自動でスポーンする。

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "KakurenboArena.generated.h"

class UStaticMesh;
class UStaticMeshComponent;

UCLASS()
class KAKURENBOINCREMENTAL_API AKakurenboArena : public AActor
{
	GENERATED_BODY()

public:
	AKakurenboArena();

	/** グリッドのマス数（X 方向） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Arena")
	int32 GridSizeX = 24;

	/** グリッドのマス数（Y 方向） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Arena")
	int32 GridSizeY = 24;

	/** 1 マスの大きさ（cm）。ブロック 1 個の大きさでもある */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Arena")
	float CellSize = 100.f;

	/** 外周の壁の高さ（cm）。壊せない */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Arena")
	float BorderHeight = 250.f;

	/** レベルにライトが無い場合、太陽・空・環境光を自動で作る */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Arena")
	bool bSpawnLightingIfMissing = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Arena")
	FLinearColor FloorColor = FLinearColor(0.35f, 0.4f, 0.35f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Arena")
	FLinearColor BorderColor = FLinearColor(0.2f, 0.2f, 0.22f);

	/** グリッド (0,0) マスの角（最小 XY、床の上面の高さ） */
	FVector GetGridOrigin() const;

protected:
	virtual void BeginPlay() override;

private:
	UStaticMeshComponent* AddCube(const FVector& Center, const FVector& SizeCm, const FLinearColor& Color);
	void SpawnLighting();

	UPROPERTY()
	TObjectPtr<UStaticMesh> CubeMesh;
};
