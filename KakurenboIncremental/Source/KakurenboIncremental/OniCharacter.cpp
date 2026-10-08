#include "OniCharacter.h"

#include "Animation/AnimInstance.h"
#include "Animation/AnimSequence.h"
#include "Materials/MaterialInterface.h"
#include "Components/CapsuleComponent.h"
#include "Components/PointLightComponent.h"
#include "EngineUtils.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/SpotLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GridPathfinder.h"
#include "HiderCharacter.h"
#include "KakurenboFx.h"
#include "KakurenboGameMode.h"
#include "KakurenboGridSubsystem.h"
#include "KakurenboLibrary.h"
#include "KakurenboOniBlackboard.h"
#include "KakurenboSoundSubsystem.h"
#include "PlaceableBlock.h"
#include "SmokeCloud.h"
#include "TreasureActor.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	constexpr float EyeHeight = 60.f;           // カプセル中心から目までの高さ
	constexpr float ReachDistance = 25.f;       // マスの中心にこれだけ近づいたら「着いた」
	constexpr float RepathInterval = 1.f;       // 定期的に経路を作り直す間隔（秒）
	constexpr float ChaseRepathInterval = 0.3f; // 追いかけ中は相手が動くので頻繁に作り直す
	constexpr float SenseInterval = 0.1f;       // 視界チェックの間隔（秒）
	constexpr float WallHuntInterval = 0.5f;    // パワー鬼が見えている壁を探す間隔（秒）
	constexpr float ChaseStopDistance = 70.f;   // 追いかけ中、相手（の真下）にこれより近いときは歩かず向くだけ
}

AOniCharacter::AOniCharacter()
{
	PrimaryActorTick.bCanEverTick = true;

	GetCapsuleComponent()->InitCapsuleSize(38.f, 95.f);

	// AI が自動で操作する（AIController が付く）
	AutoPossessAI = EAutoPossessAI::PlacedInWorldOrSpawned;

	// 進む方向へ体を向ける
	bUseControllerRotationYaw = false;
	UCharacterMovementComponent* Move = GetCharacterMovement();
	Move->bOrientRotationToMovement = true;
	Move->RotationRate = FRotator(0.f, 540.f, 0.f);
	Move->MaxWalkSpeed = WanderSpeed;

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderFinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeFinder(TEXT("/Engine/BasicShapes/Cube.Cube"));

	BodyMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BodyMesh"));
	BodyMesh->SetupAttachment(RootComponent);
	BodyMesh->SetStaticMesh(CylinderFinder.Object);
	BodyMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	BodyMesh->SetRelativeScale3D(BaseBodyScale);

	FaceMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("FaceMesh"));
	FaceMesh->SetupAttachment(RootComponent);
	FaceMesh->SetStaticMesh(CubeFinder.Object);
	FaceMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	FaceMesh->SetRelativeLocation(FVector(36.f, 0.f, EyeHeight));
	FaceMesh->SetRelativeScale3D(FVector(0.15f, 0.5f, 0.15f));

	Flashlight = CreateDefaultSubobject<USpotLightComponent>(TEXT("Flashlight"));
	Flashlight->SetupAttachment(RootComponent);
	Flashlight->SetRelativeLocation(FVector(30.f, 0.f, EyeHeight));
	Flashlight->SetMobility(EComponentMobility::Movable);
	Flashlight->SetIntensity(20000.f);
	Flashlight->SetLightColor(FLinearColor(1.f, 0.85f, 0.6f));
	Flashlight->SetCastShadows(true);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereFinder(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	for (int32 i = 0; i < 3; ++i)
	{
		UStaticMeshComponent* Star = CreateDefaultSubobject<UStaticMeshComponent>(*FString::Printf(TEXT("StunStar%d"), i));
		Star->SetupAttachment(RootComponent);
		Star->SetStaticMesh(SphereFinder.Object);
		Star->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Star->SetCastShadow(false);
		Star->SetRelativeScale3D(FVector(0.16f));
		Star->SetHiddenInGame(true);
		StunStars.Add(Star);
	}

	TypeMarker = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("TypeMarker"));
	TypeMarker->SetupAttachment(RootComponent);
	TypeMarker->SetStaticMesh(SphereFinder.Object);
	TypeMarker->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	TypeMarker->SetCastShadow(false);
	TypeMarker->SetRelativeLocation(FVector(0.f, 0.f, 130.f));
	TypeMarker->SetRelativeScale3D(FVector(0.28f));
	TypeMarker->SetHiddenInGame(true);

	// キャラクターのモデル（スケルタルメッシュ）は見た目だけ。視線や設置のカーソルの邪魔をしない
	GetMesh()->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	AccessoryMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("AccessoryMesh"));
	AccessoryMesh->SetupAttachment(GetMesh());
	AccessoryMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	AccessoryMesh->SetHiddenInGame(true);
}

void AOniCharacter::SetSkeletalAppearance(const FKakurenboOniLook& InLook)
{
	Look = InLook;
}

void AOniCharacter::SetBodySquash(const FVector& Ratio)
{
	BodyMesh->SetRelativeScale3D(BaseBodyScale * Ratio);
	if (Look.Mesh)
	{
		GetMesh()->SetRelativeScale3D(BaseMeshScale * Ratio);
	}
}

void AOniCharacter::UpdateMeshAnimation()
{
	if (!Look.Mesh || Look.AnimClass)
	{
		return; // 円柱のまま、またはアニメーション BP に任せる
	}
	// 今の様子に合ったアニメーションを選ぶ（無いものは待機で代用）
	UAnimSequence* Desired = Look.Idle;
	float PlayRate = 1.f;
	const float Speed = GetVelocity().Size2D();
	if (!FoundReason.IsNone() && Look.Win)
	{
		Desired = Look.Win; // 見つけた！
	}
	else if (State == EOniState::Stunned && Look.Stunned)
	{
		Desired = Look.Stunned;
	}
	else if (State == EOniState::Attack && Look.Attack)
	{
		Desired = Look.Attack;
	}
	else if (Speed > 30.f && Look.Run)
	{
		Desired = Look.Run;
		PlayRate = FMath::Clamp(Speed / FMath::Max(Look.RunAnimSpeed, 1.f), 0.5f, 2.5f);
	}
	if (!Desired)
	{
		return;
	}
	USkeletalMeshComponent* MeshComp = GetMesh();
	if (Desired != PlayingAnim)
	{
		PlayingAnim = Desired;
		// 攻撃・見つけたは 1 回だけ、それ以外は繰り返す
		const bool bLoop = Desired != Look.Attack && Desired != Look.Win;
		MeshComp->PlayAnimation(Desired, bLoop);
	}
	MeshComp->SetPlayRate(PlayRate);
}

void AOniCharacter::DebugGoTo(const FIntPoint& Cell)
{
	if (bActive)
	{
		NoiseCell = Cell;
		StartInvestigate(Cell);
	}
}

void AOniCharacter::SetStunStarsVisible(bool bVisible)
{
	for (UStaticMeshComponent* Star : StunStars)
	{
		if (Star)
		{
			Star->SetHiddenInGame(!bVisible);
		}
	}
}

UKakurenboGridSubsystem* AOniCharacter::Grid() const
{
	return GetWorld() ? GetWorld()->GetSubsystem<UKakurenboGridSubsystem>() : nullptr;
}

UKakurenboOniBlackboard* AOniCharacter::Blackboard() const
{
	return GetWorld() ? GetWorld()->GetSubsystem<UKakurenboOniBlackboard>() : nullptr;
}

void AOniCharacter::ApplyTypeSettings(EOniType Type, const FKakurenboOniTypeRow& Row)
{
	OniType = Type;
	TypeDisplayName = Row.DisplayName;
	BodyColor = Row.Color;
	BodyScaleMultiplier = Row.BodyScale;
	SightRadius = Row.SightRadius;
	SightHalfAngle = Row.SightHalfAngle;
	bSingleTargetAttack = Row.bSingleTargetAttack;
	AttackRadius = Row.AttackRadius;
	AttackWindup = Row.AttackWindup;
	PocketInspectChance = Row.PocketInspectChance;
	StunScale = Row.StunScale;
	bDisarmTraps = Row.bDisarmTraps;
	SummonRadius = Row.SummonRadius;

	// 足音（どたどた）の大きさと高さ：大きい鬼ほど低く重く、すばしこい鬼ほど軽く
	switch (Type)
	{
	// （うろうろしているだけの鬼も、遠くからどっちにいるかわかる大きさにする）
	case EOniType::Breaker:  StepVolume = 1.7f; StepPitch = 0.75f; break;
	case EOniType::Scout:    StepVolume = 1.2f; StepPitch = 1.3f; break;
	case EOniType::Careful:  StepVolume = 1.1f; StepPitch = 1.1f; break;
	case EOniType::Treasure: StepVolume = 1.25f; StepPitch = 1.18f; break;
	case EOniType::Detector: StepVolume = 1.35f; StepPitch = 0.9f; break;
	default:                 StepVolume = 1.4f; StepPitch = 1.f; break;
	}
}

