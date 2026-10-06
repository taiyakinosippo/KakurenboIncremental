#include "OniCharacter.h"

#include "Components/CapsuleComponent.h"
#include "Components/SpotLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GridPathfinder.h"
#include "HiderCharacter.h"
#include "KakurenboFx.h"
#include "KakurenboGridSubsystem.h"
#include "KakurenboLibrary.h"
#include "KakurenboOniBlackboard.h"
#include "KakurenboSoundSubsystem.h"
#include "PlaceableBlock.h"
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
}

void AOniCharacter::BeginPlay()
{
	Super::BeginPlay();
	BaseBodyScale = FVector(0.76f * BodyScaleMultiplier, 0.76f * BodyScaleMultiplier, 1.9f);
	ResetBodyScale();
	UKakurenboLibrary::ApplyColor(BodyMesh, BodyColor);
	UKakurenboLibrary::ApplyColor(FaceMesh, FLinearColor(0.02f, 0.02f, 0.02f));
	for (UStaticMeshComponent* Star : StunStars)
	{
		UKakurenboLibrary::ApplyColor(Star, FLinearColor(1.f, 0.9f, 0.2f));
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
}

void AOniCharacter::Deactivate()
{
	bActive = false;
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
	// 見つかったことを音で知らせる（方向は画面の「！」で分かるので、画面全体で鳴らす）
	UKakurenboSoundSubsystem::Play2D(this, EKakurenboSfx::Alert, 0.9f);
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
	StunTimer = Seconds;
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

	IntentElapsed += DeltaSeconds;
	SightCooldown = FMath::Max(0.f, SightCooldown - DeltaSeconds);

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
	case EOniState::Wander:      TickWander(DeltaSeconds); break;
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

	// 慎重鬼：自分の周り 3×3 マスを「調べた」と仲間と共有する
	if (OniType == EOniType::Careful)
	{
		if (UKakurenboOniBlackboard* BB = Blackboard())
		{
			for (int32 DY = -1; DY <= 1; ++DY)
			{
				for (int32 DX = -1; DX <= 1; ++DX)
				{
					BB->MarkChecked(Here + FIntPoint(DX, DY), Now);
				}
			}
		}
	}
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
		}
		return;
	}

	// 次の行き先を決めるまで、その場で軽く見回す
	TickLookAround(DeltaSeconds, 35.f);
	IdleTimer -= DeltaSeconds;
	if (IdleTimer <= 0.f)
	{
		if (!TryInspectPocket() && !ChooseWanderTarget())
		{
			IdleTimer = 1.f; // 行き先が見つからなければ少し待って再挑戦
		}
	}
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
	ChaseRepathTimer -= DeltaSeconds;
	if (!bHasGoal || TargetCell != GoalCell || ChaseRepathTimer <= 0.f)
	{
		ChaseRepathTimer = ChaseRepathInterval;
		if (!RequestPathTo(TargetCell, EPathMode::BreakIfNeeded))
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

void AOniCharacter::TickStunned(float DeltaSeconds)
{
	// ぷるぷる震えて、頭の上で星が回る（動けないことを見せる）
	StunTimer -= DeltaSeconds;
	const float Now = GetWorld()->GetTimeSeconds();
	const float Wobble = 1.f + 0.06f * FMath::Sin(Now * 30.f);
	BodyMesh->SetRelativeScale3D(FVector(BaseBodyScale.X * Wobble, BaseBodyScale.Y * Wobble, BaseBodyScale.Z / Wobble));
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
			if (G->GetColumnHeight(Cell) > 0)
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
			if (G->GetColumnHeight(Cell) == 0 && RequestPathTo(Cell, EPathMode::WalkOnly))
			{
				return true;
			}
		}
	}
	return false;
}

