#include "TreasureActor.h"

#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "KakurenboGameMode.h"
#include "KakurenboLibrary.h"
#include "UObject/ConstructorHelpers.h"

ATreasureActor::ATreasureActor()
{
	PrimaryActorTick.bCanEverTick = true;

	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));

	// 斜めに傾けた立方体を回して、宝石っぽく見せる
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeFinder(TEXT("/Engine/BasicShapes/Cube.Cube"));
	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	Mesh->SetupAttachment(RootComponent);
	Mesh->SetStaticMesh(CubeFinder.Object);
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision); // 取得は距離で判定する
	Mesh->SetRelativeLocation(FVector(0.f, 0.f, 60.f));
	Mesh->SetRelativeRotation(FRotator(45.f, 0.f, 45.f));
	Mesh->SetRelativeScale3D(FVector(0.35f));

	Glow = CreateDefaultSubobject<UPointLightComponent>(TEXT("Glow"));
	Glow->SetupAttachment(Mesh);
	Glow->SetMobility(EComponentMobility::Movable);
	Glow->SetIntensityUnits(ELightUnits::Candelas);
	Glow->SetIntensity(3.f);
	Glow->SetAttenuationRadius(300.f);
	Glow->SetCastShadows(false);
}

void ATreasureActor::BeginPlay()
{
	Super::BeginPlay();
	UKakurenboLibrary::ApplyColor(Mesh, Color);
	Glow->SetLightColor(Color);
	BaseMeshLocation = Mesh->GetRelativeLocation();
	Time = FMath::FRandRange(0.f, 10.f); // 揺れのタイミングをお宝ごとにずらす
}

void ATreasureActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	// ふわふわ浮かびながら回る
	Time += DeltaSeconds;
	Mesh->SetRelativeLocation(BaseMeshLocation + FVector(0.f, 0.f, 10.f * FMath::Sin(Time * 2.5f)));
	Mesh->AddRelativeRotation(FRotator(0.f, 90.f * DeltaSeconds, 0.f));

	// プレイヤーが近づいたら取得
	const APlayerController* PC = GetWorld()->GetFirstPlayerController();
	const APawn* Pawn = PC ? PC->GetPawn() : nullptr;
	if (!Pawn)
	{
		return;
	}
	const FVector Delta = Pawn->GetActorLocation() - GetActorLocation();
	if (Delta.Size2D() <= PickupRadius && FMath::Abs(Delta.Z) <= 200.f)
	{
		if (AKakurenboGameMode* GM = GetWorld()->GetAuthGameMode<AKakurenboGameMode>())
		{
			GM->CollectTreasure(this);
		}
	}
}