void AOniCharacter::BeginPlay()
{
	Super::BeginPlay();
	BaseBodyScale = FVector(0.76f * BodyScaleMultiplier, 0.76f * BodyScaleMultiplier, 1.9f);
	UKakurenboLibrary::ApplyColor(BodyMesh, BodyColor);
	UKakurenboLibrary::ApplyColor(FaceMesh, FLinearColor(0.02f, 0.02f, 0.02f));
	UKakurenboLibrary::ApplyColor(TypeMarker, BodyColor);
	for (UStaticMeshComponent* Star : StunStars)
	{
		UKakurenboLibrary::ApplyColor(Star, FLinearColor(1.f, 0.9f, 0.2f));
	}

	if (Look.Mesh)
	{
		// キャラクターのモデルを足元に合わせて置き、円柱の体と顔は隠す。種類は色違いのマテリアルと頭の上の玉で示す
		USkeletalMeshComponent* MeshComp = GetMesh();
		MeshComp->SetSkeletalMesh(Look.Mesh);
		if (Look.Material)
		{
			// 体が複数の部分（マテリアルの枠）に分かれているモデルもあるので、全部を色違いにする
			for (int32 i = 0; i < MeshComp->GetNumMaterials(); ++i)
			{
				MeshComp->SetMaterial(i, Look.Material);
			}
		}
		if (Look.AnimClass)
		{
			MeshComp->SetAnimInstanceClass(Look.AnimClass);
		}
		else
		{
			// アニメーション BP が無いので、1 つのアニメーションを直接再生するモードにする（UpdateMeshAnimation で切り替える）
			MeshComp->SetAnimationMode(EAnimationMode::AnimationSingleNode);
		}
		// モデルの大きさはアセットによってまちまちなので、体（カプセル）の高さに合わせて拡大・縮小し、足を床に置く。
		// Look.Scale はその上にかける倍率
		const float HalfHeight = GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
		const FBoxSphereBounds Bounds = Look.Mesh->GetImportedBounds();
		const float MeshHeight = FMath::Max(Bounds.BoxExtent.Z * 2.f, 1.f);
		const float MeshBottom = Bounds.Origin.Z - Bounds.BoxExtent.Z;
		const float Fit = HalfHeight * 2.f / MeshHeight * Look.Scale * BodyScaleMultiplier;
		MeshComp->SetRelativeLocation(FVector(0.f, 0.f, -HalfHeight - MeshBottom * Fit + Look.ZOffset));
		MeshComp->SetRelativeRotation(FRotator(0.f, Look.Yaw, 0.f));
		BaseMeshScale = FVector(Fit);
		if (Look.Accessory)
		{
			// 小物は体と同じ骨の動きをそのまま使う（LeaderPose: 自分ではアニメーションせず、親の姿勢に合わせる）
			AccessoryMesh->SetSkeletalMesh(Look.Accessory);
			AccessoryMesh->SetLeaderPoseComponent(MeshComp);
			AccessoryMesh->SetHiddenInGame(false);
		}
		BodyMesh->SetHiddenInGame(true);
		FaceMesh->SetHiddenInGame(true);
		TypeMarker->SetHiddenInGame(false);
		UpdateMeshAnimation();
	}
	BuildTypeDecoration();
	LastStepLocation = GetActorLocation();
	ResetBodyScale();
}

void AOniCharacter::BuildTypeDecoration()
{
	if (OniType != EOniType::Treasure && OniType != EOniType::Detector)
	{
		return;
	}
	// 頭のてっぺんの高さ（モデルも円柱も、体の高さはカプセルに合わせてある）
	const float HeadZ = GetCapsuleComponent()->GetScaledCapsuleHalfHeight() * FMath::Max(BodyScaleMultiplier, 0.5f) + 4.f;
	UStaticMesh* Cylinder = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	UStaticMesh* Sphere = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	auto AddPart = [this](UStaticMesh* PartMesh, const FVector& Location, const FVector& Scale, const FLinearColor& Color)
	{
		UStaticMeshComponent* Part = NewObject<UStaticMeshComponent>(this);
		Part->SetStaticMesh(PartMesh);
		Part->SetupAttachment(RootComponent);
		Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Part->SetCastShadow(false);
		Part->SetRelativeLocation(Location);
		Part->SetRelativeScale3D(Scale);
		Part->RegisterComponent();
		UKakurenboLibrary::ApplyColor(Part, Color);
		DecorationMeshes.Add(Part);
		return Part;
	};

	if (OniType == EOniType::Treasure)
	{
		// 金の王冠（輪と、ふちの宝石）
		const FLinearColor CrownGold(1.f, 0.72f, 0.1f);
		AddPart(Cylinder, FVector(0.f, 0.f, HeadZ + 10.f), FVector(0.5f, 0.5f, 0.22f), CrownGold);
		for (int32 i = 0; i < 5; ++i)
		{
			const float Angle = i * 2.f * UE_PI / 5.f;
			AddPart(Sphere, FVector(FMath::Cos(Angle) * 23.f, FMath::Sin(Angle) * 23.f, HeadZ + 26.f), FVector(0.12f), i % 2 == 0 ? FLinearColor(1.f, 0.1f, 0.3f) : CrownGold);
		}
		TypeMarker->SetRelativeLocation(FVector(0.f, 0.f, HeadZ + 55.f));
	}
	else
	{
		// 頭の上のアンテナと赤いランプ（見張り中は光る）
		AddPart(Cylinder, FVector(0.f, 0.f, HeadZ + 22.f), FVector(0.05f, 0.05f, 0.45f), FLinearColor(0.2f, 0.2f, 0.25f));
		AddPart(Sphere, FVector(0.f, 0.f, HeadZ + 48.f), FVector(0.18f), FLinearColor(1.f, 0.05f, 0.05f));
		UPointLightComponent* Lamp = NewObject<UPointLightComponent>(this);
		Lamp->SetupAttachment(RootComponent);
		Lamp->SetRelativeLocation(FVector(0.f, 0.f, HeadZ + 48.f));
		Lamp->SetIntensityUnits(ELightUnits::Candelas);
		Lamp->SetIntensity(12.f);
		Lamp->SetAttenuationRadius(350.f);
		Lamp->SetLightColor(FLinearColor(1.f, 0.1f, 0.1f));
		Lamp->SetCastShadows(false);
		Lamp->RegisterComponent();
		TypeMarker->SetRelativeLocation(FVector(0.f, 0.f, HeadZ + 80.f));
		// 見張りの懐中電灯は赤っぽく
		Flashlight->SetLightColor(FLinearColor(1.f, 0.45f, 0.4f));
	}
}

void AOniCharacter::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UKakurenboOniBlackboard* BB = Blackboard())
	{
		BB->ClearReservedTarget(this);
	}
	Super::EndPlay(EndPlayReason);
}

void AOniCharacter::Activate(AHiderCharacter* InTarget)
{
	Target = InTarget;
	bActive = true;
	FoundReason = NAME_None;
	LastProgressLocation = GetActorLocation();

	// 懐中電灯を視界と同じ形にする
	Flashlight->SetOuterConeAngle(FMath::Min(SightHalfAngle, 80.f));
	Flashlight->SetInnerConeAngle(FMath::Min(SightHalfAngle, 80.f) * 0.7f);
	Flashlight->SetAttenuationRadius(SightRadius);

	if (const UKakurenboGridSubsystem* G = Grid())
	{
		const int32 RegionsX = FMath::DivideAndRoundUp(G->GetSizeX(), RegionSize);
		const int32 RegionsY = FMath::DivideAndRoundUp(G->GetSizeY(), RegionSize);
		RegionVisitTime.Init(-1.f, RegionsX * RegionsY);
	}

	SetState(EOniState::Wander);
	BeginLookAround();
	IdleTimer = 0.5f;
	LastStepLocation = GetActorLocation();

	// 探知鬼は決まった見張りの場所へ向かう（無ければ今いる場所で見張る）
	if (OniType == EOniType::Detector)
	{
		bAtGuardPost = !bHasGuardCell;
		GuardLookYaw = GetActorRotation().Yaw;
	}
}

void AOniCharacter::Deactivate()
{
	bActive = false;
	bLeaping = false;
	GetCharacterMovement()->StopMovementImmediately();
	ResetBodyScale();
	SetStunStarsVisible(false);
}

// ---------------------------------------------------------------- 状態

void AOniCharacter::SetState(EOniState NewState)
{
	State = NewState;
	UCharacterMovementComponent* Move = GetCharacterMovement();
	switch (State)
	{
	case EOniState::Wander:      Move->MaxWalkSpeed = WanderSpeed; break;
	case EOniState::Investigate: Move->MaxWalkSpeed = InvestigateSpeed; break;
	case EOniState::Inspect:     Move->MaxWalkSpeed = InvestigateSpeed; break;
	case EOniState::Chase:       Move->MaxWalkSpeed = ChaseSpeed; break;
	case EOniState::Attack:
	case EOniState::Stunned:     Move->StopMovementImmediately(); break;
	}
}

void AOniCharacter::StartInvestigate(const FIntPoint& Cell)
{
	ResetBodyScale();
	SetState(EOniState::Investigate);
	StateBeforeAttack = EOniState::Investigate;
	IntentElapsed = 0.f;
	if (!RequestPathTo(Cell, EPathMode::BreakIfNeeded))
	{
		bHasGoal = false;
		BeginLookAround();
		SearchTimer = SearchDuration;
	}
}

void AOniCharacter::StartChase()
{
	ResetBodyScale();
	SetState(EOniState::Chase);
	StateBeforeAttack = EOniState::Chase;
	IntentElapsed = 0.f;
	LostSightTimer = 0.f;
	ChaseRepathTimer = 0.f;
	ChaseStartTime = GetWorld()->GetTimeSeconds();
	bHasGoal = false;
	if (Target)
	{
		LastKnownTargetLocation = Target->GetActorLocation();
	}
	// 見つかったことを音で知らせる。その鬼の場所から鳴らすので、画面を見なくてもどっちから来るかわかる
	UKakurenboSoundSubsystem::Play3D(this, EKakurenboSfx::Alert, GetActorLocation() + FVector(0.f, 0.f, 60.f), 1.1f);
}