bool AOniCharacter::ChooseCarefulTarget()
{
	// 仲間と共有している記録を見て、まだ誰も調べていない一番近いマスへ行く。
	// 仲間が向かっているマスの近くは避ける。壁に囲まれていれば壊してでも入る（隅々まで調べる）
	UKakurenboGridSubsystem* G = Grid();
	UKakurenboOniBlackboard* BB = Blackboard();
	if (!BB)
	{
		return false;
	}
	const float Now = GetWorld()->GetTimeSeconds();
	const FVector Here = GetActorLocation();

	TArray<TPair<float, FIntPoint>> Candidates;
	for (int32 Y = 0; Y < G->GetSizeY(); ++Y)
	{
		for (int32 X = 0; X < G->GetSizeX(); ++X)
		{
			const FIntPoint Cell(X, Y);
			if (G->GetColumnHeight(Cell) > 0 || BB->IsChecked(Cell, Now, CarefulMemorySeconds) || BB->IsNearOthersTarget(this, Cell, 3))
			{
				continue;
			}
			Candidates.Emplace(FVector::DistSquared2D(Here, G->CellFloorCenter(Cell)), Cell);
		}
	}
	Candidates.Sort([](const TPair<float, FIntPoint>& A, const TPair<float, FIntPoint>& B) { return A.Key < B.Key; });

	for (int32 i = 0; i < FMath::Min(5, Candidates.Num()); ++i)
	{
		const FIntPoint Cell = Candidates[i].Value;
		if (RequestPathTo(Cell, EPathMode::BreakIfNeeded))
		{
			BB->SetReservedTarget(this, Cell);
			return true;
		}
	}
	return false;
}

bool AOniCharacter::TryInspectPocket()
{
	if (PocketInspectChance <= 0.f || FMath::FRand() > PocketInspectChance)
	{
		return false;
	}
	// 壁に囲まれて歩いては入れない空きマス（空洞）のうち、一番近いマスを調べに行く
	const TArray<TArray<FIntPoint>> Pockets = Grid()->FindEnclosedPockets();
	const FVector Here = GetActorLocation();
	float BestDistSq = TNumericLimits<float>::Max();
	FIntPoint Best = FIntPoint::ZeroValue;
	for (const TArray<FIntPoint>& Pocket : Pockets)
	{
		for (const FIntPoint& Cell : Pocket)
		{
			const float DistSq = FVector::DistSquared2D(Here, Grid()->CellFloorCenter(Cell));
			if (DistSq < BestDistSq)
			{
				BestDistSq = DistSq;
				Best = Cell;
			}
		}
	}
	if (BestDistSq == TNumericLimits<float>::Max())
	{
		return false;
	}
	StartInspect(Best);
	return State == EOniState::Inspect;
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

void AOniCharacter::HearNoise(const FVector& NoiseLocation, float Loudness)
{
	UKakurenboGridSubsystem* G = Grid();
	if (!bActive || !G || HearingRadius <= 0.f || State == EOniState::Stunned)
	{
		return;
	}
	// 追いかけている間は、音より目で追うのを優先する
	if (GetIntent() == EOniState::Chase)
	{
		return;
	}
	const float Dist = FVector::Dist(NoiseLocation, GetActorLocation());
	const float Range = HearingRadius * Loudness;
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
	WalkGrid.SetExtra(Start, 0.f); // 自分のいるマスは常に通れる扱い
	bool bFound = KakurenboPathfinding::FindPath(WalkGrid, Start, Goal, NewPath);

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
	SetState(EOniState::Attack);
}

void AOniCharacter::ResetBodyScale()
{
	BodyMesh->SetRelativeScale3D(BaseBodyScale);
}

void AOniCharacter::TickAttack(float DeltaSeconds)
{
	UKakurenboGridSubsystem* G = Grid();
	FaceTowards(G->CellFloorCenter(AttackCell), DeltaSeconds, 720.f);

	AttackTimer += DeltaSeconds;

	// 溜め中は体を縮める（見た目の予兆）
	const float Windup = FMath::Clamp(AttackTimer / AttackWindup, 0.f, 1.f);
	const float Squash = bAttackFired ? 1.f : 1.f - 0.25f * Windup;
	BodyMesh->SetRelativeScale3D(FVector(BaseBodyScale.X / FMath::Sqrt(Squash), BaseBodyScale.Y / FMath::Sqrt(Squash), BaseBodyScale.Z * Squash));

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
