#include "HiderCharacter.h"

#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Animation/AnimationAsset.h"
#include "Components/PoseableMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Controller.h"
#include "GameFramework/SpringArmComponent.h"
#include "Engine/World.h"
#include "KakurenboGameMode.h"
#include "KakurenboLibrary.h"
#include "PlaceableBlock.h"
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
	// 壁の上から歩いて下りると、ほぼ真下に落ちる（勢いで隙間の先の壁へ飛び移れないように。ジャンプも無い）。
	// 空中では歩く入力を受けない（PlayerController）ので、横の勢いはすぐ止まる
	Move->AirControl = 0.f;
	Move->FallingLateralFriction = 4.f;
	Move->BrakingDecelerationFalling = 1800.f;

	// しのび足のときはしゃがむ（体が半分の高さになり、低い家具の陰に隠れられる）
	Move->NavAgentProps.bCanCrouch = true;
	Move->SetCrouchedHalfHeight(CrouchedHalfHeight);
	Move->bCanWalkOffLedgesWhenCrouching = true;

	// ジャンプは無い（M14。代わりによじ登る：転生のお店で解放）
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
	StandingBodyScale = BodyBaseScale;
	GetCharacterMovement()->SetCrouchedHalfHeight(CrouchedHalfHeight);
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
	// 三人称のアームの長さは、入るたびに今の長さから測り直す
	ZoomArmLength = -1.f;
	CollisionArmLength = -1.f;
}

float AHiderCharacter::GetCameraArmLength() const
{
	return CameraBoom ? CameraBoom->TargetArmLength : 0.f;
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

void AHiderCharacter::AddTopDownTilt(float DeltaDegrees)
{
	TopDownPitch = FMath::Clamp(TopDownPitch + DeltaDegrees, -90.f, TopDownMaxTiltPitch);
}

void AHiderCharacter::SetTitleCamera(bool bTitle)
{
	if (bTitle == bTitleCamera)
	{
		return;
	}
	bTitleCamera = bTitle;
	if (bTitle)
	{
		SavedOverheadPitch = OverheadPitch;
		SavedOverheadDistance = OverheadDistance;
		OverheadPitch = TitlePitch;
		OverheadDistance = TitleDistance;
	}
	else
	{
		OverheadPitch = SavedOverheadPitch;
		OverheadDistance = SavedOverheadDistance;
	}
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

	if (bClimbing)
	{
		TickClimb(DeltaSeconds);
	}

	// ダッシュの時間とクールタイム
	if (DashTimeRemaining > 0.f)
	{
		DashTimeRemaining -= DeltaSeconds;
		if (DashTimeRemaining <= 0.f)
		{
			DashTimeRemaining = 0.f;
			UpdateWalkSpeed();
			UpdateCrouch();
		}
	}

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
		// 真上から（ピッチ -90。傾けると斜め上から）。画面の上が OverheadYaw の方向になる
		CameraBoom->SetWorldLocation(TopDownFocus);
		CameraBoom->SetWorldRotation(FRotator(TopDownPitch, OverheadYaw, 0.f));
		CameraBoom->TargetArmLength = FMath::FInterpTo(CameraBoom->TargetArmLength, TopDownDistance, DeltaSeconds, 8.f);
	}
	else
	{
		// 三人称：マウスで動かした向き（コントローラーの向き）から見る
		const FRotator ControlRot = Controller ? Controller->GetControlRotation() : GetActorRotation();
		const FRotator ArmRot(ControlRot.Pitch, ControlRot.Yaw, 0.f);
		CameraBoom->SetWorldRotation(ArmRot);
		if (ZoomArmLength < 0.f)
		{
			ZoomArmLength = CameraBoom->TargetArmLength;
		}
		ZoomArmLength = FMath::FInterpTo(ZoomArmLength, ThirdPersonDistance, DeltaSeconds, 8.f);
		// M14：壁（部屋の壁・外周の壁・出入り口の上の壁）に当たらない長さを自分で測る。当たったらすぐ寄せ、離れるときはゆっくり戻す。
		// （アームの当たり判定だけに任せると、壁ぎわ・廊下では毎フレーム寄ったり離れたりして「グワングワン」揺れた。アームの当たり判定は念のため残す）
		const FVector ArmOrigin = CameraBoom->GetComponentLocation() + CameraBoom->TargetOffset;
		float FreeLength = ZoomArmLength;
		FHitResult Hit;
		const FCollisionQueryParams Params(SCENE_QUERY_STAT(HiderCameraArm), false, this);
		if (GetWorld()->SweepSingleByChannel(Hit, ArmOrigin, ArmOrigin - ArmRot.Vector() * ZoomArmLength, FQuat::Identity, CameraBoom->ProbeChannel,
			FCollisionShape::MakeSphere(CameraBoom->ProbeSize), Params))
		{
			FreeLength = FMath::Max(static_cast<float>(Hit.Distance), 0.f);
		}
		// フレームが引っかかって（シェーダーの準備など）1 フレームが長くても一気に戻らないよう、戻すときの時間は 1/20 秒までにする
		CollisionArmLength = (CollisionArmLength < 0.f || FreeLength < CollisionArmLength) ? FreeLength
			: FMath::FInterpTo(CollisionArmLength, FreeLength, FMath::Min(DeltaSeconds, 0.05f), CameraReturnSpeed);
		CameraBoom->TargetArmLength = CollisionArmLength;
	}

	// 外周の壁際でカメラが体のすぐ後ろまで寄ったときは、自分の体で前が見えなくならないよう体を映さない
	const float CameraDistance = FVector::Dist(Camera->GetComponentLocation(), GetActorLocation());
	BodyMesh->SetOwnerNoSee(ViewMode == EHiderViewMode::ThirdPerson && CameraDistance < 150.f);
	if (LookMesh)
	{
		LookMesh->SetOwnerNoSee(ViewMode == EHiderViewMode::ThirdPerson && CameraDistance < 150.f);
	}

	// 連打の反応は体を縮めるだけ（カメラは動かさない）
	MashPulse = FMath::Max(0.f, MashPulse - DeltaSeconds * 8.f);
	const float Squash = 1.f - 0.15f * MashPulse;
	BodyMesh->SetRelativeScale3D(FVector(BodyBaseScale.X / Squash, BodyBaseScale.Y / Squash, BodyBaseScale.Z * Squash));
	if (LookMesh)
	{
		LookMesh->SetRelativeScale3D(FVector(LookBaseScale.X / Squash, LookBaseScale.Y / Squash, LookBaseScale.Z * Squash));
		UpdateLookPose(DeltaSeconds);
	}
}

