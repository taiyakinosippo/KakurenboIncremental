#include "OniCharacter.h"

#include "Components/CapsuleComponent.h"
#include "Components/SpotLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GridPathfinder.h"
#include "HiderCharacter.h"
#include "KakurenboGridSubsystem.h"
#include "KakurenboLibrary.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	constexpr float EyeHeight = 60.f;      // カプセル中心から目までの高さ
	constexpr float ReachDistance = 25.f;  // マスの中心にこれだけ近づいたら「着いた」
	constexpr float RepathInterval = 1.f;  // 定期的に経路を作り直す間隔（秒）
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
	BodyMesh->SetRelativeScale3D(FVector(0.76f, 0.76f, 1.9f));

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
}

UKakurenboGridSubsystem* AOniCharacter::Grid() const
{
	return GetWorld() ? GetWorld()->GetSubsystem<UKakurenboGridSubsystem>() : nullptr;
}

void AOniCharacter::BeginPlay()
{
	Super::BeginPlay();
	UKakurenboLibrary::ApplyColor(BodyMesh, FLinearColor(0.9f, 0.1f, 0.08f));
	UKakurenboLibrary::ApplyColor(FaceMesh, FLinearColor(0.02f, 0.02f, 0.02f));
}

void AOniCharacter::Activate(AHiderCharacter* InTarget)
{
	Target = InTarget;
	bActive = true;
	LastProgressLocation = GetActorLocation();

	// 懐中電灯を視界と同じ形にする
	Flashlight->SetOuterConeAngle(SightHalfAngle);
	Flashlight->SetInnerConeAngle(SightHalfAngle * 0.7f);
	Flashlight->SetAttenuationRadius(SightRadius);

	SetState(EOniState::Wander);
	IdleTimer = 0.5f;
}

void AOniCharacter::SetState(EOniState NewState)
{
	State = NewState;
	GetCharacterMovement()->MaxWalkSpeed = (State == EOniState::Investigate) ? InvestigateSpeed : WanderSpeed;
	if (State == EOniState::Attack)
	{
		GetCharacterMovement()->StopMovementImmediately();
	}
}

// ---------------------------------------------------------------- Tick

void AOniCharacter::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (!bActive || !Grid() || !Grid()->IsConfigured())
	{
		return;
	}

	// 視界チェックは 0.1 秒ごと（毎フレームでなくても十分）
	SenseTimer += DeltaSeconds;
	if (SenseTimer >= 0.1f)
	{
		SenseTimer = 0.f;
		if (CanSeeTarget())
		{
			bActive = false;
			GetCharacterMovement()->StopMovementImmediately();
			if (Target)
			{
				SetActorRotation(FRotator(0.f, (Target->GetActorLocation() - GetActorLocation()).Rotation().Yaw, 0.f));
			}
			OnFoundHider.Broadcast();
			return;
		}
	}

	switch (State)
	{
	case EOniState::Attack:
		TickAttack(DeltaSeconds);
		break;

	case EOniState::Wander:
		if (bHasGoal)
		{
			FollowPath(DeltaSeconds);
		}
		else
		{
			IdleTimer -= DeltaSeconds;
			AddActorWorldRotation(FRotator(0.f, 60.f * DeltaSeconds, 0.f));
			if (IdleTimer <= 0.f)
			{
				PickWanderTarget();
			}
		}
		break;

	case EOniState::Investigate:
		if (bHasGoal)
		{
			FollowPath(DeltaSeconds);
		}
		else
		{
			// 音のした場所で見回す
			SearchTimer -= DeltaSeconds;
			AddActorWorldRotation(FRotator(0.f, 150.f * DeltaSeconds, 0.f));
			if (SearchTimer <= 0.f)
			{
				SetState(EOniState::Wander);
				IdleTimer = 0.f;
			}
		}
		break;
	}

	if (bDrawDebug)
	{
		DrawDebug();
	}
}

// ---------------------------------------------------------------- 感覚

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

void AOniCharacter::HearNoise(const FVector& NoiseLocation, float Loudness)
{
	UKakurenboGridSubsystem* G = Grid();
	if (!bActive || !G)
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
	const float ErrorCm = NoiseInaccuracyCells * G->GetCellSize() * (Dist / Range);
	const FVector2D Offset = FMath::RandPointInCircle(ErrorCm);
	FIntPoint Cell = G->WorldToCell(NoiseLocation + FVector(Offset.X, Offset.Y, 0.f));
	Cell.X = FMath::Clamp(Cell.X, 0, G->GetSizeX() - 1);
	Cell.Y = FMath::Clamp(Cell.Y, 0, G->GetSizeY() - 1);

	// すでにその辺りへ向かっているなら、経路は作り直さない（連打のたびに迷わないように）
	const bool bAlreadyGoingThere = (State == EOniState::Investigate || StateBeforeAttack == EOniState::Investigate)
		&& bHasGoal && FMath::Abs(Cell.X - NoiseCell.X) + FMath::Abs(Cell.Y - NoiseCell.Y) <= 2;
	if (bAlreadyGoingThere)
	{
		return;
	}
	NoiseCell = Cell;

	if (State == EOniState::Attack)
	{
		// 攻撃が終わったら音の方へ向かう
		StateBeforeAttack = EOniState::Investigate;
		GoalCell = Cell;
		bHasGoal = true;
		return;
	}

	SetState(EOniState::Investigate);
	if (!RequestPathTo(Cell, true))
	{
		bHasGoal = false;
		SearchTimer = SearchDuration;
	}
}

