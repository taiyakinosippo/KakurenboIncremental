#include "SmokeCloud.h"

#include "Camera/PlayerCameraManager.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "KakurenboLibrary.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
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

	// 半透明の煙のマテリアル（Tools/ImportSourceArt.ps1 が作る）。無いパソコンでは灰色の不透明な球
	UMaterialInterface* SmokeMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Kakurenbo/Materials/M_KakuSmoke.M_KakuSmoke"), nullptr, LOAD_NoWarn | LOAD_Quiet);
	bTranslucent = SmokeMaterial != nullptr;

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
		const FLinearColor Color(Shade, Shade, Shade * 1.05f);
		if (bTranslucent)
		{
			UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(SmokeMaterial, Puff);
			MID->SetVectorParameterValue(TEXT("Color"), Color);
			MID->SetScalarParameterValue(TEXT("Opacity"), PuffOpacity);
			Puff->SetMaterial(0, MID);
			PuffMaterials.Add(MID);
		}
		else
		{
			UKakurenboLibrary::ApplyColor(Puff, Color);
			PuffMaterials.Add(nullptr);
		}
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
	// 消えかけの煙は向こうが透けて見える（半径の 9 割で判定）。残る時間を過ぎた煙（見た目だけ薄く残っている）はさえぎらない
	if (Age >= Duration)
	{
		return false;
	}
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

int32 ASmokeCloud::GetVisiblePuffCount() const
{
	int32 Count = 0;
	for (int32 i = 0; i < Puffs.Num(); ++i)
	{
		if (Puffs[i] && Puffs[i]->IsVisible() && PuffVisibleOpacity.IsValidIndex(i) && PuffVisibleOpacity[i] > 0.05f)
		{
			++Count;
		}
	}
	return Count;
}

void ASmokeCloud::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	Age += DeltaSeconds;
	// 半透明の煙は、残る時間（鬼の目をさえぎる時間）を過ぎてもしばらく薄く漂ってから消える（短い煙幕でも投げたのがわかるように）
	const float Lifetime = Duration + (bTranslucent ? LingerSeconds : 0.f);
	if (Age >= Lifetime)
	{
		Destroy();
		return;
	}

	// カメラとプレイヤーの位置（カメラのすぐ近く・カメラとプレイヤーの間の煙は薄くする）
	FVector CameraLocation = FVector::ZeroVector;
	FVector PlayerLocation = FVector::ZeroVector;
	bool bHasView = false;
	if (const APlayerController* PC = GetWorld()->GetFirstPlayerController(); PC && PC->PlayerCameraManager && PC->GetPawn())
	{
		CameraLocation = PC->PlayerCameraManager->GetCameraLocation();
		PlayerLocation = PC->GetPawn()->GetActorLocation();
		bHasView = true;
	}

	// 大きさ：半透明なら広がったらそのまま（消えるときは薄くなる）、不透明なら小さくなって消える
	const float Grow = FMath::Min(GrowSeconds, Duration * 0.2f);
	const float GrowT = FMath::Clamp(Age / FMath::Max(Grow, 0.01f), 0.f, 1.f);
	const float Scale = bTranslucent ? (1.f - FMath::Square(1.f - GrowT)) * (1.f + 0.08f * FMath::Max(0.f, Age - Grow)) : GetCurrentRadius() / FMath::Max(Radius, 1.f);
	// 濃さ：残る時間の最後の少し前から薄くなり、漂い終わるときに消える
	const float FadeStart = FMath::Max(Grow, Duration - FMath::Min(0.4f, Duration * 0.3f));
	const float Fade = Age <= FadeStart ? 1.f : FMath::Clamp(1.f - (Age - FadeStart) / FMath::Max(Lifetime - FadeStart, 0.01f), 0.f, 1.f);

	const FVector Center = GetCloudCenter();
	PuffVisibleOpacity.SetNum(Puffs.Num());
	for (int32 i = 0; i < Puffs.Num(); ++i)
	{
		// ゆっくり上へ漂いながら、ふわふわ揺れる
		const float Wobble = FMath::Sin(Age * 1.7f + PuffPhases[i]);
		const FVector PuffCenter = Center + PuffOffsets[i] * Scale + FVector(Wobble * 12.f, FMath::Cos(Age * 1.3f + PuffPhases[i]) * 12.f, Age * 14.f);
		const float Size = FMath::Max(1.f, PuffSizes[i] * Scale * (1.f + 0.05f * Wobble));
		Puffs[i]->SetWorldLocation(PuffCenter);
		Puffs[i]->SetWorldScale3D(FVector(Size / 50.f)); // 球は直径 100cm（半径 50）

		const float ToCamera = bHasView ? FVector::Dist(PuffCenter, CameraLocation) : 1e6f;
		const bool bBetween = bHasView && DistanceToSegment(PuffCenter, CameraLocation, PlayerLocation) < Size * 0.8f;
		if (bTranslucent)
		{
			// カメラが煙の中・すぐそばなら消す（画面が灰色で埋まらないように）。カメラとプレイヤーの間の煙は薄くする（隠さない）
			const float NearCamera = FMath::Clamp((ToCamera - Size - 40.f) / 220.f, 0.f, 1.f);
			const float Opacity = PuffOpacity * Fade * NearCamera * (bBetween ? BetweenOpacityScale : 1.f);
			PuffVisibleOpacity[i] = Opacity;
			if (UMaterialInstanceDynamic* MID = PuffMaterials[i])
			{
				MID->SetScalarParameterValue(TEXT("Opacity"), Opacity);
			}
			Puffs[i]->SetVisibility(Opacity > 0.01f);
		}
		else
		{
			// 不透明な球：カメラのすぐ近くの球だけ隠す（画面いっぱいに映って前が見えなくなる）
			const bool bVisible = ToCamera > Size + 150.f;
			PuffVisibleOpacity[i] = bVisible ? 1.f : 0.f;
			Puffs[i]->SetVisibility(bVisible);
		}
	}
}