void AHiderCharacter::PlayMashFeedback()
{
	MashPulse = 1.f;
}

bool AHiderCharacter::TryStartDash()
{
	// 回数制（クールタイムは無い）。ダッシュ中・よじ登っている途中にもう一度押しても使わない
	if (!bDashUnlocked || DashUsesLeft <= 0 || IsDashing() || bClimbing)
	{
		return false;
	}
	--DashUsesLeft;
	DashTimeRemaining = DashDuration;
	UpdateWalkSpeed();
	UpdateCrouch(); // 煙幕ダッシュは立って走る
	return true;
}

void AHiderCharacter::ResetDash()
{
	DashTimeRemaining = 0.f;
	DashUsesLeft = bDashUnlocked ? FMath::Max(0, DashUsesPerRound) : 0;
	UpdateWalkSpeed();
}

void AHiderCharacter::SetSneaking(bool bInSneaking)
{
	if (bSneaking != bInSneaking)
	{
		bSneaking = bInSneaking;
		UpdateWalkSpeed();
		UpdateCrouch();
	}
}

void AHiderCharacter::UpdateCrouch()
{
	if (bSneaking && !IsDashing())
	{
		Crouch();
	}
	else
	{
		UnCrouch(); // 頭の上に何かあって立てないときは、しゃがんだまま（エンジンが判定する）
	}
}

void AHiderCharacter::OnStartCrouch(float HalfHeightAdjust, float ScaledHalfHeightAdjust)
{
	Super::OnStartCrouch(HalfHeightAdjust, ScaledHalfHeightAdjust);
	// カプセルが低くなり体の中心が下がるので、見た目を上げて足を床に置いたままにする
	CrouchMeshOffset = ScaledHalfHeightAdjust;
	const float Ratio = GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight() / FMath::Max(GetDefaultHalfHeight(), 1.f);
	BodyBaseScale = FVector(StandingBodyScale.X, StandingBodyScale.Y, StandingBodyScale.Z * Ratio);
	if (!LookMesh)
	{
		BodyMesh->SetRelativeLocation(FVector::ZeroVector);
	}
}

void AHiderCharacter::OnEndCrouch(float HalfHeightAdjust, float ScaledHalfHeightAdjust)
{
	Super::OnEndCrouch(HalfHeightAdjust, ScaledHalfHeightAdjust);
	CrouchMeshOffset = 0.f;
	BodyBaseScale = StandingBodyScale;
}