// ---------------------------------------------------------------- 移動

bool AOniCharacter::RequestPathTo(const FIntPoint& Goal, bool bAllowWalls)
{
	UKakurenboGridSubsystem* G = Grid();
	if (!G)
	{
		return false;
	}

	FKakurenboPathGrid PathGrid = G->BuildPathGrid(AttackDamage, PathCostPerAttack);
	FIntPoint Start = G->WorldToCell(GetActorLocation());
	Start.X = FMath::Clamp(Start.X, 0, G->GetSizeX() - 1);
	Start.Y = FMath::Clamp(Start.Y, 0, G->GetSizeY() - 1);
	PathGrid.SetExtra(Start, 0.f); // 自分のいるマスは常に通れる扱い

	TArray<FIntPoint> NewPath;
	if (!KakurenboPathfinding::FindPath(PathGrid, Start, Goal, NewPath))
	{
		return false;
	}
	if (!bAllowWalls && KakurenboPathfinding::PathContainsWalls(PathGrid, NewPath))
	{
		return false;
	}

	Path = MoveTemp(NewPath);
	PathIndex = 0;
	GoalCell = Goal;
	bHasGoal = true;
	PathGridVersion = G->GetGridVersion();
	RepathTimer = RepathInterval;
	StuckTimer = 0.f;
	LastProgressLocation = GetActorLocation();
	return true;
}

void AOniCharacter::PickWanderTarget()
{
	UKakurenboGridSubsystem* G = Grid();
	for (int32 Try = 0; Try < 8; ++Try)
	{
		FIntPoint Cell;
		if (G->FindRandomFreeCell(Cell) && RequestPathTo(Cell, false))
		{
			return;
		}
	}
	IdleTimer = 1.f; // 見つからなければ少し待って再挑戦
}

void AOniCharacter::FollowPath(float DeltaSeconds)
{
	UKakurenboGridSubsystem* G = Grid();
	const bool bAllowWalls = (State == EOniState::Investigate);

	// 配置が変わった・一定時間たった → 経路を作り直す
	RepathTimer -= DeltaSeconds;
	if (PathGridVersion != G->GetGridVersion() || RepathTimer <= 0.f)
	{
		if (!RequestPathTo(GoalCell, bAllowWalls))
		{
			bHasGoal = false;
			IdleTimer = 0.5f;
			SearchTimer = SearchDuration;
			return;
		}
	}

	if (PathIndex >= Path.Num())
	{
		// 到着
		bHasGoal = false;
		if (State == EOniState::Investigate)
		{
			SearchTimer = SearchDuration;
		}
		else
		{
			IdleTimer = FMath::FRandRange(0.3f, 1.2f);
		}
		return;
	}

	const FIntPoint Next = Path[PathIndex];
	const FVector NextCenter = G->CellFloorCenter(Next);
	const FVector Loc = GetActorLocation();
	const float Dist2D = FVector::Dist2D(Loc, NextCenter);

	// 次のマスに壁がある → 十分近づいたら壊す
	if (G->GetColumnHeight(Next) > 0)
	{
		if (Dist2D <= G->GetCellSize() * 1.1f)
		{
			BeginAttack(Next);
			return;
		}
	}
	else if (Dist2D <= ReachDistance)
	{
		++PathIndex;
		return;
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
}

void AOniCharacter::FaceTowards(const FVector& Location, float DeltaSeconds, float DegreesPerSecond)
{
	const FRotator Current = GetActorRotation();
	const FRotator Desired(0.f, (Location - GetActorLocation()).Rotation().Yaw, 0.f);
	SetActorRotation(FMath::RInterpConstantTo(Current, Desired, DeltaSeconds, DegreesPerSecond));
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

void AOniCharacter::TickAttack(float DeltaSeconds)
{
	UKakurenboGridSubsystem* G = Grid();
	FaceTowards(G->CellFloorCenter(AttackCell), DeltaSeconds, 720.f);

	AttackTimer += DeltaSeconds;

	// 溜め中は体を縮める（見た目の予兆）
	const float Windup = FMath::Clamp(AttackTimer / AttackWindup, 0.f, 1.f);
	const float Squash = bAttackFired ? 1.f : 1.f - 0.25f * Windup;
	BodyMesh->SetRelativeScale3D(FVector(0.76f / FMath::Sqrt(Squash), 0.76f / FMath::Sqrt(Squash), 1.9f * Squash));

	if (!bAttackFired && AttackTimer >= AttackWindup)
	{
		bAttackFired = true;
		const int32 Destroyed = G->DamageBlocksInRadius(GetActorLocation(), AttackRadius, AttackDamage);
		DrawDebugSphere(GetWorld(), GetActorLocation(), AttackRadius, 16, FColor(255, 80, 40), false, 0.25f, 0, 3.f);
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

		// 壊し終わった → 元の行動に戻って経路を作り直す
		SetState(StateBeforeAttack);
		StateBeforeAttack = EOniState::Wander;
		if (bHasGoal && !RequestPathTo(GoalCell, State == EOniState::Investigate))
		{
			bHasGoal = false;
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
	DrawDebugCircle(GetWorld(), GetActorLocation(), HearingRadius, 48, FColor(255, 255, 255, 60), false, 0.f, 0, 2.f, FVector(1, 0, 0), FVector(0, 1, 0), false);
}
