#include "TreasureActor.h"

#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "KakurenboGameMode.h"
#include "KakurenboLibrary.h"
#include "KakurenboSoundSubsystem.h"
#include "UObject/ConstructorHelpers.h"

ATreasureActor::ATreasureActor()
{
	PrimaryActorTick.bCanEverTick = true;

	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));

	Spin = CreateDefaultSubobject<USceneComponent>(TEXT("Spin"));
	Spin->SetupAttachment(RootComponent);
	Spin->SetRelativeLocation(FVector(0.f, 0.f, 60.f));

	// 宝石のメッシュが無いときは、斜めに傾けた立方体を回して宝石っぽく見せる
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeFinder(TEXT("/Engine/BasicShapes/Cube.Cube"));
	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	Mesh->SetupAttachment(Spin);
	Mesh->SetStaticMesh(CubeFinder.Object);
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision); // 取得は距離で判定する
	Mesh->SetRelativeRotation(FRotator(45.f, 0.f, 45.f));
	Mesh->SetRelativeScale3D(FVector(0.35f));

	Glow = CreateDefaultSubobject<UPointLightComponent>(TEXT("Glow"));
	Glow->SetupAttachment(Spin);
	Glow->SetMobility(EComponentMobility::Movable);
	Glow->SetIntensityUnits(ELightUnits::Candelas);
	Glow->SetIntensity(3.f);
	Glow->SetAttenuationRadius(300.f);
	Glow->SetCastShadows(false);
}

void ATreasureActor::BeginPlay()
{
	Super::BeginPlay();
	if (LookMesh)
	{
		// 宝石のメッシュ：一番長い辺を LookSize に合わせ、中心が回る軸に来るように置く（アセットの原点はずれていることがある）
		bGemLook = true;
		Mesh->SetStaticMesh(LookMesh);
		const FBox Bounds = LookMesh->GetBoundingBox();
		const float Scale = LookSize / FMath::Max(Bounds.GetSize().GetMax(), 1.f);
		Mesh->SetRelativeRotation(FRotator::ZeroRotator);
		Mesh->SetRelativeScale3D(FVector(Scale));
		Mesh->SetRelativeLocation(-Bounds.GetCenter() * Scale);
		if (LookMaterial)
		{
			for (int32 i = 0; i < Mesh->GetNumMaterials(); ++i)
			{
				Mesh->SetMaterial(i, LookMaterial);
			}
		}
		else
		{
			UKakurenboLibrary::ApplyColor(Mesh, Color);
		}
	}
	else
	{
		UKakurenboLibrary::ApplyColor(Mesh, Color);
	}
	Glow->SetLightColor(Color);
	BaseSpinLocation = Spin->GetRelativeLocation();
	Time = FMath::FRandRange(0.f, 10.f); // 揺れのタイミングをお宝ごとにずらす
	SparkleTimer = FMath::FRandRange(0.2f, SparkleInterval); // キラキラの音もお宝ごとにずらす
}

void ATreasureActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	// ふわふわ浮かびながら回る
	Time += DeltaSeconds;
	Spin->SetRelativeLocation(BaseSpinLocation + FVector(0.f, 0.f, 10.f * FMath::Sin(Time * 2.5f)));
	Spin->AddRelativeRotation(FRotator(0.f, 90.f * DeltaSeconds, 0.f));

	// ときどきキラキラと鳴る（その場所から聞こえるので、見えなくてもどこにあるかわかる）
	SparkleTimer -= DeltaSeconds;
	if (SparkleTimer <= 0.f)
	{
		SparkleTimer = SparkleInterval * FMath::FRandRange(0.8f, 1.2f);
		++SparkleCount;
		UKakurenboSoundSubsystem::Play3D(this, EKakurenboSfx::TreasureSparkle, Spin->GetComponentLocation(), SparkleVolume, FMath::FRandRange(0.94f, 1.06f));
	}

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
