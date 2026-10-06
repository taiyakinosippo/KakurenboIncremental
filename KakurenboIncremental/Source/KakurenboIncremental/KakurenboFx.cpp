#include "KakurenboFx.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "KakurenboGridSubsystem.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	/** 演出用のメッシュの共通設定（当たり判定なし・影なし） */
	UStaticMeshComponent* CreateFxMesh(AActor* Owner, UStaticMesh* Mesh, UMaterialInterface* Material)
	{
		UStaticMeshComponent* Comp = NewObject<UStaticMeshComponent>(Owner);
		Comp->SetStaticMesh(Mesh);
		Comp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Comp->SetCastShadow(false);
		Comp->SetMobility(EComponentMobility::Movable);
		Comp->SetupAttachment(Owner->GetRootComponent());
		Comp->RegisterComponent();
		if (Material)
		{
			Comp->SetMaterial(0, Material);
		}
		return Comp;
	}

	UMaterialInstanceDynamic* MakeColorMaterial(UObject* Outer, const FLinearColor& Color)
	{
		static TWeakObjectPtr<UMaterialInterface> BaseMaterial;
		if (!BaseMaterial.IsValid())
		{
			BaseMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
		}
		UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(BaseMaterial.Get(), Outer);
		if (MID)
		{
			MID->SetVectorParameterValue(TEXT("Color"), Color);
		}
		return MID;
	}
}

// ---------------------------------------------------------------- 破片

AKakurenboBurstFx::AKakurenboBurstFx()
{
	PrimaryActorTick.bCanEverTick = true;
	SetActorEnableCollision(false);
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeFinder(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereFinder(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	CubeMesh = CubeFinder.Object;
	SphereMesh = SphereFinder.Object;
}

void AKakurenboBurstFx::Start(const FKakurenboBurstParams& Params, float FloorZ)
{
	Lifetime = FMath::Max(0.05f, Params.Lifetime);
	Gravity = Params.Gravity;
	PieceScale = Params.Size / 100.f; // 基本の形は 100cm
	Floor = FloorZ;
	Age = 0.f;

	UMaterialInstanceDynamic* Material = MakeColorMaterial(this, Params.Color);
	UStaticMesh* Mesh = Params.bSpheres ? SphereMesh.Get() : CubeMesh.Get();
	for (int32 i = 0; i < Params.Count; ++i)
	{
		UStaticMeshComponent* Piece = CreateFxMesh(this, Mesh, Material);
		Piece->SetWorldScale3D(FVector(PieceScale));
		Piece->SetWorldLocation(GetActorLocation() + FMath::VRand() * Params.Size * 0.5f);
		Pieces.Add(Piece);

		// 横方向はランダム、上方向は UpBias で強める
		FVector Dir = FMath::VRand();
		Dir.Z = FMath::Lerp(FMath::Abs(Dir.Z), 1.f, Params.UpBias);
		Velocities.Add(Dir.GetSafeNormal() * Params.Speed * FMath::FRandRange(0.6f, 1.2f));
		Spins.Add(FRotator(FMath::FRandRange(-360.f, 360.f), FMath::FRandRange(-360.f, 360.f), FMath::FRandRange(-360.f, 360.f)));
	}
}

void AKakurenboBurstFx::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	Age += DeltaSeconds;
	if (Age >= Lifetime)
	{
		Destroy();
		return;
	}
	// 最後の 4 割で小さくなって消える
	const float T = Age / Lifetime;
	const float Shrink = T < 0.6f ? 1.f : 1.f - (T - 0.6f) / 0.4f;

	for (int32 i = 0; i < Pieces.Num(); ++i)
	{
		UStaticMeshComponent* Piece = Pieces[i];
		if (!Piece)
		{
			continue;
		}
		FVector& V = Velocities[i];
		V.Z -= Gravity * DeltaSeconds;
		FVector P = Piece->GetComponentLocation() + V * DeltaSeconds;

		// 床で跳ねて、だんだん止まる
		const float Half = PieceScale * 50.f * Shrink;
		if (P.Z < Floor + Half)
		{
			P.Z = Floor + Half;
			if (V.Z < 0.f)
			{
				V.Z *= -0.35f;
				V.X *= 0.6f;
				V.Y *= 0.6f;
			}
		}
		Piece->SetWorldLocationAndRotation(P, Piece->GetComponentRotation() + Spins[i] * DeltaSeconds);
		Piece->SetWorldScale3D(FVector(FMath::Max(0.001f, PieceScale * Shrink)));
	}
}

// ---------------------------------------------------------------- 輪

AKakurenboRingFx::AKakurenboRingFx()
{
	PrimaryActorTick.bCanEverTick = true;
	SetActorEnableCollision(false);
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeFinder(TEXT("/Engine/BasicShapes/Cube.Cube"));
	CubeMesh = CubeFinder.Object;
}

void AKakurenboRingFx::Start(float InStartRadius, float InEndRadius, float InDuration, const FLinearColor& Color, float InThickness)
{
	StartRadius = InStartRadius;
	EndRadius = FMath::Max(InEndRadius, 1.f);
	Duration = FMath::Max(0.05f, InDuration);
	Thickness = InThickness;
	Age = 0.f;
	bBusy = true;

	if (!Material)
	{
		Material = MakeColorMaterial(this, Color);
	}
	else
	{
		Material->SetVectorParameterValue(TEXT("Color"), Color);
	}

	// 輪の大きさに合わせて板の数を決める（50cm に 1 枚くらい。多すぎると重いので上限 64）
	ActiveSegments = FMath::Clamp(FMath::RoundToInt(2.f * UE_PI * FMath::Max(StartRadius, EndRadius) / 50.f), 12, 64);
	while (Segments.Num() < ActiveSegments)
	{
		Segments.Add(CreateFxMesh(this, CubeMesh, Material));
	}
	for (int32 i = 0; i < Segments.Num(); ++i)
	{
		Segments[i]->SetVisibility(i < ActiveSegments);
	}
	SetActorHiddenInGame(false);
	SetActorTickEnabled(true);
	UpdateSegments(StartRadius, 1.f);
}

void AKakurenboRingFx::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!bBusy)
	{
		return;
	}
	Age += DeltaSeconds;
	const float T = FMath::Clamp(Age / Duration, 0.f, 1.f);
	if (Age >= Duration)
	{
		// 隠して、次に使われるまで待つ
		bBusy = false;
		SetActorHiddenInGame(true);
		SetActorTickEnabled(false);
		return;
	}
	// 速く広がって、ゆっくり止まる。細くなって消える
	const float Ease = 1.f - FMath::Square(1.f - T);
	UpdateSegments(FMath::Lerp(StartRadius, EndRadius, Ease), 1.f - T);
}