void AOniCharacter::StartInspect(const FIntPoint& Cell)
{
	ResetBodyScale();
	SetState(EOniState::Inspect);
	StateBeforeAttack = EOniState::Inspect;
	IntentElapsed = 0.f;
	InspectCell = Cell;
	if (!RequestPathTo(Cell, EPathMode::BreakIfNeeded))
	{
		ReturnToWander(false);
	}
}

void AOniCharacter::ReturnToWander(bool bWithSightCooldown)
{
	ResetBodyScale();
	SetState(EOniState::Wander);
	StateBeforeAttack = EOniState::Wander;
	bHasGoal = false;
	IdleTimer = 0.5f;
	IntentElapsed = 0.f;
	BeginLookAround();
	if (bWithSightCooldown)
	{
		SightCooldown = GiveUpSightCooldown;
	}
	if (UKakurenboOniBlackboard* BB = Blackboard())
	{
		BB->ClearReservedTarget(this);
	}
}

void AOniCharacter::Stun(float Seconds)
{
	if (!bActive)
	{
		return;
	}
	ResetBodyScale();
	SetState(EOniState::Stunned);
	StateBeforeAttack = EOniState::Wander;
	bHasGoal = false;
	StunTimer = Seconds * FMath::Max(StunScale, 0.f);
	SetStunStarsVisible(true);
	if (UKakurenboOniBlackboard* BB = Blackboard())
	{
		BB->ClearReservedTarget(this);
	}
}

void AOniCharacter::BeginLookAround()
{
	LookAroundBaseYaw = GetActorRotation().Yaw;
	LookAroundTime = 0.f;
}

void AOniCharacter::TickLookAround(float DeltaSeconds, float AmplitudeDegrees)
{
	// 元の向きを中心に左右へゆっくり往復する（その場でくるくる回らない）
	LookAroundTime += DeltaSeconds;
	const float DesiredYaw = LookAroundBaseYaw + AmplitudeDegrees * FMath::Sin(LookAroundTime * 2.2f);
	SetActorRotation(FMath::RInterpTo(GetActorRotation(), FRotator(0.f, DesiredYaw, 0.f), DeltaSeconds, 6.f));
}

// ---------------------------------------------------------------- Tick

void AOniCharacter::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	UpdateMeshAnimation(); // 止まっている間（見つけた後など）もアニメーションは切り替える
	TickFootsteps();

	UKakurenboGridSubsystem* G = Grid();
	if (!bActive || !G || !G->IsConfigured())
	{
		return;
	}

	// 罠で動けない間は、見ることも捕まえることもできない
	if (State == EOniState::Stunned)
	{
		TickStunned(DeltaSeconds);
		return;
	}
	StunImmunityTimer = FMath::Max(0.f, StunImmunityTimer - DeltaSeconds);

	// ぶつかったら（体が触れたら）発見。一瞬の接触も逃さないよう毎フレーム調べる
	if (IsTouchingTarget())
	{
		FoundTarget(TEXT("touch"));
		return;
	}

	// 台へ飛び乗っている最中は、跳び終わるまでほかのことをしない
	LeapCooldown = FMath::Max(0.f, LeapCooldown - DeltaSeconds);
	if (bLeaping)
	{
		TickLeap(DeltaSeconds);
		return;
	}

	IntentElapsed += DeltaSeconds;
	SightCooldown = FMath::Max(0.f, SightCooldown - DeltaSeconds);
	SummonTimer = FMath::Max(0.f, SummonTimer - DeltaSeconds);

	// 視界チェック（見えたら追いかける。見えただけでは負けにならない）
	SenseTimer += DeltaSeconds;
	if (SenseTimer >= SenseInterval)
	{
		SenseTimer = 0.f;
		UpdateMemory();
		bTargetInSight = CanSeeTarget();
		if (bTargetInSight && Target)
		{
			LastKnownTargetLocation = Target->GetActorLocation();
		}
		if (GetIntent() == EOniState::Chase)
		{
			LostSightTimer = bTargetInSight ? 0.f : LostSightTimer + SenseInterval;
		}
		else if (bTargetInSight && OniType == EOniType::Detector)
		{
			HandleGuardSighting(); // 探知鬼は追いかけず、その場から仲間を呼ぶ
		}
		else if (bTargetInSight && SightCooldown <= 0.f)
		{
			StartChase();
		}
	}

	// 一定時間たった／見失ったら、うろうろに戻る（壁を壊している途中でも）
	const EOniState Intent = GetIntent();
	if (Intent == EOniState::Chase && LostSightTimer >= LoseSightDuration)
	{
		ReturnToWander(false); // 見失っただけなので、また見つけたら追いかける
	}
	else if (Intent == EOniState::Chase && IntentElapsed >= ChaseMaxDuration)
	{
		ReturnToWander(true); // 時間切れ。見えたままでもしばらく追わない（追いかけっこが終わらなくなるのを防ぐ）
	}
	else if (Intent == EOniState::Investigate && IntentElapsed >= InvestigateMaxDuration)
	{
		ReturnToWander(false);
	}
	else if (Intent == EOniState::Inspect && IntentElapsed >= InspectMaxDuration)
	{
		ReturnToWander(false);
	}

	// パワー鬼：うろうろ中に壁が見えたら壊しに行く
	if (OniType == EOniType::Breaker && GetIntent() == EOniState::Wander)
	{
		WallHuntTimer -= DeltaSeconds;
		FIntPoint WallCell;
		if (WallHuntTimer <= 0.f)
		{
			WallHuntTimer = WallHuntInterval;
			if (FindVisibleWallCell(WallCell))
			{
				StartInspect(WallCell);
			}
		}
	}

	switch (State)
	{
	case EOniState::Attack:      TickAttack(DeltaSeconds); break;
	case EOniState::Wander:      OniType == EOniType::Detector ? TickGuard(DeltaSeconds) : TickWander(DeltaSeconds); break;
	case EOniState::Investigate: TickInvestigate(DeltaSeconds); break;
	case EOniState::Inspect:     TickInspect(DeltaSeconds); break;
	case EOniState::Chase:       TickChase(DeltaSeconds); break;
	case EOniState::Stunned:     break;
	}

	if (bDrawDebug)
	{
		DrawDebug();
	}
}

void AOniCharacter::UpdateMemory()
{
	const float Now = GetWorld()->GetTimeSeconds();
	const FIntPoint Here = GetCurrentCell();

	// スピード鬼：今いる区画を「訪れた」と覚える
	if (OniType == EOniType::Scout && RegionVisitTime.Num() > 0)
	{
		const int32 RegionsX = FMath::DivideAndRoundUp(Grid()->GetSizeX(), RegionSize);
		const int32 Index = (Here.Y / RegionSize) * RegionsX + (Here.X / RegionSize);
		if (RegionVisitTime.IsValidIndex(Index))
		{
			RegionVisitTime[Index] = Now;
		}
	}

	// 慎重鬼：見渡せるマスを「調べた」と仲間と共有する
	if (OniType == EOniType::Careful)
	{
		MarkVisibleCellsChecked(Now);
	}
}

void AOniCharacter::MarkVisibleCellsChecked(float Now)
{
	UKakurenboOniBlackboard* BB = Blackboard();
	const UKakurenboGridSubsystem* G = Grid();
	if (!BB || !G)
	{
		return;
	}
	// CarefulLookCells マス以内で、間に壁・家具の無いマス（マスの中心どうしを結ぶ線が通れるマスだけを通る）
	const FIntPoint Here = GetCurrentCell();
	const int32 R = FMath::Max(1, CarefulLookCells);
	for (int32 DY = -R; DY <= R; ++DY)
	{
		for (int32 DX = -R; DX <= R; ++DX)
		{
			if (DX * DX + DY * DY > R * R)
			{
				continue;
			}
			const FIntPoint Cell = Here + FIntPoint(DX, DY);
			if (!G->IsWalkable(Cell))
			{
				continue;
			}
			// 線の上のマスを順に調べる（分割数は長い方の差）
			const int32 Steps = FMath::Max(FMath::Abs(DX), FMath::Abs(DY));
			bool bVisible = true;
			for (int32 s = 1; s < Steps && bVisible; ++s)
			{
				const float T = static_cast<float>(s) / Steps;
				const FIntPoint P(Here.X + FMath::RoundToInt(DX * T), Here.Y + FMath::RoundToInt(DY * T));
				bVisible = G->IsWalkable(P);
			}
			if (bVisible)
			{
				BB->MarkChecked(Cell, Now);
			}
		}
	}
}

bool AOniCharacter::IsInCarefulSector(const FIntPoint& Cell) const
{
	const UKakurenboGridSubsystem* G = Grid();
	if (!G || CarefulSectorCount <= 1)
	{
		return true;
	}
	// 舞台を Y 方向に等分した帯のどれか
	const int32 Sector = FMath::Clamp(Cell.Y * CarefulSectorCount / FMath::Max(1, G->GetSizeY()), 0, CarefulSectorCount - 1);
	return Sector == CarefulSector;
}

