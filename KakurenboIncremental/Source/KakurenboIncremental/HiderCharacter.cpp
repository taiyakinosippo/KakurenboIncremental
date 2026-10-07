#include "HiderCharacter.h"

#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Controller.h"
#include "GameFramework/SpringArmComponent.h"
#include "KakurenboGameMode.h"
#include "KakurenboLibrary.h"
#include "UObject/ConstructorHelpers.h"

AHiderCharacter::AHiderCharacter()
{
	PrimaryActorTick.bCanEverTick = true;

	// カプセルは 1 マス(100cm)の通路を通れる太さ。背の高さ 160cm（ブロック 1 段と同じ）
	GetCapsuleComponent()->InitCapsuleSize(35.f, 80.f);

	// 体は歩く方向を向く（カメラの向きとは独立）
	bUseControllerRotationYaw = false;
	bUseControllerRotationPitch = false;
	bUseControllerRotationRoll = false;

	UCharacterMovementComponent* Move = GetCharacterMovement();
	Move->bOrientRotationToMovement = true;
	Move->RotationRate = FRotator(0.f, 720.f, 0.f);
	Move->MaxWalkSpeed = 420.f;
	Move->JumpZVelocity = 650.f; // ブロック 1 段（160cm）に飛び乗れる高さ
	Move->AirControl = 0.5f;

	// ジャンプは最初はできない（転生のお店で解放すると GameMode が 1 にする）
	JumpMaxCount = 0;

	// CreateDefaultSubobject: コンストラクタでコンポーネントを作る UE の決まった書き方
	CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
	CameraBoom->SetupAttachment(RootComponent);
	CameraBoom->SetUsingAbsoluteRotation(true); // 向きはワールド固定（Tick で設定する）
	CameraBoom->bUsePawnControlRotation = false;
	CameraBoom->TargetArmLength = ThirdPersonDistance;
	CameraBoom->ProbeChannel = ECC_Camera;      // カメラ用の当たり判定（置いたブロックは無視する）
	CameraBoom->bEnableCameraLag = true;        // 追従をなめらかにする
	CameraBoom->CameraLagSpeed = 12.f;
	CameraBoom->CameraRotationLagSpeed = 10.f;

	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->SetupAttachment(CameraBoom, USpringArmComponent::SocketName);
	Camera->bUsePawnControlRotation = false;

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
	OverheadYaw = GetActorRotation().Yaw;
	WalkSpeed = GetCharacterMovement()->MaxWalkSpeed;
	TopDownFocus = GetActorLocation();
	LastStepLocation = GetActorLocation();
	SetViewMode(ViewMode);
}

void AHiderCharacter::SetViewMode(EHiderViewMode NewMode)
{
	ViewMode = NewMode;
	const bool bThirdPerson = (NewMode == EHiderViewMode::ThirdPerson);
	const bool bTopDown = (NewMode == EHiderViewMode::TopDown);

	// 俯瞰・真上はなめらかに回す。三人称はマウスにすぐ追従させる
	CameraBoom->bEnableCameraRotationLag = !bThirdPerson;
	// 三人称は舞台の外周の壁に当たったらカメラを手前に寄せる（外へ出て壁の裏しか見えなくなるのを防ぐ）。
	// 置いたブロックはカメラを通す設定なので、囲まれていても壁越しに見える。俯瞰は外周の上から見下ろすので当たらない
	CameraBoom->bDoCollisionTest = bThirdPerson;
	// 三人称は注視点を少し上げて、壁越しに見渡しやすくする
	CameraBoom->TargetOffset = bThirdPerson ? FVector(0.f, 0.f, ThirdPersonLookHeight) : FVector::ZeroVector;

	// 真上からのときは、アームの根元をプレイヤーから切り離して自由に動かす（SetUsingAbsoluteLocation: 親の位置に付いていかない）。
	// プレイヤーの体は表示しない（当たり判定は残るので、立っている場所には壁を置けない）
	CameraBoom->SetUsingAbsoluteLocation(bTopDown);
	if (bTopDown)
	{
		CameraBoom->SetWorldLocation(TopDownFocus);
	}
	else
	{
		CameraBoom->SetRelativeLocation(FVector::ZeroVector);
	}
	SetActorHiddenInGame(bTopDown);
}

float AHiderCharacter::GetViewYaw() const
{
	if (ViewMode != EHiderViewMode::ThirdPerson)
	{
		return OverheadYaw;
	}
	return Controller ? Controller->GetControlRotation().Yaw : GetActorRotation().Yaw;
}

void AHiderCharacter::SetOverheadYaw(float Yaw)
{
	OverheadYaw = FRotator::NormalizeAxis(Yaw);
}

void AHiderCharacter::AddOverheadYaw(float DeltaDegrees)
{
	SetOverheadYaw(OverheadYaw + DeltaDegrees);
}

void AHiderCharacter::AddZoom(float DeltaCm)
{
	if (ViewMode == EHiderViewMode::Overhead)
	{
		OverheadDistance = FMath::Clamp(OverheadDistance + DeltaCm, OverheadMinDistance, OverheadMaxDistance);
	}
	else if (ViewMode == EHiderViewMode::TopDown)
	{
		TopDownDistance = FMath::Clamp(TopDownDistance + DeltaCm * 1.5f, TopDownMinDistance, TopDownMaxDistance);
	}
	else
	{
		ThirdPersonDistance = FMath::Clamp(ThirdPersonDistance + DeltaCm * 0.5f, ThirdPersonMinDistance, ThirdPersonMaxDistance);
	}
}