void AKakurenboRingFx::UpdateSegments(float Radius, float Fade)
{
	const FVector Center = GetActorLocation();
	const float Step = 2.f * UE_PI / ActiveSegments;
	const float Length = FMath::Max(1.f, Radius * Step * 0.7f); // 板と板の間を少し空ける（点線の輪）
	const float Width = FMath::Max(0.5f, Thickness * FMath::Sqrt(FMath::Max(Fade, 0.f)));
	for (int32 i = 0; i < ActiveSegments; ++i)
	{
		const float Angle = Step * i;
		const FVector Offset(FMath::Cos(Angle) * Radius, FMath::Sin(Angle) * Radius, 0.f);
		// 板の長い辺（X）を円の接線の向きにする
		const FRotator Rot(0.f, FMath::RadiansToDegrees(Angle) + 90.f, 0.f);
		Segments[i]->SetWorldLocationAndRotation(Center + Offset, Rot);
		Segments[i]->SetWorldScale3D(FVector(Length / 100.f, Width / 100.f, 0.03f));
	}
}

// ---------------------------------------------------------------- 窓口

float UKakurenboFxSubsystem::GetFloorZ() const
{
	const UKakurenboGridSubsystem* Grid = GetWorld()->GetSubsystem<UKakurenboGridSubsystem>();
	return (Grid && Grid->IsConfigured()) ? Grid->CellFloorCenter(FIntPoint(0, 0)).Z : 0.f;
}

void UKakurenboFxSubsystem::Burst(const FVector& Location, const FKakurenboBurstParams& Params)
{
	UWorld* World = GetWorld();
	if (!World || Params.Count <= 0)
	{
		return;
	}
	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	if (AKakurenboBurstFx* Fx = World->SpawnActor<AKakurenboBurstFx>(AKakurenboBurstFx::StaticClass(), Location, FRotator::ZeroRotator, SpawnParams))
	{
		Fx->Start(Params, GetFloorZ());
		++BurstCount;
	}
}

void UKakurenboFxSubsystem::Ring(const FVector& FloorLocation, float StartRadius, float EndRadius, float Duration, const FLinearColor& Color, float Thickness)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	// 使い終わった輪があれば使い回す（連打のたびに作り直さない）
	AKakurenboRingFx* Ring = nullptr;
	for (AKakurenboRingFx* Pooled : RingPool)
	{
		if (Pooled && !Pooled->IsBusy())
		{
			Ring = Pooled;
			break;
		}
	}
	if (!Ring)
	{
		FActorSpawnParameters SpawnParams;
		SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Ring = World->SpawnActor<AKakurenboRingFx>(AKakurenboRingFx::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams);
		if (!Ring)
		{
			return;
		}
		RingPool.Add(Ring);
	}
	Ring->SetActorLocation(FVector(FloorLocation.X, FloorLocation.Y, GetFloorZ() + 2.f));
	Ring->Start(StartRadius, EndRadius, Duration, Color, Thickness);
	++RingCount;
}