void AOniCharacter::TickFootsteps()
{
	// 歩いた距離が歩幅を超えるたびに、足元から「どたっ」と鳴らす（姿が見えなくても、音でどこにいるかわかる）
	const FVector Loc = GetActorLocation();
	const float Moved = FVector::Dist2D(Loc, LastStepLocation);
	LastStepLocation = Loc;
	if (Moved > 200.f || !GetCharacterMovement()->IsMovingOnGround() || GetVelocity().Size2D() < 40.f)
	{
		return; // 瞬間移動した・止まっている
	}
	StepDistance += Moved;
	if (StepDistance < StepStride * FMath::Max(BodyScaleMultiplier, 0.5f))
	{
		return;
	}
	StepDistance = 0.f;
	++StepSoundCount;
	const float Pitch = StepPitch * ((StepSoundCount % 2 == 0) ? 1.f : 0.9f) * FMath::FRandRange(0.97f, 1.03f);
	const FVector Feet = Loc - FVector(0.f, 0.f, GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
	// 走っている（音を調べに来る・追いかけてくる）ときは大きく、少し高く（向かってくるのが音でわかる）
	const EOniState Intent = GetIntent();
	const float Urgency = Intent == EOniState::Chase ? ChaseStepVolumeScale
		: (Intent == EOniState::Investigate || Intent == EOniState::Inspect) ? InvestigateStepVolumeScale : 1.f;
	UKakurenboSoundSubsystem::Play3D(this, EKakurenboSfx::OniStep, Feet, StepVolume * Urgency, Pitch * (Intent == EOniState::Chase ? 1.08f : 1.f));
}

void AOniCharacter::TickWander(float DeltaSeconds)
{
	if (bHasGoal)
	{
		const EFollowResult Result = FollowPath(DeltaSeconds);
		if (Result == EFollowResult::Arrived || Result == EFollowResult::Failed)
		{
			bHasGoal = false;
			IdleTimer = FMath::FRandRange(0.3f, 1.0f);
			BeginLookAround();
			if (UKakurenboOniBlackboard* BB = Blackboard())
			{
				BB->ClearReservedTarget(this); // 着いたので、向かっていた場所・建物の予約を外す
			}
		}
		return;
	}

	// 次の行き先を決めるまで、その場で軽く見回す
	TickLookAround(DeltaSeconds, 35.f);
	IdleTimer -= DeltaSeconds;
	if (IdleTimer <= 0.f)
	{
		bool bFound = TryEscapeEnclosure();
		if (!bFound && OniType == EOniType::Careful)
		{
			// 慎重鬼：まず広い場所を手分けして見渡す。壁に囲まれた場所（空洞）は、広い場所をほぼ調べ終わってから
			bFound = ChooseCarefulTarget() || TryInspectPocket() || ChooseRandomTarget(0);
		}
		else if (!bFound && OniType == EOniType::Treasure)
		{
			// 宝物鬼：お宝の周りをぐるぐる回る（空洞は調べない）
			bFound = ChooseTreasurePatrolTarget() || ChooseRandomTarget(5);
		}
		else if (!bFound)
		{
			bFound = TryInspectPocket() || ChooseWanderTarget();
		}
		if (!bFound)
		{
			IdleTimer = 1.f; // 行き先が見つからなければ少し待って再挑戦
		}
	}
}

void AOniCharacter::TickGuard(float DeltaSeconds)
{
	// 探知鬼：見張りの場所へ向かう（他に道が無ければ壁を壊してでも）
	if (!bAtGuardPost)
	{
		if (bHasGoal)
		{
			const EFollowResult Result = FollowPath(DeltaSeconds);
			if (Result == EFollowResult::Arrived)
			{
				bHasGoal = false;
				bAtGuardPost = true;
				GuardLookYaw = GetActorRotation().Yaw;
			}
			else if (Result == EFollowResult::Failed)
			{
				bHasGoal = false;
			}
		}
		else if (!RequestPathTo(GuardCell, EPathMode::BreakIfNeeded))
		{
			bAtGuardPost = true; // 行けない場所なら、今いる場所で見張る
			GuardLookYaw = GetActorRotation().Yaw;
		}
		return;
	}

	// 着いたら動かない。プレイヤーが見えていればそちらを向き、いなければゆっくり首を回して見張る
	if (bTargetInSight && Target)
	{
		FaceTowards(Target->GetActorLocation(), DeltaSeconds, 240.f);
		GuardLookYaw = GetActorRotation().Yaw;
	}
	else
	{
		GuardLookYaw = FRotator::NormalizeAxis(GuardLookYaw + ScanDegreesPerSecond * DeltaSeconds);
		SetActorRotation(FRotator(0.f, GuardLookYaw, 0.f));
	}
}

void AOniCharacter::HandleGuardSighting()
{
	if (SummonTimer > 0.f || !Target)
	{
		return;
	}
	SummonTimer = SummonCooldown;
	LastSummonTime = GetWorld()->GetTimeSeconds();
	ChaseStartTime = LastSummonTime; // HUD の「！」
	UE_LOG(LogTemp, Log, TEXT("Detector %s spotted the hider and calls others within %.0f cm"), *GetName(), SummonRadius);
	OnSummon.Broadcast(this, Target->GetActorLocation());
}

void AOniCharacter::Summon(const FVector& Location)
{
	UKakurenboGridSubsystem* G = Grid();
	if (!bActive || !G || State == EOniState::Stunned || OniType == EOniType::Detector || GetIntent() == EOniState::Chase)
	{
		return;
	}
	// 呼ばれた場所へ、音を聞いたときと同じように急いで向かう（他に道が無ければ壁を壊して）
	const FIntPoint Cell = ClampToGrid(G->WorldToCell(Location));
	NoiseCell = Cell;
	if (State == EOniState::Attack)
	{
		StateBeforeAttack = EOniState::Investigate;
		IntentElapsed = 0.f;
		GoalCell = Cell;
		CurrentPathMode = EPathMode::BreakIfNeeded;
		bHasGoal = true;
		return;
	}
	StartInvestigate(Cell);
}

void AOniCharacter::TickInvestigate(float DeltaSeconds)
{
	if (bHasGoal)
	{
		const EFollowResult Result = FollowPath(DeltaSeconds);
		if (Result == EFollowResult::Arrived || Result == EFollowResult::Failed)
		{
			bHasGoal = false;
			BeginLookAround();
			SearchTimer = SearchDuration;
		}
		return;
	}

	// 音のした場所で左右を見渡す。何も見つからなければ（見失ったら）うろうろに戻る
	TickLookAround(DeltaSeconds, 80.f);
	SearchTimer -= DeltaSeconds;
	if (SearchTimer <= 0.f)
	{
		ReturnToWander(false);
	}
}

void AOniCharacter::TickInspect(float DeltaSeconds)
{
	if (bHasGoal)
	{
		const EFollowResult Result = FollowPath(DeltaSeconds);
		if (Result == EFollowResult::Arrived || Result == EFollowResult::Failed)
		{
			bHasGoal = false;
			BeginLookAround();
			SearchTimer = SearchDuration * 0.6f;
		}
		return;
	}

	TickLookAround(DeltaSeconds, 80.f);
	SearchTimer -= DeltaSeconds;
	if (SearchTimer <= 0.f)
	{
		ReturnToWander(false);
	}
}

void AOniCharacter::TickChase(float DeltaSeconds)
{
	if (!Target)
	{
		ReturnToWander(false);
		return;
	}

	// 見えている間は相手の今の位置、見えなくなったら最後に見えた場所へ向かう。
	// 相手は動くので、経路はこまめに作り直す
	const FVector ChaseLocation = bTargetInSight ? Target->GetActorLocation() : LastKnownTargetLocation;
	const FIntPoint TargetCell = ClampToGrid(Grid()->WorldToCell(ChaseLocation));

	// 台（家具・積んだ壁）の上にいる相手には、そばまで歩いて行って飛び乗る。
	// （下で待っているだけだと、乗られたら手が出せず無敵になってしまうため）。高すぎる積んだ壁は今まで通り下から壊す
	FIntPoint PathGoal = TargetCell;
	float PlatformHeight = 0.f;
	if (bTargetInSight && IsTargetOnPlatform(PlatformHeight) && PlatformHeight <= LeapMaxHeight)
	{
		if (TryStartLeap())
		{
			return;
		}
		FIntPoint Approach;
		if (FindApproachCell(TargetCell, ChaseLocation, Approach))
		{
			PathGoal = Approach;
		}
	}

	ChaseRepathTimer -= DeltaSeconds;
	if (!bHasGoal || PathGoal != GoalCell || ChaseRepathTimer <= 0.f)
	{
		ChaseRepathTimer = ChaseRepathInterval;
		if (!RequestPathTo(PathGoal, EPathMode::BreakIfNeeded))
		{
			bHasGoal = false;
		}
	}

	// 相手（の真下）のすぐ近くでは歩かず向くだけにする。
	// （ジャンプしたプレイヤーの真下で行ったり来たりすると、体の向きが反転し続けてくるくる回って見えるため）
	const float Dist2D = FVector::Dist2D(ChaseLocation, GetActorLocation());
	if (Dist2D <= ChaseStopDistance)
	{
		FaceTowards(ChaseLocation, DeltaSeconds, 360.f);
		return;
	}

	const FVector ToTarget = (ChaseLocation - GetActorLocation()).GetSafeNormal2D();
	if (!bHasGoal)
	{
		AddMovementInput(ToTarget);
		return;
	}

	// 同じマスまで来たら、まっすぐ相手（最後に見えた場所）に向かう（ぶつかれば発見）
	if (FollowPath(DeltaSeconds) == EFollowResult::Arrived)
	{
		AddMovementInput(ToTarget);
	}
}

bool AOniCharacter::IsTargetOnPlatform(float& OutHeight) const
{
	OutHeight = 0.f;
	const ACharacter* TargetChar = Target;
	if (!TargetChar || !TargetChar->GetCharacterMovement() || !TargetChar->GetCharacterMovement()->IsMovingOnGround())
	{
		return false; // ジャンプ中（空中）は台に乗っているとはみなさない
	}
	const float MyFeet = GetActorLocation().Z - GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	const float TheirFeet = TargetChar->GetActorLocation().Z - TargetChar->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	OutHeight = TheirFeet - MyFeet;
	return OutHeight > 40.f; // 段差（45cm 以下）は歩いて上れる
}

bool AOniCharacter::FindApproachCell(const FIntPoint& PlatformCell, const FVector& TargetLocation, FIntPoint& OutCell) const
{
	const UKakurenboGridSubsystem* G = Grid();
	const FIntPoint Here = GetCurrentCell();
	float BestScore = TNumericLimits<float>::Max();
	// 近い順に探す（大きな家具の真ん中にいても、2 マス先までのそばのマスから選ぶ）
	for (int32 Radius = 0; Radius <= 2 && BestScore == TNumericLimits<float>::Max(); ++Radius)
	{
		for (int32 DY = -Radius; DY <= Radius; ++DY)
		{
			for (int32 DX = -Radius; DX <= Radius; ++DX)
			{
				if (FMath::Max(FMath::Abs(DX), FMath::Abs(DY)) != Radius)
				{
					continue;
				}
				const FIntPoint Cell = PlatformCell + FIntPoint(DX, DY);
				if (!G->IsWalkable(Cell) && Cell != Here)
				{
					continue;
				}
				// 相手に近いマスほどよい。同じくらいなら自分に近い方
				const float Score = FVector::Dist2D(G->CellFloorCenter(Cell), TargetLocation)
					+ 0.1f * FVector::Dist2D(G->CellFloorCenter(Cell), GetActorLocation());
				if (Score < BestScore)
				{
					BestScore = Score;
					OutCell = Cell;
				}
			}
		}
	}
	return BestScore < TNumericLimits<float>::Max();
}

bool AOniCharacter::TryStartLeap()
{
	UKakurenboGridSubsystem* G = Grid();
	UCharacterMovementComponent* Move = GetCharacterMovement();
	if (!Target || !G || LeapCooldown > 0.f || !Move->IsMovingOnGround())
	{
		return false;
	}
	const FVector Me = GetActorLocation();
	const FVector Goal = Target->GetActorLocation();
	if (FVector::Dist2D(Me, Goal) > 320.f)
	{
		return false;
	}

	// 相手の方向のすぐ前が台のマスで、その縁まで来ているときだけ跳ぶ
	const FVector Dir = (Goal - Me).GetSafeNormal2D();
	const float Radius = GetCapsuleComponent()->GetScaledCapsuleRadius();
	const FIntPoint Here = G->WorldToCell(Me);
	const FIntPoint Ahead = G->WorldToCell(Me + Dir * (Radius + 30.f));
	if (Ahead == Here || !G->IsInside(Ahead) || G->IsWalkable(Ahead))
	{
		return false;
	}

	// 目の前の台の上面の高さを測る（家具の当たり判定の箱・積んだ壁）
	const float FeetZ = Me.Z - GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	const FVector AheadCenter = G->CellFloorCenter(Ahead);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(OniLeap), false, this);
	Params.AddIgnoredActor(Target);
	FHitResult Hit;
	const FVector From(AheadCenter.X, AheadCenter.Y, FeetZ + LeapMaxHeight + 100.f);
	const FVector To(AheadCenter.X, AheadCenter.Y, FeetZ - 10.f);
	if (!GetWorld()->LineTraceSingleByChannel(Hit, From, To, ECC_Visibility, Params))
	{
		return false;
	}
	const float Rise = Hit.ImpactPoint.Z - FeetZ;
	if (Rise < 20.f || Rise > LeapMaxHeight)
	{
		return false; // 段差でない・高すぎる
	}

	// 真上に跳ぶ（台の縁にぶつからないよう、上に出てから前へ進む。TickLeap）
	LeapTopZ = Hit.ImpactPoint.Z;
	const float Gravity = FMath::Max(-Move->GetGravityZ(), 1.f);
	LaunchCharacter(FVector(0.f, 0.f, FMath::Sqrt(2.f * Gravity * (Rise + LeapClearance))), true, true);
	SetActorRotation(FRotator(0.f, Dir.Rotation().Yaw, 0.f));
	bLeaping = true;
	LeapElapsed = 0.f;
	LeapCooldown = LeapCooldownSeconds;
	++LeapCount;
	UKakurenboSoundSubsystem::Play3D(this, EKakurenboSfx::OniStep, Me, 1.5f, 0.75f);
	return true;
}

