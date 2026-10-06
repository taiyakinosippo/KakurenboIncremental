#include "TrapActor.h"

#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "KakurenboGameMode.h"
#include "KakurenboLibrary.h"
#include "OniCharacter.h"
#include "UObject/ConstructorHelpers.h"

ATrapActor::ATrapActor()
{
	PrimaryActorTick.bCanEverTick = true;
	SetActorEnableCollision(false); // プレイヤーも鬼も上を通れる

	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderFinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereFinder(TEXT("/Engine/BasicShapes/Sphere.Sphere"));

	BaseMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BaseMesh"));
	BaseMesh->SetupAttachment(RootComponent);
	BaseMesh->SetStaticMesh(CylinderFinder.Object);
	BaseMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	TopMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("TopMesh"));
	TopMesh->SetupAttachment(RootComponent);
	TopMesh->SetStaticMesh(SphereFinder.Object);
	TopMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
}

void ATrapActor::InitTrap(int32 InTrapTypeIndex, const FKakurenboTrapRow& InDef)
{
	TrapTypeIndex = InTrapTypeIndex;
	Def = InDef;

	// 基本の形（円柱・球）は 100cm。マスの床の上に置く
	if (Def.Kind == ETrapKind::Sticky)
	{
		// 床に広がった平たいもち
		BaseScale = FVector(0.7f, 0.7f, 0.04f);
		TopScale = FVector(0.35f, 0.35f, 0.1f);
		BaseMesh->SetRelativeLocation(FVector(0.f, 0.f, 2.f));
		TopMesh->SetRelativeLocation(FVector(0.f, 0.f, 4.f));
		UKakurenboLibrary::ApplyColor(BaseMesh, Def.Color);
		UKakurenboLibrary::ApplyColor(TopMesh, Def.Color * 0.75f);
	}
	else
	{
		// 小さな人形（胴体と頭）
		BaseScale = FVector(0.35f, 0.35f, 0.3f);
		TopScale = FVector(0.3f);
		BaseMesh->SetRelativeLocation(FVector(0.f, 0.f, 15.f));
		TopMesh->SetRelativeLocation(FVector(0.f, 0.f, 42.f));
		UKakurenboLibrary::ApplyColor(BaseMesh, Def.Color);
		UKakurenboLibrary::ApplyColor(TopMesh, FMath::Lerp(Def.Color, FLinearColor::White, 0.4f));
	}
	BaseMesh->SetRelativeScale3D(BaseScale);
	TopMesh->SetRelativeScale3D(TopScale);
	AnimTime = FMath::FRandRange(0.f, 5.f);
}

bool ATrapActor::IsArmed() const
{
	const AKakurenboGameMode* GM = GetWorld()->GetAuthGameMode<AKakurenboGameMode>();
	return GM && GM->GetPhase() == EKakurenboPhase::Hide && GM->GetOnis().Num() > 0;
}

bool ATrapActor::IsOniOnTrap(const AOniCharacter* Oni) const
{
	if (!Oni)
	{
		return false;
	}
	// 体の中心が罠の近くにあり、床の上にいる（壁の上などではない）
	const FVector OniLocation = Oni->GetActorLocation();
	const float FeetZ = OniLocation.Z - Oni->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	return FVector::Dist2D(OniLocation, GetActorLocation()) <= Def.TriggerRadius
		&& FMath::Abs(FeetZ - GetActorLocation().Z) < 60.f;
}

void ATrapActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	TickAnimation(DeltaSeconds);

	if (bTriggered || !IsArmed())
	{
		return;
	}
	if (Def.Kind == ETrapKind::Sticky)
	{
		TickSticky();
	}
	else
	{
		TickDecoy(DeltaSeconds);
	}
}

void ATrapActor::TickSticky()
{
	AKakurenboGameMode* GM = GetWorld()->GetAuthGameMode<AKakurenboGameMode>();
	for (AOniCharacter* Oni : GM->GetOnis())
	{
		if (!Oni || !IsOniOnTrap(Oni))
		{
			continue;
		}
		// 動けなくなった直後の鬼には効かない（続けて踏んでも動けないままにならないように）
		if (Oni->CanBeStunned())
		{
			bTriggered = true;
			GM->HandleTrapTriggered(this, Oni); // ここで罠は消える
			return;
		}
		// パワー鬼は、抜け出した直後に踏んだトリモチを壊してしまう
		if (Oni->CanDisarmTraps())
		{
			bTriggered = true;
			GM->HandleTrapDisarmed(this, Oni); // ここで罠は消える
			return;
		}
	}
}

void ATrapActor::TickDecoy(float DeltaSeconds)
{
	AKakurenboGameMode* GM = GetWorld()->GetAuthGameMode<AKakurenboGameMode>();

	// 鬼が触れたら壊れる
	for (AOniCharacter* Oni : GM->GetOnis())
	{
		if (Oni && Oni->IsActive() && Oni->GetOniState() != EOniState::Stunned && IsOniOnTrap(Oni))
		{
			bTriggered = true;
			GM->HandleTrapTriggered(this, Oni); // ここで罠は消える
			return;
		}
	}

	// 一定の間隔で音を出す
	NoiseTimer -= DeltaSeconds;
	if (NoiseTimer <= 0.f)
	{
		NoiseTimer = FMath::Max(0.2f, Def.NoiseInterval);
		++PingCount;
		PingPulse = 1.f;
		GM->HandleDecoyPing(this);
	}
}

void ATrapActor::TickAnimation(float DeltaSeconds)
{
	AnimTime += DeltaSeconds;
	PingPulse = FMath::Max(0.f, PingPulse - DeltaSeconds * 3.f);
	if (Def.Kind == ETrapKind::Sticky)
	{
		// もちがゆっくり膨らんだり縮んだりする
		const float Breath = 1.f + 0.08f * FMath::Sin(AnimTime * 2.f);
		TopMesh->SetRelativeScale3D(FVector(TopScale.X * Breath, TopScale.Y * Breath, TopScale.Z / Breath));
	}
	else
	{
		// 音を出すたびにぴょんと跳ねる
		const float Jump = 18.f * FMath::Sin(PingPulse * UE_PI);
		const float Swell = 1.f + 0.3f * PingPulse;
		BaseMesh->SetRelativeLocation(FVector(0.f, 0.f, 15.f + Jump));
		TopMesh->SetRelativeLocation(FVector(0.f, 0.f, 42.f + Jump));
		TopMesh->SetRelativeScale3D(TopScale * Swell);
	}
}
