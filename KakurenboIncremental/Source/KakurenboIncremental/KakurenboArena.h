// かくれんぼの舞台（床・外周の壁・ライト）を C++ だけで組み立てるアクター。
// レベルに何も置いていなくても遊べるよう、GameMode が自動でスポーンする。

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "KakurenboArena.generated.h"

class UPostProcessComponent;
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

	/** 舞台の外側の地面の色 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Arena")
	FLinearColor OuterGroundColor = FLinearColor(0.12f, 0.16f, 0.12f);

	/**
	 * 明るさの自動補正の下限（EV100）。これより暗い場所は無理に明るくしない。
	 * 壁で囲まれた中（実測 EV -4.6、手元の明かりありで -1.6）が青白く飛んで見えるのを防ぎ、
	 * 「暗い場所に隠れている」見た目にする。屋外は実測 EV 1.3 なので影響しない
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Arena|Look")
	float MinExposureEV100 = 0.f;

	/** モーションブラーを切る（視点をすばやく動かしたときに画面がぶれないように） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Arena|Look")
	bool bDisableMotionBlur = true;

	/** 画面全体に効く見た目の設定 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Arena|Look")
	TObjectPtr<UPostProcessComponent> PostProcess;

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