void AOniCharacter::TickLeap(float DeltaSeconds)
{
	UCharacterMovementComponent* Move = GetCharacterMovement();
	LeapElapsed += DeltaSeconds;

	// 台の上面より上に出たら、相手の方へ進む
	const float FeetZ = GetActorLocation().Z - GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	if (Target && FeetZ >= LeapTopZ + 5.f)
	{
		const FVector Dir = (Target->GetActorLocation() - GetActorLocation()).GetSafeNormal2D();
		Move->Velocity.X = Dir.X * LeapForwardSpeed;
		Move->Velocity.Y = Dir.Y * LeapForwardSpeed;
	}

	// 着地したら終わり（跳んだ直後のフレームはまだ地面にいる扱いなので少し待つ）。念のため 2 秒で打ち切る
	if ((LeapElapsed > 0.15f && Move->IsMovingOnGround()) || LeapElapsed > 2.f)
	{
		bLeaping = false;
		bHasGoal = false; // 乗った台の上から経路を作り直す
	}
}

void AOniCharacter::TickStunned(float DeltaSeconds)
{
	// ぷるぷる震えて、頭の上で星が回る（動けないことを見せる）
	StunTimer -= DeltaSeconds;
	const float Now = GetWorld()->GetTimeSeconds();
	const float Wobble = 1.f + 0.06f * FMath::Sin(Now * 30.f);
	SetBodySquash(FVector(Wobble, Wobble, 1.f / Wobble));
	for (int32 i = 0; i < StunStars.Num(); ++i)
	{
		const float Angle = Now * 5.f + i * 2.f * UE_PI / StunStars.Num();
		StunStars[i]->SetRelativeLocation(FVector(FMath::Cos(Angle) * 38.f, FMath::Sin(Angle) * 38.f, 120.f));
	}
	if (StunTimer <= 0.f)
	{
		SetStunStarsVisible(false);
		StunImmunityTimer = StunImmunitySeconds;
		ReturnToWander(false);
	}
}

// ---------------------------------------------------------------- うろうろの行き先

bool AOniCharacter::ChooseWanderTarget()
{
	switch (OniType)
	{
	case EOniType::Scout:   return ChooseScoutTarget() || ChooseRandomTarget(0);
	case EOniType::Breaker: return ChooseRandomTarget(6); // 積極的に探さない：近場をうろうろ
	case EOniType::Careful: return ChooseCarefulTarget() || ChooseRandomTarget(0);
	case EOniType::Treasure: return ChooseTreasurePatrolTarget() || ChooseRandomTarget(5);
	case EOniType::Detector: return false; // 見張りの場所から動かない
	default:                return ChooseRandomTarget(0);
	}
}

bool AOniCharacter::ChooseRandomTarget(int32 MaxDistanceCells)
{
	UKakurenboGridSubsystem* G = Grid();
	const FIntPoint Here = GetCurrentCell();
	for (int32 Try = 0; Try < 12; ++Try)
	{
		FIntPoint Cell;
		if (MaxDistanceCells > 0)
		{
			Cell = ClampToGrid(Here + FIntPoint(FMath::RandRange(-MaxDistanceCells, MaxDistanceCells), FMath::RandRange(-MaxDistanceCells, MaxDistanceCells)));
			if (!G->IsWalkable(Cell))
			{
				continue;
			}
		}
		else if (!G->FindRandomFreeCell(Cell))
		{
			continue;
		}
		if (Cell != Here && RequestPathTo(Cell, EPathMode::WalkOnly))
		{
			return true;
		}
	}
	return false;
}

bool AOniCharacter::ChooseScoutTarget()
{
	// 一番長く訪れていない区画へ行く（マップ全体を大まかに回る）
	UKakurenboGridSubsystem* G = Grid();
	if (RegionVisitTime.Num() == 0)
	{
		return false;
	}
	const int32 RegionsX = FMath::DivideAndRoundUp(G->GetSizeX(), RegionSize);
	const FIntPoint Here = GetCurrentCell();
	const int32 HereRegion = (Here.Y / RegionSize) * RegionsX + (Here.X / RegionSize);

	TArray<int32> Order;
	for (int32 i = 0; i < RegionVisitTime.Num(); ++i)
	{
		if (i != HereRegion)
		{
			Order.Add(i);
		}
	}
	// 古い順（同じなら遠い順に近い形でばらつかせる）
	Order.Sort([this](int32 A, int32 B) { return RegionVisitTime[A] < RegionVisitTime[B]; });
	const int32 Candidates = FMath::Min(3, Order.Num());
	for (int32 c = 0; c < Candidates; ++c)
	{
		const int32 Region = Order[FMath::RandRange(0, Candidates - 1)];
		const FIntPoint Origin((Region % RegionsX) * RegionSize, (Region / RegionsX) * RegionSize);
		for (int32 Try = 0; Try < 6; ++Try)
		{
			const FIntPoint Cell = ClampToGrid(Origin + FIntPoint(FMath::RandRange(0, RegionSize - 1), FMath::RandRange(0, RegionSize - 1)));
			if (G->IsWalkable(Cell) && RequestPathTo(Cell, EPathMode::WalkOnly))
			{
				return true;
			}
		}
	}
	return false;
}

