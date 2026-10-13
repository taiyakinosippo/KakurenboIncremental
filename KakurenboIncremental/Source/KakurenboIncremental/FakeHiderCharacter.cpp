#include "FakeHiderCharacter.h"

#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "KakurenboGameMode.h"
#include "KakurenboSoundSubsystem.h"
#include "OniCharacter.h"
#include "TrapActor.h"

AFakeHiderCharacter::AFakeHiderCharacter()
{
	bIsDecoyDoll = true;
	// コントローラーが無くても AddMovementInput で歩けるようにする
	GetCharacterMovement()->bRunPhysicsWithNoController = true;
	AutoPossessAI = EAutoPossessAI::Disabled;
	AutoPossessPlayer = EAutoReceiveInput::Disabled;
}

void AFakeHiderCharacter::BeginPlay()
{
	// 歩く速さ＝逃げる速さ（親の BeginPlay がこの速さを「歩く速さ」として覚える）
	GetCharacterMovement()->MaxWalkSpeed = FleeSpeed;
	Super::BeginPlay();
	SetViewMode(EHiderViewMode::ThirdPerson); // 体はいつも見せる（カメラは使わない）
	CameraBoom->bDoCollisionTest = false;
}

void AFakeHiderCharacter::ReturnHome()
{
	bFleeing = false;
	CalmTimer = 0.f;
	if (FVector::DistSquared(GetActorLocation(), HomeLocation) > 25.f)
	{
		GetCharacterMovement()->StopMovementImmediately();
		SetActorLocation(HomeLocation, false, nullptr, ETeleportType::TeleportPhysics);
	}
}

FVector AFakeHiderCharacter::ChooseFleeDirection(const FVector& Away) const
{
	// 鬼から離れる向きに近い順に、前がふさがっていない向きを探す（壁・家具・置いた壁）
	const FVector Start = GetActorLocation();
	FCollisionQueryParams Params(SCENE_QUERY_STAT(KakuFakeFlee), false, this);
	FVector Best = Away;
	float BestScore = -TNumericLimits<float>::Max();
	for (int32 i = 0; i < 16; ++i)
	{
		const FVector Dir = FRotator(0.f, i * 22.5f, 0.f).RotateVector(FVector::ForwardVector);
		const float Facing = FVector::DotProduct(Dir, Away);
		if (Facing < -0.2f)
		{
			continue; // 鬼の方へは逃げない
		}
		FHitResult Hit;
		const bool bBlocked = GetWorld()->SweepSingleByChannel(Hit, Start, Start + Dir * 160.f, FQuat::Identity, ECC_Pawn, FCollisionShape::MakeSphere(30.f), Params)
			&& !Cast<APawn>(Hit.GetActor());
		const float Score = Facing + (bBlocked ? -10.f : 0.f);
		if (Score > BestScore)
		{
			BestScore = Score;
			Best = Dir;
		}
	}
	return Best;
}

void AFakeHiderCharacter::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	AKakurenboGameMode* GM = GetWorld()->GetAuthGameMode<AKakurenboGameMode>();
	if (!GM || GM->GetPhase() != EKakurenboPhase::Hide)
	{
		return;
	}

	// 自分を追いかけている鬼（一番近いもの）
	const AOniCharacter* Chaser = nullptr;
	for (const AOniCharacter* Oni : GM->GetOnis())
	{
		if (Oni && Oni->IsActive() && Oni->GetIntent() == EOniState::Chase && Oni->GetChaseTarget() == this
			&& (!Chaser || FVector::DistSquared2D(Oni->GetActorLocation(), GetActorLocation()) < FVector::DistSquared2D(Chaser->GetActorLocation(), GetActorLocation())))
		{
			Chaser = Oni;
		}
	}

	if (Chaser)
	{
		CalmTimer = 0.f;
		const FVector Away = (GetActorLocation() - Chaser->GetActorLocation()).GetSafeNormal2D();
		if (!bFleeing)
		{
			// 見つかった：鬼と反対の方へ、まっすぐ逃げ始める
			bFleeing = true;
			FleeDir = ChooseFleeDirection(Away);
			NoiseTimer = 0.f;
			StuckTimer = 0.f;
		}
		// 壁や家具にぶつかって止まったら、別の向きへ（なるべく鬼から離れる向き）
		StuckTimer = GetVelocity().Size2D() < 60.f ? StuckTimer + DeltaSeconds : 0.f;
		if (StuckTimer > 0.25f)
		{
			StuckTimer = 0.f;
			FleeDir = ChooseFleeDirection(Away);
		}
	}
	else if (bFleeing)
	{
		CalmTimer += DeltaSeconds;
		if (CalmTimer >= CalmDownSeconds)
		{
			bFleeing = false; // 追われなくなったら止まる
		}
	}

	if (!bFleeing)
	{
		return;
	}
	AddMovementInput(FleeDir);

	// 逃げながら大きな音を立てる（ドタバタ。周りの鬼も寄ってくる）
	NoiseTimer -= DeltaSeconds;
	if (NoiseTimer <= 0.f)
	{
		NoiseTimer = GM->FakeHiderNoiseInterval;
		++NoiseCount;
		if (ATrapActor* Trap = OwnerTrap.Get())
		{
			GM->HandleGadgetNoise(Trap, GM->FakeHiderNoiseLoudness, EKakurenboNoise::Dash);
		}
		const FVector Feet = GetActorLocation() - FVector(0.f, 0.f, GetSimpleCollisionHalfHeight());
		UKakurenboSoundSubsystem::Play3D(this, EKakurenboSfx::OniStep, Feet, 1.3f, 1.6f);
	}
}