void AHiderCharacter::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	// ダッシュの時間とクールタイム
	if (DashTimeRemaining > 0.f)
	{
		DashTimeRemaining -= DeltaSeconds;
		if (DashTimeRemaining <= 0.f)
		{
			DashTimeRemaining = 0.f;
			UpdateWalkSpeed();
		}
	}
	DashCooldownRemaining = FMath::Max(0.f, DashCooldownRemaining - DeltaSeconds);

	// 足音：地面を歩いた距離が 1 歩ぶんになるたびに鳴る（ダッシュ中は歩幅が広い）
	const FVector Loc = GetActorLocation();
	const float Moved = FVector::Dist2D(Loc, LastStepLocation);
	LastStepLocation = Loc;
	if (Moved < 200.f && GetCharacterMovement()->IsMovingOnGround() && GetVelocity().Size2D() > 50.f)
	{
		StepDistance += Moved;
		if (StepDistance >= StepStride * (IsDashing() ? 1.4f : 1.f))
		{
			StepDistance = 0.f;
			NotifyStep();
		}
	}

	// アームの向きと長さを今の視点に合わせる
	if (ViewMode == EHiderViewMode::Overhead)
	{
		CameraBoom->SetWorldRotation(FRotator(OverheadPitch, OverheadYaw, 0.f));
		CameraBoom->TargetArmLength = FMath::FInterpTo(CameraBoom->TargetArmLength, OverheadDistance, DeltaSeconds, 8.f);
	}
	else if (ViewMode == EHiderViewMode::TopDown)
	{
		// 真上から（ピッチ -90）。画面の上が OverheadYaw の方向になる
		CameraBoom->SetWorldLocation(TopDownFocus);
		CameraBoom->SetWorldRotation(FRotator(-90.f, OverheadYaw, 0.f));
		CameraBoom->TargetArmLength = FMath::FInterpTo(CameraBoom->TargetArmLength, TopDownDistance, DeltaSeconds, 8.f);
	}
	else
	{
		// 三人称：マウスで動かした向き（コントローラーの向き）から見る
		const FRotator ControlRot = Controller ? Controller->GetControlRotation() : GetActorRotation();
		CameraBoom->SetWorldRotation(FRotator(ControlRot.Pitch, ControlRot.Yaw, 0.f));
		CameraBoom->TargetArmLength = FMath::FInterpTo(CameraBoom->TargetArmLength, ThirdPersonDistance, DeltaSeconds, 8.f);
	}

	// 外周の壁際でカメラが体のすぐ後ろまで寄ったときは、自分の体で前が見えなくならないよう体を映さない
	const float CameraDistance = FVector::Dist(Camera->GetComponentLocation(), GetActorLocation());
	BodyMesh->SetOwnerNoSee(ViewMode == EHiderViewMode::ThirdPerson && CameraDistance < 150.f);

	// 連打の反応は体を縮めるだけ（カメラは動かさない）
	MashPulse = FMath::Max(0.f, MashPulse - DeltaSeconds * 8.f);
	const float Squash = 1.f - 0.15f * MashPulse;
	BodyMesh->SetRelativeScale3D(FVector(BodyBaseScale.X / Squash, BodyBaseScale.Y / Squash, BodyBaseScale.Z * Squash));
}

void AHiderCharacter::PlayMashFeedback()
{
	MashPulse = 1.f;
}

bool AHiderCharacter::TryStartDash()
{
	if (!bDashUnlocked || DashCooldownRemaining > 0.f)
	{
		return false;
	}
	DashTimeRemaining = DashDuration;
	DashCooldownRemaining = DashCooldown;
	UpdateWalkSpeed();
	return true;
}

void AHiderCharacter::ResetDash()
{
	DashTimeRemaining = 0.f;
	DashCooldownRemaining = 0.f;
	UpdateWalkSpeed();
}

void AHiderCharacter::SetSneaking(bool bInSneaking)
{
	if (bSneaking != bInSneaking)
	{
		bSneaking = bInSneaking;
		UpdateWalkSpeed();
	}
}

void AHiderCharacter::UpdateWalkSpeed()
{
	// ダッシュ中はダッシュの速さ、しのび足はゆっくり
	GetCharacterMovement()->MaxWalkSpeed = IsDashing() ? WalkSpeed * DashSpeedMultiplier
		: bSneaking ? WalkSpeed * SneakSpeedMultiplier
		: WalkSpeed;
}

void AHiderCharacter::NotifyStep()
{
	++StepCount;
	if (AKakurenboGameMode* GM = GetWorld()->GetAuthGameMode<AKakurenboGameMode>())
	{
		const FVector Feet = GetActorLocation() - FVector(0.f, 0.f, GetSimpleCollisionHalfHeight());
		GM->HandlePlayerStep(Feet, bSneaking && !IsDashing());
	}
}

void AHiderCharacter::OnJumped_Implementation()
{
	Super::OnJumped_Implementation();
	if (AKakurenboGameMode* GM = GetWorld()->GetAuthGameMode<AKakurenboGameMode>())
	{
		GM->HandlePlayerJump(GetActorLocation() - FVector(0.f, 0.f, GetSimpleCollisionHalfHeight()));
	}
}

void AHiderCharacter::Landed(const FHitResult& Hit)
{
	Super::Landed(Hit);
	StepDistance = 0.f;
	NotifyStep(); // 着地も 1 歩
}

void AHiderCharacter::GetSightTargetPoints(TArray<FVector>& OutPoints) const
{
	const float HalfHeight = GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	const FVector Center = GetActorLocation();
	OutPoints.Add(Center + FVector(0, 0, HalfHeight * 0.8f)); // 頭
	OutPoints.Add(Center);                                   // 体
	OutPoints.Add(Center - FVector(0, 0, HalfHeight * 0.7f)); // 足
}