bool AOniCharacter::ChooseCarefulTarget()
{
	// 仲間と共有している記録を見て、まだ誰も見ていない広い場所のマスへ行く（歩いて行ける所だけ。壁は壊さない）。
	//   ・自分の担当の区画を先に（仲間と手分けする）
	//   ・細い道（両側が壁・家具）のマスは後回し（広い場所を見渡す方が早く探せる）
	//   ・仲間が向かっているマスの近く・仲間が調べている建物は避ける
	// 広い場所をほぼ見終わったら false を返し、壁に囲まれた場所（空洞）を調べに行く（TryInspectPocket）
	UKakurenboGridSubsystem* G = Grid();
	UKakurenboOniBlackboard* BB = Blackboard();
	if (!BB)
	{
		return false;
	}
	const float Now = GetWorld()->GetTimeSeconds();
	const FIntPoint Here = GetCurrentCell();
	const TArray<bool> MainArea = G->GetMainAreaMask();

	int32 MainCells = 0;
	int32 Unchecked = 0;
	TArray<TPair<float, FIntPoint>> Candidates;
	for (int32 Y = 0; Y < G->GetSizeY(); ++Y)
	{
		for (int32 X = 0; X < G->GetSizeX(); ++X)
		{
			const FIntPoint Cell(X, Y);
			if (!MainArea[Y * G->GetSizeX() + X])
			{
				continue; // 壁・家具・壁に囲まれた空洞
			}
			++MainCells;
			if (BB->IsChecked(Cell, Now, CarefulMemorySeconds))
			{
				continue;
			}
			++Unchecked;
			if (BB->IsNearOthersTarget(this, Cell, 3) || BB->IsInOthersArea(this, Cell))
			{
				continue;
			}
			// 縦横の 4 マスのうち通れないマスの数（2 以上なら細い道・行き止まり）
			int32 Blocked = 0;
			for (const FIntPoint& D : { FIntPoint(1, 0), FIntPoint(-1, 0), FIntPoint(0, 1), FIntPoint(0, -1) })
			{
				Blocked += G->IsWalkable(Cell + D) ? 0 : 1;
			}
			const float Dist = FMath::Sqrt(static_cast<float>(FMath::Square(Cell.X - Here.X) + FMath::Square(Cell.Y - Here.Y)));
			const float Score = Dist + CarefulNarrowPenalty * FMath::Max(0, Blocked - 1)
				+ (IsInCarefulSector(Cell) ? 0.f : CarefulOtherSectorPenalty);
			Candidates.Emplace(Score, Cell);
		}
	}

	// 広い場所の調べ残しが少なくなったら、空洞（調べに行ける物があれば）へ
	if (Unchecked <= FMath::Max(3, FMath::RoundToInt(MainCells * CarefulPocketThreshold)))
	{
		for (const TArray<FIntPoint>& Pocket : G->FindEnclosedPockets())
		{
			if (!Pocket.Contains(Here) && !BB->IsInOthersArea(this, Pocket[0]))
			{
				return false;
			}
		}
	}

	Candidates.Sort([](const TPair<float, FIntPoint>& A, const TPair<float, FIntPoint>& B) { return A.Key < B.Key; });
	for (int32 i = 0; i < FMath::Min(6, Candidates.Num()); ++i)
	{
		const FIntPoint Cell = Candidates[i].Value;
		if (RequestPathTo(Cell, EPathMode::WalkOnly))
		{
			BB->SetReservedTarget(this, Cell);
			return true;
		}
	}
	return false;
}

bool AOniCharacter::ChooseTreasurePatrolTarget()
{
	UKakurenboGridSubsystem* G = Grid();
	// 回る場所：今あるお宝と、このラウンドで取られたお宝のあった場所（取られた後も見回りに来る）
	TArray<FIntPoint> Spots;
	TArray<float> Weights;
	for (TActorIterator<ATreasureActor> It(GetWorld()); It; ++It)
	{
		const FIntPoint Cell = IsValid(*It) ? G->WorldToCell(It->GetActorLocation()) : FIntPoint::ZeroValue;
		if (IsValid(*It) && !Spots.Contains(Cell))
		{
			Spots.Add(Cell);
			Weights.Add(2.f); // 今あるお宝の方を少し多めに回る
		}
	}
	if (const AKakurenboGameMode* GM = GetWorld()->GetAuthGameMode<AKakurenboGameMode>())
	{
		for (const FIntPoint& Taken : GM->GetTakenTreasureCells())
		{
			if (!Spots.Contains(Taken))
			{
				Spots.Add(Taken);
				Weights.Add(1.f);
			}
		}
	}
	if (Spots.Num() == 0)
	{
		bHasPatrolCenter = false;
		return false; // お宝が無い → 近場をうろうろ
	}

	// 回る周りのマス（8 方向。角度の順）
	const int32 R = FMath::Max(1, TreasurePatrolRadiusCells);
	const FIntPoint Ring[8] = { { R, 0 }, { R, R }, { 0, R }, { -R, R }, { -R, 0 }, { -R, -R }, { 0, -R }, { R, -R } };

	// 何周か回ったら別の場所へ（今の場所とは違う所を重みつきで選ぶ）。始めは自分に一番近い周りのマスから、ランダムな向きで回る
	if (!bHasPatrolCenter || !Spots.Contains(PatrolCenter) || PatrolSteps >= TreasurePatrolLaps * 8)
	{
		float Total = 0.f;
		for (int32 i = 0; i < Spots.Num(); ++i)
		{
			Total += (bHasPatrolCenter && Spots.Num() > 1 && Spots[i] == PatrolCenter) ? 0.f : Weights[i];
		}
		float Pick = FMath::FRandRange(0.f, Total);
		FIntPoint Next = Spots[0];
		for (int32 i = 0; i < Spots.Num(); ++i)
		{
			const float W = (bHasPatrolCenter && Spots.Num() > 1 && Spots[i] == PatrolCenter) ? 0.f : Weights[i];
			if (W > 0.f && Pick <= W)
			{
				Next = Spots[i];
				break;
			}
			Pick -= W;
		}
		PatrolCenter = Next;
		bHasPatrolCenter = true;
		PatrolSteps = 0;
		PatrolDirection = FMath::RandBool() ? 1 : -1;
		const FIntPoint Center = Next;
		float BestDistSq = TNumericLimits<float>::Max();
		for (int32 i = 0; i < 8; ++i)
		{
			const float DistSq = FVector::DistSquared2D(GetActorLocation(), G->CellFloorCenter(Center + Ring[i]));
			if (DistSq < BestDistSq)
			{
				BestDistSq = DistSq;
				PatrolIndex = i;
			}
		}
	}

	const FIntPoint Center = PatrolCenter;
	const FIntPoint Here = GetCurrentCell();
	for (int32 Try = 0; Try < 8; ++Try)
	{
		const int32 Index = ((PatrolIndex + PatrolDirection * Try) % 8 + 8) % 8;
		const FIntPoint Cell = ClampToGrid(Center + Ring[Index]);
		// お宝（のあった場所）へは最短で向かう。途中を壁で塞がれていれば壊して進む（壊す力は弱いので時間はかかる）
		if (Cell != Here && G->IsWalkable(Cell) && RequestPathTo(Cell, EPathMode::Shortest))
		{
			PatrolIndex = ((Index + PatrolDirection) % 8 + 8) % 8;
			PatrolSteps += Try + 1;
			return true;
		}
	}
	PatrolSteps = TreasurePatrolLaps * 8; // このお宝の周りは回れない → 次は別のお宝
	return false;
}

bool AOniCharacter::TryInspectPocket()
{
	if (PocketInspectChance <= 0.f || FMath::FRand() > PocketInspectChance)
	{
		return false;
	}
	// 壁に囲まれて歩いては入れない空きマス（空洞）のうち、一番近いマスを調べに行く。
	// 自分がいる空洞と、慎重鬼の仲間が調べている建物の空洞は除く。慎重鬼は自分の担当の区画の空洞を先に
	const TArray<TArray<FIntPoint>> Pockets = Grid()->FindEnclosedPockets();
	const FVector Here = GetActorLocation();
	const FIntPoint HereCell = GetCurrentCell();
	const UKakurenboOniBlackboard* BB = Blackboard();
	const bool bCareful = OniType == EOniType::Careful;
	float BestScore = TNumericLimits<float>::Max();
	FIntPoint Best = FIntPoint::ZeroValue;
	for (const TArray<FIntPoint>& Pocket : Pockets)
	{
		if (Pocket.Contains(HereCell))
		{
			continue;
		}
		if (bCareful && BB)
		{
			bool bTaken = false;
			for (const FIntPoint& Cell : Pocket)
			{
				if (BB->IsInOthersArea(this, Cell) || BB->IsNearOthersTarget(this, Cell, 1))
				{
					bTaken = true; // 仲間が調べている（向かっている）建物
					break;
				}
			}
			if (bTaken)
			{
				continue;
			}
		}
		for (const FIntPoint& Cell : Pocket)
		{
			float Score = FVector::Dist2D(Here, Grid()->CellFloorCenter(Cell)) / Grid()->GetCellSize();
			if (bCareful && !IsInCarefulSector(Cell))
			{
				Score += CarefulOtherSectorPenalty;
			}
			if (Score < BestScore)
			{
				BestScore = Score;
				Best = Cell;
			}
		}
	}
	if (BestScore == TNumericLimits<float>::Max())
	{
		return false;
	}
	StartInspect(Best);
	if (State == EOniState::Inspect && bCareful)
	{
		ReserveStructure(Best);
		if (UKakurenboOniBlackboard* MutableBB = Blackboard())
		{
			MutableBB->SetReservedTarget(this, Best);
		}
	}
	return State == EOniState::Inspect;
}

