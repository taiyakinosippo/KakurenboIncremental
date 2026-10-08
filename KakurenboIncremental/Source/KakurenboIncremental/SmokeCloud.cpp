#include "SmokeCloud.h"

#include "Camera/PlayerCameraManager.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "KakurenboLibrary.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	constexpr int32 NumPuffs = 14;
	constexpr float GrowSeconds = 0.35f; // ぼふっと広がる時間（短い煙幕では残る時間の 2 割まで）
	constexpr float FadeSeconds = 1.2f;  // 最後に小さくなって消える時間（短い煙幕では残る時間の 3 割まで）

	/** 点から線分までの距離 */
	float DistanceToSegment(const FVector& P, const FVector& A, const FVector& B)
	{
		return FMath::PointDistToSegment(P, A, B);
	}
}

ASmokeCloud::ASmokeCloud()
{
	PrimaryActorTick.bCanEverTick = true;
	SetActorEnableCollision(false); // 通るのは邪魔しない（目だけをさえぎる）
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));

	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereFinder(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	SphereMesh = SphereFinder.Object;
}

void ASmokeCloud::BeginPlay()
{
	Super::BeginPlay();

	// 灰色の球を、煙の真ん中のまわりにばらまく（下は広く、上は少し細く）
	for (int32 i = 0; i < NumPuffs; ++i)
	{
		UStaticMeshComponent* Puff = NewObject<UStaticMeshComponent>(this);
		Puff->SetStaticMesh(SphereMesh);
		Puff->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Puff->SetCastShadow(false);
		Puff->SetMobility(EComponentMobility::Movable);
		Puff->SetupAttachment(RootComponent);
		Puff->RegisterComponent();
		const float Shade = FMath::FRandRange(0.5f, 0.68f);
		UKakurenboLibrary::ApplyColor(Puff, FLinearColor(Shade, Shade, Shade * 1.05f));
		Puffs.Add(Puff);

		const FVector2D Flat = FMath::RandPointInCircle(Radius * 0.55f);
		PuffOffsets.Add(FVector(Flat.X, Flat.Y, FMath::FRandRange(-70.f, 90.f)));
		PuffSizes.Add(FMath::FRandRange(Radius * 0.55f, Radius * 0.85f));
		PuffPhases.Add(FMath::FRandRange(0.f, 2.f * UE_PI));
	}
}

float ASmokeCloud::GetCurrentRadius() const
{
	const float Grow = FMath::Min(GrowSeconds, Duration * 0.2f);
	const float Fade = FMath::Min(FadeSeconds, Duration * 0.3f);
	if (Age < Grow)
	{
		const float T = Age / FMath::Max(Grow, 0.01f);
		return Radius * (1.f - FMath::Square(1.f - T));
	}
	const float Left = Duration - Age;
	if (Left < Fade)
	{
		return Radius * FMath::Max(0.f, Left / FMath::Max(Fade, 0.01f));
	}
	return Radius;
}

bool ASmokeCloud::BlocksSight(const FVector& From, const FVector& To) const
{
	// 消えかけの煙は向こうが透けて見える（半径の 9 割で判定）
	const float R = GetCurrentRadius() * 0.9f;
	return R > 30.f && DistanceToSegment(GetCloudCenter(), From, To) <= R;
}

bool ASmokeCloud::IsSightBlocked(const UWorld* World, const FVector& From, const FVector& To)
{
	if (!World)
	{
		return false;
	}
	// TActorIterator: ワールドにある、このクラスのアクターを順に見る
	for (TActorIterator<ASmokeCloud> It(const_cast<UWorld*>(World)); It; ++It)
	{
		if (It->BlocksSight(From, To))
		{
			return true;
		}
	}
	return false;
}

void ASmokeCloud::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	Age += DeltaSeconds;
	if (Age >= Duration)
	{
		Destroy();
		return;
	}

	// カメラとプレイヤーの間に入った球は隠す（煙越しでも自分の姿と行く先が見えるように）
	FVector CameraLocation = FVector::ZeroVector;
	FVector PlayerLocation = FVector::ZeroVector;
	bool bHasView = false;
	if (const APlayerController* PC = GetWorld()->GetFirstPlayerController(); PC && PC->PlayerCameraManager && PC->GetPawn())
	{
		CameraLocation = PC->PlayerCameraManager->GetCameraLocation();
		PlayerLocation = PC->GetPawn()->GetActorLocation();
		bHasView = true;
	}

	const float Scale = GetCurrentRadius() / FMath::Max(Radius, 1.f);
	const FVector Center = GetCloudCenter();
	for (int32 i = 0; i < Puffs.Num(); ++i)
	{
		// ゆっくり上へ漂いながら、ふわふわ揺れる
		const float Wobble = FMath::Sin(Age * 1.7f + PuffPhases[i]);
		const FVector PuffCenter = Center + PuffOffsets[i] * Scale + FVector(Wobble * 12.f, FMath::Cos(Age * 1.3f + PuffPhases[i]) * 12.f, Age * 14.f);
		const float Size = FMath::Max(1.f, PuffSizes[i] * Scale * (1.f + 0.05f * Wobble));
		Puffs[i]->SetWorldLocation(PuffCenter);
		Puffs[i]->SetWorldScale3D(FVector(Size / 50.f)); // 球は直径 100cm（半径 50）

		// カメラとプレイヤーの間にある球と、カメラのすぐ近くの球（画面いっぱいに映って前が見えなくなる）を隠す
		const bool bBlocksView = bHasView
			&& (DistanceToSegment(PuffCenter, CameraLocation, PlayerLocation) < Size + 40.f || FVector::Dist(PuffCenter, CameraLocation) < Size + 200.f);
		Puffs[i]->SetVisibility(!bBlocksView);
	}
}
