// 仮の演出（パーティクルの代わり）。エンジン付属の立方体・球を飛び散らせたり、床に輪を広げたりする。
// Niagara のアセットを作ったら置き換えてよい。演出の呼び出しは UKakurenboFxSubsystem に集めている。

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Subsystems/WorldSubsystem.h"
#include "KakurenboFx.generated.h"

class UMaterialInstanceDynamic;
class UStaticMesh;
class UStaticMeshComponent;

/** 飛び散る破片の設定 */
struct FKakurenboBurstParams
{
	FLinearColor Color = FLinearColor::White;
	int32 Count = 10;        // 破片の数
	float Size = 15.f;       // 破片 1 個の大きさ（cm）
	float Speed = 400.f;     // 飛び散る速さ（cm/秒）
	float UpBias = 0.5f;     // 上向きの強さ（0: 横に広がる 〜 1: 上に吹き上がる）
	float Gravity = 980.f;   // 重力（cm/秒^2）
	float Lifetime = 0.8f;   // 消えるまでの時間（秒）
	bool bSpheres = false;   // true なら球、false なら立方体
};

/** 破片が飛び散って消える演出（1 回使い切り） */
UCLASS(NotBlueprintable)
class KAKURENBOINCREMENTAL_API AKakurenboBurstFx : public AActor
{
	GENERATED_BODY()

public:
	AKakurenboBurstFx();

	/** 破片を作って飛ばし始める。FloorZ より下には落ちない（床で跳ねる） */
	void Start(const FKakurenboBurstParams& Params, float FloorZ);

protected:
	virtual void Tick(float DeltaSeconds) override;

private:
	UPROPERTY()
	TObjectPtr<UStaticMesh> CubeMesh;

	UPROPERTY()
	TObjectPtr<UStaticMesh> SphereMesh;

	UPROPERTY()
	TArray<TObjectPtr<UStaticMeshComponent>> Pieces;

	TArray<FVector> Velocities;
	TArray<FRotator> Spins;
	float Age = 0.f;
	float Lifetime = 1.f;
	float Gravity = 980.f;
	float PieceScale = 0.15f;
	float Floor = 0.f;
};

/** 床に広がる輪（音の届く範囲・衝撃波）。使い終わったら隠して次に使い回す */
UCLASS(NotBlueprintable)
class KAKURENBOINCREMENTAL_API AKakurenboRingFx : public AActor
{
	GENERATED_BODY()

public:
	AKakurenboRingFx();

	/** 半径 StartRadius から EndRadius へ Duration 秒かけて広がりながら細くなって消える */
	void Start(float InStartRadius, float InEndRadius, float InDuration, const FLinearColor& Color, float InThickness);

	bool IsBusy() const { return bBusy; }

protected:
	virtual void Tick(float DeltaSeconds) override;

private:
	void UpdateSegments(float Radius, float Fade);

	UPROPERTY()
	TObjectPtr<UStaticMesh> CubeMesh;

	/** 輪を作る細長い板（輪の大きさに応じて使う数を変える） */
	UPROPERTY()
	TArray<TObjectPtr<UStaticMeshComponent>> Segments;

	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> Material;

	int32 ActiveSegments = 0;
	float Age = 0.f;
	float Duration = 0.3f;
	float StartRadius = 0.f;
	float EndRadius = 100.f;
	float Thickness = 6.f;
	bool bBusy = false;
};

/** 演出の窓口（ワールドに 1 つ） */
UCLASS()
class KAKURENBOINCREMENTAL_API UKakurenboFxSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	/** 破片を飛び散らせる */
	void Burst(const FVector& Location, const FKakurenboBurstParams& Params);

	/** 床に輪を広げる（FloorLocation の高さは無視して床の上に出す） */
	void Ring(const FVector& FloorLocation, float StartRadius, float EndRadius, float Duration, const FLinearColor& Color, float Thickness = 6.f);

	/** 出した演出の数（テスト用） */
	int32 GetBurstCount() const { return BurstCount; }
	int32 GetRingCount() const { return RingCount; }

private:
	float GetFloorZ() const;

	UPROPERTY()
	TArray<TObjectPtr<AKakurenboRingFx>> RingPool;

	int32 BurstCount = 0;
	int32 RingCount = 0;
};