bool AOniCharacter::TryEscapeEnclosure()
{
	UKakurenboGridSubsystem* G = Grid();
	if (G->IsInMainArea(GetCurrentCell()))
	{
		return false;
	}
	// 一番広い空き地のどこかへ、他に道が無ければ壁を壊して出る（出入り口の前を壁で囲まれたときなど）
	FRandomStream Stream(FMath::Rand());
	const TArray<FIntPoint> Cells = G->FindRandomFreeCells(1, {}, 0.f, Stream);
	return Cells.Num() > 0 && RequestPathTo(Cells[0], EPathMode::BreakIfNeeded);
}

void AOniCharacter::ReserveStructure(const FIntPoint& Cell)
{
	if (UKakurenboOniBlackboard* BB = Blackboard())
	{
		BB->SetReservedArea(this, Grid()->GetStructureAround(Cell));
	}
}

// ---------------------------------------------------------------- 感覚

void AOniCharacter::FoundTarget(FName Reason)
{
	bActive = false;
	FoundReason = Reason;
	GetCharacterMovement()->StopMovementImmediately();
	ResetBodyScale();
	if (Target)
	{
		SetActorRotation(FRotator(0.f, (Target->GetActorLocation() - GetActorLocation()).Rotation().Yaw, 0.f));
	}
	UE_LOG(LogTemp, Log, TEXT("Oni %s (%s) found the hider by %s"), *GetName(), *UEnum::GetValueAsString(OniType), *Reason.ToString());
	OnFoundHider.Broadcast();
}

bool AOniCharacter::IsTouchingTarget() const
{
	if (!Target)
	{
		return false;
	}
	// 2 つのカプセル（縦長の円柱とみなす）が、横方向にも縦方向にも重なりかけているか
	const UCapsuleComponent* Mine = GetCapsuleComponent();
	const UCapsuleComponent* Theirs = Target->GetCapsuleComponent();
	const FVector A = GetActorLocation();
	const FVector B = Target->GetActorLocation();
	const float HorizontalLimit = Mine->GetScaledCapsuleRadius() + Theirs->GetScaledCapsuleRadius() + TouchMargin;
	const float VerticalLimit = Mine->GetScaledCapsuleHalfHeight() + Theirs->GetScaledCapsuleHalfHeight() + TouchMargin;
	return FVector::DistSquared2D(A, B) <= FMath::Square(HorizontalLimit) && FMath::Abs(A.Z - B.Z) <= VerticalLimit;
}

bool AOniCharacter::CanSeeTarget() const
{
	if (!Target)
	{
		return false;
	}

	const FVector Eye = GetActorLocation() + FVector(0.f, 0.f, EyeHeight);
	const FVector Forward2D = GetActorForwardVector().GetSafeNormal2D();
	const float CosHalfAngle = FMath::Cos(FMath::DegreesToRadians(SightHalfAngle));

	FCollisionQueryParams Params(SCENE_QUERY_STAT(OniSight), false, this);
	Params.AddIgnoredActor(Target);

	TArray<FVector> Points;
	Target->GetSightTargetPoints(Points);
	for (const FVector& P : Points)
	{
		const FVector ToP = P - Eye;
		const bool bClose = ToP.Size2D() <= CloseSenseRadius;
		if (!bClose)
		{
			if (ToP.Size() > SightRadius)
			{
				continue;
			}
			// 左右の角度だけで判定する（上下はよく見える）
			if (FVector::DotProduct(Forward2D, ToP.GetSafeNormal2D()) < CosHalfAngle)
			{
				continue;
			}
		}
		// 煙幕の中・向こう側は見えない
		if (ASmokeCloud::IsSightBlocked(GetWorld(), Eye, P))
		{
			continue;
		}
		// 間に壁などがなければ見えている
		if (!GetWorld()->LineTraceTestByChannel(Eye, P, ECC_Visibility, Params))
		{
			return true;
		}
	}
	return false;
}

bool AOniCharacter::FindVisibleWallCell(FIntPoint& OutCell) const
{
	const UKakurenboGridSubsystem* G = Grid();
	const FVector Eye = GetActorLocation() + FVector(0.f, 0.f, EyeHeight);
	const FVector Forward2D = GetActorForwardVector().GetSafeNormal2D();
	const float CosHalfAngle = FMath::Cos(FMath::DegreesToRadians(SightHalfAngle));
	FCollisionQueryParams Params(SCENE_QUERY_STAT(OniWallHunt), false, this);
	if (Target)
	{
		Params.AddIgnoredActor(Target);
	}

	float BestDistSq = TNumericLimits<float>::Max();
	for (int32 Y = 0; Y < G->GetSizeY(); ++Y)
	{
		for (int32 X = 0; X < G->GetSizeX(); ++X)
		{
			const FIntPoint Cell(X, Y);
			if (G->GetColumnHeight(Cell) == 0)
			{
				continue;
			}
			const FVector BlockCenter = G->CellToWorld(Cell, 0);
			const FVector ToBlock = BlockCenter - Eye;
			const float DistSq = ToBlock.SizeSquared2D();
			if (DistSq > FMath::Square(SightRadius) || DistSq >= BestDistSq)
			{
				continue;
			}
			if (FVector::DotProduct(Forward2D, ToBlock.GetSafeNormal2D()) < CosHalfAngle)
			{
				continue;
			}
			// 視線がそのマスのブロックに当たれば「見えている」
			FHitResult Hit;
			if (GetWorld()->LineTraceSingleByChannel(Hit, Eye, BlockCenter, ECC_Visibility, Params))
			{
				const APlaceableBlock* Block = Cast<APlaceableBlock>(Hit.GetActor());
				if (Block && Block->Cell == Cell)
				{
					BestDistSq = DistSq;
					OutCell = Cell;
				}
			}
		}
	}
	return BestDistSq < TNumericLimits<float>::Max();
}

void AOniCharacter::HearNoise(const FVector& NoiseLocation, float Loudness, EKakurenboNoise Kind)
{
	UKakurenboGridSubsystem* G = Grid();
	// 音の種類ごとに別の距離で聞く（スピード鬼はおとりにだまされない・パワー鬼はおとりだけに寄っていく・宝物鬼は足音に敏感）
	const float Radius = Kind == EKakurenboNoise::Decoy ? DecoyHearingRadius
		: Kind == EKakurenboNoise::Step ? StepHearingRadius
		: HearingRadius;
	if (!bActive || !G || Radius <= 0.f || State == EOniState::Stunned || OniType == EOniType::Detector)
	{
		return; // 探知鬼は見張りの場所から動かない
	}
	// 追いかけている間は、音より目で追うのを優先する
	if (GetIntent() == EOniState::Chase)
	{
		return;
	}
	const float Dist = FVector::Dist(NoiseLocation, GetActorLocation());
	const float Range = Radius * Loudness;
	if (Dist > Range)
	{
		return;
	}

	// 遠い音ほど場所がずれて聞こえる
	const float ErrorCm = NoiseInaccuracyCells * G->GetCellSize() * (Dist / FMath::Max(Range, 1.f));
	const FVector2D Offset = FMath::RandPointInCircle(ErrorCm);
	const FIntPoint Cell = ClampToGrid(G->WorldToCell(NoiseLocation + FVector(Offset.X, Offset.Y, 0.f)));

	// すでにその辺りへ向かっているなら、経路は作り直さない（連打のたびに迷わないように）
	const bool bAlreadyGoingThere = GetIntent() == EOniState::Investigate && bHasGoal
		&& FMath::Abs(Cell.X - NoiseCell.X) + FMath::Abs(Cell.Y - NoiseCell.Y) <= 2;
	if (bAlreadyGoingThere)
	{
		return;
	}
	NoiseCell = Cell;

	// プレイヤーの音に気づいた：「ン？」とその鬼の場所から鳴らす（どっちの鬼に気づかれたか、音でわかる）
	if (Kind != EKakurenboNoise::Decoy && GetIntent() != EOniState::Investigate)
	{
		const float Now = GetWorld()->GetTimeSeconds();
		if (Now - LastNoticeSoundTime > NoticeSoundCooldown)
		{
			LastNoticeSoundTime = Now;
			++NoticeSoundCount;
			UKakurenboSoundSubsystem::Play3D(this, EKakurenboSfx::OniNotice, GetActorLocation() + FVector(0.f, 0.f, 60.f), 1.f, StepPitch);
		}
	}

	if (State == EOniState::Attack)
	{
		// 攻撃が終わったら音の方へ向かう
		StateBeforeAttack = EOniState::Investigate;
		IntentElapsed = 0.f;
		GoalCell = Cell;
		CurrentPathMode = EPathMode::BreakIfNeeded;
		bHasGoal = true;
		return;
	}
	StartInvestigate(Cell);
}

// ---------------------------------------------------------------- 移動

FIntPoint AOniCharacter::ClampToGrid(const FIntPoint& Cell) const
{
	const UKakurenboGridSubsystem* G = Grid();
	return FIntPoint(FMath::Clamp(Cell.X, 0, G->GetSizeX() - 1), FMath::Clamp(Cell.Y, 0, G->GetSizeY() - 1));
}

FIntPoint AOniCharacter::GetCurrentCell() const
{
	return ClampToGrid(Grid()->WorldToCell(GetActorLocation()));
}