void AHiderCharacter::UpdateWalkSpeed()
{
	// ダッシュ中はダッシュの速さ、しのび足はゆっくり
	GetCharacterMovement()->MaxWalkSpeed = IsDashing() ? WalkSpeed * DashSpeedMultiplier
		: bSneaking ? WalkSpeed * SneakSpeedMultiplier
		: WalkSpeed;
	GetCharacterMovement()->MaxWalkSpeedCrouched = GetCharacterMovement()->MaxWalkSpeed; // しゃがんでいるときも同じ速さ
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

// ---------------------------------------------------------------- よじ登る

bool AHiderCharacter::FindClimbTarget(const FVector& InDir, FVector& OutStandLocation, float& OutSeconds) const
{
	const FVector Dir = InDir.GetSafeNormal2D();
	const UWorld* World = GetWorld();
	if (Dir.IsNearlyZero() || !World)
	{
		return false;
	}
	const UCapsuleComponent* Capsule = GetCapsuleComponent();
	const float Radius = Capsule->GetScaledCapsuleRadius();
	const float StandHalfHeight = GetDefaultHalfHeight(); // しゃがんでいても、上には立って乗る
	const FVector Loc = GetActorLocation();
	const float FeetZ = Loc.Z - Capsule->GetScaledCapsuleHalfHeight();

	FCollisionQueryParams Params(SCENE_QUERY_STAT(HiderClimb), false, this);
	// 1) 前に壁・家具の面があるか（ひざの高さで前へ）
	FHitResult Face;
	const FVector KneeStart(Loc.X, Loc.Y, FeetZ + 30.f);
	if (!World->LineTraceSingleByChannel(Face, KneeStart, KneeStart + Dir * (Radius + ClimbReach), ECC_Visibility, Params)
		|| Cast<ACharacter>(Face.GetActor()))
	{
		return false; // 何も無い・鬼
	}
	// 面に向かってまっすぐ登る（斜めから押しても、角の近くで体がはみ出さないように）
	const FVector FaceNormal = Face.ImpactNormal.GetSafeNormal2D();
	const FVector ClimbDir = FaceNormal.IsNearlyZero() ? Dir : -FaceNormal;
	// 2) その上面の高さ（面の 30cm 奥を上から測る）。高すぎ（2 段以上・部屋の壁）・低すぎ（じゅうたん）は登れない。
	//    2 段に積んだ壁の上面より上から測る（中から測ると下の段の上面が見えてしまう）
	FHitResult Top;
	const FVector Probe = Face.ImpactPoint + ClimbDir * 30.f;
	const FVector TopStart(Probe.X, Probe.Y, FeetZ + ClimbMaxHeight + 300.f);
	if (!World->LineTraceSingleByChannel(Top, TopStart, FVector(Probe.X, Probe.Y, FeetZ - 5.f), ECC_Visibility, Params)
		|| Top.ImpactNormal.Z < 0.7f)
	{
		return false;
	}
	const float Rise = Top.ImpactPoint.Z - FeetZ;
	if (Rise < ClimbMinHeight || Rise > ClimbMaxHeight)
	{
		return false;
	}
	// 3) 上に立つ場所（縁から体 1 つぶん奥）に体が入るか・上がる途中と前へ出る途中に何も無いか
	const FVector Stand(Face.ImpactPoint.X + ClimbDir.X * (Radius + 12.f), Face.ImpactPoint.Y + ClimbDir.Y * (Radius + 12.f), Top.ImpactPoint.Z + StandHalfHeight + 2.f);
	const FCollisionShape Shape = FCollisionShape::MakeCapsule(Radius - 2.f, StandHalfHeight - 2.f);
	FCollisionQueryParams SweepParams(SCENE_QUERY_STAT(HiderClimbSweep), false, this);
	if (World->OverlapBlockingTestByChannel(Stand, FQuat::Identity, ECC_Pawn, Shape, SweepParams))
	{
		return false;
	}
	const FVector Raised(Loc.X, Loc.Y, Stand.Z);
	const FCollisionShape Thin = FCollisionShape::MakeCapsule(Radius - 4.f, FMath::Min(Capsule->GetScaledCapsuleHalfHeight(), StandHalfHeight) - 4.f);
	if (World->SweepTestByChannel(Loc, Raised, FQuat::Identity, ECC_Pawn, Thin, SweepParams)
		|| World->SweepTestByChannel(Raised, Stand, FQuat::Identity, ECC_Pawn, Thin, SweepParams))
	{
		return false;
	}
	OutStandLocation = Stand;
	// 登る秒数：置いた壁なら種類ごと（クライミング壁は速い）、家具はゆっくり
	OutSeconds = 1.f;
	if (const AKakurenboGameMode* GM = World->GetAuthGameMode<AKakurenboGameMode>())
	{
		const APlaceableBlock* Block = Cast<APlaceableBlock>(Top.GetActor());
		OutSeconds = GM->GetClimbSeconds(Block ? Block->WallTypeIndex : INDEX_NONE);
	}
	return true;
}

bool AHiderCharacter::TryStartClimb(const FVector& Dir)
{
	UCharacterMovementComponent* Move = GetCharacterMovement();
	if (!bClimbUnlocked || bClimbing || IsDashing() || !Move->IsMovingOnGround())
	{
		return false;
	}
	FVector Stand;
	float Seconds = 1.f;
	if (!FindClimbTarget(Dir, Stand, Seconds))
	{
		return false;
	}
	// しゃがんでいたら立ってから登る（頭の上に物があって立てなければ登らない）
	if (bIsCrouched)
	{
		Move->bWantsToCrouch = false;
		Move->UnCrouch(false);
		if (bIsCrouched)
		{
			return false;
		}
	}
	Move->bWantsToCrouch = false;
	Move->StopMovementImmediately();
	Move->SetMovementMode(MOVE_Flying); // 登っている間は重力で落ちない（位置はこちらで動かす）
	bClimbing = true;
	ClimbElapsed = 0.f;
	ClimbDuration = FMath::Max(0.05f, Seconds);
	ClimbFrom = GetActorLocation();
	ClimbTo = Stand;
	++ClimbCount;
	SetActorRotation(FRotator(0.f, (Stand - ClimbFrom).GetSafeNormal2D().Rotation().Yaw, 0.f)); // 登る面の方を向く
	if (AKakurenboGameMode* GM = GetWorld()->GetAuthGameMode<AKakurenboGameMode>())
	{
		GM->HandlePlayerClimb(ClimbFrom - FVector(0.f, 0.f, GetSimpleCollisionHalfHeight()));
	}
	return true;
}

void AHiderCharacter::TickClimb(float DeltaSeconds)
{
	ClimbElapsed += DeltaSeconds;
	const float T = FMath::Clamp(ClimbElapsed / ClimbDuration, 0.f, 1.f);
	// 前半（7 割）で真上へ上がり（壁の面に沿うので何にもぶつからない）、後半で前へ出て上に立つ
	constexpr float RiseShare = 0.7f;
	const FVector Raised(ClimbFrom.X, ClimbFrom.Y, ClimbTo.Z);
	const FVector Pos = T < RiseShare
		? FMath::Lerp(ClimbFrom, Raised, FMath::InterpEaseInOut(0.f, 1.f, T / RiseShare, 2.f))
		: FMath::Lerp(Raised, ClimbTo, (T - RiseShare) / (1.f - RiseShare));
	SetActorLocation(Pos, false, nullptr, ETeleportType::TeleportPhysics);
	if (T >= 1.f)
	{
		CancelClimb();
	}
}

void AHiderCharacter::CancelClimb()
{
	if (!bClimbing)
	{
		return;
	}
	bClimbing = false;
	GetCharacterMovement()->StopMovementImmediately();
	GetCharacterMovement()->SetMovementMode(MOVE_Walking);
	StepDistance = 0.f;
	LastStepLocation = GetActorLocation();
	UpdateCrouch(); // しゃがむキーを押したままなら、上でしゃがむ
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

// ---------------------------------------------------------------- 見た目（キャラクターのモデル）

namespace
{
	/** 骨の名前が左右どちらか（"_l" "Left" "l_" など、よくある書き方） */
	int32 BoneSide(const FString& Lower)
	{
		if (Lower.Contains(TEXT("left")) || Lower.EndsWith(TEXT("_l")) || Lower.EndsWith(TEXT(".l")) || Lower.StartsWith(TEXT("l_")) || Lower.Contains(TEXT("_l_")))
		{
			return -1;
		}
		if (Lower.Contains(TEXT("right")) || Lower.EndsWith(TEXT("_r")) || Lower.EndsWith(TEXT(".r")) || Lower.StartsWith(TEXT("r_")) || Lower.Contains(TEXT("_r_")))
		{
			return 1;
		}
		return 0;
	}
}

void AHiderCharacter::ApplyLook(USkeletalMesh* Model, UMaterialInterface* Material, float MeshYaw)
{
	if (!Model)
	{
		return;
	}
	// 骨を直接動かせるメッシュ（PoseableMesh）で表示する（アニメーションが無いモデルでも手足を動かせる）
	LookMesh = NewObject<UPoseableMeshComponent>(this, TEXT("LookMesh"));
	LookMesh->SetupAttachment(RootComponent);
	LookMesh->SetSkinnedAssetAndUpdate(Model);
	LookMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	LookMesh->RegisterComponent();
	if (Material)
	{
		for (int32 i = 0; i < LookMesh->GetNumMaterials(); ++i)
		{
			LookMesh->SetMaterial(i, Material);
		}
	}
	// 体（カプセル）の高さに合わせて大きさを変え、足を床に置く
	const float HalfHeight = GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	const FBoxSphereBounds Bounds = Model->GetImportedBounds();
	const float MeshHeight = FMath::Max(Bounds.BoxExtent.Z * 2.f, 1.f);
	const float MeshBottom = Bounds.Origin.Z - Bounds.BoxExtent.Z;
	const float Fit = HalfHeight * 2.f / MeshHeight;
	LookBaseScale = FVector(Fit);
	LookBaseLocation = FVector(0.f, 0.f, -HalfHeight - MeshBottom * Fit);
	LookMesh->SetRelativeScale3D(LookBaseScale);
	LookMesh->SetRelativeLocation(LookBaseLocation);
	LookMesh->SetRelativeRotation(FRotator(0.f, MeshYaw, 0.f));
	BodyMesh->SetHiddenInGame(true);

	// 脚・腕の骨を名前で探す（モデルによって名前の付け方が違うので、よくある書き方を順に試す）
	const FReferenceSkeleton& Ref = Model->GetRefSkeleton();
	TArray<FString> Names;
	for (int32 i = 0; i < Ref.GetNum(); ++i)
	{
		const FName Name = Ref.GetBoneName(i);
		Names.Add(Name.ToString());
		const FString Lower = Name.ToString().ToLower();
		const int32 Side = BoneSide(Lower);
		auto Pick = [&](FName& Left, FName& Right)
		{
			FName& Slot = Side < 0 ? Left : Right;
			if (Side != 0 && Slot.IsNone())
			{
				Slot = Name;
			}
		};
		const bool bTwistOrEnd = Lower.Contains(TEXT("twist")) || Lower.Contains(TEXT("end")) || Lower.Contains(TEXT("roll"));
		if (bTwistOrEnd || Lower.StartsWith(TEXT("ik_")))
		{
			continue;
		}
		if (Lower.Contains(TEXT("foot")))
		{
			Pick(LeftFoot, RightFoot);
			continue;
		}
		if (Lower.Contains(TEXT("thigh")) || Lower.Contains(TEXT("upleg")) || Lower.Contains(TEXT("upperleg")) || Lower.Contains(TEXT("up_leg")))
		{
			Pick(LeftThigh, RightThigh);
		}
		else if (Lower.Contains(TEXT("calf")) || Lower.Contains(TEXT("shin")) || Lower.Contains(TEXT("lowerleg")) || Lower.Contains(TEXT("knee"))
			|| (Lower.Contains(TEXT("leg")) && !Lower.Contains(TEXT("up"))))
		{
			Pick(LeftCalf, RightCalf);
		}
		else if (Lower.Contains(TEXT("forearm")) || Lower.Contains(TEXT("lowerarm")) || Lower.Contains(TEXT("elbow")))
		{
			Pick(LeftForearm, RightForearm);
		}
		else if (Lower.Contains(TEXT("upperarm")) || Lower.Contains(TEXT("uparm")) || (Lower.Contains(TEXT("arm")) && !Lower.Contains(TEXT("hand"))))
		{
			Pick(LeftUpperArm, RightUpperArm);
		}
		else if (Spine.IsNone() && Lower.Contains(TEXT("spine")))
		{
			Spine = Name;
		}
	}
	UE_LOG(LogTemp, Log, TEXT("Player look: %s, %d bones [%s] -> thigh %s/%s calf %s/%s arm %s/%s forearm %s/%s spine %s"),
		*Model->GetName(), Ref.GetNum(), *FString::Join(Names, TEXT(",")), *LeftThigh.ToString(), *RightThigh.ToString(), *LeftCalf.ToString(), *RightCalf.ToString(),
		*LeftUpperArm.ToString(), *RightUpperArm.ToString(), *LeftForearm.ToString(), *RightForearm.ToString(), *Spine.ToString());
}

void AHiderCharacter::RotateBone(FName Bone, const FQuat& DeltaComponentSpace)
{
	if (Bone.IsNone() || !LookMesh)
	{
		return;
	}
	const FQuat Current = LookMesh->GetBoneRotationByName(Bone, EBoneSpaces::ComponentSpace).Quaternion();
	LookMesh->SetBoneRotationByName(Bone, (DeltaComponentSpace * Current).Rotator(), EBoneSpaces::ComponentSpace);
}

void AHiderCharacter::UpdateLookPose(float DeltaSeconds)
{
	if (!LookMesh || IsHidden())
	{
		return;
	}
	if (HasOfficialAnimation())
	{
		// 公式のアニメーション：地面では速さに合わせて待機〜歩く〜走る、空中では落ちるアニメーション
		const bool bOnGround = GetCharacterMovement()->IsMovingOnGround();
		UAnimationAsset* Wanted = (bOnGround || !FallAnim) ? MoveAnim.Get() : FallAnim.Get();
		if (Wanted && Wanted != PlayingAnim)
		{
			PlayingAnim = Wanted;
			AnimDriver->PlayAnimation(Wanted, true);
		}
		// 速さはなめらかに変える（家具にぶつかって速さが 0 と少しの間を行き来しても、待機と歩きが毎フレーム入れ替わらないように）
		SmoothedAnimSpeed = FMath::FInterpTo(SmoothedAnimSpeed, GetVelocity().Size2D(), DeltaSeconds, 8.f);
		if (UAnimSingleNodeInstance* Single = AnimDriver->GetSingleNodeInstance(); Single && PlayingAnim == MoveAnim)
		{
			// 公式のブレンドスペースは 横軸 = 向き（体は進む方を向くので 0）、縦軸 = 速さ（0〜600）
			Single->SetBlendSpacePosition(FVector(0.f, SmoothedAnimSpeed, 0.f));
		}
		CopyDriverPose();
		ApplyCrouchPose(DeltaSeconds, 0.f);
		return;
	}
	// 歩く速さに合わせて足を交互に振る（止まると元に戻る）
	const float Speed = GetVelocity().Size2D();
	const bool bOnGround = GetCharacterMovement()->IsMovingOnGround();
	WalkBlend = FMath::FInterpTo(WalkBlend, (bOnGround && Speed > 30.f) ? 1.f : 0.f, DeltaSeconds, 10.f);
	WalkPhase = FMath::Fmod(WalkPhase + DeltaSeconds * Speed / FMath::Max(StepStride, 1.f) * UE_PI, 2.f * UE_PI);
	IdleTime += DeltaSeconds;
	const float Swing = FMath::Sin(WalkPhase) * WalkBlend;

	// 部品の座標での向き：体の前・右（モデルの向き MeshYaw を戻して求める）
	const FRotator MeshRot = LookMesh->GetRelativeRotation();
	const FVector Forward = MeshRot.UnrotateVector(FVector::ForwardVector);
	const FVector Right = MeshRot.UnrotateVector(FVector::RightVector);

	// 元の姿勢に戻してから、親（太もも・二の腕）→ 子（すね・前腕）の順に回す
	for (const FName Bone : { LeftThigh, RightThigh, LeftCalf, RightCalf, LeftUpperArm, RightUpperArm, LeftForearm, RightForearm, Spine })
	{
		if (!Bone.IsNone())
		{
			LookMesh->ResetBoneTransformByName(Bone);
		}
	}
	// 脚：右の太ももが前に出るとき左は後ろ。後ろの脚はひざを曲げる
	auto SwingAbout = [&](const FVector& Axis, float Degrees) { return FQuat(Axis, FMath::DegreesToRadians(Degrees)); };
	// 右を軸に回すと前へ出る向き（足の先が Forward へ動く）を、骨の向きから決める
	auto ForwardSign = [&](FName Bone, FName Child) -> float
	{
		if (Bone.IsNone() || Child.IsNone())
		{
			return 1.f;
		}
		const FVector Dir = (LookMesh->GetBoneLocationByName(Child, EBoneSpaces::ComponentSpace) - LookMesh->GetBoneLocationByName(Bone, EBoneSpaces::ComponentSpace)).GetSafeNormal();
		const FVector Moved = SwingAbout(Right, 10.f).RotateVector(Dir);
		return FVector::DotProduct(Moved - Dir, Forward) >= 0.f ? 1.f : -1.f;
	};
	const float LegSign = ForwardSign(LeftThigh, LeftCalf);
	RotateBone(LeftThigh, SwingAbout(Right, LegSign * LegSwingDegrees * Swing));
	RotateBone(RightThigh, SwingAbout(Right, -LegSign * LegSwingDegrees * Swing));
	// ひざ：後ろへ振っている脚だけ曲げる（すねが後ろへ）
	RotateBone(LeftCalf, SwingAbout(Right, -LegSign * LegSwingDegrees * 1.2f * FMath::Max(0.f, -Swing)));
	RotateBone(RightCalf, SwingAbout(Right, -LegSign * LegSwingDegrees * 1.2f * FMath::Max(0.f, Swing)));

	// 腕：T ポーズなら体の横へ下ろしてから、脚と反対に振る
	auto LowerArm = [&](FName Upper, FName Fore, float SideSign)
	{
		if (Upper.IsNone() || Fore.IsNone())
		{
			return;
		}
		const FVector Dir = (LookMesh->GetBoneLocationByName(Fore, EBoneSpaces::ComponentSpace) - LookMesh->GetBoneLocationByName(Upper, EBoneSpaces::ComponentSpace)).GetSafeNormal();
		const float Down = FMath::DegreesToRadians(ArmDownDegrees);
		const FVector Wanted = (Right * SideSign * FMath::Cos(Down) - FVector::UpVector * FMath::Sin(Down)).GetSafeNormal();
		RotateBone(Upper, FQuat::FindBetweenNormals(Dir, Wanted));
	};
	LowerArm(LeftUpperArm, LeftForearm, -1.f);
	LowerArm(RightUpperArm, RightForearm, 1.f);
	const float ArmSign = ForwardSign(LeftUpperArm, LeftForearm);
	RotateBone(LeftUpperArm, SwingAbout(Right, -ArmSign * ArmSwingDegrees * Swing));
	RotateBone(RightUpperArm, SwingAbout(Right, ArmSign * ArmSwingDegrees * Swing));

	// 歩くと上下に弾む・止まっているときはゆっくり息をする
	const float Bob = FMath::Abs(FMath::Sin(WalkPhase)) * 4.f * WalkBlend + FMath::Sin(IdleTime * 2.2f) * 0.8f * (1.f - WalkBlend);
	ApplyCrouchPose(DeltaSeconds, Bob);
}

void AHiderCharacter::ApplyCrouchPose(float DeltaSeconds, float Bob)
{
	// しゃがむ：太ももを前へ、すねを後ろへ曲げ、背中を少し丸める。足が浮いた分だけモデルを下げて、足を床に付ける
	CrouchBlend = FMath::FInterpTo(CrouchBlend, bIsCrouched ? 1.f : 0.f, DeltaSeconds, 12.f);
	float Lift = 0.f;
	if (CrouchBlend > 0.01f && !LeftThigh.IsNone() && !RightThigh.IsNone() && !LeftCalf.IsNone() && !RightCalf.IsNone())
	{
		const FRotator MeshRot = LookMesh->GetRelativeRotation();
		const FVector Forward = MeshRot.UnrotateVector(FVector::ForwardVector);
		const FVector Right = MeshRot.UnrotateVector(FVector::RightVector);
		auto About = [&](float Degrees) { return FQuat(Right, FMath::DegreesToRadians(Degrees)); };
		// 右を軸に + へ回すと、骨の先が前へ動くか（モデルによって骨の向きが違うので、実際の向きから決める）
		auto ForwardSign = [&](FName Bone, FName Child) -> float
		{
			const FVector Dir = (LookMesh->GetBoneLocationByName(Child, EBoneSpaces::ComponentSpace) - LookMesh->GetBoneLocationByName(Bone, EBoneSpaces::ComponentSpace)).GetSafeNormal();
			return FVector::DotProduct(About(10.f).RotateVector(Dir) - Dir, Forward) >= 0.f ? 1.f : -1.f;
		};
		const FName LeftEnd = LeftFoot.IsNone() ? LeftCalf : LeftFoot;
		const FName RightEnd = RightFoot.IsNone() ? RightCalf : RightFoot;
		const FName Head = LookMesh->GetBoneIndex(TEXT("head")) != INDEX_NONE ? FName(TEXT("head")) : NAME_None;
		auto Bend = [&](float Blend)
		{
			const float Knee = ForwardSign(LeftThigh, LeftCalf);
			RotateBone(LeftThigh, About(Knee * CrouchThighDegrees * Blend));
			RotateBone(RightThigh, About(Knee * CrouchThighDegrees * Blend));
			if (!LeftEnd.IsNone() && LeftEnd != LeftCalf)
			{
				const float Shin = ForwardSign(LeftCalf, LeftEnd);
				RotateBone(LeftCalf, About(-Shin * CrouchCalfDegrees * Blend));
				RotateBone(RightCalf, About(-Shin * CrouchCalfDegrees * Blend));
			}
			if (!Spine.IsNone() && !Head.IsNone())
			{
				RotateBone(Spine, About(ForwardSign(Spine, Head) * CrouchSpineDegrees * Blend));
			}
		};

		// 足が浮く量は、元の姿勢（T ポーズ）でしゃがませて一度だけ測る。
		// 歩いている姿勢で毎フレーム測ると、脚の振りで値が変わって体が上下にがくがく揺れる（家具に沿って歩くと目立った）
		if (!bCrouchLiftMeasured)
		{
			bCrouchLiftMeasured = true;
			const TArray<FTransform> Pose = LookMesh->BoneSpaceTransforms;
			LookMesh->BoneSpaceTransforms = LookMesh->GetSkinnedAsset()->GetRefSkeleton().GetRefBonePose();
			auto FootZ = [&]() { return FMath::Min(LookMesh->GetBoneLocationByName(LeftEnd, EBoneSpaces::ComponentSpace).Z, LookMesh->GetBoneLocationByName(RightEnd, EBoneSpaces::ComponentSpace).Z); };
			const float Before = FootZ();
			Bend(1.f);
			FullCrouchLift = FMath::Max(0.f, (FootZ() - Before) * LookBaseScale.Z);
			LookMesh->BoneSpaceTransforms = Pose;
		}
		Bend(CrouchBlend);
		Lift = FullCrouchLift * CrouchBlend;
	}
	SmoothedCrouchLift = Lift;
	LookMesh->SetRelativeLocation(LookBaseLocation + FVector(0.f, 0.f, CrouchMeshOffset + Bob - SmoothedCrouchLift));
}

void AHiderCharacter::ApplyOfficialAnimation(USkeletalMesh* SourceMesh, UAnimationAsset* Move, UAnimationAsset* Fall)
{
	if (!LookMesh || !SourceMesh || !Move)
	{
		return;
	}
	// 見えないマネキン。見えなくてもアニメーションと骨の計算は毎フレーム行う
	AnimDriver = NewObject<USkeletalMeshComponent>(this, TEXT("AnimDriver"));
	AnimDriver->SetupAttachment(RootComponent);
	AnimDriver->SetSkeletalMeshAsset(SourceMesh);
	AnimDriver->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	AnimDriver->SetCastShadow(false);
	AnimDriver->SetHiddenInGame(true);
	AnimDriver->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
	AnimDriver->RegisterComponent();
	AnimDriver->SetAnimationMode(EAnimationMode::AnimationSingleNode);
	MoveAnim = Move;
	FallAnim = Fall;
	PlayingAnim = Move;
	AnimDriver->PlayAnimation(Move, true);

	// モデルの骨ごとに、同じ名前のマネキンの骨を探す
	const FReferenceSkeleton& Ref = LookMesh->GetSkinnedAsset()->GetRefSkeleton();
	const FReferenceSkeleton& DriverRef = SourceMesh->GetRefSkeleton();
	DriverBoneIndex.Init(INDEX_NONE, Ref.GetNum());
	DrivenBoneCount = 0;
	for (int32 i = 0; i < Ref.GetNum(); ++i)
	{
		const FName Name = Ref.GetBoneName(i);
		if (Name == TEXT("root"))
		{
			continue; // 根元は動かさない（体の位置はキャラクターが決める）
		}
		DriverBoneIndex[i] = DriverRef.FindBoneIndex(Name);
		DrivenBoneCount += DriverBoneIndex[i] != INDEX_NONE ? 1 : 0;
	}
	PelvisBoneIndex = Ref.FindBoneIndex(TEXT("pelvis"));
	// 腰の上下の動きは、モデルとマネキンの背の高さの比で縮める（モデルの単位で）
	const float ModelHeight = LookMesh->GetSkinnedAsset()->GetBounds().BoxExtent.Z;
	const float DriverHeight = SourceMesh->GetImportedBounds().BoxExtent.Z;
	DriverHeightRatio = DriverHeight > 1.f ? ModelHeight / DriverHeight : 1.f;
	LookMesh->SetRelativeLocation(LookBaseLocation);
	UE_LOG(LogTemp, Log, TEXT("Player look: official animation %s on %d of %d bones (height ratio %.2f)"), *Move->GetName(), DrivenBoneCount, Ref.GetNum(), DriverHeightRatio);
}

void AHiderCharacter::CopyDriverPose()
{
	// マネキンの骨の回転（親から見た向き）をそのまま使い、骨の長さ（位置）はモデルのまま。腰だけは上下の動きも写す。
	// 親 → 子の順に、部品の座標での姿勢を計算して設定する（子の骨は親に付いてくる）
	const TArray<FTransform> Source = AnimDriver->GetBoneSpaceTransforms();
	const TArray<FTransform>& DriverRefPose = AnimDriver->GetSkinnedAsset()->GetRefSkeleton().GetRefBonePose();
	const FReferenceSkeleton& Ref = LookMesh->GetSkinnedAsset()->GetRefSkeleton();
	const TArray<FTransform>& RefPose = Ref.GetRefBonePose();
	if (Source.Num() == 0 || DriverBoneIndex.Num() != Ref.GetNum())
	{
		return;
	}
	TArray<FTransform> ComponentSpace;
	ComponentSpace.SetNum(Ref.GetNum());
	for (int32 i = 0; i < Ref.GetNum(); ++i)
	{
		FTransform Local = RefPose[i];
		const int32 S = DriverBoneIndex[i];
		const bool bDriven = Source.IsValidIndex(S);
		if (bDriven)
		{
			Local.SetRotation(Source[S].GetRotation());
			if (i == PelvisBoneIndex && DriverRefPose.IsValidIndex(S))
			{
				Local.SetTranslation(RefPose[i].GetTranslation() + (Source[S].GetTranslation() - DriverRefPose[S].GetTranslation()) * DriverHeightRatio);
			}
		}
		const int32 Parent = Ref.GetParentIndex(i);
		ComponentSpace[i] = Parent >= 0 ? Local * ComponentSpace[Parent] : Local;
		if (bDriven)
		{
			LookMesh->SetBoneTransformByName(Ref.GetBoneName(i), ComponentSpace[i], EBoneSpaces::ComponentSpace);
		}
	}
}