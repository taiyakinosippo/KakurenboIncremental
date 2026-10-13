#include "TreasureActor.h"

#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
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
	Glow->SetIntensity(12.f); // M15：館の暖かい明かりに埋もれないよう明るく（3 → 12）
	Glow->SetAttenuationRadius(420.f);
	Glow->SetCastShadows(false);

	// 足元の光る輪（薄い円柱。宝石の色で光る）
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderFinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereFinder(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	GroundGlow = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("GroundGlow"));
	GroundGlow->SetupAttachment(RootComponent);
	GroundGlow->SetStaticMesh(CylinderFinder.Object);
	GroundGlow->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	GroundGlow->SetCastShadow(false);
	GroundGlow->SetRelativeLocation(FVector(0.f, 0.f, 1.f));
	GroundGlow->SetRelativeScale3D(FVector(0.8f, 0.8f, 0.01f));

	// 宝石のまわりを回る白い光の粒（キラッ）
	for (int32 i = 0; i < 3; ++i)
	{
		UStaticMeshComponent* Twinkle = CreateDefaultSubobject<UStaticMeshComponent>(*FString::Printf(TEXT("Twinkle%d"), i));
		Twinkle->SetupAttachment(Spin);
		Twinkle->SetStaticMesh(SphereFinder.Object);
		Twinkle->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Twinkle->SetCastShadow(false);
		Twinkle->SetRelativeScale3D(FVector(0.06f));
		Twinkles.Add(Twinkle);
	}
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
	// 宝石は自分で光る（明滅させるので、自分のマテリアルの写しを作る）
	GemMID = Cast<UMaterialInstanceDynamic>(Mesh->GetMaterial(0));
	if (!GemMID && Mesh->GetMaterial(0))
	{
		GemMID = Mesh->CreateDynamicMaterialInstance(0);
	}
	if (GemMID)
	{
		for (int32 i = 1; i < Mesh->GetNumMaterials(); ++i)
		{
			Mesh->SetMaterial(i, GemMID);
		}
	}
	// 足元の輪と光の粒は、宝石の色・白で光らせる（館の暗さ・明かりの色に関係なく見える）
	GroundMID = UKakurenboLibrary::CreateGlowMaterial(this, FLinearColor::LerpUsingHSV(Color, FLinearColor::White, 0.15f), 2.f);
	if (GroundMID)
	{
		GroundGlow->SetMaterial(0, GroundMID);
	}
	for (UStaticMeshComponent* Twinkle : Twinkles)
	{
		if (UMaterialInstanceDynamic* MID = UKakurenboLibrary::CreateGlowMaterial(this, FLinearColor(1.f, 1.f, 0.95f), 6.f))
		{
			Twinkle->SetMaterial(0, MID);
		}
	}
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

	// 明滅（キラッと強く光る瞬間がある）・足元の輪がゆっくり脈打つ・光の粒が回る
	const float Pulse = 0.5f + 0.5f * FMath::Sin(Time * 3.f);
	const float Flash = FMath::Pow(FMath::Max(0.f, FMath::Sin(Time * 1.7f)), 16.f); // ときどき一瞬だけ強く
	if (GemMID)
	{
		GemMID->SetScalarParameterValue(TEXT("Emissive"), FMath::Lerp(GemGlowMin, GemGlowMax, FMath::Max(Pulse * 0.6f, Flash)));
	}
	if (GroundMID)
	{
		GroundMID->SetScalarParameterValue(TEXT("Emissive"), 1.2f + 1.3f * Pulse);
	}
	GroundGlow->SetRelativeScale3D(FVector(0.7f + 0.15f * Pulse, 0.7f + 0.15f * Pulse, 0.01f));
	Glow->SetIntensity(9.f + 6.f * Pulse + 10.f * Flash);
	for (int32 i = 0; i < Twinkles.Num(); ++i)
	{
		const float Angle = Time * 2.2f + i * 2.f * UE_PI / Twinkles.Num();
		const float Radius = LookSize * 0.75f;
		Twinkles[i]->SetRelativeLocation(FVector(FMath::Cos(Angle) * Radius, FMath::Sin(Angle) * Radius, 12.f * FMath::Sin(Angle * 2.f + Time)));
		const float Twinkle = 0.04f + 0.05f * FMath::Max(0.f, FMath::Sin(Time * 6.f + i * 1.7f));
		Twinkles[i]->SetRelativeScale3D(FVector(Twinkle));
	}

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
