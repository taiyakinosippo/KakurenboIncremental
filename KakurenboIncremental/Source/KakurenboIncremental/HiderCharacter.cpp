#include "HiderCharacter.h"

#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "KakurenboLibrary.h"
#include "UObject/ConstructorHelpers.h"

AHiderCharacter::AHiderCharacter()
{
	PrimaryActorTick.bCanEverTick = true;

	// カプセルは 1 マス(100cm)の通路を通れる太さにする
	GetCapsuleComponent()->InitCapsuleSize(35.f, 80.f);

	// マウスの左右で体ごと向きを変える
	bUseControllerRotationYaw = true;
	bUseControllerRotationPitch = false;
	bUseControllerRotationRoll = false;

	UCharacterMovementComponent* Move = GetCharacterMovement();
	Move->MaxWalkSpeed = 450.f;
	Move->JumpZVelocity = 560.f; // ブロック 1 段(100cm)に飛び乗れる高さ
	Move->AirControl = 0.5f;

	// CreateDefaultSubobject: コンストラクタでコンポーネントを作る UE の決まった書き方
	CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
	CameraBoom->SetupAttachment(RootComponent);
	CameraBoom->TargetArmLength = 450.f;
	CameraBoom->SocketOffset = FVector(0.f, 0.f, 80.f);
	CameraBoom->bUsePawnControlRotation = true; // マウスの上下左右でカメラを回す
	CameraBoom->bDoCollisionTest = true;

	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->SetupAttachment(CameraBoom, USpringArmComponent::SocketName);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderFinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	BodyMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BodyMesh"));
	BodyMesh->SetupAttachment(RootComponent);
	BodyMesh->SetStaticMesh(CylinderFinder.Object);
	BodyMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	// 円柱は直径 100cm・高さ 100cm なので、カプセルの大きさに合わせる
	BodyMesh->SetRelativeScale3D(FVector(0.7f, 0.7f, 1.6f));
}

void AHiderCharacter::BeginPlay()
{
	Super::BeginPlay();
	UKakurenboLibrary::ApplyColor(BodyMesh, BodyColor);
	BodyBaseScale = BodyMesh->GetRelativeScale3D();
}

void AHiderCharacter::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (MashPulse > 0.f)
	{
		MashPulse = FMath::Max(0.f, MashPulse - DeltaSeconds * 8.f);
		const float Squash = 1.f - 0.15f * MashPulse;
		BodyMesh->SetRelativeScale3D(FVector(BodyBaseScale.X / Squash, BodyBaseScale.Y / Squash, BodyBaseScale.Z * Squash));
	}
}

void AHiderCharacter::PlayMashFeedback()
{
	MashPulse = 1.f;
}

void AHiderCharacter::GetSightTargetPoints(TArray<FVector>& OutPoints) const
{
	const float HalfHeight = GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	const FVector Center = GetActorLocation();
	OutPoints.Add(Center + FVector(0, 0, HalfHeight * 0.8f)); // 頭
	OutPoints.Add(Center);                                   // 体
	OutPoints.Add(Center - FVector(0, 0, HalfHeight * 0.7f)); // 足
}
