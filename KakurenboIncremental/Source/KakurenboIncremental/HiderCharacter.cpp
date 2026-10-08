#include "HiderCharacter.h"

#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/PoseableMeshComponent.h"
#include "Engine/SkeletalMesh.h"
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

void AHiderCharacter::AddTopDownTilt(float DeltaDegrees)
{
	TopDownPitch = FMath::Clamp(TopDownPitch + DeltaDegrees, -90.f, TopDownMaxTiltPitch);
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
		CameraBoom->SetWorldRotation(FRotator(ControlRot.Pitch, ControlRot.Yaw, 0.f));
		CameraBoom->TargetArmLength = FMath::FInterpTo(CameraBoom->TargetArmLength, ThirdPersonDistance, DeltaSeconds, 8.f);
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
	// 回数制（クールタイムは無い）。ダッシュ中にもう一度押しても使わない
	if (!bDashUnlocked || DashUsesLeft <= 0 || IsDashing())
	{
		return false;
	}
	--DashUsesLeft;
	DashTimeRemaining = DashDuration;
	UpdateWalkSpeed();
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
		if (bTwistOrEnd)
		{
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
	LookMesh->SetRelativeLocation(LookBaseLocation + FVector(0.f, 0.f, Bob));
}