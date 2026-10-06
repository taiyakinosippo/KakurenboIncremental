#include "HiderCharacter.h"

#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/PointLightComponent.h"
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
	BaseEyeHeight = EyeHeight;

	UCharacterMovementComponent* Move = GetCharacterMovement();
	Move->MaxWalkSpeed = 450.f;
	Move->JumpZVelocity = 560.f; // ブロック 1 段(100cm)に飛び乗れる高さ
	Move->AirControl = 0.5f;
	Move->RotationRate = FRotator(0.f, 720.f, 0.f);

	// CreateDefaultSubobject: コンストラクタでコンポーネントを作る UE の決まった書き方

	// ---- 俯瞰カメラ ----
	CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
	CameraBoom->SetupAttachment(RootComponent);
	CameraBoom->SetUsingAbsoluteRotation(true); // 向きはワールド固定（Tick で設定する）
	CameraBoom->bUsePawnControlRotation = false;
	CameraBoom->TargetArmLength = OverheadDistance;
	CameraBoom->bDoCollisionTest = false;       // 壁に遮られてもカメラを押し込まない
	CameraBoom->bEnableCameraLag = true;        // 追従をなめらかにする
	CameraBoom->CameraLagSpeed = 10.f;
	CameraBoom->bEnableCameraRotationLag = true;
	CameraBoom->CameraRotationLagSpeed = 10.f;

	OverheadCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("OverheadCamera"));
	OverheadCamera->SetupAttachment(CameraBoom, USpringArmComponent::SocketName);
	OverheadCamera->bUsePawnControlRotation = false;
	OverheadCamera->SetAutoActivate(false);

	// ---- 一人称カメラ ----
	FirstPersonCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FirstPersonCamera"));
	FirstPersonCamera->SetupAttachment(RootComponent);
	FirstPersonCamera->SetRelativeLocation(FVector(0.f, 0.f, EyeHeight));
	FirstPersonCamera->bUsePawnControlRotation = true; // マウスの向き＝視線
	FirstPersonCamera->SetAutoActivate(true);          // 最初のラウンドはかくれんぼから始まる

	// ---- 見た目 ----
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderFinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	BodyMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BodyMesh"));
	BodyMesh->SetupAttachment(RootComponent);
	BodyMesh->SetStaticMesh(CylinderFinder.Object);
	BodyMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	// 円柱は直径 100cm・高さ 100cm なので、カプセルの大きさに合わせる
	BodyMesh->SetRelativeScale3D(FVector(0.7f, 0.7f, 1.6f));

	// ---- 一人称用の明かり（屋外では日光よりずっと弱いので目立たない） ----
	HideLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("HideLight"));
	HideLight->SetupAttachment(RootComponent);
	HideLight->SetRelativeLocation(FVector(0.f, 0.f, EyeHeight + 20.f));
	HideLight->SetMobility(EComponentMobility::Movable);
	HideLight->SetIntensityUnits(ELightUnits::Candelas);
	HideLight->SetIntensity(0.4f);
	HideLight->SetAttenuationRadius(400.f);
	HideLight->SetLightColor(FLinearColor(1.f, 0.88f, 0.72f));
	HideLight->SetCastShadows(false);

	// 初期状態は一人称（ViewMode の初期値と合わせる）
	BodyMesh->SetOwnerNoSee(true);
	bUseControllerRotationYaw = true;
	bUseControllerRotationPitch = false;
	bUseControllerRotationRoll = false;
	Move->bOrientRotationToMovement = false;
}

void AHiderCharacter::BeginPlay()
{
	Super::BeginPlay();
	UKakurenboLibrary::ApplyColor(BodyMesh, BodyColor);
	BodyBaseScale = BodyMesh->GetRelativeScale3D();
	BaseFOV = FirstPersonCamera->FieldOfView;
	OverheadYaw = GetActorRotation().Yaw;
}

void AHiderCharacter::SetViewMode(EHiderViewMode NewMode)
{
	ViewMode = NewMode;
	const bool bFirstPerson = (NewMode == EHiderViewMode::FirstPerson);

	// 有効なカメラコンポーネントが 1 つだけになるようにする（有効なものが視点になる）
	FirstPersonCamera->SetActive(bFirstPerson);
	OverheadCamera->SetActive(!bFirstPerson);

	// 一人称では自分の体を自分のカメラに映さず、身の回りを明かりで照らす
	BodyMesh->SetOwnerNoSee(bFirstPerson);
	HideLight->SetVisibility(bFirstPerson);

	// 一人称: 体はマウスの向きに合わせて回る
	// 俯瞰  : 体は歩く方向を向く（カメラの向きとは独立）
	bUseControllerRotationYaw = bFirstPerson;
	GetCharacterMovement()->bOrientRotationToMovement = !bFirstPerson;
}

void AHiderCharacter::SetOverheadYaw(float Yaw)
{
	OverheadYaw = FRotator::NormalizeAxis(Yaw);
}

void AHiderCharacter::AddOverheadYaw(float DeltaDegrees)
{
	SetOverheadYaw(OverheadYaw + DeltaDegrees);
}

void AHiderCharacter::AddOverheadZoom(float DeltaCm)
{
	OverheadDistance = FMath::Clamp(OverheadDistance + DeltaCm, OverheadMinDistance, OverheadMaxDistance);
}

void AHiderCharacter::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	// 俯瞰カメラ：決まった角度で斜め上から見下ろす（一人称の間も更新しておくと切り替えが自然）
	CameraBoom->SetWorldRotation(FRotator(OverheadPitch, OverheadYaw, 0.f));
	CameraBoom->TargetArmLength = FMath::FInterpTo(CameraBoom->TargetArmLength, OverheadDistance, DeltaSeconds, 8.f);

	// 連打の反応
	MashPulse = FMath::Max(0.f, MashPulse - DeltaSeconds * 8.f);
	const float Squash = 1.f - 0.15f * MashPulse;
	BodyMesh->SetRelativeScale3D(FVector(BodyBaseScale.X / Squash, BodyBaseScale.Y / Squash, BodyBaseScale.Z * Squash));
	FirstPersonCamera->SetRelativeLocation(FVector(0.f, 0.f, EyeHeight - 5.f * MashPulse));
	FirstPersonCamera->SetFieldOfView(BaseFOV + 2.f * MashPulse);
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