bool AOniCharacter::RequestPathTo(const FIntPoint& Goal, EPathMode Mode)
{
	UKakurenboGridSubsystem* G = Grid();
	if (!G)
	{
		return false;
	}
	const FIntPoint Start = GetCurrentCell();
	TArray<FIntPoint> NewPath;

	// 1) 壁を「通れない」ものとして探す（入り口があればそこから入る）
	FKakurenboPathGrid WalkGrid = G->BuildWalkGrid();
	if (OniType == EOniType::Careful && State == EOniState::Wander)
	{
		// 慎重鬼はうろうろ中、細い道（縦横の 2 方向以上が壁・家具）を通るのを嫌がる（遠回りでも広い所を通る）
		const FKakurenboPathGrid Base = WalkGrid;
		for (int32 Y = 0; Y < WalkGrid.SizeY; ++Y)
		{
			for (int32 X = 0; X < WalkGrid.SizeX; ++X)
			{
				const FIntPoint P(X, Y);
				if (!Base.IsFree(P) || P == Goal)
				{
					continue;
				}
				int32 Blocked = 0;
				for (const FIntPoint& D : { FIntPoint(1, 0), FIntPoint(-1, 0), FIntPoint(0, 1), FIntPoint(0, -1) })
				{
					Blocked += Base.IsFree(P + D) ? 0 : 1;
				}
				if (Blocked >= 2)
				{
					WalkGrid.SetExtra(P, 3.f);
				}
			}
		}
	}
	WalkGrid.SetExtra(Start, 0.f); // 自分のいるマスは常に通れる扱い
	bool bFound = false;
	if (Mode == EPathMode::Shortest)
	{
		// 最短経路：遠回りするより壊す方が近ければ、途中の壁を壊して進む（宝物鬼がお宝へまっすぐ向かう）
		FKakurenboPathGrid ShortGrid = G->BuildPathGrid(AttackDamage, TreasurePathCostPerAttack);
		ShortGrid.SetExtra(Start, 0.f);
		bFound = KakurenboPathfinding::FindPath(ShortGrid, Start, Goal, NewPath);
	}
	else
	{
		bFound = KakurenboPathfinding::FindPath(WalkGrid, Start, Goal, NewPath);
	}

	// 2) 他に行く道が無いときだけ、壁を壊す経路を使う（目の前の壁から壊していく）
	if (!bFound && Mode == EPathMode::BreakIfNeeded)
	{
		FKakurenboPathGrid BreakGrid = G->BuildPathGrid(AttackDamage, PathCostPerAttack);
		BreakGrid.SetExtra(Start, 0.f);
		bFound = KakurenboPathfinding::FindPath(BreakGrid, Start, Goal, NewPath);
	}
	if (!bFound)
	{
		return false;
	}

	Path = MoveTemp(NewPath);
	PathIndex = 0;
	GoalCell = Goal;
	bHasGoal = true;
	CurrentPathMode = Mode;
	PathGridVersion = G->GetGridVersion();
	RepathTimer = RepathInterval;
	StuckTimer = 0.f;
	LastProgressLocation = GetActorLocation();
	return true;
}

AOniCharacter::EFollowResult AOniCharacter::FollowPath(float DeltaSeconds)
{
	UKakurenboGridSubsystem* G = Grid();

	// 配置が変わった（壁が壊れて道ができた等）・一定時間たった → 経路を作り直す
	RepathTimer -= DeltaSeconds;
	if (PathGridVersion != G->GetGridVersion() || RepathTimer <= 0.f)
	{
		if (!RequestPathTo(GoalCell, CurrentPathMode))
		{
			return EFollowResult::Failed;
		}
	}

	if (PathIndex >= Path.Num())
	{
		return EFollowResult::Arrived;
	}

	const FIntPoint Next = Path[PathIndex];
	const FVector NextCenter = G->CellFloorCenter(Next);
	const FVector Loc = GetActorLocation();
	const float Dist2D = FVector::Dist2D(Loc, NextCenter);

	// 次のマスに壁がある（他に道が無かった）→ 目の前まで来たら壊す
	if (G->GetColumnHeight(Next) > 0)
	{
		if (Dist2D <= G->GetCellSize() * 1.1f)
		{
			BeginAttack(Next);
			return EFollowResult::Attacking;
		}
	}
	// 速いほど 1 フレームで進む距離が大きいので、「着いた」とみなす距離も広げる（行き過ぎて往復しないように）
	else if (Dist2D <= FMath::Max(ReachDistance, GetCharacterMovement()->MaxWalkSpeed * DeltaSeconds * 2.f))
	{
		++PathIndex;
		return PathIndex >= Path.Num() ? EFollowResult::Arrived : EFollowResult::Moving;
	}

	AddMovementInput((NextCenter - Loc).GetSafeNormal2D());

	// 引っかかり検出：しばらく進めていなければ経路を作り直す
	StuckTimer += DeltaSeconds;
	if (StuckTimer >= 1.f)
	{
		const bool bStuck = FVector::Dist2D(Loc, LastProgressLocation) < 20.f;
		StuckTimer = 0.f;
		LastProgressLocation = Loc;
		if (bStuck)
		{
			RepathTimer = 0.f;
		}
	}
	return EFollowResult::Moving;
}

void AOniCharacter::FaceTowards(const FVector& Location, float DeltaSeconds, float DegreesPerSecond)
{
	const FVector To = Location - GetActorLocation();
	if (To.SizeSquared2D() < 1.f)
	{
		return; // ほぼ真上・真下：向きを決めない（くるくる回らないように）
	}
	const FRotator Desired(0.f, To.Rotation().Yaw, 0.f);
	SetActorRotation(FMath::RInterpConstantTo(GetActorRotation(), Desired, DeltaSeconds, DegreesPerSecond));
}

// ---------------------------------------------------------------- 攻撃

void AOniCharacter::BeginAttack(const FIntPoint& WallCell)
{
	if (State != EOniState::Attack)
	{
		StateBeforeAttack = State;
	}
	AttackCell = WallCell;
	AttackTimer = 0.f;
	bAttackFired = false;
	PlayingAnim = nullptr; // 攻撃のアニメーションを最初から再生し直す
	SetState(EOniState::Attack);

	// 慎重鬼：壊している壁とつながった建物には、仲間の慎重鬼は来ない
	if (OniType == EOniType::Careful)
	{
		ReserveStructure(WallCell);
	}
}

void AOniCharacter::ResetBodyScale()
{
	SetBodySquash(FVector::OneVector);
}

void AOniCharacter::TickAttack(float DeltaSeconds)
{
	UKakurenboGridSubsystem* G = Grid();
	FaceTowards(G->CellFloorCenter(AttackCell), DeltaSeconds, 720.f);

	AttackTimer += DeltaSeconds;

	// 溜め中は体を縮める（見た目の予兆）
	const float Windup = FMath::Clamp(AttackTimer / AttackWindup, 0.f, 1.f);
	const float Squash = bAttackFired ? 1.f : 1.f - 0.25f * Windup;
	SetBodySquash(FVector(1.f / FMath::Sqrt(Squash), 1.f / FMath::Sqrt(Squash), Squash));

	if (!bAttackFired && AttackTimer >= AttackWindup)
	{
		bAttackFired = true;
		int32 Destroyed = 0;
		UKakurenboFxSubsystem* Fx = GetWorld()->GetSubsystem<UKakurenboFxSubsystem>();
		if (bSingleTargetAttack)
		{
			// 目の前の壁 1 個だけ（壁の破片と音はグリッドからの通知で GameMode が出す）
			Destroyed = G->DamageBottomBlock(AttackCell, AttackDamage);
			if (Fx)
			{
				Fx->Ring(G->CellFloorCenter(AttackCell), 20.f, G->GetCellSize() * 0.6f, 0.2f, FLinearColor(1.f, 0.35f, 0.15f), 8.f);
			}
		}
		else
		{
			// 範囲攻撃：足元から衝撃波が広がる
			Destroyed = G->DamageBlocksInRadius(GetActorLocation(), AttackRadius, AttackDamage);
			if (Fx)
			{
				Fx->Ring(GetActorLocation(), 30.f, AttackRadius, 0.25f, FLinearColor(1.f, 0.35f, 0.15f), 12.f);
			}
		}
		if (Destroyed > 0)
		{
			OnDestroyedWalls.Broadcast(Destroyed);
		}
	}

	if (AttackTimer >= AttackWindup + AttackRecovery)
	{
		if (G->GetColumnHeight(AttackCell) > 0)
		{
			// まだ残っている → もう一度
			AttackTimer = 0.f;
			bAttackFired = false;
			PlayingAnim = nullptr;
			return;
		}

		// 壊し終わった → 元の行動に戻る（経路は次の Tick で作り直す）
		ResetBodyScale();
		SetState(StateBeforeAttack);
		if (State == EOniState::Chase)
		{
			bHasGoal = false;
		}
		else if (bHasGoal && !RequestPathTo(GoalCell, CurrentPathMode))
		{
			bHasGoal = false;
			BeginLookAround();
			SearchTimer = SearchDuration;
		}
	}
}

// ---------------------------------------------------------------- デバッグ

void AOniCharacter::DrawDebug() const
{
	UKakurenboGridSubsystem* G = Grid();
	for (int32 i = PathIndex; i < Path.Num(); ++i)
	{
		const FVector P = G->CellFloorCenter(Path[i]) + FVector(0, 0, 5);
		DrawDebugPoint(GetWorld(), P, 8.f, G->GetColumnHeight(Path[i]) > 0 ? FColor::Red : FColor::Yellow, false, 0.f);
	}
	if (HearingRadius > 0.f)
	{
		DrawDebugCircle(GetWorld(), GetActorLocation(), HearingRadius, 48, FColor(255, 255, 255, 60), false, 0.f, 0, 2.f, FVector(1, 0, 0), FVector(0, 1, 0), false);
	}
}
